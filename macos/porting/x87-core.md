# x87-core: exact software x87 arithmetic and the 27 msvc8_x87_* routines

Package scope:
- `src/platform/posix/math/X87Core.h` (new): Layers 1 and 2.
- `src/platform/posix/math/PcRoundCore.h`: a verbatim copy of the user's rosettax87 code, with its MIT notice.
- `src/platform/posix/math/X87Emu.cpp` (new): the 27 routines.
- The one-line guard in `src/legacy/math/X87Math.cpp:9`.
- The test project `macos/tests/x87-core/**`, built in `build/x87-core-tests`.

Design: `macos/docs/DETERMINISM.md` §3. Contract: `src/platform/posix/math/X87Types.h`, used as is.

## Status

- `legacy/math/X87Math.cpp` compiles with `macos/tu.sh`. Its only edit is line 9, which is
  now `#if defined(_M_X64) || defined(FAF_POSIX)`.
- `platform/posix/math/X87Emu.cpp` compiles with `macos/tu.sh` (also clean with
  `-Wall -Wextra`). The GLOB in `macos/CMakeLists.txt` picks it up, so no build change is
  needed.
- The object built with the engine command line:
  - defines all 27 `msvc8_x87_*` symbols, with the exact parameter and return types of
    `X87Math.cpp:12-41`;
  - leaves undefined only the six kernels of `X87Types.h` (from `X87Kernels.cpp`, package
    x87-kernels, now in the tree) and `sqrt`. `sqrt` is the errno slow path that
    `-fmath-errno` adds after `fsqrt`, and is reached only for NaN results;
  - contains no `fmadd`/`fmsub`/`fnmadd`/`fnmsub`.
- `X87Core.h` builds for x86_64 and arm64 with no undefined symbols. It uses `__int128`
  only for multiplies and shifts, never for division, so it needs no compiler-rt helper on
  clang-cl.
- No stubs, and no `FAF_POSIX_STUB` or `FAF_POSIX_TODO` in src.

## Layer 1 (`faf_posix::x87`, header-only, pure C++20)

- **Operations.** `Add`, `Sub`, `Mul`, `Div` and `Sqrt` on `Ext` with `Precision::P24/P53/P64`.
  - The exact intermediate is built in `unsigned __int128`:
    - add: 128-bit alignment, with the shifted-out bits jammed into bit 0;
    - mul: 64×64 → 128;
    - div: 128/64 long division with 32-bit digits (Hacker's Delight), the remainder giving
      the round and sticky bits;
    - sqrt: a double estimate, one integer Newton step, then an exact fix-up.
  - The result is rounded once with `core::RoundPack`, a transcription of SoftFloat 3e's
    `softfloat_roundPackToExtF80` for round-to-nearest-even. The extended exponent is kept,
    so PC24 neither overflows at FLT_MAX nor goes denormal below FLT_MIN. Beyond the
    extended range (never reached) the behaviour is SoftFloat's.
- **Loads.** `FromFloat`, `FromDouble` and `FromInt32` are exact, and an SNaN is quieted.
- **Stores.** `ToFloat` and `ToDouble` round once, with denormals and overflow. NaN keeps
  its sign and top payload bits.
- **Integer store.** `ToInt32` behaves as `fistp`: round-to-nearest-even, with NaN, inf or
  out of range giving `0x80000000`.
- **Other instructions.**
  - `Frndint`: round-to-nearest-even; precision control does not apply.
  - `Fscale`: truncates ST1 toward zero. Intel's special table is followed: `0·2^+inf` and
    `inf·2^-inf` are invalid.
  - `Fchs` and `Fabs` touch the sign only.
- **x87 NaN rules** (`core::PropagateNaN`, equal to SoftFloat's 8086 rule):
  - an invalid operation gives `kIndefinite`;
  - an SNaN is quieted, and an SNaN against a QNaN gives the QNaN;
  - between two NaNs the larger significand wins; with equal significands the positive one
    wins.
- **Exported for reuse:** `core::Normalize`, `core::RoundPack(sign, exp, sig, sigExtra, P)`
  and `core::PropagateNaN`.

## Layer 2 (`faf_posix::x87::fast`)

- `Round24` rounds a double to 24 bits on its bit pattern (add and mask at bit 29).
- `Add24`, `Sub24`, `Mul24`, `Div24` and `Sqrt24` are for F24 operands: a double operation
  followed by `Round24`. That is exact because 53 ≥ 2·24 + 2 (Figueroa).
- `Add24Odd` and `Sub24Odd` are for F53 operands. They use `pcround::add24` (TwoSum,
  round-to-odd at 53 bits, then round to 24).
- Each one is proven against Layer 1 below.
- **Double-double extension (one F64 operand): not implemented, a deliberate choice.**
  - Those operations (`atan2_plus`, `atan2_difference`, `sin_times`, `scaled_ln_quotient`,
    `intrinsic_exp`, `pow`) run on Layer 1, which costs 3–12 ns per operation.
  - With the real kernels those routines cost 120–490 ns per call (bench below), so a dd
    path would save at most about 10%.
  - It would also need its own proof (the three-term neighbour argument) before promotion.
  - It can be added later if profiling shows those routines are hot.

## X87Emu.cpp

- **Transcriptions (`emu::ref`).** Every routine is first written instruction by instruction
  against `X87Stack`, a model of the register stack. Each line is commented with the asm
  instruction it models.
  - MASM operand order is kept: `fsub(1, 0)` is ST1 ← ST1 − ST0, and `fdivrp(1)` is
    ST1 ← ST0 / ST1.
  - Memory operands are read and written exactly where the asm touches them, so aliasing
    behaves as on Windows.
  - Reading an empty register gives the indefinite. That models `tanf` for |x| ≥ 2^63:
    `fptan` pushes nothing, then `fstp st(0); fstp m32` stores `0xFFC00000`.
- **Precision.** Routines whose asm does `fnstcw/and 0fffffcffh/fldcw` run at PC24.
  `round_to_int`, `sinf`, `cosf`, `tanf` and `atan2f` run at the thread's 0x027F (PC53) and
  use only PC-independent instructions. `control_word` returns 0x027F.
- **Fast paths (`emu::fast`, what the extern "C" symbols call):**
  - `round_to_int`;
  - `bilinear_height`, `box_radius_on_axis`, `vector_length_zyx`;
  - `scale_vector`, `scale_vector_to_length`, `scale_vector_max_length`,
    `scale_vector_to_length_of`;
  - the arithmetic part of `acos` and `asin`. The fpatan step still goes through the kernel.
- **Range argument.** For finite float inputs every intermediate is 0, an exact infinity, or
  within [2^-447, 2^408], which is inside the double normal range.
- **Fallbacks to the transcription:**
  - any NaN output (invalid operations and NaN inputs always propagate to the output in
    these chains);
  - dest and source that partially overlap;
  - aliased x/y/z in `scale_vector_max_length`;
  - acos/asin with x outside [-1, 1].
- **Transcriptions only:** the other 17 routines, which are built on kernels (`sinf`,
  `cosf`, `tanf`, `atan2f`, `pow`, `atan2_*`, `sin_times*`, `cos_times_cos`, `slerp`,
  `intrinsic_*`, `scaled_ln_quotient`).

## Tests (`macos/tests/x87-core`, built in `build/x87-core-tests`)

Build and run:

```
cmake -S macos/tests/x87-core -B build/x87-core-tests -G Ninja && ninja -C build/x87-core-tests
build/x87-core-tests/x87core_tests [--quick] [suite...]
build/x87-core-tests/x87core_tests_kernels [--quick] [suite...]
```

- `x87core_tests` uses `KernelStandIn.cpp`: **TEST-ONLY** host-libm kernels, not
  x87-exact, so these results depend on nothing but X87Core.h and X87Emu.cpp.
- `x87core_tests_kernels` links the real `X87Kernels.cpp` instead, when it exists.
- SoftFloat is built exactly as `macos/deps.cmake` builds it: 8086 specialization,
  `LITTLEENDIAN`, `SOFTFLOAT_FAST_INT64`, `SOFTFLOAT_ROUND_ODD`,
  `THREAD_LOCAL=_Thread_local`, and `macos/softfloat/platform.h`.
- It is called through a C shim (`SoftFloatShim.c`), with `extF80_roundingPrecision`
  32/64/80 standing for P24/P53/P64.
- Inputs are deterministic: each 2^20 chunk is seeded from its index.

**Full run (27/10/2026): 52 tests, 0 mismatches, 133 s on the M2 Max** (log:
`build/x87-core-tests/full-run.log`).

| Suite | What | Inputs | Mismatches |
|---|---|---|---|
| ops | Layer 1 add/sub/mul/div/sqrt × P24/P53/P64 vs SoftFloat (15 tests). Operands: F24/F53/F64 classes over the whole range, float/double/extended denormals, near-overflow and near-underflow, specials (±0, ±inf, QNaN/SNaN payloads, the indefinite), cancellation, alignment edges (gaps 62–66, 126–130), and exactly constructed ties (add, mul, div; exact squares for sqrt) | 10^8 each | 0 |
| conv | FromDouble, ToFloat, ToDouble, ToInt32 (fistp), Frndint vs SoftFloat; Fscale vs Intel's table plus a SoftFloat power-of-two multiply | 10^8 each | 0 |
| exhaustive | All 2^32 floats through FromFloat, ToFloat round trip, fistp, frndint and sqrt P24 (then store) vs SoftFloat; all 2^32 int32 through FromInt32; Layer 2 `round_to_int` vs its transcription | 2^32 each (7 tests) | 0 |
| helpers | `Round24`, `Add24`/`Sub24`/`Mul24`/`Div24`/`Sqrt24` (F24 operands, exponents −460…420, ties, cancellation) and `Add24Odd`/`Sub24Odd` (F53 operands) vs Layer 1 | 10^8 each | 0 (cases whose result leaves the double normal range are skipped: 0.06–4.4%) |
| routines | The 9 fast routines vs their transcriptions: difffuzz generators (GenFloat, GenVec3, GenVecPair, GenT, edges), denormal and huge lanes, near-degenerate SAT axes, caller-formed `lengthSq`, in-place and partially overlapping dest/source | 10^8 each | 0 |
| routines | Edge corpus: every combination of the 43 edge values (vector length and the four scale routines), 16^6 for bilinear, every box lane against every edge | 20,276,298 | 0 |
| oracle | The transcriptions of bilinear, box radius, vector length and the 4 scale routines vs the same chains written independently in SoftFloat, NaN bits included | 2·10^7 each | 0 |
| kat | Known answers that need no kernel (113 checks): see the list below | 113 | 0 |
| integration (real kernels) | `asin(0x3F052EAA)` → `0x3F0C1118`; `pow(0x41AB54C2, 2)` → `0x43E554AF`; trig with \|x\| ≥ 2^63, inf and NaN; acos/asin fast vs transcription | 12 + 2·(2·10^7) | 0 |

The `kat` checks:
- `VectorLengthZYX(1e-25)` → `0x15F79688` on each lane (float arithmetic gives 0);
- 1e-25 on all three lanes → `0x16566AE2`;
- five `ScaleVectorToLength` cases from `x87_model.py`: four with denormal outputs, where
  rounding to 24 bits and then storing differs from float math, and one whose scale
  overflows float;
- 24 `round_to_int` edges (ties, ±2^31, ±inf, NaN, SNaN, denormal, −0);
- `control_word` = 0x027F;
- `tanf(2^63)` → `0xFFC00000`;
- NaN rules, PC24 keeping the extended range, frndint, fscale and the four constants.

`x87_model.py` is an exact `fractions.Fraction` model of the PC24 chains (Python stdlib,
independent of the C++). It reproduces `0x15F79688` independently. Rerun it with
`python3 -I macos/tests/x87-core/x87_model.py`.

**Mutation check.** I built the tests against deliberately broken copies in the scratchpad:
- Layer 1 ties-away: caught by ops (≈85k per 10^6) and oracle;
- fast `Round24` ties-away: caught by helpers and routines;
- one wrong stack operand in the box-radius transcription: caught by routines and oracle.

So the zero counts above are not a harness artefact.

### Timings (single thread, M2 Max, ns per call)

| Routine | Fast path (shipped) | Transcription |
|---|---|---|
| box_radius_on_axis | **7.6** | 214 |
| bilinear_height | **5.5** | 99 |
| vector_length_zyx | **3.0** | 77 |
| scale_vector | **2.2** | 27 |
| scale_vector_to_length | **3.5** | 87 |
| scale_vector_max_length | **4.6** | 88 |
| scale_vector_to_length_of | **7.1** | 126 |
| round_to_int | **0.9** | 6.8 |

- Layer 1 per operation: Add P24 12 ns, Mul P24 3.0, Div P24 11, Sqrt P24 10, Mul P64 3.8,
  FromFloat + ToFloat 2.4.
- With the real kernels: sinf 121, atan2f 139, sin_times_cos 217, atan2_plus 149,
  intrinsic_exp 147, intrinsic_ln 126, scaled_ln_quotient 151, acos 154, pow 255,
  slerp_spherical_blend 486.

## Open issues and risks

- **FPCR.** The fast paths' final `(float)` store and `round_to_int`'s `frintx` rely on
  FPCR = 0 (FZ = 0, RMode = nearest) on every thread that calls them. That is the same
  assumption as the rest of the sim (DETERMINISM.md §5 rule 8). Layer 1 itself is
  integer-only and does not depend on FPCR.
- **Never reached by the game, not verified on hardware.** These follow SoftFloat:
  - extended-denormal results at PC24/PC53 (rounded at the same bit positions);
  - which NaN wins when two NaNs have equal significands;
  - unnormal and pseudo operands, taken at their numeric value where a 387+ would raise
    invalid. Nothing here produces one.
- **Hardware oracle pending.** The difffuzz64 run on the Windows PC (DETERMINISM.md §6.2)
  still has to happen. X87Core.h and X87Emu.cpp are pure C++ and X87Core.h compiles for
  x86_64. The Windows test would compile them with the `msvc8_x87_*` symbols renamed.
- **Real-kernel known answers.** They pass with today's X87Kernels.cpp. Rerun
  `x87core_tests_kernels kat integration` whenever the kernels change.

## Requests

- **Lead:** nothing blocks the build. Optional:
  - Add `x87core_tests --quick` (about 7 s) and `x87core_tests_kernels --quick kat
    integration` to the pre-link checks.
  - Add `msvc8_x87_vector_length_zyx({1e-25f, 0, 0}) == 0x15F79688` and
    `msvc8_x87_control_word() == 0x027F` to the startup self-test (§6.5), next to the
    FPCR == 0 assert that the fast paths rely on.
- **x87-kernels:**
  - `X87Emu.cpp` calls `Fsin` and `Fcos` with `outOfRange = nullptr`. A false return from
    `Fptan` is taken as "nothing pushed, ST0 unchanged".
  - Your `PropagateNaN2` matches `core::PropagateNaN` (both use SoftFloat's 8086 rule), so
    NaN results compose consistently.
  - `x87core_tests_kernels` in this project exercises your kernels through all 27 routines.
