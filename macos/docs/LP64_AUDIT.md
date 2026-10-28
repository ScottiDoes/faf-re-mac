# LP64 / wchar_t audit for the macOS arm64 build

This is a read-only audit; no engine files were changed. Audit date: 2026-10-27, tree state around 17:00 local.
Other porting agents were editing the tree while this ran, so line numbers can drift.

## What changes on macOS arm64

| Type | Windows x64 (upstream, LLP64) | macOS arm64 (LP64) | Original x86 binary |
|---|---|---|---|
| `long` / `unsigned long` | 32 | **64** | 32 |
| `wchar_t` | 16, unsigned | **32, signed** (`WCHAR_MIN` = -2^31) | 16 |
| `size_t`, `intptr_t`, pointers | 64 (`unsigned long long`) | 64 (`unsigned long`) | 32 |
| `long double` | 64 (same as `double`) | 64 (same as `double`) | 64/80 x87 |
| `int64_t` | `long long` | `long long` | same |
| `time_t` | `__time64_t` (`long long`) | `long` (64) | 32/64 |
| `clock_t`, `CLOCKS_PER_SEC` | `long`, 1000 | `unsigned long`, 1000000 | `long`, 1000 |
| `DWORD`/`LONG`/`ULONG` (WinCompat.h) | `unsigned long`/`long` | `uint32_t`/`int32_t` (same size, **different type identity**) | 32 |

Pointer/`size_t` width is the same as upstream's x64 build. It is out of scope here, except where it reaches the same code paths as `long`.

## Method

1. **Grep over every `long`, `unsigned long`, `wchar_t`, `WCHAR`, `L"..."`, `%l*` and `%I64` format, `L`/`UL` literal, `LONG_MAX`, `lrint*`/`strtol*`/`ldiv`/`ftell` and `clock` use in `src/`.**
   - 533 bare-`long` lines were reviewed and classified (lists below).
   - `src/boost`, `src/zlib`, `src/libpng` and `moho/misc/CrtRuntimeHelpers.cpp` were checked only for whether they are built. They are not in `src/sources.cmake`.
2. **Compile probe.** Every non-excluded TU in `src/sources.cmake` (923) was compiled with `macos/tu.sh <tu> -fsyntax-only -ferror-limit=0 -Wdeprecated-declarations -iquote <probe> -isystem <stub-sdk>`.
   - `<probe>` holds instrumented copies of `gpg/core/streams/{Stream,BinaryReader,BinaryWriter}.h`. Every raw-bytes template (`Stream::Write<T>`, `Write<E>`, `Write<T,N>`, `BinaryReader::ReadExact<T>`/`ReadExactArray<T>`, `BinaryWriter::Write<T>`) calls a `[[deprecated]]` marker function when `T` is `long`, `unsigned long` (which is also `size_t` on macOS) or `wchar_t`. It calls a second marker for every class type, so the structs written raw could be listed and inspected.
   - `<stub-sdk>` holds empty wx/D3D/XACT/ffmpeg headers, so the compiler does not stop at a missing include.
   - 784 TUs parsed with no errors. 139 had errors, mostly missing wx or Win32 declarations, but clang kept instantiating templates.
   - **Scalar hits:** `moho/net/SPeer.cpp:124` and `moho/net/CLobby.cpp:1025`, both `u_long`. Both are resolved now (see LP64-12).
   - **Class types written raw:** `gpg::MD5Digest`, `moho::SCoordsVec2`, `Wm3::Vector3f`, `Wm3::Quaternionf`, `moho::VTransform`, `SZipCentralDirectoryFileHeader`, `SZipEndOfCentralDirectoryRecord`, `SWorldMapFileHeader`. All of them have fixed-width fields only.
3. **Hand review of everything that feeds the sim checksum.** That is every `mContext.Update` / `md5.Update` site, plus `REF_UpdateMD5`, `CRandomStream`/`CMersenneTwister`, the replay header and body, the net packet structs, GPGNet framing, zip/scm/sca/scmap headers and the save-game header.
4. **Small runtime check** of macOS libc behaviour (cc, arm64):
   - `snprintf("%I64\n")` prints `I64`.
   - `strtoul("-1")` gives 18446744073709551615 (Windows gives 4294967295).
   - `strtoul("FFFFFFFFFF",16)` gives 1099511627775 (Windows saturates to 4294967295).
   - `lrintf(3e9f)` gives 3000000000 and `lrintf(NAN)` gives 0. The x87 `fistp` in the original binary gives 0x80000000 for both.

Severity scale:
- **Critical**: breaks milestone-1 replay checksums on normal data.
- **High**: breaks a persisted or wire format, or crashes.
- **Medium**: diverges from Windows on reachable but unusual input, or breaks a non-milestone-1 feature.
- **Low**: latent, cosmetic, or a compile error only.

## Summary

**No Critical finding.** On ordinary replay data, every input to the sim MD5 checksum, every replay-format field and every UDP packet field already uses a fixed-width type. The bare-`long` and `wchar_t` problems that remain are edge cases on the sim path (LP64-01, LP64-02), or they sit on save games, map saving, start-up, tools and UI.

| ID | Sev | Area | File:line | One-line |
|---|---|---|---|---|
| LP64-01 | **Medium** | sim / determinism | 109 `std::lrint*`/`lround` sites, e.g. `moho/projectile/Projectile.cpp:449`, `moho/lua/SCR_Math.cpp:1401` | `lrintf` returns 64-bit `long`: NaN and out-of-int32 values no longer give 0x80000000 |
| LP64-02 | **Medium** | sim Lua | `lua/LuaObject.cpp:14745` | `tonumber(s, base)` uses 64-bit `strtoul`: negative or >32-bit input gives a different number |
| LP64-03 | **High** (saves) | save-game format | `moho/serialization/SaveGameFileHeader.h:34-35` | `wchar_t[1024]` header fields make the struct 0x3028 bytes: writing it clobbers 4 KiB of the archive, and loading over-reads 4 KiB |
| LP64-04 | Medium | map file format | `moho/sim/CWldMap.cpp:1401-1405` | Map preview name is written as UTF-32 (`sizeof(wchar_t)`) instead of UTF-16 |
| LP64-05 | Medium | start-up | `moho/sim/WldSessionInfo.cpp:164` | `CreateSemaphoreW(..., LONG_MAX)`: `LONG_MAX` becomes -1 as a 32-bit `LONG`, so creation fails and the handle is NULL |
| LP64-06 | Low (latent High) | hashing | `legacy/containers/HashMap.h:72-80,120,122` | `msvc8::hash_value(long)` and `hash_compare` hash 64 bits: bucket order differs from Windows for integral and pointer keys |
| LP64-07 | Low-Medium | wide strings | `gpg/core/containers/String.cpp:133-215` | UTF-8 to wide conversion: 32-bit signed `wchar_t` changes results for invalid bytes and 4-byte sequences (paths, UI) |
| LP64-08 | Low (latent) | serialization | `gpg/core/containers/ReadArchive.cpp:321-335,551-565,700-714` | `ReadLong`/`ReadULong` fill only 4 of 8 bytes; text archive accepts 64-bit values |
| LP64-09 | Low (latent, checksum) | reflection / MD5 | `moho/sim/Sim.cpp:730,736`; `gpg/core/reflection/RIntegerTypes.cpp:1310,1432` | `RIntType<long>::size_` = 8, so `REF_UpdateMD5` would hash 8 bytes for a reflected `long`; no such field exists today |
| LP64-10 | Low | sim pathfinding (memory) | `gpg/core/algorithms/Cluster.cpp:30-35,59` | `SharedCountControl{long,long}` on top of an `{int32,int32,px}` block: the weak decrement hits `px`, so the control block leaks |
| LP64-11 | Low | Lua `os` | `lua/LuaObject.cpp:6437`; `6618-6628` | `os.clock` hard-codes `CLOCKS_PER_SEC`=1000 and truncates `clock_t`; `os.date` passes `__time64_t*` where `time_t*` is expected (compile error) |
| LP64-12 | ~~High~~ resolved | lobby wire | `moho/net/SPeer.cpp:124`, `moho/net/CLobby.cpp:1020-1025` | `u_long` was 8 bytes in `LOBMSG_NewPeer`; fixed by `compat/Sockets.h:47-56` (`#define u_long faf_posix_u_long` = `uint32_t`), re-probed clean |
| LP64-13 | Low | sim debug log | `moho/sim/Sim.cpp:6020` | `"%I64"` prints `I64` on macOS, but nothing on Windows, so sim log lines differ |
| LP64-14 | Low | compile | `moho/ui/UiRuntimeTypes.cpp:1607` | `std::bit_cast<long>(float)`: size mismatch, does not compile |
| LP64-15 | Low (latent) | macro | `platform/Platform.h:44-45` | `LODWORD`/`HIDWORD` read 8 bytes through `unsigned long*`; unused today |
| LP64-16 | Low | diagnostics | `gpg/core/diagnostics/CrashReport.cpp:1011-1014` and ~40 `%lu`/`%lX` sites | 8-byte store in a 4-byte DLGTEMPLATE slot; `%lu` given 32-bit `DWORD` arguments |
| LP64-17 | Low | prefs / misc | `moho/misc/StartupHelpers.cpp:1123`; `moho/misc/CrtRuntimeExportedHelpers.cpp:67` | `strtoul`/`wcstol` do not saturate at 32 bits |
| LP64-18 | Info | boost ABI | `gpg/core/utils/BoostWrappers.h:1287-1306` vs boost `sp_counted_base_pt.hpp:37-38` | boost 1.34.1 picks the pthread `sp_counted_base` on arm64 (`long` counts plus a mutex); engine views of control blocks assume `{vptr,int32,int32,px}` |
| LP64-19 | Info | UI/render | `moho/render/d3d/CD3DFont.h:24-29`, `moho/app/WinApp.cpp:2272`, `moho/misc/CDiskWatch.cpp:149` | `wchar_t` layout and byte assumptions (kerning pairs, clipboard, `FILE_NOTIFY_INFORMATION`) |

Three adjacent findings are not LP64 but turned up during the audit; see A1–A3 at the end. **A1** (non-atomic stat fallbacks on POSIX next to `SimWorkerPool` parallel jobs) should be checked before milestone 1.

---

## Findings in detail

### LP64-01 `std::lrintf` / `std::lrint` / `std::lround` return a 64-bit `long` (sim) — Medium

**Code.** The sim rounds world coordinates to cells, and lifetimes to ticks, with `lrintf`. The engine comments say this reproduces the binary's `fistp`. There are 109 call sites; the sim ones are:

- `moho/unit/core/Unit.cpp` (16)
- `moho/sim/STIMap.cpp` (13)
- `moho/unit/tasks/CUnitMobileBuildTask.cpp` (10)
- `CUnitMeleeAttackTargetTask.cpp` (9)
- `CUnitGuardTask.cpp` (8)
- `RUnitBlueprint.cpp`, `Prop.cpp`, `CAiFormationInstance.cpp` (4 each)
- `CUnitAttackTargetTask.cpp`, `CAiNavigatorLand.cpp` (3 each)
- 2 each in `CUnitPatrolTask`, `CUnitMoveTask`, `CUnitCallTeleport`, `CUnitCallAirStagingPlatform`, `CUnitMotion`, `CUnitCommand`, `Sim.cpp` (26960-26961, `lrint`), `CSquad`, `Entity.cpp`, `CAiTransportImpl`, `CAiPathSpline`, `CAiPathNavigator`, `CAiPathFinder`, `CAiNavigatorImpl`
- 1 each in `Projectile.cpp:449`, `SCR_Math.cpp:1401` (`MATH_IRound`), `CAiNavigatorAir.cpp:29`

Examples:

```cpp
// moho/projectile/Projectile.cpp:448
mLifetimeEnd = curTick + static_cast<std::uint32_t>(std::lrintf(lifetimeSeconds * 10.0f));
// moho/lua/SCR_Math.cpp:1401  (MATH_IRound, reachable from blueprint/sim Lua)
lua_pushnumber(state->m_state, static_cast<float>(std::lrintf(value)));
// moho/ai/CAiPathNavigator.cpp:232
cell.x = static_cast<std::int16_t>(std::lrintf(position.x - (static_cast<float>(footprint.mSizeX) * 0.5f)));
```

**Why it matters (determinism).**
- Within int32 range the result is identical.
- Outside it, and for NaN, behaviour differs:
  - Windows' 32-bit `long` cannot hold the value. `fistp`, which the original binary uses, and `cvtss2si` produce 0x80000000 (`INT32_MIN`). Casts then give `int16` 0, `int32` -2^31, `uint32` 2^31, and `float` -2147483648.
  - macOS returns the exact 64-bit value, and 0 for NaN. Measured: `lrintf(3e9)` = 3000000000 and `(int)` of that is -1294967296; `lrintf(NaN)` = 0.
- So a NaN or huge position (a unit flung off-map, a Lua-supplied position, a bad blueprint value) gives a different cell, lifetime or `MATH_IRound` result. That desyncs the replay, but only in that edge case.

**Fix.** Give the POSIX build an `lrint` family with Windows' 32-bit `long` semantics:

```cpp
// WinCompat (Misc or a new Math area), POSIX only
inline long faf_posix_lrintf(float x) noexcept {
  if (!(x >= -2147483648.0f && x < 2147483648.0f)) return -2147483647L - 1;   // integer indefinite
  return static_cast<long>(static_cast<std::int32_t>(::lrintf(x)));          // current rounding mode
}
inline long faf_posix_lrint(double x) noexcept {
  if (!(x >= -2147483648.0 && x < 2147483648.0)) return -2147483647L - 1;
  return static_cast<long>(static_cast<std::int32_t>(::lrint(x)));
}
```

All call sites spell `std::lrintf(...)`. There are two ways to route them:
- **(a)** The lead adds a force-included shim, for example a `namespace std { using ::faf_posix_lrintf; }` plus `#define lrintf faf_posix_lrintf` placed after `<cmath>`. It must cover `lrint` and `lround` as well.
- **(b)** A mechanical, guarded edit at the 109 sites using a macro `FAF_LRINTF` that expands to `std::lrintf` on Windows.

Option (a) leaves the engine sources untouched. `lround` is only used in UI (`SimDriver.cpp:1890`) and the gal backends, so it is cosmetic there. Related: **A3**, `static_cast<int>(float)` with NaN or out-of-range values.

### LP64-02 `luaB_tonumber` with a base uses 64-bit `strtoul` — Medium

```cpp
// lua/LuaObject.cpp:14745
const unsigned long value = std::strtoul(text, &end, base);
...
lua_pushnumber(state, static_cast<lua_Number>(value));
```

**Why it matters (sim Lua).** `tonumber(s, 16)` and similar calls are reachable from sim Lua. MSVC `strtoul` is 32-bit, saturates to 4294967295 on overflow, and negates modulo 2^32. On macOS:
- `tonumber("-1",10)` is unaffected, because base 10 takes the `lua_tonumber` path.
- `tonumber("-1",16)` gives 1.8446744e19 instead of 4294967296.0f.
- `tonumber("FFFFFFFFFF",16)` gives 1.0995116e12 instead of 4294967296.0f.

**Fix (guarded).**

```cpp
#if defined(FAF_POSIX)
    errno = 0;
    const unsigned long long wide = std::strtoull(text, &end, base);
    const char* s = text; while (std::isspace(static_cast<unsigned char>(*s))) ++s;
    const bool negative = (*s == '-');
    const unsigned long long magnitude = negative ? (0ull - wide) : wide;
    const std::uint32_t value = (errno == ERANGE || magnitude > 0xFFFFFFFFull)
      ? 0xFFFFFFFFu : static_cast<std::uint32_t>(wide);   // MSVC strtoul: saturate, else negate mod 2^32
#else
    const unsigned long value = std::strtoul(text, &end, base);
#endif
```

### LP64-03 Save-game file header holds `wchar_t` arrays — High (save games; not milestone 1)

```cpp
// moho/serialization/SaveGameFileHeader.h:34-35  (static_assert(sizeof == 0x2028) is disabled by LayoutAsserts.h)
wchar_t mAppNameUtf16[kSaveGameFileHeaderTextCapacity]{};     // +0x0028
wchar_t mSessionNameUtf16[kSaveGameFileHeaderTextCapacity]{}; // +0x0828
```

**Why it matters (file format).** On macOS `sizeof(SSaveGameFileHeader)` is 0x3028.

- **Write path.** `CSaveGameRequestImpl` first writes a blank header of `kSaveGameFileHeaderSize` (0x2028) bytes at `:281`, then the archive. `RewriteSaveHeader` (`:84`) then writes `sizeof(header)` = 0x3028 bytes at offset 0. That overwrites the first 4 KiB of serialized sim data and stores UTF-32 names.
- **Load path.** `CSavedGame::CSavedGame` (`moho/misc/SessionStartup.cpp:1427`) `fread`s 0x3028 bytes, so the archive starts 4 KiB late.

Native save/load is broken, and so is exchanging saves with Windows.

**Fix (guarded).**
- Declare the two arrays as `char16_t` under `FAF_POSIX`. The header then measures 0x2028 bytes again.
- In `CopyWideStringTruncated` (`CSaveGameRequestImpl.cpp:50`), store `static_cast<char16_t>(c)` per element. This reproduces exactly the 16-bit values Windows stores, including its lossy handling of non-BMP characters (see LP64-07).
- Any reader of the names must widen `char16_t` to `wchar_t`.

### LP64-04 Map preview name written as UTF-32 — Medium (map files written by the engine)

```cpp
// moho/sim/CWldMap.cpp:1401-1405  (RWldMapPreviewChunk::Save)
const std::wstring previewNameWide = gpg::STR_Utf8ToWide(mPreviewName.c_str());
writer.Write(reinterpret_cast<const char*>(previewNameWide.c_str()), (previewNameWide.size() + 1U) * sizeof(wchar_t));
```

**Why it matters (file format, .scmap).** The loader (`:1299-1306`) correctly reads `uint16` units. The saver writes 4-byte units, so a map saved on macOS is unreadable on Windows and on macOS.

**Fix (guarded).**

```cpp
std::u16string utf16; utf16.reserve(previewNameWide.size() + 1);
for (wchar_t c : previewNameWide) utf16.push_back(static_cast<char16_t>(c));  // same 16-bit units Windows emits
utf16.push_back(u'\0');
writer.Write(reinterpret_cast<const char*>(utf16.data()), utf16.size() * sizeof(char16_t));
```

### LP64-05 `CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr)` fails — Medium (start-up)

```cpp
// moho/sim/WldSessionInfo.cpp:164
, mQueueSemaphore(CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr))
```

**Why it matters.** `LONG_MAX` is 2^63-1 on macOS. Passed to the 32-bit `LONG` parameter it becomes -1, and `CreateSemaphoreImpl` (`platform/posix/WinCompatSync.cpp:535`) rejects `maximumCount <= 0`. So `mQueueSemaphore` is NULL on every scenario-load control.

It is unused today: only the constructor and destructor touch it. It would break silently once it is used.

**Fix.** Guarded `0x7FFFFFFF`, or `(std::numeric_limits<LONG>::max)()`, which equals `LONG_MAX` on Windows.

### LP64-06 `msvc8::hash_value(long)` / `hash_compare` hash 64 bits — Low now, High if a sim map gains an integral key

```cpp
// legacy/containers/HashMap.h:72-80
inline std::size_t hash_value(long key) noexcept {
  const std::ldiv_t parts = std::ldiv(key, 127773L);
  long scrambled = 16807L * parts.rem - 2836L * parts.quot; ...
// :120 / :122  (hash_compare::operator())
return hash_value(static_cast<long>(reinterpret_cast<std::uintptr_t>(key) ^ hash_seed));
return hash_value(static_cast<long>(static_cast<std::size_t>(key) ^ hash_seed));
```

**Why it matters (hashing, so bucket and iteration order).** On Windows, `static_cast<long>(x)` keeps the low 32 bits as a signed value. For any key below 2^31, `key ^ 0xDEADBEEF` has bit 31 set and is negative. On macOS it stays a positive 64-bit value, so `ldiv` gives different results and every integral key lands in a different bucket.

**Current exposure** (checked every `msvc8::hash_map`/`hash_set` instantiation):
- The sim tables are safe. That is the pathfinder (`PathTables.cpp:57`, `SOCellPos.h:66`) and the HaStar caches (`Cluster.cpp:872,890,2238`). They pre-cast to `int32` or `uint16`, so they get identical buckets.
- The affected users are UI and debug: `CWldSession.cpp:1142` `WidthMap<uint32_t>`, `CWldSession.cpp:3033` (edge hash with `static_cast<long>(uint32)`), and `WinMain.cpp:98` `hash_set<uintptr_t>`.

**Fix (guarded, one place, covers every caller).**

```cpp
#if defined(FAF_POSIX)
inline std::size_t hash_value(long key) noexcept {      // Windows long is 32-bit: hash the low 32 bits
  const std::int32_t k = static_cast<std::int32_t>(static_cast<std::uint32_t>(key));
  const std::div_t parts = std::div(k, 127773);
  std::int32_t scrambled = 16807 * parts.rem - 2836 * parts.quot;   // Schrage: no int32 overflow
  if (scrambled < 0) scrambled += 2147483647;
  return static_cast<std::size_t>(scrambled);
}
#else ...existing...
#endif
```

### LP64-07 UTF-8 ⇄ wide conversion with 32-bit signed `wchar_t` — Low-Medium (paths and UI; no sim consumer)

```cpp
// gpg/core/containers/String.cpp:175   (STR_DecodeUtf8Char, invalid lead byte path)
wchar_t codepoint = static_cast<wchar_t>(leadSigned);          // Win: 0xFF80..0xFFFF   macOS: -128..-1
// :195  (4-byte lead)
codepoint = static_cast<wchar_t>((c2 & 0x3Fu) << 12);          // Win: truncated to 16 bits   macOS: up to 0x3F000
// :133-156 (STR_EncodeUtf8Char): `chr >= 0x80` is false for negative chr -> raw byte; chr > 0xFFFF -> (chr>>12)|0xE0 garbage lead
```

**Why it matters.**
- Round-tripping a string through `STR_Utf8ToWide` and `STR_WideToUtf8` gives different bytes than on Windows for:
  - invalid UTF-8, such as CP1252 names (Windows: `EF BE xx`; macOS: the original byte);
  - 4-byte UTF-8, such as emoji (macOS emits an `FF`-lead sequence).
- POSIX `*W` APIs that return real UTF-32 (home or Documents paths, directory listings) are also mis-encoded for non-BMP characters.
- Consumers are file paths (`FileStream.cpp:149`, `SessionStartup.cpp:113,1416`, `StartupHelpers.cpp` profile and prefs paths, `Sim.cpp:19512,19689`), clipboard and UI text, and font glyph lookup. `CD3DFont::GetCharInfo` masks to `uint16`, so it is safe.
- `STR_Utf8Len` and `STR_Utf8SubString`, which sim Lua can reach, iterate bytes and are unaffected.

**Fix (guarded, String.cpp).**
- In `STR_DecodeUtf8Char`, store `static_cast<wchar_t>(static_cast<std::uint16_t>(codepoint))` for the invalid-lead result, as unsigned. For 4-byte sequences, either mask to 16 bits (Windows-identical, lossy) or decode properly. Proper decoding is preferred for paths, because nothing in the sim consumes it.
- In `STR_EncodeUtf8Char`, emit 4-byte UTF-8 when `chr > 0xFFFF`. That case cannot occur on Windows.

The Files-area `NativePath()` (`platform/posix/WinCompatInternal.h:56`) already converts proper UTF-32 to UTF-8.

### LP64-08 `ReadArchive::ReadLong` / `ReadULong` write only 4 of 8 bytes — Low (latent, no callers)

```cpp
// gpg/core/containers/ReadArchive.cpp:551-565
void ReadLong2(void* const outValue)  { ReadUInt2(outValue); }   // fread(outValue, 4, ...)
void ReadULong2(void* const outValue) { ReadUInt2(outValue); }
// :700-714 (text archive)  (*mCachedStream) >> *value;   // accepts 64-bit range
```

**Why it matters (serialization).**
- The upper 4 bytes of the 8-byte `long` keep their previous contents, with no sign extension.
- `WriteLong`/`WriteULong` (`WriteArchive.cpp:525-540`) write `&value` with size 4, which is the correct low half on little-endian.
- There are no callers today: `Sim.cpp:3711` is a dead `requires` branch. Any future `long` field would load garbage.

**Fix (guarded).** Read into `std::int32_t` / `std::uint32_t` temporaries and sign- or zero-extend. In the text archive, parse into the 32-bit temporary.

### LP64-09 Reflected `long` is 8 bytes, and `REF_UpdateMD5` hashes `size_` bytes — Low (latent, checksum-critical if triggered)

```cpp
// moho/sim/Sim.cpp:730,736 (REF_UpdateMD5, seeds every session checksum from the blueprints)
refType == ... GetLongRType() ...;  md5.Update(ref.mObj, static_cast<std::size_t>(refType->size_));
// gpg/core/reflection/RIntegerTypes.cpp:1310  size_ = static_cast<int>(sizeof(T));   // long -> 8
```

**Why it matters (checksum).**
- A reflected blueprint field of type `long` would hash 8 bytes instead of 4.
- I checked all 527 `AddField<...>` sites and every `LookupRType(typeid(...))`: there is no `long`, `unsigned long`, `DWORD`, `LONG` or `wchar_t` field and no container of them. Today's blueprint checksum is therefore unaffected.
- Note the type-identity change: on Windows a `DWORD`/`LONG` field would register as `unsigned long`/`long`. On macOS it registers as `unsigned int`/`int`, and `REF_UpdateMD5` hashes `int` and `long` but not `unsigned int`/`unsigned long`.

**Fix.** Keep it that way. Optionally add a POSIX guard in `REF_UpdateMD5` that hashes `static_cast<std::int32_t>` for `GetLongRType()`. Have the reflection owner refuse `long`/`unsigned long`/`DWORD`/`LONG` in `AddField`.

`RIntType<long>::SetLexical` (`ParseLegacyIntegerLexicalBounded<long>`) also accepts 64-bit values. That is latent too.

### LP64-10 HaStar cluster-cache control block: `long` counters over an `int32` layout — Low (leak; sim pathfinding)

```cpp
// gpg/core/algorithms/Cluster.cpp:30-35
struct SharedCountControl { void** vtable; volatile long useCount; volatile long weakCount; };
// the block itself is boost::SpCountedImplStorage<void> {void*; int32 useCount; int32 weakCount; px} (BoostWrappers.h:1287)
// :59
if (_InterlockedExchangeAdd(&control->weakCount, -1) == 1) { ...destroy... }
```

**Why it matters.**
- On macOS `weakCount` sits at +0x10, which is the low half of `px`.
- After `dispose`, the "weak" decrement edits the stale `px`, never reaches 1, and the 24-byte control block leaks on every `ClusterCache` teardown.
- `useCount` (+0x08) still lines up, because the compat `long` Interlocked overloads use 32 bits. There is no determinism effect.

**Fix.** Change both members to `volatile LONG`. That is identical on Windows, where `LONG` is `long`, and compat `Sync.h` has `LONG` overloads.

### LP64-11 Lua `os.clock` and `os.date` — Low

```cpp
// lua/LuaObject.cpp:6437
const int tickCount = std::clock();
lua_pushnumber(state, static_cast<lua_Number>(static_cast<float>(tickCount) * 0.001f));   // assumes CLOCKS_PER_SEC == 1000
// :6618-6628  __time64_t when ...; _gmtime64(&when); _localtime64(&when);   // time_t is `long` on macOS: does not compile
```

**Why it matters.**
- `clock_t` is `unsigned long` with `CLOCKS_PER_SEC` = 10^6 on macOS, so `os.clock()` reads 1000× too high and wraps after about 35 minutes of CPU time.
- Windows `clock()` is wall time since start; POSIX `clock()` is CPU time.
- `os.date` does not compile.
- Not sim-checksummed.

**Fix (guarded).** For `os.clock`, use milliseconds of wall time since process start (`std::chrono::steady_clock`) as an `int`. For `os.date`, use `std::time_t` with `gmtime`/`localtime` (or `_r`).

### LP64-12 Lobby `LOBMSG_NewPeer` carried an 8-byte `u_long` — resolved

`moho/net/SPeer.cpp:124` `stream.Write(address)` and `moho/net/CLobby.cpp:1020-1025` `br.ReadExact(host)`, where both are `u_long`. The probe flagged both, because `<sys/types.h>` makes `u_long` an `unsigned long`.

The net agent's `platform/posix/compat/Sockets.h:47-56` (modified 17:00) now does `typedef std::uint32_t faf_posix_u_long; #define u_long faf_posix_u_long` with a real `static_assert(sizeof(u_long) == 4)`. Re-probing both TUs at 17:01 gave 0 hits.

That macro must keep being included before any engine use of `u_long`. Every other `u_long` (`CNetUDPConnection.h:366 mAddr`, `INetDatagramSocket::Send`, `ioctlsocket` argp) is in memory only and is now 32-bit.

### LP64-13 `"%I64"` in the sim checksum log — Low

```cpp
// moho/sim/Sim.cpp:6019-6024
Logf("    mMaxStorage[%d]=%I64\n", resource, lo32, hi32);
```

**Why it matters.** The checksum is unaffected. But the sim log, which is what desync triage diffs, gets `I64` after `=` on macOS; Windows/MSVC prints nothing there.

**Fix.** Use `"    mMaxStorage[%d]=\n"` under `FAF_POSIX`.

### LP64-14 `std::bit_cast<long>(float)` — Low (compile error)

`moho/ui/UiRuntimeTypes.cpp:1607`: `const long nextBits = std::bit_cast<long>(value);` does not compile because the sizes differ.

**Fix.** `static_cast<long>(std::bit_cast<std::int32_t>(value))`, which is identical on Windows.

### LP64-15 `LODWORD` / `HIDWORD` — Low (latent)

`platform/Platform.h:44-45` reads and writes through `unsigned long*`, which is 8 bytes on macOS. `HIDWORD` also uses `&(x)+1`. Nothing uses them today.

**Fix.** Use `std::uint32_t*` and `reinterpret_cast<std::uint32_t*>(&(x)) + 1`.

### LP64-16 Diagnostics: 8-byte store and `%lu` with 32-bit arguments — Low (cosmetic)

- `gpg/core/diagnostics/CrashReport.cpp:1011-1014`: `putDword` stores `unsigned long` (8 bytes) and then advances 4. This is the Windows crash dialog template, so exclude or guard the TU.
- `%lu` / `%lX` with `DWORD`/`GetLastError()`/`GetCurrentThreadId()` arguments. A 32-bit vararg is read as 64-bit, so the upper half is garbage. The sites:
  - `CrashReport.cpp:198-526`
  - `CrashHooks.cpp:173-344`
  - `ThreadPlacement.cpp:250,257`
  - `PathTables.cpp:1709` (OK: the `?:` with `0ul` promotes)
  - `Shadow.cpp:537`
  - `LuaCrashDump.cpp:335,527`
  - `gal/effects/EffectRuntime.cpp:363`

  **Fix.** Cast the argument to `unsigned long`, as most gal sites already do.
- `gpg::Logf`/`Warnf`/`STR_Printf` carry no `format` attribute, so clang cannot flag these. Adding `__attribute__((format(printf, N, M)))` under `FAF_POSIX` would make the compiler find every mismatch.

### LP64-17 `strtoul` / `wcstol` without 32-bit saturation — Low

- `moho/misc/StartupHelpers.cpp:1123`: user-prefs hex value. Windows saturates at 0xFFFFFFFF; macOS truncates. Not sim.
- `moho/misc/CrtRuntimeExportedHelpers.cpp:67`: `EngineWtoiFromWide`, which has no callers.
- The sim `strtol` sites check their range and are identical: `Unit.cpp:1443`, `Sim.cpp:1940,4419`, `CSimConVarInstanceBase.h:57` (also checks `ERANGE` and int range), `ThreadPlacement.cpp:188`, `SimWorkerPool.cpp:54`.

### LP64-18 boost `shared_ptr` control-block layout — Info

On arm64 clang, boost 1.34.1 `sp_counted_base.hpp` falls through to `sp_counted_base_pt.hpp`: `long use_count_; long weak_count_; pthread_mutex_t m_;`.

The engine's hand-written views assume the x64-Windows `sp_counted_base_w32` layout `{vptr, int32 use, int32 weak}` with `px` at +0x10:
- `boost::SpCountedImplStorage`, `SpCountedImplPd*` (`gpg/core/utils/BoostWrappers.h:1287-1306`, `BoostWrappers.cpp:1515,1749`);
- the dispose helpers in `CD3DPrimBatcher.cpp:137` and `CD3DTextureBatcher.cpp:71`.

Engine code that hands such a block to real boost code, or reads a real boost block through such a view, is wrong on macOS. `SharedCountPair` users (`pi->release()`) are fine.

Today only `Cluster.cpp` fabricates a block, and only engine code touches it (LP64-10). Two possible fixes:
- If more of these appear, a macOS boost patch could add an atomic `sp_counted_base` with `int32` counts, matching the layout the engine mimics. That is a lead decision in `deps.cmake`.
- Alternatively, keep such views out of the POSIX build.

### LP64-19 Other `wchar_t` layout and byte assumptions — Info

- `moho/render/d3d/CD3DFont.h:24-29` `SKerningPair{wchar_t,wchar_t,float}` (renderer, GDI kerning).
- `moho/app/WinApp.cpp:2272` clipboard `CF_UNICODETEXT` sizing.
- `moho/misc/CDiskWatch.cpp:149` `FileNameLength / sizeof(wchar_t)`. A POSIX `ReadDirectoryChangesW` stand-in must report `FileNameLength` in bytes of 4-byte `wchar_t`.
- `moho/misc/StrSafeRuntime.cpp:199` `_vsnwprintf`: in a wide format `%s` means wide on MSVC and narrow on POSIX.
- `gpg/core/utils/Global.cpp:540` (`GetCommandLineW`) depends on the Misc area building a wide command line.

None of these is sim-relevant.

---

## Verified clean: fixed-width already

- **Sim MD5 checksum inputs.** All of these are `uint32`/`int32`/`float`/fixed structs:
  - `Sim::UpdateChecksum` (`Sim.cpp:6002-6121`): `SEconTotals`, entity id/health/orient/velocity, the MT state `uint32_t[624]`, the Marsaglia pair;
  - `CUnitCommand.cpp:854-856,1557`, `CUnitCommandQueue.cpp:318-319` (`EntId`/`CmdId` = `int32_t`);
  - `CAiReconDBImpl.cpp:683-685,836,1258-1316`, `CAiPathNavigator.cpp:1087-1940` (`SOCellPos`, `Vector3f`);
  - `UnitWeapon.cpp:3885-3888`;
  - `Sim.cpp:4264-4313`, `7792`, `7893-7894`.
- **Hashing and RNG.** `gpg::MD5Context` (`uint32` words), `HashBytes`/`HashDword` (`unsigned int`), `CMersenneTwister::StateWords = uint32_t[624]`, `CRandomStream`.
- **Replay `.scfareplay`.**
  - Header writer and reader (`SessionStartup.cpp:466-510,586-686`): strings, `int32` lengths, `uint8` counts, `SSTICommandSource::mTimeouts` and `mInitSeed` (`int32`).
  - Body: `CMessage` 3-byte header (`uint8` type + `uint16` size), `CReplayClient` `ReadExact` of int types.
- **Network.**
  - `SNetPacketHeader` and the packed `SPacketBodyConnect`/`SPacketBodyAnswer` (`int64`, `uint16`, `enum class : int32_t`, `uint8`), `PacketLogRecord`.
  - GPGNet framing (`CGpgNetInterface.cpp`: `uint32` lengths, `int32` args).
  - Lobby messages, after LP64-12.
- **Game data.**
  - `CZipFile` packed zip records (`uint16`/`uint32`), `SScmFile`, `SScaHeader`/`SScaAnimKey`, `SWorldMapFileHeader`, scmap preview chunk *load* (`uint16` units).
  - The real zlib 1.2.3 is used through `z_stream` (LP64-clean). The engine's own zlib/libpng reconstructions are not built.
- **Serialization.** No engine caller uses `ReadLong`/`WriteLong`. Archive strings are `uint32` lengths. Reflection fields hold no `long`/`wchar_t` (LP64-09).
- **Lua.**
  - `lua_Number` = `float`; `Instruction`/`lu_hash` = `unsigned int`; `lua_WChar` = `unsigned short` (`LUAPLUS_HAS_WCHAR_T` is not defined).
  - `string.format` `%d`/`%x` go through `int`/`unsigned int`, and `%b` packs fixed widths.
  - `LuaPush(long)` preserves the value. `luaL_checklong`/`luaL_optlong` are unused. `LuaFileSeek` uses `long` for `fseek` (fine).
- **Interlocked on `volatile long*`.** 49 `volatile long*` casts in 21 files reinterpret 32-bit `StatItem`/refcount fields as `volatile long*`. `platform/posix/compat/Sync.h:25-61` maps the `long`/`unsigned long` overloads to 32-bit atomics on the first 4 bytes. Every CAS loop reads `observed` through `InterlockedCompareExchange` (sign-extended on both sides) rather than through a raw 8-byte load, so none can spin.
- **Containers.** `std::unordered_map` users on the sim side (`Entity.cpp:892`, `VisionDB.cpp:155`, `CAiReconDBImpl.cpp:74-76`, `RRuleGameRules.cpp:1559`, `CVFSImpl.cpp:122`) are lookups only, never iterated. The VFS sorts its directory listings (`CVFSImpl.cpp:331,765`).

## Harmless `long` uses (classified)

- **Interlocked casts of 32-bit fields** (correct through compat `Sync.h`):
  - `StatItem.cpp`, `InstanceCounter.h:52,76`, `CountedObject.cpp:34,50`, `Unit.cpp:1574-1580,6824-6827`, `UnitWeapon.cpp:1224,3533,3644`, `CScApp.cpp:177-181`, `EntityDb.cpp:226`, `Entity.cpp:341-361`;
  - `CD3DDevice.cpp:83-126`, `CUserSoundManager.cpp:338-1230`, `CAiBrain.cpp:576-724,3370`, `Low/Medium/HighFidelityTerrain.cpp`, `CEfxEmitter.cpp:1149`, `CEfxTrailEmitter.cpp:538`, `CArmyStats.cpp:104-121`, `SimDriver.cpp:302`, `Shoreline.cpp:125-129`, `WxRuntimeTypes.cpp:3271-3273`;
  - `FileWaitHandleSet.h:78` (`volatile long mLock`: only the low half is ever written).
- **wx API types** (`long` style, item index): `ScrFileCtrl.*`, `ScrWatchCtrl.cpp:11`, `ScrDebugWindow.cpp:463`, `WEmitterWx.cpp:83`, `WxRuntimeTypes.*`, `Sim.cpp:370`, `WD3DViewport.h:124`, `ResolutionCommands.cpp:72,78`.
- **Range-checked `strtol`** (LP64-17), **`ftell`/`fseek`** (`CSaveGameRequestImpl.cpp:341`, `MemBufferStream.cpp:340`, `CrashReport.cpp:810,1057`, `LuaObject.cpp:6844-6858`), **`timespec::tv_nsec`** (`gpg/core/utils/Sync.h:45`, `WinCompatSync.cpp:90,599`).
- **Hash callers that pre-cast to int32/uint16:** `SOCellPos.h:66`, `Cluster.cpp:872,890,2238`.
- **Movie/audio volume:** `IndependentSfdBackend.*`, `DirectSoundMovieSink.*`, `StartupHelpers.cpp:6004`.
- **Others:**
  - `WinMain.cpp:560-561` affinity bit index;
  - `Shadow.cpp:27,534` (HRESULT as `long`);
  - `LuaObject.cpp:13016-13100` (GC-verify counters and `%ld`);
  - `RIntegerTypes.*`/`Reflection.*` `long` RTypes (LP64-09);
  - `Sim.cpp:3711` (dead branch);
  - Wild Magic `Wm3System.cpp` timer;
  - every other match is a comment or the text "too long".
- **Not built on macOS:**
  - `moho/misc/CrtRuntimeHelpers.cpp` (81 `long`s; MSVC CRT re-implementation; not in `sources.cmake`);
  - `src/zlib/*`, `src/libpng/*`, `gpg/core/streams/ZLibDeflateRuntime.cpp`;
  - `gpg/gal/backends/{d3d9,d3d10,d3d11,d3d12,dxgi}`;
  - `ID3DTextureSheet.h:13-19` (the `RECT{long...}` branch is compiled only when neither `_WIN32` nor `FAF_POSIX` is defined).

## Adjacent findings (not LP64, found while auditing)

- **A1 — non-atomic stat updates on POSIX (verify before milestone 1).**
  - `moho/sim/CArmyStats.cpp:100-128` (`#if defined(_MSC_VER)`) and `moho/misc/StatItem.cpp:476-575,2203-2225` (`#if defined(_WIN32)`) take a plain read-modify-write fallback on POSIX.
  - The upstream clang-cl build defines both macros, so this path is new on macOS. The sim has `CSimWorkerPool::ParallelFor` jobs (`Sim.cpp:4620,10573`, `Unit.cpp:12509`, `UnitUpdateFill.cpp:82`).
  - If any of them touches army stats, updates can be lost. Army stats are readable from sim Lua through `GetArmyStat`, so a lost update could desync the replay.
  - **Fix.** Take the Interlocked branch under `FAF_POSIX` too; compat `Sync.h` provides it.
- **A2 — `luaO_str2d` uses `std::strtod`** (`lua/LuaObject.cpp:17675-17690`).
  - macOS (and the UCRT) parse hex (`"0x10"`), `"inf"` and `"nan"`. The VC8 CRT in the original binary does not, so `tonumber("0x10")` / `"0x10"+1` returns nil or errors there.
  - This is shared with upstream x64, so it is not a POSIX regression. A strict decimal-only parser would match the original.
- **A3 — float→int conversion of NaN/out-of-range values.**
  - arm64 `fcvtzs` saturates (NaN → 0); x86 `cvttss2si`/`fistp` give 0x80000000. `-fno-strict-float-cast-overflow` keeps the native behaviour.
  - This hits every `static_cast<int>(float)` in the sim, not just `lrintf` (LP64-01). It needs its own audit or a cast helper.

## Lead requests

1. **LP64-01.** Choose the `lrintf`/`lrint` strategy: a force-included WinCompat shim, or a guarded macro at the 109 sites. The shim belongs to a lead-owned file (`WinCompat.h` plus an area header).
2. **Assign owners for the guarded fixes:**
   - LP64-06 (`legacy/containers/HashMap.h`), LP64-07 (`gpg/core/containers/String.cpp`), LP64-08 (`ReadArchive.cpp`): core-sys;
   - LP64-03 (`SaveGameFileHeader.h`, `CSaveGameRequestImpl.cpp`, `SessionStartup.cpp`);
   - LP64-04 (`CWldMap.cpp`), LP64-05 (`WldSessionInfo.cpp`), LP64-10 (`Cluster.cpp`), LP64-13 (`Sim.cpp:6020`): sim-game;
   - LP64-02 and LP64-11 (`lua/LuaObject.cpp`);
   - LP64-14 (`UiRuntimeTypes.cpp`): wx/ui;
   - LP64-15 (`platform/Platform.h`).
3. **Keep the format checks that `LayoutAsserts.h` currently disables.**
   - `LayoutAsserts.h` turns off every `static_assert`, including those on pointer-free file and wire formats. `SaveGameFileHeader.h`'s `sizeof == 0x2028` would have caught LP64-03.
   - Suggestion: a `FAF_FORMAT_ASSERT(...)` macro that `LayoutAsserts.h` leaves alone, used for `SSaveGameFileHeader`, `SPacketBody*`, the zip records, `SScaHeader`/`SScmFile`, `PacketLogRecord` and `SWorldMapFileHeader`.
4. **Optional checks.**
   - Add `__attribute__((format(printf, …)))` to `gpg::Logf`/`Warnf`/`Debugf`/`STR_Printf` and `Sim::Logf` under `FAF_POSIX`, so clang reports `%lu`/`%d` width mismatches (LP64-16).
   - Keep the raw-I/O probe as a CI check. It is a shadow `Stream.h`/`BinaryReader.h`/`BinaryWriter.h` that calls a `[[deprecated]]` marker for `long`/`unsigned long`/`wchar_t` `T`. Run it with `macos/tu.sh <tu> -fsyntax-only -Wdeprecated-declarations -iquote <probe-dir>`.
5. **A1 needs an owner** (sim-game / core-sys) before milestone-1 verification.
