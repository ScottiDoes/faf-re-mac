# crt: the C runtime behaviour the sim depends on

Package scope:
- `src/platform/posix/crt/**` (new). The `platform/posix` glob compiles it into `faf_engine`.
- `src/platform/posix/compat/Math.h`: extended, and the lead's lrint shim is unchanged.
- `macos/tests/crt/**` (new). The tests build in `build/crt-tests`.

The design is in `macos/docs/DETERMINISM.md` §2.2, §4, §5, §6.4 and §6.5.

## Status

- All 4 TUs compile with `macos/tu.sh`: `CrtMath.cpp`, `CrtMathTrace.cpp`, `MsvcRand.cpp` and `SelfTest.cpp`.
- `macos/tests/crt/run_tests.sh` passes:
  - unit tests: 225/225;
  - self-test: 75/75 against the x87 test double;
  - the deliberately wrong test double is caught with 20 FAIL lines;
  - the oracle table is in sync;
  - both checkers pass, and both reject their negative controls.
- `check_fma.sh` on the current `build/macos` objects found **0 fused instructions**. It scanned 944 `faf_engine` objects plus `libWm3Foundation.a` and `libfaf_zlib.a`.
- `check_symbols.sh` passes on a link probe: every current engine object, the crt objects, Wild Magic and zlib, linked with `-undefined dynamic_lookup`.
  - Only exact functions stay in libSystem: `frexp`, `ldexp`, `llround`, `lrintf`, `lround` and `lroundf`.
  - `rand` and `srand` are the executable's own.

## Which C math entry points the engine references

Measured with `nm -u` on 27/10/2026. The objects are the 944 under `build/macos/CMakeFiles/faf_engine.dir/Volumes/...`, plus `libWm3Foundation.a` and `libfaf_zlib.a`. The `Users/...` copies in that directory are stale objects from an older configure.

**Engine objects:**

| Symbol | References |
|---|---|
| `cos`, `sin` | 43 objects each |
| `lrintf` | 27 |
| `sqrtf` | 16 (the NaN branch of the inline `fsqrt`) |
| `cosf`, `sinf` | 12 each |
| `atan2f` | 3 |
| `tanf` | 2 |
| `exp2f`, `log2f`, `logf` | `CameraImpl` |
| `pow` | `CWldSession` |
| `fmod` | `CWldSession` |
| `powf` | `HighFidelityWater` |
| `ldexp`, `sqrtf` | `LuaObject` |
| `lround`, `lroundf`, `llround` | UI, movie and D3D11 text |
| `rand`, `srand` | `LuaObject` |

Where the 43 `sin`/`cos` references come from:
- They are the **static initializers** of the header constants `kQuatYawNeg45`/`kQuatYawPos45` in `moho/math/QuaternionMath.h` (plus their ±90° siblings).
- Those are `Wm3::Quaternionf(axis, angle)`, which computes `(float)sin/cos((double)(float)angle/2)`.
- The arguments are `±0x3FE921FB60000000` and `±0x3FD921FB60000000`.
- Every result is at least 0.037 float ulp away from a rounding tie (`kat_oracle.py`), so any faithful double libm gives the same floats. The self-test checks them.

**Wild Magic:**

| Symbol | Objects |
|---|---|
| `fmod` | 99 |
| `sqrt` | 77 |
| `sqrtf` | 72 |
| `sin` | 14 |
| `cos` | 13 |
| `atan2` | 6 |
| `acos` | 5 |
| `log`, `pow` | 4 each |
| `atan` | 2 |
| `asin`, `tan` | 1 each |
| `rand` | 3 objects that the engine does not link (see RAND_MAX) |

**zlib:** none.

**Not referenced yet:** `X87Math.cpp` does not compile yet (core-sys). Once it does, it adds `asin`, `acos`, `pow`, `exp`, `log` (the msvc8 fallback lanes), `fmod`, and the exact `frexp`/`fma`/`nextafter`/`floor`.

## faf_crtmath (`CrtMath.h`, `CrtMathInternal.h`, `CrtMath.cpp`)

**The 33 entry points:**
- The UCRT 10.0.22000 set the x64 build links: `acos` `asin` `atan` `atan2` `atan2f` `cosh` `sinh` `tanh` `sqrt` `sqrtf` `cos` `cosf` `sin` `sinf` `tan` `tanf` `exp` `log` `log10` `logf` `pow` `powf` `fmod`.
- `exp2f` and `log2f`, which the engine references.
- The float twins that libc++'s `std::` float overloads call: `acosf` `asinf` `atanf` `expf` `log10f` `coshf` `sinhf` `tanhf`. A new call site of one of these is then traced instead of silently binding to libSystem.
- The list is the X-macro `FAF_CRTMATH_FUNCTIONS`.

**Binding.**
- ld64 binds every reference from the engine objects and the static archives to the executable's definitions. libc++.dylib and the frameworks keep their own libSystem bindings.
- Each entry point reaches Apple libm through `dlsym(RTLD_NEXT, name)`, resolved once and stored in a constant-initialized atomic. That makes it safe from any static initializer.
- **No recursion:**
  - `Resolve()` aborts if dlsym returns null or the entry point itself.
  - `faf_crtmathCheckBindings()` resolves all 33 and checks that each lives in a different image than its entry point. The self-test calls it.
  - The unit test confirms all 33 are defined in the test executable and differ from libm's.

**Results.** The bodies are Apple libm plus the UCRT's special-value semantics:
- **Domain error** (a NaN result from non-NaN operands): the x86 default NaN, `0xFFF8000000000000` / `0xFFC00000` (`-nan(ind)`).
  - Apple returns `0x7FF8000000000000` / `0x7FC00000`.
  - Measured cases: `asin(2)`, `acos(-2)`, `log(-1)`, `log10(-1)`, `logf(-1)`, `sin`/`cos`/`tan(inf)`, `pow(-2, 0.5)`, `fmod(1, 0)`, `fmod(inf, 1)`, `sqrt(-1)`.
- **NaN operand:** returned quieted, keeping its sign and payload; with two NaN operands, the first one wins.
  - Apple's `pow` canonicalizes a NaN operand to `0x7FF8000000000000`; ours keeps the payload, as x86 does.
- **Exact ±π and ±π/2 results** of `atan2f`/`atanf`/`asinf`/`acosf`: the nearest float, `0x40490FDB` / `0x3FC90FDB`.
  - The cases are `atan2f(±0, −0 or x<0)`, `atan2f(y, −inf)`, `atan2f(±inf, x)`, `atanf(±inf)`, `asinf(±1)` and `acosf(−1)`.
  - Apple rounds these toward zero (`0x40490FDA`, `0x3FC90FDA`, measured). Its own `atan2f(1, 0)` and `acosf(0)` are already the nearest float.
- **Other Annex F values:** Apple's, which are Annex F. The unit test covers about 70 of them.
- **Everything else:** bit-identical to Apple libm. The test compares 200,000 random inputs per function, all 33 functions.

**Cost with tracing off.**
- The fast path:
  1. load the pointer;
  2. store the operands to a stack slot;
  3. call libm;
  4. one predicted branch to the out-of-line `SlowPath` (taken on a NaN result, a π candidate, or tracing not off).
- Measured on a sin loop: 3.7 ns against libm's 3.7 ns.
- An earlier version kept the operand in `d8` across the libm call. The caller's `d8` then had to wait for the restore, which serialized back-to-back calls: 13 ns per call.

**FAF_POSIX_TODO(determinism), marked in the code:**
- Finite results are Apple's, so the exact UCRT 22000 clones are still to come.
- The two-NaN-operand rule is not verified.
- Apple's `atan2f` also stays at or below `0x40490FDA` for results just under π. That is render-only.

## Trace mode (`CrtMathTrace.cpp`)

`FAF_CRTMATH_TRACE=1` turns it on. Any value other than empty or `0` counts as on.
- The variable is read once, at the first call of any entry point, and the decision never changes.
- The file is `FAF_CRTMATH_TRACE_FILE`. The default is `./faf_crtmath_trace.txt`, fixed to the directory the process started in, even if the engine later calls `chdir`. It never defaults to `/tmp`.
- The file is written at exit (`atexit`) and on `faf_crtmathTraceDump()`.

**What is recorded.**
- A site is the key `(function, return address, caller's return address)`. The second address tells the callers of a wrapper apart: for example `msvc8::acos`'s fallback lane, or the two callers of `WrapAcos` in the test.
- Per site:
  - the call count;
  - NaN operands;
  - domain errors, with the first domain-error operand;
  - the operand range ("none" when every operand was NaN);
  - the first 8 distinct operands with their results;
  - a 4096-bit linear-counting estimate of the distinct operands.
- A per-function summary lists every entry point, including those with 0 calls, and the calls dropped because the table was full.

**The table.**
- 4,096 sites in zero-initialized POD storage of about 3.6 MB of BSS, untouched while tracing is off.
- Insertion is lock-free (CAS on a per-slot state) and the counters are atomic.
- It works from static initializers and from every thread at once.

**Symbolizing.** The header prints the main image's load address and slide. Use `atos -o <image> -l <load address> <addresses>`. Each address also carries the nearest exported symbol from `dladdr`, which can be far off inside the main executable.

**Exit paths.** `_exit` skips `atexit`, and so do `TerminateProcess(GetCurrentProcess())` and `WinAppPosix.cpp`'s `std::_Exit(1)`. Callers on those paths must call `faf_crtmathTraceDump()` first (see the requests below).

**Tested** by the unit test, which spawns itself four ways: tracing off, `=0`, `=1` with the default file, and `=1` with `FAF_CRTMATH_TRACE_FILE`. It checks:
- the exact counts: `sin` 102, `asin` 3 calls with 3 domain errors, `atan2f` 5, `acos` 16 over 2 sites with the same `ra0` and different `ra1`;
- 4 threads × 10,000 `cos` calls with none lost;
- that the static initializer's `atan` call is traced;
- the distinct-operand estimate (≈4,000 and ≈10);
- the samples;
- the first domain-error operand.

## MSVC rand (`MsvcRand.cpp`)

- `thread_local uint32_t` state, constant-initialized to 1 on every thread.
- `srand(seed)` sets the state. `rand()` computes `s = s*214013 + 2531011` and returns `(s >> 16) & 0x7FFF`.

**Tested:**
- 41, 18467, 6334, 26500, 19169 from state 1 and after `srand(1)`;
- 100,000 draws against the reference LCG from 6 seeds, including wrap-around seeds;
- 8 threads, each with its own sequence, leaving the main thread's state untouched;
- the first `math.random()` sample.

**RAND_MAX.** No source under `src/` uses `RAND_MAX`: `math_random` hard-codes `% 0x7FFF`, and `CrtRuntimeHelpers.cpp` is not built.
- Wild Magic's `UnitRandom`/`SymmetricRandom`/`IntervalRandom` divide by `RAND_MAX`, and `ContCircle2`, `ContSphere3`, `RandomHypersphere` and `LCPPolyDist` call `rand()`.
- None of them is linked:
  - `-why_load` on the link probe loads 32 Wm3 members, and none of these four is among them.
  - The probe contains no `UnitRandom` symbol.
- **So macOS's 0x7FFFFFFF changes no sim result.**
- Even so, `compat/Math.h` now redefines `RAND_MAX` as `0x7FFF` after `<cstdlib>` for engine TUs. With our `rand`, any future engine use then stays on the Windows scale. Wild Magic's own TUs do not get `WinCompat.h`, and nothing linked there uses it.

## Self-test (`SelfTest.h`, `SelfTest.cpp`)

`bool faf_posixRunSelfTests(bool verbose)`:
- prints every failure (every check when `verbose`) to stderr with the prefix `[faf selftest]`;
- returns true only if all 75 checks pass;
- has no side effects on the calling thread: the `rand` and MXCSR checks run on a temporary `std::thread`.

The checks:
- **FPCR** == 0 on the calling thread and on a new thread. `_mm_setcsr(FTZ)` leaves FPCR.FZ clear, and `_mm_getcsr` reports the recorded FTZ.
- **Saturating casts:**

  | Cast | Result |
  |---|---|
  | `(int)NaN` | 0 |
  | `(int)3e9f` | `INT_MAX` |
  | `(int)-3e9f` | `INT_MIN` |
  | `(unsigned)-1.5f` | 0 |
  | `(unsigned)5e9f` | `UINT_MAX` |
  | `(short)70000.0f` | `SHRT_MAX` |
  | `(int)(double)NaN` | 0 |

- **Fused-versus-unfused**, built with the sim flags: `a*b+c` must be 0, where fused would give −2⁻⁵⁴ (double) or −2⁻²⁶ (float). The object contains `fmul` + `fadd`.
- **MSVC rand** sequences.
- **faf_crtmath:**
  - the bindings;
  - `rand`, `srand`, `sin`, `atan2f` and `pow` live in this executable;
  - `atan(1.0) == 0x3FE921FB54442D18`;
  - the UCRT NaN cases;
  - `pow(NaN, 0) == 1` and `log(0) == -inf`;
  - the four quaternion half-angle sin/cos values.
- **`Wm3::Math<float/double>`:** `PI`, `TWO_PI`, `HALF_PI`, `INV_PI`, `INV_TWO_PI`, `DEG_TO_RAD` and `RAD_TO_DEG`. Note that `RAD_TO_DEG` float is `0x42652EE0`.
- **x87 known answers** through `msvc8_x87_*`, declared `extern "C"` in the file:
  - the control word is `0x027F`;
  - asin(`0x3F052EAA`) is `0x3F0C1118`;
  - pow(`0x41AB54C2`, 2) is `0x43E554AF`;
  - VectorLengthZYX({1e-25, 0, 0}) is `0x15F79688`;
  - `round_to_int`: 2.5, 3.5, −2.5, −2³¹, 2147483520, 3e9 and NaN;
  - `sinf`/`cosf`(2⁶³) is 2⁶³, and `tanf`(2⁶³) is `0xFFC00000`;
  - `sinf(0)`, `cosf(0)`, `atan2f(1, 1)` = `0x3F490FDB`, `intrinsic_exp(0)`, `intrinsic_ln(1)`;
  - `msvc8::MultiplyIntegerX87(2028178766, 0x3C0B8117)` is `0x4B83C107`;
  - two cases where the denormal-range double rounding is visible: `scale_vector` gives `0x476`, where single rounding would give `0x477`; `scale_vector_to_length` gives `0x5D2` against `0x5D1`;
  - `scale_vector_to_length` with an extended exponent: lengthSq 2⁻¹⁴⁹, length 1e30, source 1e-30 gives `0x64B504F4`, where float arithmetic gives inf.

  `macos/tests/crt/kat_oracle.py` derives these from exact arithmetic (`fractions`/`decimal`, transcribing `X87Math64.asm` under PC24). `--check` keeps its table in sync with `SelfTest.cpp`. The double-based model in `x87_test_double.cpp` cross-checks it independently, and the two agree.

## Scripts (`macos/tests/crt`)

| File | Purpose |
|---|---|
| `check_symbols.sh <binary>...` | Reads `nm -m`. Fails on any `(from libSystem)` import that `libsystem_m.tbd` exports, unless it is exact (floor/ceil/trunc/round/rint/nearbyint/lrint/lround/ldexp/scalbn/frexp/modf/copysign/fma/nextafter/ilogb/logb/fdim/nan and their f/l forms, plus the classification helpers). Also fails on `rand`/`srand` from libSystem. `fmin`/`fmax`, `fesetround`/`fesetenv` and other generators (`random`, `rand_r`, `drand48`, `arc4random`) are warnings. Lists which owned symbols the binary defines. |
| `check_fma.sh <objects\|archives\|dirs>...` | `objdump -d` in parallel. Reports each function containing `fmadd`/`fmsub`/`fnmadd`/`fnmsub`/`fmla`/`fmls`/`fmlal`/`fmlsl`, with demangled names and archive members. Whitelist: `legacy/math/X87Math.*` and `platform/posix/math/`. The full engine scan takes about 7 s. |
| `run_tests.sh` | Builds everything in `build/crt-tests` and runs it. |
| `engine_cc.py <src> <out> [args]` | Compiles with the engine's exact `compile_commands.json` command (like `tu.sh`) but keeps the object. |
| `crt_unit_test.cpp` | The unit tests. |
| `selftest_main.cpp` | Runs the self-test. |
| `x87_test_double.cpp` | Test double for the 27 `msvc8_x87_*`. **Not an emulator.** The PC24 arithmetic routines are a real double-based model; the transcendental ones are lookups of the documented answers. The `-DFAF_X87_TEST_WRONG` build is 1 ulp off everywhere. |
| `kat_oracle.py` | The known-answer oracle. |

The self-test build compiles `X87Math.cpp` with `-D_M_X64`, in the test directory only, to get its `msvc8_x87_*` declarations until core-sys adds the `FAF_POSIX` guard.

## Open issues and risks

- **UCRT clones.** The finite results of all 33 functions are Apple's. Known sim reach today:
  - static-init constants: verified robust;
  - `Wm3::Math` PI: `atan(1)` is exact on both sides;
  - `msvc8::asinf` → `std::asin(double)` at `Vector4f.cpp:130`: rounding to float hides about 2⁻²⁹ per call;
  - the msvc8 `acos`/`asin`/`pow` special-value lanes: covered by the NaN rules;
  - `CWldSession` `pow`/`fmod`: sim-rate UI.

  Run the trace (§6.4) on the core replay set to see the real set.
- **Assumed UCRT behaviour.** The "domain error → `0xFFF8…`" rule matches MSVC's well-known `-nan(ind)`, but it is not verified per function against 10.0.22000; `fmod` in particular. The same goes for the two-NaN rule.
- **errno.** UCRT sets `errno` to EDOM/ERANGE; faf_crtmath does not. Engine code never reads `errno` after a math call (checked: the errno users are file, socket and `strtol` paths).
- **NaN sign of plain arithmetic** (ARM's default NaN is positive) is out of scope here. That belongs to the NaN audit (DETERMINISM.md §2.3).

## Requests

**Lead (runner/headless):**
- Call `faf_posixRunSelfTests(verbose)` (`#include "platform/posix/crt/SelfTest.h"`) at start-up, after static init and before the sim. Treat false as fatal, or at least as a loud warning.
- Call `faf_crtmathTraceDump()` (`platform/posix/crt/CrtMath.h`) when the replay ends, before any `_exit`/`TerminateProcess`/`std::_Exit`.
- After the link, add `macos/tests/crt/check_symbols.sh build/macos/faf_replay` and `check_fma.sh` on the `faf_engine` objects and the two archives as CI or post-link steps.

**core-sys:**
- `X87Math.cpp:9`: `#if defined(_M_X64) || defined(FAF_POSIX)`. The self-test links `msvc8::MultiplyIntegerX87` from it.
- `WinCompatMisc.cpp` `TerminateProcess(GetCurrentProcess())`: call `faf_crtmathTraceDump()` before `_exit`. It returns false at once when tracing is off.

**wx:** the same dump call before the `std::_Exit(1)` taken when `app->Init()` fails in `stubs/wx/WinAppPosix.cpp`.

**math (software x87):**
- Implement the 27 symbols with `X87Math.cpp`'s signatures.
- The self-test's x87 known answers above must hold. The derived ones come from `kat_oracle.py`; run `python3 -I macos/tests/crt/kat_oracle.py`.
