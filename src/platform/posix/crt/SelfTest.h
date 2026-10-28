#pragma once

// Start-up self-test of what the native build's bit-identical simulation assumes about the
// toolchain, the CPU state and the C runtime (macos/docs/DETERMINISM.md §6.5). The headless
// runner calls it before it starts the sim, so a toolchain or library change that would
// desync shows up as a failed check instead of a mismatched checksum hours later.
//
// Checks: FPCR == 0 on the calling thread and on a new thread (and the _mm_setcsr stand-in
// never sets FPCR.FZ); saturating float->int casts; no FMA contraction in code built with the
// sim flags; MSVC rand (41 18467 6334 26500 19169 on a new thread); faf_crtmath bound to its
// own definitions with the UCRT's NaN results; atan(1.0); Wm3::Math<float/double> constants;
// the sin/cos values behind the header quaternion constants; the msvc8_x87_* and
// msvc8::MultiplyIntegerX87 known answers (x87 precision control 24, extended exponent,
// double rounding at the store).
//
// Side effects: none on the calling thread (the rand and MXCSR checks run on a temporary
// thread). Every failure is printed to stderr; with `verbose` every check is.
// Returns true when every check passes.
bool faf_posixRunSelfTests(bool verbose);
