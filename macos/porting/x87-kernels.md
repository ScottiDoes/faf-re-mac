# x87-kernels: software FSIN, FCOS, FPTAN, FPATAN, FYL2X, F2XM1

Package owner notes, wave 2. Contract: `src/platform/posix/math/X87Types.h` (lead-owned, unchanged).
Design: `macos/docs/DETERMINISM.md` §3.4 and §3.6.

## Files

| File | What |
|---|---|
| `src/platform/posix/math/X87Kernels.cpp` | The six kernels of `X87Types.h`. Pure C++20 plus `unsigned __int128`. No libm and no 128-bit division or 128-bit/float conversion, so no compiler-rt helpers: the object needs only `memcpy`/`bzero` on arm64 and x86_64. The engine build picks it up through the `platform/posix` glob. |
| `src/platform/posix/math/X87KernelsTables.h` | Generated constants (do not edit): Q2.125 tables, polynomial coefficients, Intel's 66-bit pi, and 256-bit slow-path constants. |
| `src/platform/posix/math/X87KernelsTesting.h` | Test hooks. They exist only under `-DFAF_X87_KERNELS_TESTING`; the engine never defines it. |
| `macos/tests/x87-kernels/gen_tables.py` | Writes `X87KernelsTables.h` (Python `decimal` at 120 digits, self-checked: pi against known digits, sin²+cos², the double-angle identity, tan(atan t)). |
| `macos/tests/x87-kernels/oracle.py` | The independent reference: input generators plus expected 80-bit results and a 160-bit "truth" for every vector. |
| `macos/tests/x87-kernels/run_oracle.sh` | Generates the full vector sets (about 10⁶ per kernel) into `build/x87-kernels-tests/full`. |
| `macos/tests/x87-kernels/kernel_test.cpp` | `check` (vectors in 3 modes), `bench`, `kat`. |
| `macos/tests/x87-kernels/internal_test.cpp` | Unit tests of internals (includes the .cpp): division, rounding, and each polynomial's error against its analytic bound. |
| `macos/tests/x87-kernels/build.sh` | Builds into `build/x87-kernels-tests` (`ubsan` and `contract` variants). |

To rerun everything:
```
macos/tests/x87-kernels/run_oracle.sh 1000000 3        # 4.0M vectors, ~70 s wall / 290 CPU-s
macos/tests/x87-kernels/build.sh                       # + "ubsan", "contract"
build/x87-kernels-tests/kernel_test check build/x87-kernels-tests/full 1
build/x87-kernels-tests/kernel_test kat
build/x87-kernels-tests/kernel_test bench
clang++ -std=c++20 -O2 -ffp-contract=off -fwrapv -Isrc/platform/posix/math \
  macos/tests/x87-kernels/internal_test.cpp -o build/x87-kernels-tests/internal_test
```

## Model (what "correct" means here)

Every kernel returns RN64 of the model value, with ties to even. The extended exponent range
applies, denormals sit on the 2⁻¹⁶⁴⁴⁵ grid, and overflow goes to infinity.

- **FSIN/FCOS/FPTAN.** r = x − k·(π₆₆/2), with k the nearest integer to x/(π₆₆/2), computed
  exactly. π₆₆ is Intel's internal pi: its top 68 bits are `C90FDAA2 2168C234 C` (SDM §8.3.10).
  The value is then sin/cos/tan of r per quadrant (k mod 4); FPTAN in odd quadrants gives
  −cot(r). |x| < π₆₆/4 needs no reduction. The floor-octant formulation (reduce by π/4, then use
  π/4 − r in odd octants) gives the same r exactly, so the model does not depend on which one
  the hardware uses.
- **FPATAN.** The exact atan2(y, x) (quadrant offsets π/2 and π are added exactly), then RN64.
- **FYL2X.** y·log2(x) as one operation, rounded once. y is taken as given, for example the
  64-bit `fldln2` constant.
- **F2XM1.** 2^x − 1.

Hardware evidence for the reduction model: Intel's FSIN of double(pi) stores
`1.2246063538223773e-16` (`0x3CA1A60000000000`), versus the true `1.2246467991473532e-16`. Bruce
Dawson published this measurement in 2014; it is quoted from memory. The kernel returns exactly
that value (`kernel_test kat`), because r = −0x234C·2⁻⁶⁶.

### Special operands (my decisions; please review)

| Case | Result |
|---|---|
| Fsin/Fcos ±0 | ±0 / +1 |
| Fsin/Fcos ±inf | indefinite, `*outOfRange = false` (contract) |
| Fptan ±inf | stores the indefinite and **returns true** (the instruction then pushes 1.0). Not verified on hardware. `tanf` stores `0xFFC00000` either way. |
| Fsin/Fcos/Fptan with 2⁶³ ≤ \|x\| < inf | x unchanged; out of range (Fptan returns false and writes x to `*tangent`) |
| \|x\| < 2⁻³³ | sin and tan give x, cos gives 1. RN64 is exact here (proof in the code). A pseudo-denormal x is returned in canonical form (exponent field 1). |
| NaN, one operand | SNaN quieted, QNaN returned unchanged |
| NaN, two operands (FPATAN, FYL2X) | SDM Vol. 1 Table 4-7, which matches SoftFloat's 8086 `propagateNaNExtF80UI`: a QNaN beats an SNaN; two NaNs of the same kind give the larger significand; equal significands give the positive one; the result is quiet. y = ST1 is the first operand. |
| Unsupported encodings (unnormal, pseudo-NaN, pseudo-infinity) | indefinite (387+ #IA) |
| FPATAN zeros and infinities | Intel's table: atan2(±0, +x) = ±0, atan2(±0, −x) = ±π (−0 counts as negative), ±π/2 for x = ±0 with y ≠ 0, ±π/4 and ±3π/4 for inf/inf. Constants are RN64; `kPi` = `…C235`. No invalid case. |
| FYL2X | SDM table: x < 0 (not −0) gives the indefinite; x = ±0 gives ∓inf·sign(y) (divide-by-zero), or the indefinite when y = ±0; x = +inf gives ±inf, or the indefinite when y = ±0; x = 1 gives ±0 with y's sign, or the indefinite when y = ±inf; y = ±0 or ±inf otherwise gives the IEEE sign rules. x = 2ⁿ exactly gives the exact y·n with one RNE rounding (ties occur). |
| F2XM1 ±1 | exactly 1 / −0.5 |
| F2XM1 \|x\| > 1 | hardware-undefined; the game never goes there. The kernel returns the true 2^x − 1, with −1 for x ≤ −128 and for −inf, and +inf for x ≥ 16384 and for +inf. |

## Implementation

Ziv's strategy: a fast path with a rigorous error bound, and a slow path when the bound
straddles a rounding boundary.

- **Fast path.** Signed Q2.125 fixed point (a 128-bit integer v stands for v·2⁻¹²⁵; "q" =
  2⁻¹²⁵), plus an `F128` floating type (128-bit significand) that carries an error bound in
  units of its last bit through each product and quotient. Each function uses table reduction:
  - sin/cos: `j/64`, 51 entries
  - atan: `j/64`, 65 entries, with u = (t − j/64)/(1 + t·j/64) formed from exact integers, so
    only one division is needed
  - log2: 8-bit index with short exact reciprocals; the entries next to 1 use inv = 1 or 1/2,
    so m − 1 is exact and the result keeps full relative accuracy near x = 1
  - exp2: `2^(j/64)`, 129 entries

  Polynomials use Estrin's scheme in Q2.125. Their low-order tail runs in double: the error
  analysis shows each tail contributes ≤ 0.35 q. The 128-bit division is `DivQ`: a double
  reciprocal, one exact integer Newton step, then an exact remainder correction. It uses
  multiplications only and errs by at most 1 unit (unit-tested against an exact Knuth
  division on 10⁷ cases).
- **Rounding test.** `RoundF128` (or the general `RoundToExt`) decides RN64 only when the error
  interval contains no midpoint. Undecided cases go to the slow path.
- **Slow path.** 256-bit floating arithmetic (`Big`, every operation truncating) with plain
  Taylor and atanh series and a few 256-bit constants. Its error is below 2⁻²⁴⁵ relative, and
  it is checked against a threshold of 2⁻²³¹. A case it cannot decide would be a true near-tie
  within 2⁻²³¹; it would be rounded as if exact and counted (testing builds). None occurred.

### Error bounds (absolute, in q = 2⁻¹²⁵; the stated bound is the one the code uses)

| Piece | Analysis | Stated | Measured max (unit test) |
|---|---|---|---|
| `SinPoly` sin(d)/d, \|d\| ≤ 2⁻⁷ | 2.6 | 6 | 1.25 |
| `CosPoly` | 2.1 | 6 | 1.03 |
| `AtanPoly` | 2.6 | 6 | 1.18 |
| `LogPoly` ln(1+t)/t, \|t\| ≤ 2⁻⁸ | 3.1 | 8 | 2.33 |
| `ExpPoly` expm1(g)/g | 3.3 | 8 | 2.65 (with exact g) |
| sin/cos combination, j ≥ 1 | 5.7 | 16 | — |
| atan theta, fixed point | 3.6 | 16 | — |
| log2 F, and I + F | 3 | 8 | — |
| 2^x − 1, j ≠ 0 | 5.7 | 32 | — |

End to end on the 6.0M kernel evaluations in fast-only mode, the measured error of the 128-bit
approximation against the oracle's 160-bit truth never exceeded the claimed bound. The largest
measured/claimed ratios were 0.354 (f2xm1), 0.313 (fyl2x), 0.258 (fcos), 0.257 (fsin),
0.202 (fpatan) and 0.18 (fptan).

## Tests and results

### Oracle

`oracle.py` uses Python 3.9 stdlib `decimal` at 72 digits of working precision (an error of
10⁻⁶⁴ relative is assumed) and exact integer arithmetic for the reduction and the rounding. A
Ziv retry at 160 digits never triggered. Its algorithms differ from the C++ ones: atan by
Euler's series after one halving, logs by `decimal.ln`, exp by `decimal.exp`, and sin/cos by
the decimal-docs recipes.

The vector sets, about 10⁶ per kernel (4,012,146 in all):

- **trig, 1,000,468 inputs, each checked by FSIN, FCOS and FPTAN:**
  - random floats with exponents −34..30
  - floats in ±8π
  - floats within 3 ulps of k·π₆₆/2, k up to 2⁴⁰
  - floats near the quadrant boundaries (k + ½)·π₆₆/2
  - random doubles and random 64-bit Ext values (exponents up to 62)
  - 64-bit Ext values within 4 ulps of k·π₆₆/2 for k up to 2⁶¹·⁵ (deep cancellation)
  - the tiny threshold 2⁻³³ ± ulps
  - the 2⁶³ limit
  - all special encodings
- **atan, 1,009,600 pairs:**
  - random float pairs
  - floats in ±1000
  - the acos and asin shapes (double x, float sqrt((1+x)(1−x)), both orders)
  - ratios at and around the table points j/64 and the boundaries (2j+1)/128
  - random Ext pairs
  - exponent gaps of 20 to 2000
  - the cross product of 38 special operands
- **yl2x, 1,001,764 pairs:**
  - y = `fldln2` or `fldlg2` with every kind of positive float, including denormals and
    floats within 2¹⁶ ulps of 1
  - the pow shape: random doubles for y and x, and doubles within 2²⁰ ulps of 1
  - random Ext values with x across the whole exponent range
  - exact powers of two with random y (ties)
  - x at the log-table boundaries
  - 64-bit x within 2⁴⁰ ulps of 1
  - the special cross product
- **f2xm1, 1,000,314 inputs:**
  - floats in [−0.5, 0.5] and [−1, 1]
  - log-uniform small floats
  - random Ext values in [−1, 1]
  - values at the boundaries (2j+1)/128
  - values near ±1
  - tiny and denormal values
  - out-of-domain values and specials

### `kernel_test check full` (arm64, engine codegen flags, `-O3 -ffp-contract=off`)

| Mode | fsin | fcos | fptan | fpatan | fyl2x | f2xm1 |
|---|---|---|---|---|---|---|
| normal (production) | 0 mismatches | 0 | 0 | 0 | 0 | 0 |
| slow path alone (ForceSlow, every vector) | 0 | 0 | 0 | 0 | 0 | 0 |
| fast path alone (FastOnly) | 0 | 2 (near-ties, see below) | 0 | 0 | 0 | 0 |
| fast-path bound violations | 0 | 0 | 0 | 0 | 0 | 0 |

Slow-path near-ties (beyond 2⁻²³¹): **0** in the whole corpus. The fast path left 22 inputs
undecided, and the slow path decided all of them correctly. All are structured near-ties:

- **fcos at ±2⁻³²·(1 − 2⁻⁶⁴), ±2⁻³², ±2⁻³²·(1 + 2⁻⁶³).** cos ≈ 1 − 2⁻⁶⁵ − x⁴/24 lies
  2⁻¹³¹ from a midpoint. Fast-only mode misrounds the last two of these, as expected.
- **fyl2x(y = `fldln2` `3FFE B17217F7D1CF79AC`, x = `4000 FFFFFFFFFFFFFFFF`).** The result is
  2·ln2₆₄ minus 2⁻⁶⁴·ln2₆₄/ln2, which lies within about 2⁻⁶⁴ ulp of a midpoint; the answer is
  `3FFF B17217F7D1CF79AB`. The generator emitted this vector 16 times (a clamp in its near-1
  category).

### Other runs

- **UBSan + ASan build.** A 401,216-vector sample (every 10th vector), all 3 modes: 0
  mismatches, no sanitizer report.
- **`-ffp-contract=fast` build.** Full corpus: identical results. The double tails' error
  bounds cover FMA contraction, and the final rounding is decided.
- **x86_64 build run under Rosetta.** Full corpus: 0 mismatches, and the fast-path error
  statistics are identical to arm64, which means bit-identical intermediate values. This is
  a first check of DETERMINISM.md §6.2 item 3 (same bits on x64 and arm64). clang-cl/MSVC
  was not tried; there is no Windows toolchain here.
- **`internal_test 2000000`.**
  - DivQ: 10⁷ cases plus edges, always floor(Q) or floor(Q) − 1.
  - RoundToExt against an exact reference: 2·10⁶ cases, including ties, near-ties and denormal
    outputs.
  - Polynomial errors as in the table above.
- **`kernel_test kat`, all pass:**
  - Intel's fsin(double π) hardware value, as the stored double and as the Ext encoding
    (`3FCA 8D30000000000000`)
  - `msvc8_x87_asin(0x3F052EAA)` = `0x3F0C1118` and `msvc8_x87_pow(0x41AB54C2, 2)` =
    `0x43E554AF`: the retail findings of DETERMINISM.md §6.1, run through the routine's PC24
    steps around the kernels
  - fpatan(1, 1) = RN64(π/4); fyl2x(ln2, 2) = ln2; fyl2x(lg2, 1) = +0; f2xm1(−1) = −0.5
  - the out-of-range behaviour of fsin and fptan
  - fcos(inf) = indefinite
- `X87Kernels.cpp` compiles with `macos/tu.sh` under the engine flags with `-Wall -Wextra`: no
  warnings. None of the ~6,800 macros of the force-included `WinCompat.h` collides with its
  identifiers.

### Performance

M2 Max, thread CPU time, load average about 3, `kernel_test bench`, independent calls on
game-like inputs:

| Kernel | Input | ns/call |
|---|---|---|
| fsin / fcos | floats in ±4π | 55–62 / 54–57 |
| fptan | floats in ±4π | 74 |
| fpatan | float pairs; asin shape | 77–79 |
| fyl2x | `fldln2`·log2(float); pow shape | 49–50; 53–54 |
| f2xm1 | floats in ±0.5 | 39–41 |
| slow path (any kernel) | — | 3.1–5.4 µs |

The slow path runs with probability about 2⁻⁵⁰ per call on generic inputs. Getting here took
two main steps: Estrin's scheme instead of Horner, and a multiply-only division (38 → 11 ns);
before them the fast path was 110–150 ns. Fsin/Fcos also compute only the polynomial that the
quadrant needs.

## Open issues

1. **Hardware faithfulness is not proven by these tests.** They prove e = RN64(model). The
   hardware is only faithful (< 1 ulp64), so it may pick the other 64-bit neighbour; §3.6
   bounds the effect after the float store at about 2·10⁻¹². Only the fsin(π) hardware value
   is confirmed. DETERMINISM.md §6.2 (difffuzz64 on the user's Intel and AMD PCs) is the real
   check: `X87Kernels.cpp` builds there unchanged, and the hooks allow an in-process
   comparison.
2. AMD's FSIN/FCOS/FPTAN reduction is unknown (§3.4).
3. The FPATAN model adds the quadrant offsets π/2 and π exactly. If the hardware used a 66-bit
   π there too, results near ±π/2 and ±π could differ in rare rounding cases. Only hardware
   can tell.
4. Behaviour that no reference can check here: `Fptan(±inf)` (whether 1.0 is pushed, which is
   irrelevant for `tanf`), the NaN tie with equal significands, and F2XM1 outside [−1, 1].
5. The slow path costs 3–5 µs. Hitting it on purpose needs structured near-ties like the ones
   listed above; the sim's inputs come from F24/F53 operands.

## Requests

- **Lead (optional, documentation only).** Append to the `Fptan` comment in `X87Types.h`:
  "±inf: stores the indefinite and returns true (masked #IA; the instruction then pushes 1.0).
  NaN: stores the quieted NaN and returns true." Add to the kernel section: "Unsupported
  encodings (unnormals, pseudo-NaN/infinity) give the indefinite."
- **No build changes needed.** `X87Kernels.cpp` is picked up by the `platform/posix` glob, and
  the tests build in `build/x87-kernels-tests` with their own script.
