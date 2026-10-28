# platform: wave 2 notes

Package "platform" succeeds wave-1 core-sys and adds Lua, Sockets and the Platform headers.
It owns `src/gpg/core/**`, `src/moho/core/**`, `src/lua/**`, `src/legacy/**` (not
`legacy/math/X87Math*`), `src/moho/net/**`, `src/platform/*.h` (not `WxWidgets.h`), the empty
SDK stand-ins in `src/platform/posix/include/` (not `immintrin.h` or `d3d*.h`),
`dependencies/boost_1_34_1`, `macos/exclusions.d/net.cmake`, and the compat areas Errors,
Sync, Threads, Time, Files, Strings, Memory, Modules, Sockets and Misc. Wave-1 notes:
`core-sys.md` and `net.md`, which still describe those areas.

## Status

- **Every TU in the package compiles** with `macos/tu.sh`: all 109 owned TUs, that is the
  engine TUs under the owned directories plus the ten owned `WinCompat*.cpp`. The only
  warnings are two `-Wself-assign-field` in boost's `concept_check.hpp` (see "Warnings").
  The same run also compiled `legacy/math/X87Math.cpp`, `WinCompatGdi.cpp` and
  `WinCompatWindow.cpp`, which are not mine, so the lists below say 112.
- **Link probe: no unresolved symbol is referenced from a platform-package object.**
  - **Method.** `build/platform-tests/link_check.py` compiles the package's TUs afresh. It
    links them with the 834 other faf_engine objects already in `build/macos` (read only)
    and with Wm3Foundation, zlib, CoreServices and ApplicationServices. Six objects were
    missing because those TUs had not compiled at lead1.
  - **Result.** The link leaves 334 undefined symbols in all. The only ones referenced from
    the package's objects are the 27 `msvc8_x87_*` from `X87Math.cpp`, which the
    determinism math area provides and which are not mine. `___RTtypeid` is gone.
- **`moho/sim/Sim.cpp` now compiles.** Its only error was `sscanf_s`.
- **Reflection's `___RTtypeid` link blocker is gone.**
- **Whole-engine check: 952 of 952 engine TUs compile.** All of them were recompiled with
  `tu.sh` against this wave's header changes: the `Vector.h` layout and the boost control
  block. Other agents' concurrent fixes were in the tree too, so the seven lead1 failures
  also compile now.
- **Warnings fell from 874 (census-lead1) to 34.** What remains is 30
  `-Wundefined-bool-conversion` and 2 `-Winvalid-noreturn`, upstream code outside this
  package, plus the 2 boost `-Wself-assign-field` explained below.
- Nothing was committed. `cmake`/`ninja` were not run on `build/macos`. Test programs live
  in `build/platform-tests/` (sources, objects, `cc.py`, `runtu.sh`, `link_check.py`).

## 1. sscanf_s / swscanf_s (compat Strings)

Files: `compat/Strings.h` (declarations and contract) and `WinCompatStrings.cpp` (section
"Secure scanf"). Added `sscanf_s`, `vsscanf_s`, `swscanf_s` and `vswscanf_s`.

Semantics (MSVC):
- **Counted text conversions.** `%c`, `%s`, `%[` (and MSVC's `%C`/`%S`) take an
  `unsigned` element count after the pointer.
  - The count must cover the characters, plus the terminator for `%s` and `%[`.
  - When the token does not fit, the buffer gets an empty string (`buf[0] = 0` if the
    count is > 0) and errno is ENOMEM. The scan stops and returns the fields assigned so
    far.
  - A `size_t` count (`sizeof buf`) also works, because a Darwin arm64 vararg slot is 8
    bytes.
- **Text conversions are scanned by the layer.** This keeps the buffer rules and the "C"
  locale's narrow ⇄ wide mapping, where a byte is the code unit of the same value.
  - In `sscanf_s`, `%S`, `%C`, `%ls` and `%wc` are wide.
  - In `swscanf_s`, `%s` and `%c` are wide, and `%S`, `%C` and `%hs` are narrow. A wide
    character above 0xFF gives EILSEQ and stops the scan.
  - Scansets support `^`, a leading `]` and ranges (either order). Members compare as
    unsigned units.
- **Numeric conversions use the C library's own scanner**, one directive at a time with a
  trailing `%n`.
  - Size prefixes are MSVC's: `l` on an integer stores a 32-bit Windows `long`; `I64`,
    `I32` and `I` are accepted. `%n` counts from the start of the input.
  - `%f` into a float is correctly rounded on both sides (FreeBSD `strtof`; UCRT parses
    float directly), with no double rounding. A test checks this.
- **Return values.** EOF only for an input failure before the first conversion
  (suppressed ones count as conversions). A null buffer or format goes to the invalid
  parameter handler and returns EOF/EINVAL.
- **Not implemented:**
  - `fscanf_s`: no engine caller, and a stream needs push-back for the buffer rule.
  - `%[` ranges are not locale-aware. The UCRT's "C" locale is not either.

## 2. `__RTtypeid` (link blocker)

- **`faf_posixRTtypeid(void*)`** is new in `compat/Misc.h` and `WinCompatMisc.cpp`.
  - It returns the most-derived object's `std::type_info`, read from the vtable's typeinfo
    slot at [-1] (Itanium ABI 2.5.2). Every vtable of a group carries the complete
    object's RTTI, so a pointer to a secondary or virtual base works.
  - It throws `std::bad_typeid` for null, as vcruntime does.
- **`gpg/core/reflection/Reflection.cpp:185`.** The `FAF_POSIX` branch defines a static
  inline `__RTtypeid` that forwards to it, so the call site at :866 is unchanged.
- **Callers.** The only caller already returns early for null and wraps the call in
  `try { } catch (...) { nullptr }`. Both are kept.
- **Not reproduced.** vcruntime's `__non_rtti_object` (an access violation caught by SEH) has
  no POSIX equivalent; a bad pointer faults.

## 3. boost V-1: a 32-bit control block (and LP64-10)

- **New boost header.** `dependencies/boost_1_34_1/boost/detail/sp_counted_base_faf_posix.hpp`
  is a copy of `sp_counted_base_w32.hpp` with `LONG use_count_, weak_count_`. It drops only
  the VC6 workaround and behaves line for line like the original.
- **Selecting it.** `sp_counted_base.hpp`'s `FAF_POSIX` branch now includes it instead of
  the w32 header.
- **Layout.** It is `{vptr, int32 use, int32 weak}` with `sp_counted_impl_p::px_` at
  +0x10, the Windows x64 layout. That is what `boost::SpCountedImplStorage`
  (`BoostWrappers.h:1287`), Cluster.cpp and the render dispose helpers assume.
- **Patch.** The new file is marked intent-to-add (`git add -N`) in the boost repo, so
  `git -C dependencies/boost_1_34_1 diff` includes it. `boost_1_34_1_faf_macos.patch` was
  regenerated that way (6 files).
  - It reverse-applies to the tree.
  - `git archive HEAD` + `patch -p1` reproduces the tree exactly.
  - Nothing was committed.
- **`gpg/core/algorithms/Cluster.cpp` (`FAF_POSIX` only):**
  - `SharedCountControl`'s counts are `volatile LONG` (LP64-10).
  - **A second bug, fixed in the same place.** `ReleaseSharedCount` called `vtable[1]` and
    `vtable[2]` as `dispose`/`destroy`, which are MSVC's slots. Under the Itanium ABI the
    virtual destructor takes two slots, so `[1]` is the **deleting destructor**: it would
    have freed the control block before `dispose`, leaked the ClusterCache, then
    decremented freed memory.
  - The POSIX branch calls `dispose()`/`destroy()` through `boost::detail::sp_counted_base`
    instead. The block really is one: its vtable comes from a probe class derived from it
    in BoostWrappers.cpp.
- **Verified with the engine flags** (`test_sp_counted`):
  - `sizeof(sp_counted_base) == 16`, `sizeof(sp_counted_impl_p<T>) == 24`, and `px` is at
    +0x10.
  - Interlocked on the +0x08 count is visible to `use_count()`.
  - Copy, weak lock and expire work, including 8 threads × 100k copy/lock.
  - Cluster's POSIX release sequence disposes once and frees the block.

## 4. LP64 items

- **LP64-15** (`platform/Platform.h`): under `FAF_POSIX`, `LODWORD`/`HIDWORD` go through
  `std::uint32_t*`, and `HIDWORD` is the second dword. Nothing uses them today.
- **LP64-07** (`gpg/core/containers/String.cpp`, `FAF_POSIX` branches). Wide strings are
  UTF-32 here. **Every result is Windows' 16-bit value, except one:**
  - **Decode, the exception.** A complete 4-byte sequence is one `wchar_t` with the whole
    code point (lead bits included). Its low 16 bits equal Windows' truncated value, so
    16-bit consumers such as `CD3DFont::GetCharInfo` see no change.
  - **Decode, invalid bytes.** An invalid lead (0x80..0xBF, 0xF8..0xFF) gives Windows'
    sign-extended 16-bit unit 0xFF80..0xFFFF, not a negative `wchar_t`. A truncated
    4-byte sequence keeps only the low 16 bits, as on Windows. The cursor advances exactly
    as on Windows in every case.
  - **Encode.** A value 0x10000..0x1FFFFF is written as 4-byte UTF-8, so it round-trips.
    Everything else is encoded as the 16-bit unit Windows holds. For example, the stray byte
    0x96 decodes to 0xFF96 and re-encodes as `EF BE 96`, as on Windows.
- **LP64-11** (`lua/LuaObject.cpp`):
  - **`os.clock`** (`io_clock`) calls the new compat Time function
    `faf_posixClock()`. It returns UCRT `clock()` semantics: wall-clock ms since the
    process started (from the kernel's `p_starttime`, then the monotonic clock), as a
    32-bit value, -1 past 2^31-1. POSIX `clock()` was CPU µs.
  - **`os.date` compiled already**, through the compat `_gmtime64`/`_localtime64`.
  - **New: the format** is rewritten to the UCRT "C" locale forms.
    - `%c` → `%m/%d/%y %H:%M:%S`. This is os.date()'s default; macOS printed
      `Tue Nov 14 22:13:20 2023`.
    - `%x` → `%m/%d/%y`; `%X` → `%H:%M:%S`.
    - `%#c`/`%#x` give the long forms. `%#d` and the other numeric fields → `%-d` (no
      leading zero).
- **LP64-02, LP64-06 and LP64-08 checked.**
  - LP64-02: `luaB_tonumber` uses the 32-bit `StrToULong32`.
  - LP64-06: `msvc8::hash_value(std::int32_t)`.
  - LP64-08: binary `ReadLong`/`ReadULong` were done. I added the remaining **text**
    archive half: `TextReadArchive::ReadLong`/`ReadULong` now parse into an int32/uint32
    (Windows range) and widen.

## 5. Lua non-finite numbers

New header `src/lua/LuaPosixNumbers.h` (`FAF_POSIX` only, header-only, tested standalone).
`LuaObject.cpp` includes it under `FAF_POSIX`.

### (a) Spelling, as the UCRT printf does

| Value | UCRT (and now POSIX) | macOS printf |
|---|---|---|
| +inf / -inf | `inf` / `-inf` | same |
| x86 default NaN 0xFFC00000 (→ double 0xFFF8000000000000) | `-nan(ind)` | `nan` |
| other negative quiet NaN | `-nan` | `nan` |
| positive quiet NaN | `nan` | `nan` |
| signalling NaN (double) | `nan(snan)` / `-nan(snan)` | `nan` |

- **Upper case** for `%E`/`%G`: `-NAN(IND)`, `INF`.
- **Flags.** `+`/space give the sign of a non-negative value, and width and `-` justify.
  Precision does not apply.
- **Unverified against the UCRT.** Padding under `0` uses spaces, and `#` does nothing.
  Both follow the C standard's rule for infinities and NaNs; the hardware oracle should
  check them.
- **A float sNaN is unreachable.** It is quieted by the float→double widening on x86 too,
  so Lua shows it as `nan`.

Sites covered:
- `luaV_tostring`, which serves `tostring`, `print`, concatenation, `lua_tostring` and
  `luaO_pushfstring("%f")`.
- `string.format`, conversions `e E f g G`.
- `io.write`/`file:write` of a number.

Not covered:
- `io.read("*n")`, which uses `fscanf("%lf")`.
- `LuaCrashDump`'s `%g` (diagnostics).
- Engine C++ that formats Lua numbers itself.

### (b) NaN encoding

- **The difference.** arm64's default NaN is 0x7FC00000 (measured in the test); x86's is
  0xFFC00000.
- **The rule.** A NaN result whose operands are all non-NaN is set to 0xFFC00000. When an
  operand is a NaN, propagation already agrees between the two, so it is untouched.
- **Cost.** The common path is one compare on the result.

Sites covered:
1. **VM arithmetic:** `kOpAdd` (inf + -inf), `kOpSub` (inf - inf), `kOpMul` (0 × inf),
   `kOpDiv` (0/0, inf/inf), through `faf_lua_posix::FreshNaNToX86(result, a, b)`.
   - `kOpUnm` (`-0 - x`) cannot create a NaN.
   - The bit operators give integers.
   - `kOpForLoop` never stores a NaN index: the comparison fails first.
   - This fork has no `OP_POW`/`OP_MOD`; `^` goes through the global `__pow`.
2. **`math` library:** a `FAF_POSIX` copy of `kLuaMathLib` registers
   `LuaMathX86NaN<fn, argCount>` wrappers.
   - **Wrapped:** `sin cos tan asin acos atan atan2 ceil floor mod frexp ldexp sqrt log
     log10 exp deg rad pow`, and the global `__pow` that `^` uses.
   - **How the wrapper works.** It runs the function. For each NaN result it checks the
     function's own numeric arguments, using Lua's string coercion. If none is NaN, it
     stores 0xFFC00000.
   - **Not wrapped:** `abs`, `min`, `max`, `random` and `randomseed`, which cannot make a
     NaN.
   - **Why at the Lua boundary.** This holds whatever implementation sits below:
     `msvc8_x87_*` (x87's own indefinite, already 0xFFC00000), the interim `faf_crtmath`,
     or libm (0x7FF8…, measured by determinism).
   - **Assumption: the x64 UCRT/AMD LibM special-value lanes return the indefinite
     (0xFFF8000000000000) for every invalid operation.** This holds for the
     `INDEFBITPATT_DP64` returns. It should be confirmed on the hardware oracle, together
     with `faf_crtmath`. If one lane returned +NaN on x64, drop that function's wrapper.
3. **`luaO_str2d`** (`tonumber`, string coercion): a parsed NaN becomes the UCRT's NaN.
   `[sign]nan(ind)` in any case is 0xFFC00000, and every other spelling is a quiet NaN with
   the parsed sign and no payload. macOS strtod kept n-char payloads in the float
   (`nan(0x3FFFFFFFFFFF)` gave 0x7FC1FFFF) and read `nan(ind)` as positive. Now
   `tonumber(tostring(0/0))` round-trips to 0xFFC00000.
   - Assumption: UCRT gives the indeterminate (negative) for `nan(ind)` whatever its sign.
     `-nan(ind)`, the spelling the UCRT prints, is negative under either reading.

Not covered: NaNs made by engine C++ (sim code) and pushed with `lua_pushnumber`. This is the
general hardware difference in DETERMINISM.md §2.3.

## 6. V-2 and Vector.h

- **V-2.** `RegOpenKeyExA`, which `RegOpenKeyExW` calls, now logs once with
  `FAF_POSIX_STUB`. `RegCreateKeyEx` already did. The other registry functions need an open
  key, and none can exist.
- **`legacy/containers/Vector.h:1695`.** `[[no_unique_address]]` replaces
  `[[msvc::no_unique_address]]` under `FAF_POSIX`.
  - This silences the 831 `-Wunknown-attributes` warnings.
  - `msvc8::vector<T, false>` is 24 bytes, as on Windows x64 (it was 32);
    `msvc8::vector<T>` stays 32. Measured in `test_string_utf8`.
  - **This changes the in-memory layout of every type that embeds a
    `msvc8::vector<T,false>`, back to Windows'.** Every TU must be rebuilt; ninja tracks
    the header.

## 7. Warnings in the owned directories (census-lead1.log)

| Warning | Where | Action |
|---|---|---|
| `-Wnull-dereference` | `moho/net/CNetUDPConnector.cpp:975` | Fixed. The `net_DebugCrash` store is `volatile` under `FAF_POSIX`; clang deleted the non-volatile null store, so the deliberate crash never happened. |
| `-Wdelete-incomplete` ×6 | `gpg/core/utils/BoostWrappers.cpp:1492,2155` | Fixed. The D3D9/D3D10 dispose and unwind lanes (4 + 2) are unbuilt types on POSIX. They now report with `FAF_POSIX_STUB` and `std::abort()` if a live object ever reaches them, instead of `delete` on an incomplete type, which skips the destructor. Windows is unchanged. |
| `-Wconstant-conversion` | `moho/sim/WldSessionInfo.cpp:164` | Not mine (sim-game, LP64-05). |
| `-Wself-assign-field` ×2 | `boost/concept_check.hpp:223,260` (from CLobby.cpp) | Justified, left. It is boost's deliberate `a = a` assignability check. MSVC's STL defines `_ITERATOR_`, which compiles it out, so only libc++ builds see it. No codegen effect. |

## Tests (all in `build/platform-tests/`, built with the engine's flags from compile_commands)

| Program | What | Result |
|---|---|---|
| `test_scanf_s` | 61 checks. `%f`, as in Sim.cpp, including correct float rounding and hex; integers with MSVC prefixes (`%ld` writes exactly 4 bytes, `I64`/`I32`/`I`/`h`); suppression; `%n`; `%%`; literals; EOF rules; `%s`/`%c`/`%[` counts (fit, exact fit, too small → empty and ENOMEM and stop, width, `size_t` count); scansets (negation, ranges, `]`, high bytes); wide/narrow crossings in both hosts; invalid parameters | 0 failures |
| `test_rttypeid` | 13 checks. Single, multiple (secondary base at another address) and virtual-diamond inheritance; agreement with `typeid`; null throws `bad_typeid` | 0 failures |
| `test_sp_counted` | 22 checks. Layout (16/24, px@0x10); counts through the engine view; Interlocked on the counts; weak lock and expire; 8-thread stress; Cluster's POSIX release sequence | 0 failures |
| `test_string_utf8` | 2.6M checks. Decode of every lead byte × 4000 random and edge continuations against a literal 16-bit transcription of the Windows code (cursor equal; value equal, or the full code point with equal low 16 bits for complete 4-byte sequences); encode of all 65536 16-bit values byte-identical to Windows, limit handling too; negative values; every 7th supplementary code point round-trips; `STR_Utf8ToWide`/`STR_WideToUtf8` on emoji and CP1252 bytes; `msvc8::vector` sizes | 0 failures |
| `test_lua_numbers` | 52 checks. arm64 `0*inf` = 0x7FC00000; `FreshNaNToX86` on inf-inf, 0*inf, 0/0, inf/inf, sqrt(-1) → 0xFFC00000; propagated payloads and signs unchanged; spellings for every class and the `string.format` flag/width forms; `UcrtStrtodNaN` spellings; the `tostring(tonumber(tostring(0/0)))` round trip; `faf_posixClock` (starts near process start, +250 ms over a 250 ms sleep, not CPU time with 2 busy threads) | 0 failures |
| `test_strftime` | `UcrtStrftimeFormat` copied verbatim from LuaObject.cpp: `%c %x %X %#c %#x %#d %#H %#m %%`, a literal, and a trailing `%`, through macOS strftime | 9/9 ok |
| `tu.sh` on the owned TUs (`runtu.sh mine.txt tu-mine`; 112 entries including X87Math, WinCompatGdi and WinCompatWindow) | compile | 112 ok, 0 failed |
| `link_check.py` | link probe (see Status) | 0 unresolved symbols from package objects, apart from X87Math's `msvc8_x87_*` |
| `tu.sh` on the other 840 engine TUs (`runtu.sh others.txt tu-others`) | compile against the Vector.h and boost changes | 840 ok, 0 failed |

The Lua VM and `math` wrapper edits are not exercised by a running interpreter here; that
needs the engine link. Their helpers are what the tests cover, and LuaObject.cpp compiles
cleanly.

## Requests

- **For moho/misc (sim-game or whoever owns it):** `moho/misc/CrtRuntimeHelpers.cpp:15616`
  `EngineReleaseSharedControlBlock` has the same two problems Cluster.cpp had.
  - Its `{vtable, volatile long, volatile long}` view assumes 4-byte pointers (a Win32
    layout; it static_asserts 0x0C), and it calls `vtable[1]`/`vtable[2]` with MSVC slot
    numbers.
  - It has no callers today. If it gains any, it needs the Cluster.cpp treatment: `LONG`
    counts after an 8-byte vptr, and dispatch through `boost::detail::sp_counted_base`.
- **For every package: hand-indexed vtables are MSVC-ABI.** Under the Itanium ABI a virtual
  destructor occupies two slots, so every slot after it moves by one. A grep for
  `vtable[N]`/`vtbl[N]` in the engine found only the two shared-count sites above.
  `Cluster.cpp`'s `LoadVtableDwordSlot04`/`15` are unused.
- **For the lead:**
  - **Rebuild.** The control-block layout (boost header) and `msvc8::vector<T,false>`
    (Vector.h) change object layouts. The link phase must rebuild everything, which ninja's
    header dependencies will do.
  - **Boost patch generation.** The boost repo's index now has an intent-to-add entry for
    `boost/detail/sp_counted_base_faf_posix.hpp`, which is how `git diff` includes it.
    Whatever regenerates or verifies the patch must keep that, or use
    `git diff` + `git add -N`.
- **For determinism:**
  - **Hardware oracle.**
    - Confirm on the Windows PC that UCRT x64 returns the indefinite NaN
      (0xFFF8000000000000) for the domain errors of `fmod`, `pow`, `acos`, `asin`, `log` and
      `sqrt`. The Lua `math` wrappers assume it.
    - Confirm `printf("%08f")` and `printf("%#.0f")` with inf: the spelling helper pads with
      spaces and ignores `#`.
  - **`faf_crtmath`.** Once it reproduces the UCRT special values exactly, the math-library
    wrappers become no-ops. Keep them anyway: they are a cheap guard.
