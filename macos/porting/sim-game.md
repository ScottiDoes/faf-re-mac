# sim-game (wave 2): simulation and game logic

Owns `src/moho/**` except `moho/{core,net,render,mesh,particles,audio,movie,ui,app}`.
No `macos/exclusions.d/sim-game.cmake`: every TU in the package builds in place.

## Result

| TU | Before | Now |
|---|---|---|
| `moho/console/CConCommand.cpp` | `std::exception("...")` (MSVC-only constructor) | compiles |
| `moho/path/PathTables.cpp` | `GPG_PREREGISTER_INIT` undeclared | compiles |
| `moho/misc/StartupHelpers.cpp` | D3D10/D3D9 device headers, then `std::ofstream(const wchar_t*)` | compiles (no warnings apart from the shared `msvc::no_unique_address`) |
| `moho/sim/Sim.cpp` | `sscanf_s` (compat Strings, platform package) | **still waits for `sscanf_s`**. Not edited for it, as instructed. Probed with `-Dsscanf_s=sscanf`: nothing else fails, so it compiles as soon as compat Strings declares `sscanf_s` |

Every other TU touched below compiles with `macos/tu.sh` (0 errors, 0 new warnings).

## Edits (all guarded; the Windows preprocessed text is unchanged)

### 1. The failing TUs

- **`moho/console/CConCommand.cpp:1471`** (`CON_Debug_Throw`). Under `FAF_POSIX` it throws a local `DebugThrowException final : std::exception` whose `what()` returns `"Hope you really wanted to do this..."`.
  - It derives directly from `std::exception`, not from `std::runtime_error`, so it matches exactly the handlers a plain MSVC `std::exception` matches.
  - Handlers checked: `CON_Execute` catches only `msvc8::runtime_error` around `command->Handle` (`:1198`), so the exception propagates past it on both platforms. Any `catch (const std::exception&)` or `catch (...)` further out catches it, with the same `what()` text. The only `catch (const std::runtime_error&)` handlers in the tree (`lua/LuaParser.cpp`, `BitArray2D.cpp` probes) are not on this path, and would not catch either type anyway.
- **`moho/path/PathTables.cpp:35`**: `#ifdef _WIN32` widened to `|| defined(FAF_POSIX)`, for `<windows.h>` (the stand-in) and `StaticInitPhase.h`.
- **`moho/misc/StartupHelpers.cpp`**:
  - `:38-39`: the `DeviceD3D10.hpp` and `DeviceD3D9.hpp` includes are under `#if !defined(FAF_POSIX)`. Nothing in the file names a D3D9/D3D10 type; it only uses `gal::Device`, `DeviceContext`, `Head` and `HeadAdapterMode` from `Device.hpp`.
  - `:6136` (`USER_SavePreferences`): POSIX opens `<prefs>.new` by its UTF-8 path, `STR_WideToUtf8(temporaryPathWide)`.
  - Note: Windows opens it in text mode (CRLF); POSIX writes LF. Lua reads both. It is a local file only.

### 2. A1: non-atomic stat updates (WAVE1_REPORT 4.3, LP64_AUDIT A1)

These guards were widened so that POSIX takes the Interlocked branch:

| File | Lines | Guard now |
|---|---|---|
| `moho/misc/StatItem.cpp` | 478, 487, 504, 524, 562, 1555, 2202 | `_WIN32 \|\| FAF_POSIX` |
| `moho/unit/core/Unit.cpp` | 1579, 6823 | `_WIN32 \|\| FAF_POSIX` |
| `moho/entity/EntityDb.cpp` | 225 | `_WIN32 \|\| FAF_POSIX` |
| `moho/entity/Entity.cpp` | 346 | `_WIN32 \|\| FAF_POSIX` |
| `moho/sim/CArmyStats.cpp` | 102, 116 | `_MSC_VER \|\| FAF_POSIX` |

- The `_InterlockedExchangeAdd`/`_InterlockedCompareExchange` spellings at CArmyStats.cpp:102/116 are routed by compat `Sync.h`'s macros, so `<intrin.h>` (`:24`, `_MSC_VER` only) is not needed.
- The `-Wdeprecated-volatile` warning from the old `++mPrimaryValueBits` fallback (StatItem.cpp:1558) is gone.
- **Grep of every `_WIN32`/`_MSC_VER`/`_M_*` guard in the package.** After these edits, the rest are:
  - `_M_IX86`-only 32-bit layout asserts. They are inactive on x64 as well.
  - `XFileError.h`/`XException.h` layout asserts.
  - `Vector4f.h` `MOHO_USE_SSE2`. The scalar `VMatrix4` fallback has the same operation order, as wave 1 verified.
  - `SimWorkerPool.cpp` x87 control word (`_M_IX86` only).
  - `CSquad.cpp` `_stricmp`/`strcasecmp`. These are identical, because compat `_stricmp` is `strcasecmp`.
  - The `/interlocked` and `/genpath` gates (below).

  No other atomic fallback exists.
- **Unguarded Interlocked sites.** These all reinterpret an int32 as `volatile long*` and go through the 32-bit compat overloads:
  - CAiBrain.cpp, UnitWeapon.cpp, CountedObject.cpp, InstanceCounter.h, SimDriver.cpp, Shoreline.cpp;
  - the terrain files;
  - CEfxEmitter.cpp.

### 3. LP64 and determinism sites

- **LP64-05, `moho/sim/WldSessionInfo.cpp:164`.** Under `FAF_POSIX`, `CreateSemaphoreW(nullptr, 0, 0x7FFFFFFF, nullptr)`. The `-Wconstant-conversion` warning ("changes value from 9223372036854775807 to -1") is gone.
- **LP64-04, `moho/sim/CWldMap.cpp:1401`** (`RWldMapPreviewChunk::Save`). Under `FAF_POSIX` the name is written as `char16_t` units, `static_cast<char16_t>(unit)` per wide unit, plus a NUL.
  - These are the same 16-bit units Windows emits. Windows' `STR_Utf8ToWide` keeps the low 16 bits of a non-BMP code point; a negative macOS `wchar_t` from an invalid lead byte becomes 0xFF80..0xFFFF, the Windows value.
  - The loader at `:1299` already reads `uint16` units.
- **LP64-13, `moho/sim/Sim.cpp:6020`.** Under `FAF_POSIX` the format is `"    mMaxStorage[%d]=\n"`.
  - The MSVC CRT's non-validating printf consumes `I64` as a size prefix and prints the `\n` as text, so the Windows line is `"    mMaxStorage[0]=\n"`.
  - Measured here: Apple's printf prints `=I64\n`.
- **`moho/collision/CColPrimitiveBase.cpp:155`** (box SAT overlap). Under `FAF_POSIX` the two calls are explicit selects:
  - `fmin(a, b)` becomes `isnan(a) ? b : (b < a ? b : a)`;
  - `fmax(a, b)` becomes `isnan(a) ? b : (b > a ? b : a)`.

  Checks:
  - **x64 lowering.** I compiled the same loop for `-target x86_64-pc-windows-msvc -O2`. `fmin` is `cmpunordss` on a, then `minss` with b as the destination, `(b < a) ? b : a`, then a select. `fmax` is the same with `maxss`. Neither operand is "known never NaN" at this site, even after the early-out compare, so the generic three-instruction form applies. It matches DETERMINISM.md section 5 rule 6.
  - **Engine object.** No `fminnm`/`fmaxnm` and no FMA. The selects are `fcmp`/`fccmp`/`fcsel vs`.
  - **Bit test** (`build/sim-game-tests/fminmax`). std::fmin/fmax compiled for x86_64 and run under Rosetta, against the port expressions on native arm64, for 4096 input pairs: ±0, ±inf, quiet and signalling NaNs with payloads, denormals, max, and random values. **0 differences.**

    In the full overlap expression, the only differences are `inf - inf`: x86 produces the default NaN `0xFFC00000`, arm64 produces `0x7FC00000`. That is DETERMINISM.md section 2.3, a hardware difference, not the min/max.
- **`/interlocked` and `/genpath` gates.** `moho/sim/CWldSessionLoaderImpl.cpp:34` and `moho/path/PathTables.cpp:1208` were widened to `_WIN32 || FAF_POSIX`, so they read `GetCommandLineA()` (compat Misc builds it from `__argv`).
- **LP64-03 (save-game header `wchar_t[1024]`).** Not milestone 1, not changed.
  - The fix is unchanged from LP64_AUDIT: `char16_t` arrays under `FAF_POSIX` in `moho/serialization/SaveGameFileHeader.h:34-35`, and `static_cast<char16_t>` in `CopyWideStringTruncated` (`CSaveGameRequestImpl.cpp:50`).
  - Any reader must widen.

### 4. Survey of other Windows-only behaviour on the replay path (package directories)

| Item | Finding |
|---|---|
| Direct `std::` transcendentals | Same 15 sites as DETERMINISM.md 2.1, plus a console command at CConCommand.cpp:2481. None is on a sim path. They stay libm calls (`-fno-builtin-*`, `-fmath-errno`) and bind to `faf_crtmath` at link time |
| `rand`/`srand`/`<random>`/`qsort` | None in the package (Lua's `math.random` is in `src/lua`) |
| `long` in serialized or checksummed structs | None. The only `long` member is `SFileWaitHandle::mLock` (`FileWaitHandleSet.h:78`), in memory only, used through 32-bit Interlocked |
| Other `long`s | `strtol` uses are range-checked or bounded (`Unit.cpp:1443`, `Sim.cpp:1940,4419`, `CSimConVarInstanceBase.h:57`), so the results match Windows. `hash_value(static_cast<long>(...))` goes through core-sys's int32 `hash_value` |
| Checksum inputs (`Sim::UpdateChecksum`, `REF_UpdateMD5`) | `SEconTotals` (5 float pairs, then u64 pairs at +40) has no padding on either ABI. The other fields are fixed-width. `REF_UpdateMD5` reflects no `long`/`wchar_t` field (LP64-09) |
| Replay header (`VCR_SetupReplaySession`) | Every `ReadExact` target is fixed-width (`int32` timeouts and seed, `uint8` counts) |
| `wchar_t` file I/O | `_wfopen_s` in `SessionStartup.cpp:1418` and `CSaveGameRequestImpl.cpp:271` goes through compat Files. `CDiskWatch.cpp:149` divides `FileNameLength` by `sizeof(wchar_t)`; this is right as long as the future compat `ReadDirectoryChangesW` fills `FILE_NOTIFY_INFORMATION` with 4-byte units and a byte length. Today it is a stub |
| MSVC printf specifiers | Only LP64-13. `PathTables.cpp:1709` `%08lX` is diagnostic and consistent on macOS (`DWORD`, promoted to `unsigned long` by the `0ul` arm) |
| Sorting | All `std::sort`/`nth_element` calls are on unique keys or non-sim paths. Entity sets are sorted by `Entity::id_` |

## Tests

`build/sim-game-tests/run_tests.sh` uses its own directory and never touches `build/macos`.
1. **fmin/fmax.** x86_64 reference under Rosetta against the arm64 port: 0 of 4096 pairs differ.
2. **A1 atomics** (`atomics/a1_atomics_test.cpp`, compiled by `tu.sh` with the engine flags and `WinCompat.h`). It runs the exact widened idioms (StatItem::AddFloat CAS, `AddIntStatCounter`, the CArmyStats add and CAS, `StorePrimaryStatBitsAtomic`) on 8 threads × 200k iterations.
   - No updates are lost.
   - The neighbouring 4 bytes are never written.
   - Adds wrap at 32 bits; negative values survive the `long` and `LONG` round trip.
   - 8/8 pass.

## Open issues and determinism risks

- **`Sim.cpp` needs `sscanf_s`** in compat Strings (platform package). Nothing else blocks it.
- **Pointer-ordered float sum.**
  - `CArmyStatItem::SumCategory` (`CArmyStats.cpp:455`) sums floats over `mBlueprintStats`, a map keyed by `const RUnitBlueprint*`, so it adds in blueprint-address order.
  - It reaches sim Lua through `CAiBrain:GetBlueprintStat` and `GetCurrentUnits` (`CAiBrain.cpp:3524,3590`).
  - Counts are integers below 2^24, so their sums are exact in any order. Float stats (damage, mass killed) can round differently if macOS malloc orders blueprint addresses differently from Windows. The Windows LFH does not guarantee the order either.
  - Low risk. If a replay desyncs on an army-stat read, look here first.
- **inf - inf default-NaN sign** in the SAT overlap: a hardware difference (DETERMINISM.md 2.3). It is only reachable with infinite box extents.
- **Text-mode files.** `Sim.cpp:5858` (`fopen_s(..., "w")`, the sim log) and the prefs file write LF on POSIX and CRLF on Windows. Normalize line endings before diffing sim logs against Windows.
- **LP64-03** (saves) is still open; see above.
