# Native arm64 simulation determinism plan

How the native Apple Silicon build reproduces the simulation bit for bit: what it must match,
every floating-point and CRT path the sim depends on, how to emulate the x87 routines, what to
do about the UCRT's transcendental functions, the code generation rules, and how to verify it.

*Research snapshot, 27/10/2026. Branch `macos-native`. Read-only research: no source file was
changed. Facts marked **(measured)** were checked on this Mac (M2 Max, macOS 27.0, Apple clang
21.0.0) or in the released x64 binary `~/Gaming/faf-re-bin/v1.1.2/x64/main_x64.exe` and its
PDB. Everything else is analysis.*

---

## 0. Summary

1. **Target.** For Milestone 1 the arm64 sim must reproduce **faf-re x64 Release**, not retail
   directly. x64 is the build that is proven against retail: 315 of 323 vault replays match, and
   it ran a live game alongside retail and x86 with no desync (`docs/RETAIL_REPLAY_PARITY.md`).
   That build is clang-cl 22.1.7 (PDB string), statically linked `libucrt.lib` 10.0.22000,
   x87 helpers from `X87Math64.asm`, and the UCRT's FMA3 code paths on any CPU that has FMA3.
   arm64 never diverges from x64 on its own. Retail-parity fixes go into x64 and arm64
   together, or cross-play between them breaks.
2. **Three sources of bits.** x64 sim results come from three places:
   - SSE scalar and packed IEEE arithmetic. arm64 scalar and NEON give the same bits with no
     FMA contraction, FPCR = 0 and saturating float-to-int conversion on both sides.
   - The 27 `msvc8_x87_*` assembly routines, which run on the real x87 unit with `_PC_24`.
     arm64 needs a software x87 for these.
   - The static UCRT. arm64 needs a clone of the UCRT functions the sim actually reaches.
3. **x87 emulation.** Write it by hand. Do not ship SoftFloat in the engine.
   - **Base layer:** an exact soft-extended core (64-bit significand, built on `__uint128_t`)
     that reproduces precision control 24/53/64 exactly.
   - **Fast path:** a round-to-odd double path for the 8 hot routines. It is provably exact for
     operands of 53 bits or fewer. The user's MIT-licensed `PcRoundCore.h` from rosettax87
     already implements it.
   - **Transcendentals:** double-double kernels that emulate the hardware, including Intel's
     66-bit π argument reduction. They round to a 64-bit significand, then through the same
     rounding steps the asm takes.
   - **Error analysis:** a result can differ from real hardware only where the hardware's own
     ≤1-ulp64 error straddles a float rounding boundary. That is at most about 2⁻³⁹ per call.
   - **Evidence:** a precision-control-aware x87 emulator with only *double*-precision
     transcendentals played retail replay 27890640 with 0 mismatches. Plain Rosetta, which is
     not precision-control-faithful, had 88 mismatches on the same replay.
4. **UCRT.** Static analysis finds very few sim paths into UCRT `asin/acos/atan/atan2/pow`, yet
   linking UCRT 26100 desynced replay 26935800 at beat 2800. So the reached set must be measured
   (instrumentation, §6.4) before cloning anything.
   - **Short term:** a FAF-owned `faf_crtmath` defines the C entry points. ld64 binds every
     engine, Wild Magic and Lua reference to it. It starts on Apple libm plus tracing, then
     moves to exact clones of UCRT 22000 (FMA3 variant) for the reached functions.
   - **Long term (upstream):** route those sim uses through retail-faithful x87 emulation, so
     no build depends on a UCRT version.
5. **Codegen findings (measured):**
   - Apple clang 21 and libc++ **bypass `-fno-builtin-*`** for the float overloads
     `std::sin(float)` and `std::pow(float, float)`. They constant-fold them with the
     compiler's libm and turn `std::pow(x, 2.0f)` into `x*x`. **`-fmath-errno` fixes it**
     (lead request).
   - `-fno-strict-float-cast-overflow` lowers C++ float-to-int casts to **saturating**
     `llvm.fptosi.sat`, on x86 as well as arm64. The released x64 binary is full of the
     saturating sequence: 971 of 1,139 `cvtt*2si` sit in it. So x64 and arm64 agree. The
     comment in `cmake/FafCodegen.cmake` that promises "MSVC's 0x80000000" is stale.
   - `std::fmin`/`fmax` lower differently on the two (signed-zero results differ). There is one
     sim site.
   - macOS `rand()` is not MSVC's LCG. `math.random` needs an MSVC-compatible `rand`.
   - ARM's default NaN is positive (`0x7FC00000`); x86's is negative (`0xFFC00000`).

---

## 1. What "bit-identical" means here

| Level | Reference | How it is checked | Status |
|---|---|---|---|
| R | retail `ForgedAlliance.exe` (MSVC8, x86, x87 under `_controlfp(_PC_24, _MCW_PC)`, `_set_SSE2_enable(0)`) | per-beat checksums the live clients recorded in each replay | ground truth |
| X | faf-re x64 Release: clang-cl 22.1.7, `libucrt.lib` 10.0.22000, `X87Math64.asm`, FMA3 CPU | 315/323 replays, mixed live game with retail and x86 | proven against R |
| A | native arm64 (this port) | same replays; per-beat diffs against X | goal: A ≡ X on every sim path |

**The rule:** every operation whose result can reach checksummed sim state, or a decision the
sim makes (including the Lua AI's orders), must produce on arm64 the bits X produces. Where X
itself deviates from R, for example `msvc8::asinf` going through UCRT (§2), arm64 copies X.
The deviation goes upstream to be fixed in both builds.

**What the sim depends on in X:**

- **SSE arithmetic** (the bulk of the sim): IEEE single and double add, sub, mul, div, sqrt,
  conversions and compares, round-to-nearest, no FTZ/DAZ, no FMA. The engine is built for
  baseline x86-64 with no `-mfma`.
- **x87 helpers**: every x87 instruction in `main_x64.exe` is in `X87Math64.asm`
  **(measured)**. Instruction counts in the release binary: `fsin` 6, `fcos` 4, `fptan` 1,
  `fpatan` 6, `fyl2x` 6, `f2xm1` 2, `fscale` 2, `frndint` 2. Each matches the count in the asm
  source, so the UCRT contains no x87 transcendental code.
- **UCRT 22000 functions.** The PDB lists the linked math objects **(measured)**:
  - `tran\noti386\{acos,asin,atan,atan2,atan2f,cosh,sinh,tanh,sqrt,sqrtf}.c`
  - `tran\amd64\{cos,cosf,sin,sinf,tan,tanf,exp,log,log10,logf,pow,powf,fmod}.asm`
  - `remainder_piby2_forfma3.asm` and `__acrt_initialize_fma3` / `__fma3_is_available`
  - About 160 `vfmadd*`/`vfmsub*` instructions in the binary. The engine never emits FMA, so
    these are the UCRT's FMA3 variants.
- **Other CRT behaviour**: `rand`, `printf("%.14g")`, `strtod`, NaN spellings (§2.2).

---

## 2. Inventory

Counts come from grepping `src/moho`, `src/lua`, `src/gpg/core`, `src/legacy` and `src/util`,
excluding `legacy/math/X87Math*` and comments. "Sim dirs" means everything outside
`moho/{render,ui,audio,particles,app,debug,console,movie}`. Some files in sim directories are
user-side (decals, water); those are annotated. `moho/misc/CrtRuntimeHelpers.cpp` is **not in
`src/sources.cmake`** and is ignored.

### 2.1 Floating-point math

| Function (as the sim spells it) | Sites (sim dirs / other) | Sim-relevant | Retail (R) computes it with | faf-re x64 (X) | arm64 requirement |
|---|---|---|---|---|---|
| `msvc8::sin`/`cos` (float), `msvc8::sinf`/`cosf`, Lua `math.sin`/`cos` | sin 23 + cos 25 float + sinf 9 + cosf 9 = 66 | **yes**: orientations, aim, formations, fall-down, beams, quaternions | `fld; fsin` / `fld; fcos` leaves 0x005734E0 / 0x005734D0, result rounded by `fstp dword` | `msvc8_x87_sinf`/`cosf`: real x87 in long mode | emulated `fsin`/`fcos` (§3.4), then round to float |
| `msvc8::cos(0x1.41b2f6p+0)` (double literal → template → `std::cos(double)`) | 2 (`CWldMap.cpp:2147,3926`) | no (sky-dome radius) | x87 `fcos` | UCRT `cos` (FMA3) | `faf_crtmath` `cos` |
| `msvc8::tanf` (Lua `math.tan`) | 1 | yes (Lua) | `fptan` leaf 0x00919A40 | `msvc8_x87_tanf` | emulated `fptan` with the out-of-range quirk (§3.4) |
| `msvc8::atan2` (float), `atan2f`, `atanf`, Lua `math.atan`/`atan2` | 9 + 2 + 1 | **yes**: headings, aim arcs, rotators | `fld y; fld x; fpatan` leaf 0x004E9DF0; atan is `fld1; fpatan` | `msvc8_x87_atan2f` | emulated `fpatan` |
| `Atan2Difference`, `Atan2Plus`, `SinTimesCos`, `SinTimes`, `CosTimesCos`, `SlerpSphericalBlend` | 4, 2, 1, 1, 1, 1 | **yes** | inlined x87 chains under PC24 | matching asm routines | emulated chains (§3.2) |
| `IntrinsicExp`/`Ln`/`Log10` (Lua `math.exp`/`log`/`log10`, Gaussian `CRandomStream.cpp:291`), `IntrinsicLnQuotient` (`SimDriver.cpp`, game rate), `ScaledLnQuotient` (HaStar edge cost, `Cluster.cpp`) | 1, 2, 1, 1, 1 | **yes** (`ScaledLnQuotient` decides pathfinder edge buckets) | `/Oi` inline `fldln2; fyl2x`, `fldl2e … f2xm1 … fscale` | asm | emulated `fyl2x`/`f2xm1`/`fscale`/`frndint` |
| `msvc8::acos(double)` (2 in `QuaternionMath.cpp` SLERP / axis-angle, Lua `math.acos`) | 3 | **yes** | `_CIacos` x87 body at caller PC (24) | `msvc8_x87_acos` with PC24 forced when `global_mode_sse2 == 0` and \|x\| ≤ 1; otherwise UCRT `acos` | emulated chain; UCRT clone for the out-of-domain lane (NaN) |
| `msvc8::asin(double)` (Lua `math.asin`) | 1 | yes | `_CIasin` x87 body | `msvc8_x87_asin` (PC24) | emulated chain |
| `msvc8::asin(float)` → `asinf` → `std::asin(double)` (`Vector4f.cpp:130`, quaternion→Euler pitch) | 1 | probably (verify callers) | `_CIasin` x87 body, PC24 | **UCRT `asin`**, then cast. Not the x87 lane, so X differs from R here (upstream issue) | UCRT 22000 `asin` clone (§4) |
| `msvc8::pow(double)` (Lua `math.pow` and `^`) | 1 | yes | `_CIpow` x87 body (`fyl2x`/`frndint`/`f2xm1`/`fscale`, PC24) | `msvc8_x87_pow` for finite positive base or integer exponent; UCRT `pow` for 0/inf/NaN/domain error | emulated chain; UCRT special values (Annex F + UCRT NaN) |
| `msvc8::pow(float, float)` → `powf` → `std::pow(double)` | 1 (`SimDriver.cpp:1301`) | no (wall-clock sim-rate scale) | — | UCRT `pow` | `faf_crtmath` `pow` |
| `msvc8::expf`/`logf`/`powf` | 0 others | — | — | UCRT | `faf_crtmath` |
| Direct `std::sin/cos/atan2/pow` in sim dirs | 15 (`RDebugWeapons` 4 debug; `WaveSystem` 2 and `HighFidelityWater` 2 water render; `EffectLuaStartupRegistrations` 3 decals; `CEfxEmitter` 1 particles; `CWldSession` 3 `std::pow` sim-rate UI) | no, per inspection | — | UCRT (float overloads `sinf`/`cosf`/`atan2f`/`powf`) | `faf_crtmath`. With `-fmath-errno`, libc++'s `__builtin_*` overloads stay calls (§5) |
| Wild Magic `Math<Real>::Sin/Cos/ACos/ASin/ATan/ATan2/Exp/Log/Pow` (`(Real)::sin((double)x)`) | 144 sites in 17 Foundation files; 15 in `Wm3Quaternion.inl` | **not reached** per grep: the engine uses `Vector`, `Box`, `Sphere`, `Plane`, `Intr*`, `Dist*` and `ConvexHull3`, which have no transcendentals. The patched static helpers (`MakeFromAxisAngle`, `FromEulerXYZ`, `Slerp`) are not called by the sim (`Unit.cpp:14340`, `Sim.cpp:3498`) | `/Oi` → `_CI*` x87 bodies | UCRT double functions | `faf_crtmath` (link-time binding). Confirm unreachable with the tracer (§6.4) |
| `Wm3::Math<float>` static constants (`PI = (float)(4.0*atan(1.0))`, …) | static init | yes (constants) | CRT `atan` | UCRT `atan(1.0)` | `faf_crtmath` `atan(1.0)` must give `0x3FE921FB54442D18`; self-test |
| `std::sqrt` (float and double), `Wm3::SqrtfBinary`, Lua `math.sqrt` (double) | 178 / 18; `SqrtfBinary` 6 | **yes** | float: inline `fsqrt` under PC24, which equals correct float rounding; double: CRT `sqrt` under PC24, which rounds to 24 bits (`X87SqrtCall.h`) | `sqrtss` / `sqrtsd` inline (305 / 28 in the binary) | native `fsqrt s` / `fsqrt d`: identical |
| `std::fabs` 87, `copysign` 7, `floor` 65, `ceil` 25, `trunc` 2, `nearbyint` 9, `rint` 1 | as listed | yes | x87 `fabs`; CRT `floor`/`ceil` (exact) | inline or UCRT (exact) | native `frint*`/`fabs`: exact, identical including signed zero |
| `std::lround` | 1 (`SimDriver.cpp:1890`, UI font) | no | — | UCRT; `long` is 32-bit | n/a (note: `long` is 64-bit on macOS) |
| `msvc8::fmod` 12 sim (+3 UI), `std::fmod` 3, Lua `math.mod` | 15 | yes | `_CIfmod` (exact `fprem` loop; zero dividend → +0) | UCRT `fmod` (exact) plus the `msvc8::fmod` zero fix | libm `fmod` (exact) plus the same fix; NaN sign differs (§2.3) |
| `msvc8::frexp` (Lua), `std::ldexp` (Lua) | 1, 1 | yes (Lua) | CRT C bodies with zero and inf quirks | UCRT plus the `msvc8::frexp` fix | libm (exact) plus the same fix |
| `std::fmin`/`std::fmax` (`CColPrimitiveBase.cpp:155`, box SAT overlap) | 1 / 1 | **yes** (collision) | recovered code (MSVC8 has no `fmin`) | `llvm.minnum` → `cmpunordss` + `minss` select: `(b < a) ? b : a`, NaN a → b **(measured)** | **different**: arm64 emits `fminnm`/`fmaxnm` **(measured)**, and `fmin(+0, −0)` is −0 on arm64 but +0 on x64. Replace with the explicit x64-equivalent ternaries (§5) |
| `std::fma`, `std::nextafter` (`X87Math.cpp` `RoundExactProductToFloat`) | 1, 1 | yes | n/a (helpers that model x87) | UCRT `fma.c` (exact), `nextafter` | native `fmadd` (exact), libm: identical |
| `MulTwisterWord` 9, `MultiplyX87` 2, `MultiplyIntegerX87` 2, `HalfSumX87` 4, `HalfAbsDifferenceX87` 4, `TruncateToInt` 1, `RoundToFireClockTicks`, `GridPos` | as listed | yes | x87 sequences | portable C++ already | unchanged C++ (needs `-ffp-contract=off`; `std::fma` is exact) |
| `msvc8::RoundToInt` (`fld; fistp`) | 22 | **yes** (cells, ticks, AI) | `fistp`, RC = nearest | `msvc8_x87_round_to_int` | RNE plus integer-indefinite `0x80000000` for NaN or out of range (§3.2) |
| C++ float→int casts | hundreds | yes | `_ftol2_sse` (`cvttsd2si`, `0x80000000`) or `fistp` | **saturating** `llvm.fptosi.sat` (971 of 1,139 `cvtt*2si` in the binary are in the saturating sequence) **(measured)** | `fcvtzs`/`fcvtzu` plus clamps: same saturating semantics **(measured)** |
| BilinearHeight 1 (`STIMap`), BoxRadiusOnAxis 1 (SAT), ScaleVector 3, ScaleVectorToLength 4, ScaleVectorByMaxLength 1, ScaleVectorToLengthOf 1, VectorLengthZYX 1 | 12 | **yes, hot** | inlined PC24 x87 chains | asm | fast path (§3.3) |
| SIMD: `_mm_mul_ps` 8, `_mm_add_ps` 6, `loadu`/`storeu`/`shuffle` (`VMatrix4.h`, `Vector4f.h`) | ~40 | yes (`VMatrix4` transforms) | (retail x87 or SSE) | SSE packed | NEON stand-in (`include/immintrin.h`): lane-exact, no contraction |
| `_mm_getcsr`/`_mm_setcsr` (`X87Math.cpp` lane gate; `SimWorkerPool.cpp:102,112` copies the control bits to workers) | 3 | yes | MXCSR | MXCSR | stand-in maps rounding and FTZ to FPCR. Must stay RN and FTZ = 0 (§5) |
| `platform::SetX87PrecisionControl(_PC_24)` (`SimDriver.cpp:1118`, `CArmyStats.cpp:971`, `CWldSessionLoaderImpl.cpp:110`, `WinApp.cpp`, `CGpgNetInterface.cpp`, `FrameDumpCommands.cpp`) | 6 | yes | sets PC24 per thread | no-op (x64) | no-op; good place for an FPCR check. `_PC_24`/`_MCW_PC` need POSIX definitions (lead request) |
| `SetSse2Mode(0)` (`CScApp.cpp:969`, `global_mode_sse2` initialised to **1** at `MathReflection.cpp:1308`) | 1 | **yes** | `_set_SSE2_enable(0)` | gates the acos/asin/pow x87 lanes | the headless replay entry **must** run it, or acos/asin/pow fall through to UCRT |
| `long double` | 0 in sim | — | 80-bit not used by MSVC | 64-bit | 64-bit on Apple arm64 **(measured)** |

### 2.2 Non-math CRT behaviour the sim depends on

| Item | Sites | Retail / x64 | macOS default | Requirement |
|---|---|---|---|---|
| `rand()`/`srand()` (Lua `math.random`/`math.randomseed`, `LuaObject.cpp:11107,11162`; the AI scripts use `math.random`) | 2 | msvcr80 and UCRT: per-thread LCG `s = s*214013 + 2531011; return (s>>16) & 0x7FFF`, state 1 per new thread (first values 41, 18467, 6334, 26500, 19169) | different generator, `RAND_MAX` 0x7FFFFFFF | **Define `rand`/`srand` in the executable** (`thread_local uint32_t s = 1`). ld64 binds the engine's references to it. Self-test the first five values |
| Number→string `"%.14g"` (Lua `tostring`, concatenation, `string.format`) | many | UCRT ≥ 10.0.19041 printf is correctly rounded (ties-to-even on the exact value; faf-re does not link `legacy_stdio_float_rounding.obj`) | correctly rounded (gdtoa) | Same for finite values. Non-finite values differ: UCRT prints `inf`, `-inf`, `nan`, `-nan(ind)` (the x86 default NaN), `nan(snan)`. macOS prints `inf`, `-inf`, `nan`, and `nan` for a negative NaN **(measured)**. Wrap the Lua formatter for non-finite values |
| String→number `strtod` (blueprint and Lua literals, `LuaObject.cpp:17678,22768`) | many | UCRT `strtod` | correctly rounded | Expected identical (both correctly rounded). Verify with a differential corpus (§6.2) |
| Locale | — | "C" | "C" unless `setlocale` | Never call `setlocale(LC_ALL/LC_NUMERIC, "")` in the POSIX layer. Lua `os.setlocale` exists (`io_setloc`) |
| `std::sort`/`nth_element` with ties | 25 sim-dir calls | MSVC STL | libc++ (different algorithm) | Sorts on unique keys are safe (checked: `Unit.cpp:12486`, `Cluster.cpp:2478`, the pointer sorts). Order-sensitive sorts already use `legacy/algorithms/Sort.h` (msvc8 replica). Audit any new `std::sort` with ties |
| `std::unordered_map`/`set` iteration | 13 files (`EntityDb`, `Entity`, `CAiReconDBImpl`, `RRuleGameRules`, …) | MSVC STL hashing | libc++ hashing | Iteration order differs. Audit that no sim result depends on it (they appear to be lookup indexes for the performance switches) |
| `long` in hashing (`Cluster.cpp:2238` `msvc8::hash_value(static_cast<long>(…))`) | — | 32-bit | 64-bit | Value < 2¹⁶, so harmless here; covered by the LP64 audit rule |

### 2.3 NaN encoding (a hardware difference)

- **Default NaN differs.** An invalid operation (`0*inf`, `inf-inf`, `0/0`, `sqrt(-1)`) gives the
  **x86 default NaN `0xFFC00000` / `0xFFF8000000000000` (negative)** on x64 SSE. arm64 gives
  **`0x7FC00000` (positive)** **(measured)**. libm also differs: macOS `acos(2)` is
  `0x7FF8000000000000` **(measured)**.
- **Propagation mostly agrees.** For quiet NaN inputs both return the first NaN operand.
  Signalling NaNs do not occur in practice.
- **No hardware fix.** FEAT_AFP is not present on this M2 Max (`hw.optional.arm.FEAT_AFP: 0`,
  measured), so FPCR.AH cannot be used even if it would help.
- **Consequence:** a NaN produced by plain C++ sim code has a different sign bit on arm64. This
  is invisible to comparisons and to `(int)` (0 on both). It is visible to hashing raw bytes,
  `signbit`/`copysign`, bit casts and number formatting. The x87 emulation itself produces
  x87's NaNs exactly (§3.4), because it is software.
- **Mitigation:** a diagnostic `FAF_NAN_AUDIT` mode that scans checksummed state for NaN at
  each checksum and logs the entity and field (§6.5). A NaN in hashed state is a latent
  cross-architecture desync, and also an x86-versus-x64 one for payloads. Fix such sites one
  at a time.

---

## 3. x87 emulation for the 27 `msvc8_x87_*` routines

### 3.1 The contract to reproduce (what `X87Math64.asm` does)

- **Precision.** Every routine except `round_to_int`, `sinf`, `cosf`, `tanf`, `atan2f` and
  `control_word` forces **PC = 24** for its body (`and eax, 0fffffcffh`). RC is left at the
  thread default, round-to-nearest; the game never changes it. The five exceptions use only
  PC-independent instructions.
- **Control word.** `msvc8_x87_control_word` returns the thread's real CW. On Windows x64 that
  is `0x027F`. It feeds `TakesSse2Lane` (`X87Math.cpp:69`), which only checks the low byte
  `0x7F`. arm64 returns the constant `0x027F`.
- **What PC24 affects** (Intel SDM Vol. 1 §8.1.5.2): only `FADD/FSUB/FSUBR/FMUL/FDIV/FDIVR`
  (including the P, I and R forms) and `FSQRT`. Each rounds the **exact** result once to a
  24-bit significand **while keeping the 15-bit extended exponent**: no overflow at FLT_MAX and
  no denormal below FLT_MIN until the store.
- **What PC24 does not affect:** `FLD`/`FILD` (exact), the constant loads, `FSIN`, `FCOS`,
  `FPTAN`, `FPATAN`, `FYL2X`, `F2XM1`, `FSCALE`, `FRNDINT`, `FCHS`, `FABS` and the stores.
- **Stores.** `fstp dword` rounds the register (up to 64 bits) once to binary32, with
  denormals and overflow. `fstp qword` rounds to binary53. A transcendental result therefore
  goes 64 → 24 bits (two roundings in total, counting the instruction's own), and a PC24 result
  at a tiny exponent rounds twice (24 bits, then the denormal grid). These are exactly the
  difffuzz findings documented in `X87Math.h`.
- **`fistp`** rounds by RC (RNE). NaN, infinity or out-of-range gives the integer indefinite
  `0x80000000`.
- **x87 NaN rules.** Invalid operations give the *real indefinite*, negative QNaN
  (`0xFFC00000` once stored as float). With two NaN operands the larger significand wins. A
  signalling NaN loaded by `fld` is quieted.

### 3.2 Per-routine breakdown

Operand classes: **F24** (a float, or a PC24 result), **F53** (a double), **F64** (64-bit
significand: transcendental output or a constant such as `fldl2e`/`fldln2`/`fldlg2`). Paths
(§3.3): **fast** = double plus round-to-odd plus R24; **dd** = double-double round-to-odd;
**Ext** = exact 128-bit core; **T** = transcendental kernel.

| # | Routine | x87 shape | Operand classes | Path | Notes |
|---|---|---|---|---|---|
| 1 | `round_to_int` | `fld; fistp` | F24 | fast | `frintn` plus range check: `!(v >= -2^31 && v < 2^31)` → `INT32_MIN` (NaN included) |
| 2 | `control_word` | `fnstcw` | — | const | `0x027F` |
| 3 | `sinf` | `fsin` → store32 | F24 → F64 | T | \|x\| ≥ 2⁶³: result is x (C2 = 1, operand unchanged); ±inf → indefinite |
| 4 | `cosf` | `fcos` → store32 | F24 → F64 | T | as `sinf` |
| 5 | `tanf` | `fptan; fstp st0; fstp m32` | F24 → F64 | T | \|x\| ≥ 2⁶³: nothing is pushed, so the second `fstp` pops an empty register (masked underflow), stores **`0xFFC00000`** and leaves TOP rotated on x64. Emulate the value |
| 6 | `atan2f` | `fpatan` | F24, F24 → F64 | T | Intel `FPATAN` special-value table (±0, ±inf quadrants) |
| 7 | `acos` | `(1+x)·(1−x)` PC24, `fsqrt` PC24, `fxch`, `fpatan` → store64 | x F53; products F24 | fast + T | first add and subtract have an F53 operand: round-to-odd handles it |
| 8 | `asin` | as `acos` without `fxch` | same | fast + T | `fpatan(x, s)` |
| 9 | `pow` | `fyl2x(y, x)` → t F64; `frndint`; `n − t` PC24; `fchs`; `f2xm1`; `1 + ·` PC24; `fscale` → store64 | F53, F53 → F64; then F64 operands | Ext/dd + T | `n − t` with F64 t is a PC24 op on an F64 operand (dd round-to-odd). `f2xm1`'s argument is F24 and in [−0.5, 0.5] |
| 10 | `atan2_difference` | `fpatan` → store32 (rounded `firstAngle`); `fpatan`; `fsubr m32` PC24 → store32 | F24 − F64 | T + dd | |
| 11 | `atan2_plus` | `fpatan; fadd m32` PC24 → store32 | F64 + F24 | T + dd | |
| 12 | `sin_times_cos` | `fcos; fsin; fmulp` PC24 → store32 | F64 × F64 | T + Ext | |
| 13 | `sin_times` | `fsin; fmul m32` PC24 → store32 | F64 × F24 | T + dd | |
| 14 | `slerp_spherical_blend` | `fsin(a)`; `1/·` PC24; `a·(1−t)` PC24; `fsin`; `× inv` PC24; `a·t` PC24; `fsin`; `× inv` PC24; 4 lanes `q1·w1 + rhs·w2` PC24 → store32 | F64 divisor, F64 × F24, then F24 chains | T + Ext + fast | the most complex routine; transcribe line by line |
| 15 | `intrinsic_exp` | `x × fldl2e` PC24 → F24; `frndint`; `fsub` PC24; `f2xm1`; `fld1; faddp` PC24; `fscale` → store32 | F24 × F64const; F24 − F24; 1 + F64 | dd + T | |
| 16 | `intrinsic_ln` | `fyl2x(fldln2, x)` → store32 | F24 → F64 | T | y is the **64-bit** `ln2` constant, not the true ln 2 |
| 17 | `intrinsic_ln_quotient` | `fyl2x` ÷ `fyl2x(base F53)` PC24 → store32 | F64 / F64 | T + Ext | |
| 18 | `scaled_ln_quotient` | `num/den` PC24 → F24; `fyl2x`; `× scale` PC24 → store32 | F24/F24; F64 × F24 | fast + T + dd | decides HaStar cost buckets: high priority |
| 19 | `intrinsic_log10` | `fyl2x(fldlg2, x)` → store32 | F24 → F64 | T | |
| 20 | `cos_times_cos` | `fcos; fcos; fmulp` PC24 → store32 | F64 × F64 | T + Ext | |
| 21 | `scale_vector_max_length` | `fsqrt` PC24; `fdivr` PC24; 3 × `fmul` PC24 → store32 | all F24 | fast | `lengthSq` is the caller's float (as the asm comment says) |
| 22 | `scale_vector_to_length` | same shape | F24 | fast | |
| 23 | `scale_vector` | 3 × `fmul` PC24 → store32 | F24 | fast | differs from SSE only for denormal products |
| 24 | `scale_vector_to_length_of` | 3 squares, 2 adds, `fdiv`, `fsqrt`, 3 muls, all PC24 | F24 | fast | |
| 25 | `box_radius_on_axis` | about 15 mul/add plus 3 `fabs`, PC24 chain → store32 | F24 | fast | hot (SAT) |
| 26 | `vector_length_zyx` | 3 squares, 2 adds, `fsqrt`, PC24 → store32 | F24 | fast | |
| 27 | `bilinear_height` | 4 sub, 3 mul, 3 add, PC24 → store32 | F24 | fast | hot (terrain height) |

All transcendental inputs are doubles: F24, or F53 for `acos`, `asin` and `pow`. Only `fyl2x`'s
multiplier can be F64 (the constants). That keeps the kernels simple.

### 3.3 Arithmetic core

**Layer 1: `x87::Ext` (exact reference).**
- Representation: sign, `int32` exponent, `uint64` significand with the explicit integer bit,
  and a class (zero, normal, inf, NaN carrying an x87 payload).
- Operations: `add/sub/mul/div/sqrt(a, b, Prec{24,53,64})` with RC = nearest only (assert on
  anything else). The exact intermediate is built with `__uint128_t` plus a sticky bit:
  - `mul`: 64×64 → 128 bits.
  - `add`: alignment into 128 bits plus sticky.
  - `div`: `(a << 64) / b` with the remainder giving the sticky bit.
  - `sqrt`: 128-bit integer square root with remainder.
- Rounding: RNE to `Prec` bits, keeping the exponent unbounded inside the extended range.
- Conversions: `fromFloat`, `fromDouble` and `fromInt32`, all exact. `toFloat` and `toDouble`
  round once, with denormals and overflow. `toInt32Fistp` gives RNE or `INT32_MIN`.
- Constants, with 64-bit RN significands **(computed with Python `decimal`; they match QEMU's
  `floatx80` constants)**:

  | Constant | Exponent | Significand |
  |---|---|---|
  | `fldl2e` | `0x3FFF` | `B8AA3B295C17F0BC` |
  | `fldln2` | `0x3FFE` | `B17217F7D1CF79AC` |
  | `fldlg2` | `0x3FFD` | `9A209A84FBCFF799` |
  | `fldpi` | `0x4000` | `C90FDAA22168C235` |
  | `fld1` | — | exact |

  Verify them once on hardware with `fstp tbyte`.
- Every routine is first written on Layer 1, as a line-for-line transcription of
  `X87Math64.asm` against a tiny `X87Stack` helper (`fld`, `fstp`, `fxch`, `fmul_st(i,j)`,
  `fsubr_m32`, …). The C++ can then be checked against the asm by eye. The register forms keep
  Intel/MASM operand order: `fsub st(1), st(0)` means ST1 ← ST1 − ST0.

**Layer 2: fast paths (production for the hot routines).**
- For an op whose operands fit in a double (F24 or F53), compute `r = RN53(a ∘ b)` and the
  **exact** sign of the error with an error-free transform: TwoSum for ±; `fma(a, b, −r)` for ×;
  `fma(−q, b, a)` for ÷; `fma(−s, s, a)` for √.
- Then apply round-to-odd at 53 bits (if the error is non-zero and `r` is even, step one ulp
  toward the exact value) and round to a 24-bit significand on the double's bit pattern
  (add-and-mask RNE at bit 29).
- **Correctness.** Round-to-odd at p+2 or more bits followed by RN at p bits equals RN-p of the
  exact value (Boldo–Melquiond). 53 ≥ 24+2, so the result is the exact PC24 result whenever
  the double does not overflow or underflow. For F24 operands even a plain double op would be
  enough (Figueroa: double rounding is innocuous when p′ ≥ 2p+2 = 50); round-to-odd extends the
  guarantee to F53 operands (`acos`, `asin`).
- **Range argument** for routines 21–27 with finite float inputs: every intermediate stays
  between about 2⁻⁵⁵⁴ and 2⁴⁰⁷, far inside the double range. Examples: BoxRadius products of
  three floats reach at most about 2³⁸⁶; `scale_vector_to_length_of`'s sum of squares divided
  by a denormal `lengthSq` reaches about 2⁴⁰⁷. The PC24 extended-exponent semantics are
  therefore exact in double. Assert finite and normal intermediates in debug, and fall back to
  Layer 1 otherwise.
- Store32 of the final F24-in-double is a single `(float)` conversion. Its rounding onto the
  denormal grid is the x87 store's second rounding.
- **This already exists:** `~/Gaming/rosettax87/probe-src/rosetta_runtime/src/PcRoundCore.h`
  (83 lines, MIT, the user's code) implements exactly `add24`/`mul24`/`div24`/`sqrt24` with
  round-to-odd plus `finish24`. It was validated by the replay run in §3.6.
- **Double-double extension.** For ± and × with **one** F64 operand (held exactly as
  `hi + lo`, where `lo` has at most 11 bits), the exact value is a sum of at most three doubles.
  Example: `x·hi` by TwoProd plus `x·lo`, which is exact. Two TwoSums give `r` and a residual
  whose sign is exact, so the same `finish24` applies. F64 × F64, and ÷ or √ with an F64
  operand, use Layer 1. Those occur once or twice per call, in routines 12, 14, 17 and 20 only.
- **Promotion rule:** a fast path ships only after it is bit-identical to Layer 1. Run
  10⁹ random inputs plus edge corpora (denormal, near-overflow and cancellation inputs, the
  difffuzz generators) natively. Both implementations are ours, so this costs minutes.

### 3.4 Transcendental kernels

Each kernel returns the function value rounded **RN to a 64-bit significand**, as an `Ext` or a
double-double `hi + lo`. That is the model of the hardware result. Evaluate it in
double-double (about 106 bits, `std::fma`-based TwoProd) on the reduced argument. Use a Ziv
fast path for the store32-only routines (#3–6, 16, 19): evaluate in double with a rigorous error
bound ε. If the value is farther than ε + 2⁻⁶⁴·|v| from every float rounding boundary, both
roundings agree and the double result is final. Otherwise run the double-double path. Typical
cost is then a few ns.

- **`fsin`/`fcos`/`fptan`:**
  - \|x\| ≤ π/4: no reduction.
  - Otherwise reduce **exactly as Intel does, with the internal 66-bit π**. Its top 68 bits are
    `C90FDAA2 2168C234 C`, whose last two bits are zero. True π is larger: the next bits are
    `0100 0110…`. Source: SDM Vol. 1 §8.3.10, the transcendental-accuracy section revised in
    2014.
  - Compute `r = x − k·(π₆₆/2)` exactly in 128-bit fixed point, which is trivial for 24- or
    53-bit x. Then evaluate sin/cos/tan of r in double-double (minimax or Taylor to degree 25
    for 2⁻¹⁰⁶).
  - With true π instead, results near multiples of π for large \|x\| differ systematically.
    This matters for float arguments of about 10⁵ and up.
  - \|x\| ≥ 2⁶³: result is x unchanged (`fptan`: see #5). ±inf: indefinite. NaN: propagate.
  - **AMD:** reduction behaviour undocumented; measure (§6.2).
- **`fpatan(y, x)`:** atan(y/x) in double-double with a table plus a series, quadrant
  adjustment with π/2 and π, and Intel's table for ±0 and ±inf. The quadrant constants are
  64-bit RN values (π = `…C235`).
- **`fyl2x(y, x)`:** `y · log2(x)` as **one** operation. Write x = 2ᵉ·m, m ∈ [√½, √2), and
  compute `log2(m) = (2/ln 2)·atanh((m−1)/(m+1))` in double-double. Form m − 1 exactly so the
  result has relative accuracy near x ≈ 1, which matters for `math.log` near 1. Multiply by y
  exactly as given (the 64-bit `ln2`/`lg2` constant, or the F53 exponent for `pow`) and round
  once to 64 bits.
  - Special cases: x = 0 → −inf·sign(y) (divide-by-zero masked); x < 0 → indefinite;
    x = 1 → ±0 with the sign rules; x = +inf → ±inf, or indefinite when y = 0.
- **`f2xm1(f)`:** `expm1(f·ln2)` in double-double. The domain \|f\| ≤ 1 holds here, since
  arguments are in [−0.5, 0.5] after `frndint` RN.
- **`fscale`**: exact (truncate ST1 toward zero; overflow → ±inf). **`frndint`**: RNE to an
  integer, exact.

### 3.5 SoftFloat 3e versus hand-written

**Recommendation: hand-write the core (Layers 1–3 above). Do not put SoftFloat in the engine.**

**Why not SoftFloat in production:**
- **No transcendentals.** The hard part (§3.4) has to be written anyway.
- **Global state.** `extF80_roundingPrecision`, `softfloat_roundingMode` and the exception
  flags are global unless built with `THREAD_LOCAL`. The routines run on sim worker threads
  too, for example collision in the parallel phases. The hand-written core is pure functions.
- **Speed.** Estimated tens of ns per extF80 op. BoxRadius has about 30 ops and the sim calls it
  on hot paths. The user moved FAF off the Mac because of sim speed, so the hot routines need
  the round-to-odd double path whatever the slow path is.
- **Size.** The needed subset is tiny: RC = nearest, PC 24/53/64, 5 ops and 5 conversions. The
  exact-PC24 core already exists under MIT (`PcRoundCore.h`).
- **Download.** It needs the user's approval and licence bookkeeping (BSD-3: fine, but one more
  third-party tree).

**Where SoftFloat helps:** as a **test oracle** for Layer 1's basic ops (`extF80_*` with
`extF80_roundingPrecision = 32/64/80` and the "8086" specialization for x87 NaN rules), if the
user approves the download.

**No-download alternative:** Python's stdlib. `fractions.Fraction` gives an exact rational
oracle for the basic ops. `decimal` has correctly rounded `exp`, `ln`, `log10` and `sqrt` at any
precision; sin, cos and atan use the Taylor recipes from the `decimal` documentation at 60+
digits. Both are available offline (Python 3.9.6 here). `mpmath`, `gmpy2` and MPFR are not
installed.

### 3.6 Error analysis: when emulation can differ from real hardware

Let v be the true function value, h the hardware's 64-bit result and e the emulator's.

- **Hardware accuracy.** Intel documents \|h − v\| < 1 ulp₆₄ for the transcendental
  instructions, where its 66-bit-π reduction is accurate. h is faithfully rounded but not
  necessarily correctly rounded.
- **Emulator accuracy.** e = RN₆₄(v) except when the double-double error (about 2⁻¹⁰⁰) makes it
  misround. That is a ~2⁻³⁶ chance at the 64-bit level, and far less after the narrower stores.
- **When the 64-bit values differ.** h ≠ e happens on the fraction of inputs where the hardware
  picked the farther 64-bit neighbour (unknown; assume ≤ 10%).
- **When the stored result differs.** That requires a rounding boundary of the *stored*
  precision to lie between h and e:
  - **store32 or a PC24 consumer** (all routines but 7, 8, 9): boundaries are 2⁴⁰ ulp₆₄ apart,
    so P(mismatch per call) ≤ 2·2⁻⁴⁰ ≈ 2·10⁻¹².
  - **store64** (`acos`, `asin`, `pow`): the *double* differs with P ≤ 2⁻¹⁰. Every consumer then
    rounds to float: Lua `lua_Number` is float, `acosf` casts, and `QuaternionMath.cpp:530`
    compares `angleExact >= 0.001` and casts. So P(float differs) ≤ 2⁻¹⁰·2⁻²⁹ ≈ 2·10⁻¹².
  - **`pow`'s PC24 step `n − t`** (t is F64): same order, about 2⁻⁴⁰.
- **Expected impact.** Assume pessimistically 10⁹ transcendental calls per long game and that
  every one is a hardware-misrounded input. The expected number of float-level differences is
  then ≤ about 2·10⁻³ per game, and most differences never reach checksummed state.
  Retail-versus-retail between Intel and AMD clients carries the same class of risk, so FAF's
  long-standing lockstep stability bounds it empirically.
- **Systematic differences, which must be emulated rather than bounded:**
  1. 66-bit-π reduction (Intel). AMD is unknown and must be measured.
  2. Out-of-range \|x\| ≥ 2⁶³ (`fsin` returns x; `fptan` returns indefinite via underflow).
  3. Special operands of `fpatan`/`fyl2x` (signed zeros, infinities, NaN propagation with the
     larger significand, the real indefinite).
  4. `f2xm1` outside [−1, 1], which is undefined; never reached here.
  5. Any precision-control mistake (extended exponent, double rounding at stores). These are
     exactly the classes difffuzz found; Layer 1 makes them exact by construction.
- **Why double precision is not enough.** A double-only kernel (53-bit, like rosettax87
  `dist-pc`'s openlibm) misses the float rounding boundary with P ≈ 2·2⁻²⁹ per call. That is
  more than 10⁴ times worse: several float-level differences per long game.
- **Empirical evidence (measured, `~/Gaming/faf/bench/results`, replay 27890640, 16 min):**

  | Run | Engine | Checksum mismatches |
  |---|---|---|
  | `stock` | retail under plain Rosetta | **88** |
  | `rx-*` | retail under rosettax87 without precision control | 60 / 182 |
  | `pc-safe`, `pc-jit` | retail under rosettax87 `dist-pc` (exact PC24 via `PcRoundCore.h`, double-precision transcendentals) | **0** |
  | `fafre-*` | faf-re x64 under Wine + Rosetta | **0** |

  This shows PC24 fidelity is what decides parity in practice. It also shows Rosetta's own x87
  is **not** a usable oracle.

### 3.7 Integration in the tree (proposal; the owners decide)

- **`src/legacy/math/X87Math.cpp`** (core-sys owner): one guarded edit,
  `#if defined(_M_X64) || defined(FAF_POSIX)` around the `extern "C"` declarations
  (`__cdecl` is empty in `WinCompat.h:42`). Everything else in the file is already portable.
  It currently fails with `use of undeclared identifier 'msvc8_x87_control_word'` (census
  core-sys).
- **A new area `src/platform/posix/math/`** (needs an owner):
  - `X87Core.h` (Layers 1–2, header-only, with no POSIX dependencies so it **also compiles in
    the Windows build** for hardware differential tests)
  - `X87Kernels.cpp` (§3.4)
  - `X87Emu.cpp`, defining the 27 `msvc8_x87_*` symbols with the exact signatures of
    `X87Math.cpp:12-41`
  - `CrtMath.cpp` (§4), `MsvcRand.cpp` (§2.2)
- **Interim stub:** until the emulator lands, `X87Emu.cpp` may implement the routines with
  `PcRoundCore`-style fast paths plus double-precision kernels. That equals the rosettax87
  `dist-pc` configuration that passed 27890640. Mark it `FAF_POSIX_TODO(determinism)`. Never
  use plain float or `std::` math for these routines.

---

## 4. The UCRT `asin`/`acos`/`atan`/`atan2`/`pow` problem

### 4.1 Facts

- **Version pin.** `src/CMakeLists.txt:209-229`: UCRT 10.0.26100's x64 `asin`, `acos`, `atan`,
  `atan2` and `pow` (float and double) differ in the last bit on 0.1–1% of inputs from every
  earlier release. With 26100, replay 26935800 desyncs at beat 2800. The build is pinned to
  10.0.22000, which is bit-identical to 19041 and 20348.
- **Static reach is small.** Grep shows few sim paths that reach UCRT at all:
  - the `msvc8::asinf` C++ site (`Vector4f.cpp:130`)
  - the special-value lanes of `msvc8::acos`/`asin`/`pow` (out of domain, 0, inf, NaN)
  - the `/sse2` lane, if anyone launches with it
  - unknown indirect paths

  So *which* reached function caused the 26100 desync is **unknown**. Resolve that first
  (§6.4).
- **UCRT layout** (measured from the PDB): `acos`, `asin`, `atan`, `atan2` and `atan2f` are C
  (`tran\noti386\*.c`). `exp`, `log`, `log10`, `logf`, `pow`, `powf`, `sin`, `cos`, `tan` and
  their float variants are assembly with FMA3 variants (`remainder_piby2_forfma3.asm`,
  `__fma3_is_available`). The file names (`log_256_lead_tail_table`, `lsincos_array`,
  `remainder_piby2`, `atan2f.c`) are those of **AMD LibM**, the origin of Microsoft's x64 libm.
- **FMA3 path depends on the CPU.** The UCRT picks its FMA3 code by CPUID, so x64 results
  depend on whether the CPU has FMA3. faf-re never calls `_set_FMA3_enable`. Every modern PC
  takes the FMA3 path.
- **Rosetta FMA3** **(measured):** Rosetta 2 advertises FMA, AVX and AVX2 only with
  `ROSETTA_ADVERTISE_AVX=1`. `~/Gaming/bin/wine-run` sets it, so x64-under-Wine reference runs
  on this Mac take the FMA3 path like a modern PC. Without the variable they would silently
  take the SSE2 path.

### 4.2 What must be bit-identical, and to what

| Question | Answer |
|---|---|
| Reference for M1 | **UCRT 10.0.22000.0 static `libucrt.lib`, x64, FMA3 dispatch on, MXCSR default.** Not msvcr80, and not "correctly rounded". |
| Which functions | Exactly those the sim reaches. Start from the measured linked set and trim with the tracer: `asin`, `acos`, `atan`, `atan2`, `atan2f`, `pow`, `powf`, `exp`, `log`, `log10`, `logf`, `sin`, `cos`, `tan`, `sinf`, `cosf`, `tanf`, plus `cosh`, `sinh`, `tanh`, `exp2(f)`, `log2f` and `cbrt` if reached |
| Which inputs | All inputs, as a requirement, because they cannot be enumerated in advance. **Float univariate functions** are verified exhaustively (2³² inputs). Double and bivariate functions get an algorithmic clone plus very large differential tests |
| Which outputs | The returned bits, including NaN sign and payload on the special lanes, as far as they propagate into sim state |
| Relation to retail | Retail computed these with msvcr80's x87 `_CI*` bodies at PC24. X matches retail only where the x87 lane is used (`acos`/`asin`/`pow`) or where float rounding hides the last-bit difference. Example from `X87Math.h`: `asin(0x3F052EAA)` is `0x3F0C1118` with retail's x87 body and `0x3F0C1117` from a modern CRT, so `msvc8::asinf` on X can differ from retail |

### 4.3 Options

| Option | What | Exact? | Cost / risk |
|---|---|---|---|
| **A. Clone UCRT 22000** | Disassemble the objects in `libucrt.lib` 10.0.22000 on the Windows PC (`dumpbin /disasm`) and lift each reached function, **including its FMA3 variant** (use `std::fma` = arm64 `fmadd`, single rounding like `vfmadd`) and tables, into portable C++ with identical operation order | Yes, by construction | Medium–high effort per function. Licence: Microsoft proprietary code in an open repo, a maintainers' decision |
| **A′. Start from AMD LibM** (BSD-3) | The open AMD LibM at the revision Microsoft took, adjusted until it matches UCRT 22000 bit for bit (verified as in A) | Yes, once verified | Download needs user approval. Legally clean if the outputs match. May need patches where Microsoft changed things |
| B. Correctly rounded functions (CORE-MATH, MIT) plus exception tables | For **float univariate** functions, enumerate all 2³² inputs against UCRT and store the inputs where UCRT ≠ CR as a sorted table → exact | Univariate float only | Infeasible for double or bivariate functions (`atan2f`, `powf`): those stay probabilistic. UCRT 22000 differs from 26100 on 0.1–1% of inputs, so at least one of them is not CR on ≥ 0.1% |
| C. Apple libm as is | — | No | Fine for double functions whose results are rounded to float (P ≈ 2⁻²⁹ per call). **Not acceptable** for float functions (whole-ulp differences on some fraction of inputs). Interim only |
| D. Remove the sim's UCRT dependence (upstream) | Route every sim use of these functions through **retail-faithful x87 emulation** (msvcr80 `_CI*` bodies at PC24, the same core as §3): `msvc8::asinf`/`expf`/`logf`/`powf`, the template forwarders, the special-value lanes, and Wild Magic `Math<Real>` if reached | Matches retail better than X does today | Changes X (needs Windows parity reruns), but drops the UCRT pin and the FMA3 CPU dependence. The emulator is shared by all three builds |
| E. Run x86 UCRT code under an x86 emulator | — | Yes | No |

### 4.4 Recommendation

1. **Now (M1):** build `faf_crtmath`, a static library linked into the executable that
   **defines the C entry points**: `asin`, `acos`, `atan`, `atan2`, `atan2f`, `pow`, `powf`,
   `exp`, `log`, `log10`, `logf`, `sin`, `cos`, `tan`, `sinf`, `cosf`, `tanf`, `fmin`/`fmax`
   if needed, plus `rand`/`srand`.
   - **Binding:** ld64 binds every reference from the engine, Wild Magic and libc++ inline
     wrappers to an object-file definition before libSystem. Frameworks keep their own
     two-level bindings.
   - **CI check:** `nm -m` on the binary shows no `(undefined) external _asin (from libSystem)`
     and so on.
   - **First bodies:** Apple libm plus the UCRT special-value semantics (Annex F values, the
     UCRT NaN encodings), plus the tracer (§6.4).
2. **Then:** use the tracer to list the functions and call sites that reach sim state. Clone
   those with A′ (preferred, if the user approves the AMD LibM download) or A. Verify
   (§6.2): exhaustive for float univariate functions, at least 10¹⁰ random plus edge corpora for
   the rest, against the Windows PC (FMA3) and x64-under-Wine (`ROSETTA_ADVERTISE_AVX=1`).
3. **Upstream:** propose Option D, and in any case either `_set_FMA3_enable(0)` or a documented
   "FMA3 CPU required" for x64. arm64 clones whichever variant upstream fixes.

---

## 5. Code generation rules for clang arm64

| # | Rule | Status | Evidence / check |
|---|---|---|---|
| 1 | **No FMA contraction:** `-ffp-contract=off` for the engine, Wild Magic and every library | set (`macos/CMakeLists.txt:43`) | Default Apple clang contracts within a statement. **Check:** `llvm-objdump -d` of all sim objects contains no `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, `fmla` or `fmls`, except the whitelisted `std::fma` users (`X87Math.cpp`, the new x87 core). Measured: `a*b+c` compiles to `fmul` + `fadd` |
| 2 | **No fast-math family:** no `-ffast-math`, `-Ofast`, `-ffinite-math-only`, `-fno-signed-zeros`, `-freciprocal-math`, `-fapprox-func`, `-fassociative-math`; keep `-fno-fast-math` and the default `-ffp-model=precise` | set | No source pragmas found (`FP_CONTRACT`, `float_control`, `fenv_access`: none) |
| 3 | **`-fmath-errno`** (NEW) | **missing: lead request** | **Measured:** libc++ defines `std::sin(float)`, `std::pow(float, float)` and `std::atan2(float, float)` as `__builtin_sinf`/`__builtin_powf`/`__builtin_atan2f`. Darwin defaults to `-fno-math-errno`, so they become LLVM intrinsics that **ignore `-fno-builtin-*`**: `std::sin(0.5f)` is folded to a constant from the compiler's libm, and `std::pow(x, 2.0f)` becomes `fmul s0, s0, s0`, the very rewrite `FafCodegen.cmake` forbids. With `-fmath-errno` both stay calls. Side effect: `sqrt` keeps inline `fsqrt` plus an errno call only for NaN; the results are unchanged |
| 4 | **libm builtins off:** `-fno-builtin-{acos…lgamma}{,f,l}` | set | Plain-name calls (`::sinf(x)`) are protected. Keep the sim's transcendentals behind `msvc8::` or `faf_crtmath` |
| 5 | **Float→int:** keep `-fno-strict-float-cast-overflow` | set | **Measured:** Apple clang 21 lowers casts to `llvm.fptosi.sat`/`fptoui.sat` on **both** `arm64` (`fcvtzs`/`fcvtzu` plus narrow clamps) and `x86_64-pc-windows-msvc` (`cvttss2si` + `ucomiss` + `cmov`). The x64 release binary (clang 22.1.7) has the saturating sequence at 971 of 1,139 `cvtt*2si` sites. Semantics on both: NaN → 0, ±overflow → INT_MAX/INT_MIN, negative → 0 for unsigned, narrow types clamp to their own range. **arm64 ≡ x64.** The `FafCodegen.cmake:31-35` comment ("MSVC's 0x80000000") is stale: retail's 0x80000000 behaviour needs explicit helpers (`msvc8::RoundToInt`, `TruncateToInt`, `RoundToFireClockTicks`). Add a startup self-test: `(int)NaN == 0`, `(int)3e9f == INT_MAX`, `(unsigned)-1.5f == 0`, so a toolchain change is caught |
| 6 | **min/max:** `std::min`/`max`/ternaries compile to `fcmp` + `fcsel` on arm64 (measured) and `minss`/`maxss` on x64 (exact match of `a<b?a:b`): identical. **`std::fmin`/`fmax` are not** | 1 sim site | arm64 `fminnm(+0, −0) = −0` versus x64 `+0` (measured lowering). Replace at `CColPrimitiveBase.cpp:155` with `isnan(a) ? b : (b < a ? b : a)` and `isnan(a) ? b : (b > a ? b : a)`, guarded `FAF_POSIX`, or prove the zero sign unobservable there. Never use `fmin`/`fmax` in new sim code |
| 7 | **NaN:** accept the hardware default-NaN sign difference (§2.3); detect it with the NaN audit | — | Measured: `0*inf`, `inf−inf`, `sqrt(−1)` and `0/0` all give `0x7FC00000` |
| 8 | **FPCR on sim threads:** RMode = 00, FZ = 0, DN = 0 (AH, FIZ and NEP do not exist: FEAT_AFP = 0), trap enables 0 (`FP_SyncExceptions: 1`, so traps could be enabled) | default | **Measured:** FPCR = 0 on the main thread and on new pthreads; a new thread does **not** inherit FZ from its creator. `SimWorkerPool` copies MXCSR control bits through the stand-in; keep that. **Rule:** the `_mm_setcsr` stand-in must never set FPCR.FZ on a sim thread. ARM's FZ also flushes *inputs*, unlike x86 FTZ without DAZ. Assert FPCR == 0 in the POSIX branch of `platform::SetX87PrecisionControl` and at worker start (debug) |
| 9 | **Denormals and underflow:** full IEEE on both. x86 detects tininess after rounding and ARM before, which affects only the UF flag, never values | — | Nothing in the sim reads FP status flags (`CrtRuntimeHelpers.cpp` is not built) |
| 10 | **SIMD:** only lane-exact SSE ops in the stand-in (`add`, `mul`, moves, shuffles). Never add `_mm_rcp_ps`/`_mm_rsqrt_ps` (x86 and ARM estimate tables differ), FMA intrinsics or horizontal ops with a different association order | ok | `include/immintrin.h` |
| 11 | **`long double`** is 64-bit on both; avoid in the sim | ok | measured `sizeof == 8` |
| 12 | **Integer edge cases** (not FP, same class of risk): x86 traps on `/0` and `INT_MIN/−1`; arm64 returns 0 or INT_MIN silently. A path that crashes on Windows keeps running on arm64 | note | Optional diagnostic build with `-fsanitize=integer-divide-by-zero` |
| 13 | **Static-init constants** computed with libm (`Wm3::Math<float>::PI` via `atan(1.0)`): go through `faf_crtmath` and are self-tested | — | |
| 14 | **Headless entry:** call `SetSse2Mode(0)` (from `CScApp::Init`, unless `/sse2`), `SetX87PrecisionControl(_PC_24)` (no-op) and the FPCR check on the sim thread | lead request | `global_mode_sse2` starts at **1** (`MathReflection.cpp:1308`) |

---

## 6. Verification plan

### 6.1 Offline unit tests (the Mac, native; no Windows needed)

1. **Layer 1 versus an exact oracle.** Python `fractions` for add, sub, mul, div and sqrt with
   PC 24/53/64. Inputs: random, plus edge cases (denormal floats, F53 operands, exponent
   extremes, cancellation, exact ties). Optionally SoftFloat 3e (download approval needed).
2. **Layer 2 ≡ Layer 1.** Run all 8 fast-path routines and the double-double helpers on 10⁹
   random inputs plus the difffuzz generators (`GenVec3`, `GenVecPair`, `GenEdge`, near-parallel
   and antiparallel vectors, denormal-range `t`). They must be bit-identical.
3. **Kernels versus the Python `decimal` oracle** (60 digits): sin and cos (with π₆₆
   reduction), tan, atan2, `y·log2(x)` and `2^f − 1`. Assert e = RN₆₄(v) on 10⁷ samples and log
   the near-tie inputs.
4. **Known-answer tests** from the repo's own findings (`X87Math.h`, `RETAIL_REPLAY_PARITY.md`):
   - `asin(0x3F052EAA)` x87 lane → `0x3F0C1118`
   - `pow(0x41AB54C2, 2)` → `0x43E554AF`
   - `math.random(2028178766)` with `rand()%0x7FFF == 279` → `0x4B83C107`
   - `VectorLengthZYX(1e-25)` → `0x15F79688`
   - `ScaleVectorToLength` denormal-range cases
   - MSVC `rand` from seed 1 → 41, 18467, 6334, 26500, 19169
   - `Wm3::Math<float>::PI` constants

### 6.2 Hardware oracle (real x86, on the user's Windows PC; Intel **and** AMD if available)

1. **difffuzz64.** A Windows x64 test executable that links **both** `X87Math64.asm` and the
   portable core from §3.7 (compiled for x64 with the faf codegen flags, symbols renamed) and
   compares them **in process** on real x87 hardware:
   - **exhaustive 2³²** for the unary float routines `sinf`, `cosf`, `tanf`, `round_to_int`,
     `intrinsic_exp/ln/log10`
   - **≥ 10¹⁰ random plus edge cases** for the rest

   Any mismatch is a bug in the emulator, or a characterised hardware rounding case: log it and
   decide.
2. **UCRT twin.** The same executable compares `faf_crtmath` clones with `libucrt.lib`
   10.0.22000 (FMA3 on, and FMA3 forced off to characterise the other variant). Exhaustive for
   float univariate functions.
3. **Portable C++ gives the same bits on x64 and arm64.** The core is pure IEEE plus explicit
   `fma` with contraction off, so this holds by construction. Confirm by running both with the
   same seeds and comparing **block hashes**: a hash of the outputs per 2²⁰ inputs. That avoids
   moving gigabytes of vectors. Drill into blocks that differ.
4. **AMD versus Intel.** Run (1) on both vendors. Any `fsin`/`fpatan` difference between them
   tells us what retail itself tolerates.
5. **What Rosetta can and cannot be used for.** x64 code under Wine and Rosetta is usable as an
   oracle for **SSE/FMA** (UCRT clones), provided `ROSETTA_ADVERTISE_AVX=1`. It is **not**
   usable for x87, because Rosetta's x87 is not faithful (88 retail mismatches). Cross-check its
   UCRT block hashes against the PC once.

### 6.3 Engine level (replays, then live)

1. **Core set** (`docs/PR_REPLAY_CHECK.md`, 8 replays) on the headless arm64 build. Pass means
   no `Checksum for beat N mismatched` line to the end, with the documented final-beat
   exception. Then the **extended set** (10 more), then all 315 matching replays.
   `tools/replay-parity/parity.py` needs a macOS launcher mode (process spawn, log path, no
   window handling): a tools-owner task.
2. **Narrowing.**
   - Run both engines with `sim_ChecksumPeriod 1` and full per-beat state dumps: arm64, and X
     (`main_x64.exe` v1.1.2 under `~/Gaming/bin/wine-run`, already shown to match 27890640 with
     0 mismatches).
   - Diff the first differing beat down to entity, field, function, then add a difffuzz target.
   - **Caveat:** X under Rosetta runs its 27 x87 routines on Rosetta's x87. Where arm64 and
     X-under-Wine disagree but arm64 matches the recorded checksum, X-under-Wine is the one
     that is wrong. Use the Windows PC for disputed beats.
3. **Sim worker variants.** Every replay with `/simworkers 0` and `/simworkers 8`. The SWITCHES
   rule (both must match) holds on arm64 too.
4. **Live cross-architecture game.** A three-seat AI scenario like
   `tools/scenarios/3p-original-x86-x64-ai.yaml` with retail, Windows x64 and native arm64.
   Any per-beat checksum split is a desync. This is the strongest end-to-end evidence.

### 6.4 Instrumentation (do this first: it decides §4)

A diagnostic build flag `FAF_CRTMATH_TRACE` wraps every `faf_crtmath` entry and the
`msvc8::acos/asin/pow` fallback lanes. On the sim thread and workers it counts
`(function, __builtin_return_address(0))` and samples the distinct inputs. Run the core set.

**Output:** the exact list of UCRT functions and call sites that the sim reaches, and how many
distinct inputs each sees. That explains the UCRT 26100 / 26935800 desync and sizes the clone
work. Run the same instrumentation on X (Windows) to confirm the set is the same.

### 6.5 Permanent guards (CI and debug)

- **objdump scan:** no contraction instructions in sim objects (§5 rule 1).
- **nm check:** the libm entry points and `rand`/`srand` resolve inside the executable
  (§4.4, §2.2).
- **Startup self-test (debug and CI):**
  - FPCR == 0 on the sim thread and workers
  - float→int saturation semantics
  - fused-versus-unfused probe: an `a*b+c` whose fused and unfused results differ, compiled
    with the sim flags
  - `rand` sequence
  - x87 constants and the known-answer tests
  - `Wm3` constants
- **`FAF_NAN_AUDIT`:** at each checksum, scan the hashed float fields for NaN and log the
  entity, field and bits (§2.3).
- **Grep or clang-tidy rule:** no direct `std::<transcendental>(float)` and no
  `std::fmin`/`fmax` in sim directories.

---

## 7. Work order

1. **Lead:** add `-fmath-errno`; define `_PC_24`/`_PC_53`/`_PC_64`/`_MCW_PC` for POSIX; make sure
   the headless entry calls `SetSse2Mode(0)`; create the `platform/posix/math` area and assign an
   owner.
2. **core-sys:** the guarded extern edit in `X87Math.cpp`; interim `X87Emu.cpp` (`PcRoundCore`
   fast paths plus double kernels, marked `FAF_POSIX_TODO(determinism)`); `MsvcRand.cpp`;
   `faf_crtmath` on Apple libm plus the tracer.
3. **sim-game:** the `std::fmin`/`fmax` site (`CColPrimitiveBase.cpp:155`). **Lua owner:** UCRT
   spellings for non-finite numbers in `tostring` and `string.format`.
4. Run the core set and the tracer. Triage.
5. Layer 1 core and the kernels, then the Layer 2 promotion tests, then difffuzz64 on the Windows
   PC. Replace the interim implementation.
6. UCRT clones for the traced functions (A′ or A), verified exhaustively.
7. Extended set, then all 315 replays, then the live three-architecture game.
8. Upstream proposals: Option D; `_set_FMA3_enable` policy; the stale comment in
   `FafCodegen.cmake`; `msvc8::asinf` should take the x87 lane like `msvc8::asin`.

### Open questions

- AMD's `fsin`/`fcos`/`fptan` reduction, and whether its 64-bit results equal Intel's. Only
  measurable on AMD hardware.
- Which sim path reached UCRT `asin/acos/atan/atan2/pow` in replay 26935800 (§6.4).
- Whether the user approves downloading AMD LibM (A′), SoftFloat 3e (test oracle) or mpmath.
  None is required to start.
- Licence policy for lifting UCRT code (Option A) if A′ does not match.
