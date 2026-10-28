# Wave 2 report: macOS native port, link and replay pass

Verifier pass run after every wave-2 agent and the link run had finished. I rebuilt the
engine and the runner from clean, re-ran the replay and the package test suites, audited the
working tree against `src/platform/posix/README.md`, and checked the determinism-critical
code independently. I changed no source file. Nothing was committed. My own files are in
`build/verify-tests/` (untracked) and my notes are in `macos/porting/verify.md`.

## 1. Headline

**Milestone 1 is reached for replay 27890640, and it reproduces from a clean build.**

| | census-lead1 (before wave 2) | Wave 2 (this pass) |
|---|---|---|
| `faf_engine` TUs | 951 | **960** |
| Failing TUs | **7** | **0** |
| Error diagnostics | 31 | **0** |
| Warnings | 874 | **34** |
| `faf_replay` | not built | **links** (15.4 MB, no undefined or duplicate symbols) |
| Replay 27890640 | — | **0 mismatches**, runs to the end of the stream |

### Replay result (fresh binary from this pass)

| Run | Result | Sim speed |
|---|---|---|
| `SPEED=50` (serial sim, the default `/simworkers 0`) | `beats=9711 mismatches=0 exit=end-of-replay` | game ticks 1..9610 in 11.7 s: 824 ticks/s, **82.4x real time** |
| `SPEED=50 ... /simworkers 8` (threaded sim, 8 workers) | `beats=9715 mismatches=0 exit=end-of-replay` | 11.3 s: **84.8x real time** |
| **Negative control:** a copy of the replay with one digest byte flipped at beat 5000, for both players | `mismatches=2`, exit 1 | — |

- **Serial run, wall time and memory:** 26.4 s wall, including load and the end-of-stream wait. Peak RSS 654 MB.
- **Reference speed:** the x64 build under Rosetta does 17.3x on this replay at speed +50.
- **Self-tests:** 75 of 75 pass at start-up.
- **Final tick:** the last game tick is 9610, as in the x64 benchmark. `beats=` varies between runs (9711, 9712, 9715) because of the empty beats issued until the stall timeout.
- **Why the negative control matters:** it needs no instrumented binary. The engine printed its own digest for beat 5000, `fecbe63e2c7637a3c10db12024946f6d`. That is the recorded digest with the flipped byte restored, so at that beat the native sim reproduces the players' checksum exactly, and the check really runs.
  - The tool is `build/verify-tests/corrupt_replay.py`. It found 386 `CMDST_VerifyChecksum` records: 193 checksum beats × 2 players, every 50 beats from 0 to 9600.

## 2. Build census

### Command

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:/Users/scottharris/Gaming/rosettax87/.venv/bin:$PATH
ninja -C build/macos -t clean faf_replay        # everything faf_replay is built from
ninja -C build/macos -k 0 -j 10 faf_engine faf_replay > build/macos/census-wave2.log 2>&1
```

- **Before the clean:** the link run's binary and map were copied to `build/verify-tests/faf_replay.pre-wave2*`.
- **Result:** ninja exited 0 after 1,365 steps in 2 min 05 s wall.
- **Compiles:** 960 `faf_engine` TUs, 3 `faf_runner` TUs, 386 Wild Magic and 12 zlib, all from scratch. There were **0 `FAILED:` lines and 0 errors**.

### The 960 engine TUs

- 934 `src/sources.cmake` entries, with stand-ins at their originals' positions.
- `StaticInitPreRegister.cpp` first.
- 25 platform/posix TUs.

Against census-lead1, 9 TUs are new:
- `WinCompatGdi.cpp`;
- `crt/CrtMath.cpp`, `CrtMathTrace.cpp`, `MsvcRand.cpp`, `SelfTest.cpp`;
- `math/X87Emu.cpp`, `X87Kernels.cpp`;
- `stubs/render/gal/DeviceNull.cpp`, `stubs/render/moho/HeadlessRenderPosix.cpp`.

### Warnings (34)

| Flag | Count | Where | Assessment |
|---|---|---|---|
| `-Wundefined-bool-conversion` | 30 | `this` null checks in `moho/ai/*Navigator*`, `IAiCommandDispatchImpl`, `LAiAttackerImplSerializer`, `CUnitScriptTask`, `SimArmy` | Upstream code. Both builds pass `-fno-delete-null-pointer-checks` (`cmake/FafCodegen.cmake:57`, `macos/CMakeLists.txt:49`), so the checks behave the same on both |
| `-Winvalid-noreturn` | 2 | `CAnimationManipulator.cpp:174`, `CEconomyEvent.cpp:302` | Upstream |
| `-Wself-assign-field` | 2 | `boost/concept_check.hpp:223,260` | Justified (platform.md) |

The 831 `-Wunknown-attributes`, the 6 `-Wdelete-incomplete`, the `-Wnull-dereference`, the `-Wdeprecated-volatile` and the `-Wconstant-conversion` warnings are all gone.

### Post-link checks on the new binary

All four pass:
- **`macos/tests/crt/check_symbols.sh build/macos/faf_replay`:** 287 libSystem imports. All 33 faf_crtmath functions, `rand` and `srand` are defined in the executable.
- **`check_fma.sh` on the objects:** 965 objects (`faf_engine`, `faf_runner`, `libWm3Foundation.a`, `libfaf_zlib.a`), 0 FMA.
- **`otool -tv faf_replay`:** 0 `fmadd`/`fmsub`/`fnmadd`/`fnmsub`/`fmla`/`fmls`.
- **`build/link-run-tests/check_map_order.py`:**
  - `StaticInitPreRegister.cpp.o` first;
  - then the 934 sources.cmake objects in order;
  - then 25 platform/posix objects;
  - then the 3 runner objects.

## 3. Test suites (re-run from their own build directories)

| Suite | Command | Result | Time |
|---|---|---|---|
| crt | `macos/tests/crt/run_tests.sh` | **PASS**: unit tests; self-test 75/75 against the test double; the wrong double gives 20 FAIL lines as it should; `kat_oracle.py --check` in sync; both checkers pass and reject their negative controls | 10 s |
| x87-core | `ninja -C build/x87-core-tests` (rebuilt `X87Kernels.cpp.o`, which had changed since the agent's last build), then `x87core_tests` (full, every suite) | **0 failing tests**: 56 ok lines (kat, ops, conv, exhaustive, helpers, routines, oracle) | 138 s |
| x87-core with the real kernels | `x87core_tests_kernels --quick kat integration` | **0 failing tests** | 0.3 s |
| x87-kernels | `macos/tests/x87-kernels/build.sh`; `kernel_test kat`; `internal_test 2000000`; `kernel_test check build/x87-kernels-tests/full` | **KAT 12/12. internal_test 0 failures. The full 4.01 M-vector corpus has 0 mismatches for all 6 kernels in normal, fast-only and slow-only modes.** The 22 fast-path-undecided inputs are the known structured near-ties | 55 s |

## 4. Rule audit

The audit covered:
- `git diff`: 60 modified engine files, 1,034 insertions;
- `git status --porcelain`;
- the three dependency working trees;
- the lead-owned files.

### 4.1 What passed

- **Every upstream edit is guarded; Windows is unaffected.**
  - **Method:** for each of the 60 modified files I ran `unifdef -UFAF_POSIX` over both `HEAD` and the working copy and compared the results. That gives the Windows view mechanically.
  - **Result:** the only remaining differences are:
    - existing `_WIN32`/`_M_X64`/`_MSC_VER` guards widened with `|| defined(FAF_POSIX)`, plus one `!defined(_WIN32) && !defined(FAF_POSIX)` (`gpg/core/utils/Sync.h:25`). Apple's unifdef does not simplify these partial expressions; on Windows they are unchanged;
    - added comments and blank lines;
    - the known wave-1 I-2 restructure in `DeviceVulkan.cpp:1578-1581`, which is behaviour-identical.
  - **Wave 2's own edits:** 29 files, edited between 18:39 and 20:34. All take one of the allowed forms.
- **No sim math changes on Windows.**
  - POSIX-only sim-path edits:
    - `CColPrimitiveBase.cpp:155`, the fmin/fmax selects (section 5);
    - the Lua VM and math-library NaN encoding (section 5);
    - `HashMap.h` int32 `hash_value`, `ReadArchive` and `luaB_tonumber` 32-bit `strtoul` (wave 1);
    - the `Sim.cpp:6020` log text.
  - Each reproduces the Windows x64 result.
  - `faf_crtmath` only re-encodes NaNs and the exact π results.
- **Win32 types at Windows sizes.**
  - **Probe:** `build/verify-tests/probe/SizeProbe.cpp`, compiled with `tu.sh`. It turns `static_assert` back on after the includes, because the force-included `LayoutAsserts.h` turns it off. A negative-control copy fails as it should.
  - **Windows x64 sizes confirmed:**

    | Size | Types |
    |---|---|
    | 1 | `BYTE`, `CHAR` |
    | 2 | `WORD`, `SHORT`, `ATOM`, `u_short` |
    | 4 | `DWORD`, `LONG`, `ULONG`, `BOOL`, `INT`, `UINT`, `HRESULT`, `LCID`, `COLORREF`, `u_long`, `errno_t` |
    | 8 | `LONGLONG`, `ULONGLONG`, `DWORD64`, `*_PTR`, `SIZE_T`, `HANDLE`, `LPARAM`, `WPARAM`, `LRESULT`, `[U]LARGE_INTEGER`, `FILETIME`, `POINT` |
    | 16 | `SYSTEMTIME`, `GUID`, `RECT` |
    | 320 | `WIN32_FIND_DATAA` |

  - **gdi32 structures:** `BITMAPFILEHEADER` 14, `BITMAPINFOHEADER` 40.
  - **The wave-2 layout changes:**
    - `boost::detail::sp_counted_base` is 16 and `sp_counted_impl_p<int>` is 24, so `px_` is at +0x10 (V-1 fixed);
    - `msvc8::vector<T,false>` is 24 and `msvc8::vector<T>` is 32.
  - **The port's own disabled asserts** (`WAVEFORMATEX` 18, `WAVEFORMATEXTENSIBLE` 0x28, `socklen_t` ≠ `int`, the atomic sizes) also hold.
- **No 64-bit atomics on 32-bit fields.**
  - Every wave-2 Interlocked use goes through compat `Sync.h`'s `FAF_INTERLOCKED_32` overloads, which are `std::atomic<int32_t>`:
    - the A1 sites;
    - `Cluster.cpp`;
    - `sp_counted_base_faf_posix.hpp`.
  - The only 64-bit atomics in new code are on the code's own 64-bit variables:
    - `ReplayReport.cpp:33`;
    - `X87Kernels.cpp:67-68`;
    - the `CrtMathTrace.cpp` counters.
- **Stub logging.**
  - Every stub in `stubs/` logs through `FAF_POSIX_STUB`. audio-movie moved off its own macro.
  - V-2 is fixed: `RegOpenKeyEx` (`WinCompatMisc.cpp:1538`) and `RegCreateKeyEx` (`:1564`) log once.
  - `wx/wx/faf/object.h:14` defines a fallback `FAF_POSIX_STUB` under `#ifndef`, so the two definitions never conflict.
- **Lead-owned files.**
  - Only `macos/CMakeLists.txt` changed after the lead's phase (19:57). That change is the link run's documented map-file quoting fix (`target_link_options("LINKER:-map,...")`).
  - `WinCompat.h`, `LayoutAsserts.h`, `README.md`, `immintrin.h`, `startup/**`, `headless/**`, `math/X87Types.h`, `deps.cmake`, `exclusions.cmake` and `tu.sh` all date from 18:28-18:34, the lead's phase.
  - `src/sources.cmake` is unmodified.
- **Dependencies.**
  - **boost:** `git -C dependencies/boost_1_34_1 diff` is byte-identical to `dependencies/patches/boost_1_34_1_faf_macos.patch`. That covers 7 files, including the `-N` entry for `sp_counted_base_faf_posix.hpp` and the link run's recursive-mutex hunk.
    - Every hunk is `FAF_POSIX`-guarded except the wave-1 `xtime.hpp` one, which has no effect on clang-cl.
    - The recursive `boost::mutex`/`try_mutex` reproduces Windows' critical-section re-entrancy.
  - **WildMagic3p8:** `git archive HEAD` + `wildmagic3p8_faf_required.patch` + `wildmagic3p8_faf_macos.patch` equals the working tree (`diff -r` is empty).
  - **zlib-1.2.3:** the working-tree diff is exactly `zlib_1_2_3_faf_required.patch`.
- **Run folder:** no stray `C:\...` entries in `faf-re-bin/run-native` after my three runs.

### 4.2 Violations

No high or medium violations. Everything below is low or informational.

| # | Severity | Where | Rule | Problem |
|---|---|---|---|---|
| W2-1 | Low (latent) | `src/platform/Platform.h:47` | 7 (Windows semantics) | The POSIX `HIDWORD(x)` reads bytes 4..7 of `x`. The Windows macro is `*reinterpret_cast<unsigned long*>(&(x) + 1)`, which steps by `sizeof(x)`, so for an 8-byte operand Windows reads the 4 bytes after `x`. The two agree only for 4-byte operands. Today `LODWORD`/`HIDWORD` have **no callers** in `src/`. If one appears, match the Windows text (`reinterpret_cast<std::uint32_t*>(&(x) + 1)`) or keep the fix and record the deliberate difference |
| W2-2 | Info (lead-owned design) | `src/platform/posix/LayoutAsserts.h:13` | 2 (Windows sizes) | `#define static_assert(...) static_assert(true, ...)` also disables the port's own asserts: `compat/Sync.h:20`, `compat/Sockets.h:56-57`, `WinCompatSync.cpp:608,1084`, `PosixWaveFormat.h:27,56`, `X3DAudioPosix.cpp:20`. Only `WinCompatGdi.cpp` re-enables the keyword (push_macro). My probe shows these all hold today, but nothing would catch a regression. Wave-1 request: a `FAF_FORMAT_ASSERT` that stays on |
| W2-3 | Info | `src/lua/LuaPosixNumbers.h` (new, untracked) | 4 (prefer the platform directory) | A new header in an upstream directory. It is wholly inside `#if defined(FAF_POSIX)` and included only under `FAF_POSIX` (`LuaObject.cpp:54-56`), so Windows never sees it. Acceptable; it could move to `platform/posix/` |
| W2-4 | Info (still open from wave 1) | `src/gpg/core/reflection/StaticInitPhase.h:47` | — | The comment still names `platform/posix/StaticInitPreRegister.cpp`; the file is `platform/posix/startup/StaticInitPreRegister.cpp` (I-1) |
| W2-5 | Info (still open from wave 1) | `src/gpg/gal/backends/vulkan/DeviceVulkan.cpp:1578-1581` | 4 | The Windows text is restructured (a named `preprocessedText` local). Behaviour-identical (I-2) |

## 5. Determinism-critical claims, checked independently

### 5.1 X87Emu.cpp against X87Math64.asm

- **Read line by line, both files side by side, on a hand-kept stack trace:**
  - `slerp_spherical_blend`, `pow`, `scaled_ln_quotient`, `box_radius_on_axis`;
  - `bilinear_height`, `vector_length_zyx`, `acos`, `asin`;
  - `atan2_difference`, `intrinsic_exp`;
  - `scale_vector`, `scale_vector_to_length`, `scale_vector_to_length_of`, `scale_vector_max_length`.
- **Result: every transcription matches the asm.** The checks covered:
  - operand order: MASM `fsubr st(1),st(0)` is ST1 ← ST0 − ST1, `fdivrp st(1),st(0)` is ST1 ← ST0 / ST1, and the memory forms `fsubr`/`fdivr m32` are m − ST0 and m / ST0;
  - pop placement;
  - box lane offsets: 28h → [10], 2Ch → [11], 24h → [9], 38h → [14], 1Ch → [7], 20h → [8], 18h → [6], 34h → [13], 10h → [4], 14h → [5], 0Ch → [3], 30h → [12];
  - the stack-argument slots: the 5th argument at `[rsp+40]`, the 6th at `[rsp+48]`, `xmm3` spilled to `[rsp+32]`;
  - memory read and write points for aliasing;
  - precision control: PC24 everywhere the asm masks the PC bits, and the thread's PC53 for `round_to_int`, `sinf`, `cosf`, `tanf` and `atan2f`.
- **The Layer 2 fast paths** for `bilinear_height`, `box_radius_on_axis`, `vector_length_zyx`, the four `scale_vector*` routines and the `acos`/`asin` radicand keep the same operation order. They fall back to the transcription on any NaN output or partial overlap.
- **`std::sqrt` errno call:** `X87Emu.cpp.o` references `_sqrt` for the `-fmath-errno` slow path. That path is reached only for a NaN result, which the fast paths then hand to the transcription, so it changes no result.
- **Constants:** `kL2E`, `kLN2`, `kLG2` and `kPi` (`X87Types.h:45-49`) recomputed with 80-digit decimal arithmetic and rounded to 64 bits, nearest-even. All four match: `B8AA3B295C17F0BC`/3FFF, `B17217F7D1CF79AC`/3FFE, `9A209A84FBCFF799`/3FFD, `C90FDAA22168C235`/4000.
- **Independent exact oracle (new, the verifier's own).**
  - `build/verify-tests/oracle/x87_oracle.py` models the PC24 routines with Python `fractions`, written without reference to `X87Core.h`. Each PC24 operation is the exact result rounded to 24 bits, ties to even, with an unbounded exponent; the `fstp m32` rounds again, with denormals.
  - `oracle/harness.cpp` links **the engine's own `X87Emu.cpp.o` and `X87Kernels.cpp.o` from build/macos**.
  - Inputs: 50,000 per routine for `round_to_int`, `vector_length_zyx`, the four `scale_vector*`, `bilinear_height` and `box_radius_on_axis`. They mix game-range values, ties, denormals, huge values and zeros, and half the scale cases use a consistent `lengthSq`.
  - **Result: 400,000 inputs, 0 mismatches.**
  - Zero results are compared by value; the oracle does not model the sign of zero.
  - A mutated oracle (25-bit rounding) reports thousands of mismatches, so the comparison is live.
- **Independent kernel spot check.**
  - `oracle/kernel_oracle.py` uses 60-digit `decimal` arithmetic on the domains that need no argument reduction:
    - `sinf`/`cosf` on |x| < π/4: RN64 of the exact sin/cos, then the float store;
    - `intrinsic_ln`/`intrinsic_log10` on every positive normal float: the rounded `fldln2`/`fldlg2` constant times the exact log2(x), rounded once to 64 bits, then the store.
  - **Result: 120,000 inputs, 0 mismatches.** A control with the constants swapped fails every input.
  - Reduced trig arguments, `fpatan` and `f2xm1` were not re-derived here. For those I rely on the re-run kernel corpus (section 3).

### 5.2 fmin/fmax (`CColPrimitiveBase.cpp:155-166`)

- **The replacement:**
  - `fmin(a,b)` becomes `isnan(a) ? b : (b < a ? b : a)`;
  - `fmax(a,b)` becomes `isnan(a) ? b : (b > a ? b : a)`.
- **Against DETERMINISM.md §5 rule 6:** it is exactly the replacement that rule prescribes.
- **Against LLVM's x86 lowering of `llvm.minnum`/`maxnum`** (`LowerFMINNUM_FMAXNUM`):
  - `MinMax = X86ISD::FMIN(Op1, Op0)` is `minss` with b first, which returns `b < a ? b : a`;
  - then a `setuo` select on `Op0`: `isnan(a) ? b : MinMax`.
- **Signed zeros:** `fmin(+0, −0)` gives +0, as on x64.
- **Codegen flags:** neither build lists `fmin` in `-fno-builtin-*` (`cmake/FafCodegen.cmake:40-46`, `macos/CMakeLists.txt:38-55`), so both reach `minnum`.
- **Residual risk (low):** the agent's x64 check used Apple clang 21. The reference `main_x64.exe` was built with clang 22.1.7. Disassembling that binary's box-SAT loop once on the Windows PC would close this.

### 5.3 Lua NaN handling (`src/lua/LuaPosixNumbers.h`, `LuaObject.cpp`)

- **VM arithmetic.** `OP_ADD`/`SUB`/`MUL`/`DIV` (`LuaObject.cpp:17411, 17431, 17449, 17467`) replace a NaN result with 0xFFC00000 only when both operands are non-NaN. That is SSE's default-NaN rule. A propagated NaN is untouched.
  - `OP_POW` goes through the global `__pow`, which is wrapped.
  - Unary minus only flips the sign, the same on both platforms.
  - FA's VM has no string coercion on these opcodes.
- **math library.** `LuaMathX86NaN<Function, ArgumentCount>` checks the results after the call, against the numeric arguments still on the stack.
  - Correct for `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `ceil` `floor` `mod` `frexp` `ldexp` `sqrt` `log` `log10` `exp` `deg` `pow` `rad`.
  - `abs`, `min`, `max`, `random` and `randomseed` cannot create a NaN and are left alone.
  - With faf_crtmath already returning the x86 NaN, the wrappers act as guards.
- **Spelling.** `UcrtNonFiniteText` gives `-nan(ind)` only for the exact indefinite, `-nan` for other negative quiet NaNs, and `nan(snan)` for a signalling NaN. The `0xFFC00000` float widens to `0xFFF8000000000000`, which prints `-nan(ind)`.
- **Parsing.** `UcrtStrtodNaN` matches my reading of the UCRT's `assemble_floating_point_ind`: the indefinite always has sign 1, and a quiet NaN carries no payload.
  - `nan(snan)` parses to the UCRT double SNaN (mantissa 1). Narrowing that to float on x64 gives 0x7FC00000, the same as the helper's result.
- **Unverified UCRT assumptions** (the platform package says so too):
  - `printf` of inf/nan with the `0` and `#` flags;
  - per-function domain-error NaNs.

  Not raised by any package: `tostring` (`%.14g`) of an exact decimal tie, for example 2^-21, depends on the UCRT rounding ties to even (UCRT 10.0.19041 and later) as macOS does. These need the Windows hardware oracle.

### 5.4 A1 atomics

- **The widened guards:**
  - `StatItem.cpp:478,487,504,524,562,1555,2202`;
  - `Unit.cpp:1579,6823`;
  - `EntityDb.cpp:225`;
  - `Entity.cpp:346`;
  - `CArmyStats.cpp:102,116`.
- **Each now takes the Windows branch:** Interlocked on a `reinterpret_cast<volatile long*>` of an `int32_t` field.
- **Why they are correct:** compat `Sync.h:25-61` maps both the `long` and the `LONG` overloads, and the `_Interlocked*` spellings (`:105-111`), to `std::atomic<int32_t>`. So the operations are 32-bit and touch only the field.
- **Threaded sim:** `/simworkers 8` replays 27890640 with 0 mismatches (section 1). Without that switch the sim runs serially (`docs/SWITCHES.md`: `sim_Workers` defaults to 0), so the default run does not exercise A1.

## 6. Per-package results

| Package | Result | Verified here |
|---|---|---|
| platform (core-sys + Lua + Sockets + Platform headers) | All 7 tasks done:<br>• `sscanf_s` family<br>• `__RTtypeid`<br>• boost V-1 32-bit control block, plus the Cluster.cpp vtable-slot fix<br>• LP64-07/11/15<br>• Lua non-finite numbers<br>• V-2<br>• Vector.h `[[no_unique_address]]` | Builds. Sizes probed: V-1 and the vector size are fixed. V-2 logs. Patch and tree agree |
| sim-game | CConCommand, PathTables and StartupHelpers compile; Sim.cpp compiles now that `sscanf_s` exists. Also done:<br>• A1 (13 guards)<br>• LP64-04/05/13<br>• fmin/fmax<br>• the `/interlocked` and `/genpath` gates | All compile. A1, fmin/fmax and guards audited (section 5) |
| x87-core | `X87Core.h` (Layers 1 and 2), `X87Emu.cpp` (27 symbols), `PcRoundCore.h`, and the X87Math.cpp guard | Full suite 0 failing. Independent oracle: 400k inputs, 0 mismatches |
| x87-kernels | Six RN64 kernels with a fast path and a 256-bit slow path | Full corpus 0 mismatches. Independent sin/cos/ln/log10 check: 120k inputs, 0 mismatches |
| crt | `faf_crtmath` (33 functions), trace mode, MSVC `rand`, self-test (75 checks) | `run_tests.sh` PASS. In-binary self-test 75/75. Symbol and FMA checks pass on the new binary |
| render-null | gdi32 area (`compat/Gdi.h`, `WinCompatGdi.cpp`); null `gal::Device` (`DeviceNull`); `REN_InitHeadless`/`Shutdown`; DeviceVulkan and CD3DDevice compile; audio-movie on `FAF_POSIX_STUB` | Builds. Stubs log |
| runner | `faf_replay` (`RunnerMain`, `HeadlessApp`, `ReplayReport`) and `macos/run_replay.sh` | 3 runs here, with the results in section 1 |
| link-run | Reached ladder **level 5**. Fixed four problems:<br>• the map-file quoting<br>• the WrapFile MSVC-ABI function-pointer cast (SIGBUS)<br>• the boost mutex re-entrancy deadlock<br>• the stray `C:\...` files (via `faf_posixHostPathA/W`) | Reproduced from a clean build. Independent negative control |
| wx, net, audio-movie, wm3 (wave 1) | No wave-2 work. All TUs compile | — |

## 7. Requests not yet applied

**Lead-owned:**

| From | Request | Status |
|---|---|---|
| sim-game | `macos/tu.sh:34`: `eval "$cmd -o $obj $*"` breaks on an extra argument that contains a space. Run an argv array without eval | Open |
| lp64 (wave 1) / this report W2-2 | A `FAF_FORMAT_ASSERT` in `LayoutAsserts.h` that stays on, for file and wire formats and for the port's own size asserts | Open |
| lp64 (wave 1) | `format(printf)` attributes on `gpg::Logf`/`Warnf`/`Debugf`/`STR_Printf`/`Sim::Logf` under `FAF_POSIX` | Open |
| wm3 (wave 1) | Have the macOS build apply or verify `wildmagic3p8_faf_macos.patch`. The boost macOS patch has the same gap | Open. The `deps.cmake:28-29` comment was updated; verification is by hand (done here) |
| x87-kernels | `X87Types.h` comments: `Fptan(±inf)`, unsupported encodings, the two-NaN rule | Open (documentation only) |
| x87-core / crt | Make the post-link checks a script or CI step: `x87core_tests --quick`, `x87core_tests_kernels --quick kat integration`, `check_symbols.sh`, `check_fma.sh`, `check_map_order.py` | Open. Run by hand in this pass |
| x87-core | An FPCR == 0 check on every sim worker thread, not only on the calling thread and a fresh one | Partial. The self-test checks the main thread and a new one, and `SimWorkerPool` copies MXCSR, whose FTZ is never applied to FPCR |
| platform | Hardware oracle:<br>• UCRT domain-error NaN for fmod/pow/acos/asin/log/sqrt<br>• UCRT printf of inf with `0`/`#` | Open (needs the Windows PC) |
| runner | Confirm the `faf-re-bin/gamedata` symlink | Open (works) |

**Other packages:**

| From → to | Request | Status |
|---|---|---|
| crt → core-sys, wx | `faf_crtmathTraceDump()` before `_exit` in `TerminateProcess(GetCurrentProcess())` (`WinCompatMisc.cpp`) and before `std::_Exit(1)` in `WinAppPosix.cpp` | Open. Only `ReplayReport.cpp:242` dumps, so traces from abnormal exits are lost |
| platform → moho/misc | `CrtRuntimeHelpers.cpp:15616` `EngineReleaseSharedControlBlock`: Win32 count layout and MSVC vtable slots [1]/[2] | Open, latent: the file is not in `src/sources.cmake` and the function has no callers |
| platform → all | Scan hand-indexed vtables and by-value-return function-pointer casts for MSVC-ABI assumptions (link-run open issue 4) | Open |
| sim-game → Files owner | `ReadDirectoryChangesW`: fill `FILE_NOTIFY_INFORMATION` with 4-byte `wchar_t` units when it is implemented (`CDiskWatch.cpp:149`) | Open, latent (stub today) |
| runner → sim-game/core-sys | Windows-form paths into `std::filesystem`: `DISK_GetLaunchDir` (`StartupHelpers.cpp:5686`), `AppendEngineHookFolder` (`:802`), and Lua `io`/`os` file functions | Open |
| runner | A counter of matching checksum comparisons, so the result line can say `checked=` | Open |
| link-run | `DISK_ResetWatch` locks the destroyed `gDiskWatchInitMutex` (`CDiskWatch.cpp:33`) on any POSIX `exit()` | Open. The runner avoids it with `_Exit` |
| link-run | UI fonts: `UI=1` hangs on 0-high text (`CD3DFontPosix.cpp:23`) | Open (milestone 2) |
| sim-game | LP64-03, the save-game header `wchar_t[1024]` | Open (not milestone 1) |

Satisfied in wave 2, from the wave-1 list and this wave's requests:
- `sscanf_s`, the X87Math.cpp guard, `__RTtypeid`, V-1, V-2, LP64-01/04/05/07/10/11/13/15, A1, fmin/fmax.
- `-fmath-errno`, `_LIBCPP` define, `BOOST_HAS_PTHREADS`, audio-movie include path, D3D exclusion narrowing.
- `__argv` before static init, `immintrin` FTZ, gdi32 owner, null device, the math area.
- The startup self-test, `SetSse2Mode` in the runner (`HeadlessApp.cpp:148`), the `faf_crtmathTraceDump` call at the end of a replay, and the viewport before `REN_InitHeadless`.

## 8. Recommended next steps

Milestone 1 (0 mismatches on 27890640) is done and reproducible. In order:

1. **Lock it in.**
   - Turn this pass into one script (`macos/verify_replay.sh`):
     1. clean build;
     2. `check_symbols.sh`, `check_fma.sh`, `check_map_order.py`;
     3. the quick x87 suites;
     4. `SPEED=50` with `/simworkers 0` and `/simworkers 8`;
     5. the corrupted-beat negative control (`build/verify-tests/corrupt_replay.py`).
   - Add `tick=` and `checked=` to the `FAF_REPLAY_RESULT` line (runner). Today a run whose verification silently never happened would also print `mismatches=0`.
2. **The 8-replay core set** (`docs/PR_REPLAY_CHECK.md`): 26902315, 26930624, 26943656 (M28AI v294), 26883423 (M28AI v292), 26881085, 26928211, 26989770, 26897073.
   - **Inputs needed:**
     - the replays (`.fafreplay` → `.scfareplay` via `scripts/perf/convert_replay.py`);
     - game data at **version 3835**;
     - their custom maps;
     - M28AI v292/v294.
   - **Approvals:** downloads need the user's approval. `~/.faforever` must not be modified, so give the core set its own run folder and gamedata mount (a copy, or the Windows PC's 3835 set).
   - **Per replay:**
     - `SPEED=50`, with `/simworkers 0` and `/simworkers 8` (the SWITCHES rule), plus `FAF_CRTMATH_TRACE=1` once to list the libm sites the sim really reaches;
     - pass means no `Checksum for beat N mismatched` line.
   - Then the extended set: 26876279 and 26935800 match except on the final beat.
3. **If a core replay desyncs:**
   - run `SYNCLOG=1` against `main_x64.exe` under Wine for the first differing beat, then narrow per DETERMINISM.md §6.3;
   - look first at the known suspects:
     - non-reduced-domain kernel rounding (§3.6);
     - finite faf_crtmath results (Apple libm, not UCRT clones; `FAF_POSIX_TODO(determinism)` in `CrtMath.cpp`);
     - `CArmyStatItem::SumCategory`'s pointer-ordered float sum (`CArmyStats.cpp:455`);
     - NaN sign from plain C++ sim arithmetic (§2.3).
4. **Hardware oracle on the Windows PC (Intel and AMD).** Each closes an assumption that the replays only cover indirectly:
   - DETERMINISM.md §6.2 difffuzz64 of `X87Core.h`/`X87Emu.cpp`/`X87Kernels.cpp` against the real x87;
   - UCRT 22000 special values (domain-error NaNs, `strtod("nan(ind)")`, printf of inf/nan with flags, `%.14g` ties);
   - one disassembly of the box-SAT `minss`/`maxss` sequence in `main_x64.exe`.
5. **Engine hygiene before milestone 2:**
   - `CDiskWatch.cpp:33` mutex lifetime;
   - the Windows-form paths into `std::filesystem` and Lua io;
   - the MSVC-ABI scan (vtable slots, by-value-return function-pointer casts);
   - `FAF_FORMAT_ASSERT`;
   - the `tu.sh` quoting fix;
   - the two wave-1 informational items (W2-4, W2-5);
   - then font metrics, so the UI can run (`UI=1`).

## Files produced by this pass

- `build/macos/census-wave2.log`: the full rebuild.
- `build/verify-tests/`, untracked:
  - `run50/`, `run50-w8/`, `negctl/`: the replay runs;
  - `corrupt_replay.py`;
  - `probe/SizeProbe*.cpp`;
  - `oracle/`: `harness.cpp`, `x87_oracle.py`, `kernel_oracle.py` and logs;
  - `crt.log`, `x87-core.log`, `x87-kernels.log`;
  - `faf_replay.pre-wave2*`: the link run's binary and map.
- `macos/porting/verify.md`: the verifier's notes.
