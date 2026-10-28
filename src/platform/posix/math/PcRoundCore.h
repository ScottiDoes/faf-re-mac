// Everything after this notice is a verbatim copy of rosettax87's
// probe-src/rosetta_runtime/src/PcRoundCore.h, the user's MIT-licensed code; it is the exact
// PC24 core that played FAF replay 27890640 with 0 checksum mismatches under rosettax87
// `dist-pc` (macos/docs/DETERMINISM.md §3.6). Used by X87Core.h's Layer 2 for operations
// with a double (F53) operand. Do not edit: changes go to rosettax87 first.
//
// MIT License (rosettax87 probe-src/LICENSE)
//
// Copyright (c) 2025 Lifeisawful
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// x87 precision control (CW bits 8-9) for the double-based register file.
//
// Under PC=24 a real x87 rounds the exact result of FADD/FSUB/FMUL/FDIV/FSQRT (and the FI*
// forms) once to a 24-bit significand, in the current rounding control, while the register
// keeps its wide exponent. Here the double op's exact error (TwoSum / FMA residual) is folded
// into the last bit first (round-to-odd), so the following rounding to 24 bits is a single
// correct rounding of the exact result even when the operands are wider than 24 bits.
// The exponent stays at double range, which covers everything outside overflow/underflow.

#include <bit>
#include <cstdint>

#pragma clang fp contract(off)

namespace pcround {

inline uint64_t toBits(double d) { return std::bit_cast<uint64_t>(d); }

inline bool isFinite(double d) {
    return (toBits(d) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

inline int sign(double d) { return (d > 0.0) - (d < 0.0); }

// r = RN(exact), errSign = sign(exact - r).
inline uint64_t finish24(double r, int errSign, uint32_t cw) {
    uint64_t b = toBits(r);
    if (!isFinite(r)) return b;
    if (errSign != 0 && r != 0.0 && !(b & 1)) {
        // Round-to-odd: step to the neighbour on the exact value's side (it has an odd last bit).
        bool magnitudeUp = (errSign > 0) == !(b >> 63);
        b = magnitudeUp ? b + 1 : b - 1;
    }
    constexpr uint64_t kLow = (1ULL << 29) - 1, kHalf = 1ULL << 28, kUlp24 = 1ULL << 29;
    uint64_t low = b & kLow, base = b & ~kLow;
    if (!low) return base;
    bool negative = b >> 63;
    switch (cw & 0x0C00) {
        case 0x0000:  // nearest, ties to even
            if (low > kHalf || (low == kHalf && (base & kUlp24))) base += kUlp24;
            break;
        case 0x0400:  // down (toward -inf)
            if (negative) base += kUlp24;
            break;
        case 0x0800:  // up (toward +inf)
            if (!negative) base += kUlp24;
            break;
        default:  // toward zero
            break;
    }
    return base;  // a carry out of the significand bumps the exponent, as it should
}

inline uint64_t add24(double a, double b, uint32_t cw) {
    double s = a + b;
    if (!isFinite(s)) return toBits(s);
    double bb = s - a;
    double err = (a - (s - bb)) + (b - bb);  // TwoSum: exact
    return finish24(s, sign(err), cw);
}

inline uint64_t mul24(double a, double b, uint32_t cw) {
    double p = a * b;
    if (!isFinite(p)) return toBits(p);
    return finish24(p, sign(__builtin_fma(a, b, -p)), cw);
}

inline uint64_t div24(double a, double b, uint32_t cw) {
    double q = a / b;
    if (!isFinite(q) || q == 0.0) return toBits(q);
    double rem = __builtin_fma(-q, b, a);  // a - q*b, exact for q = RN(a/b)
    return finish24(q, sign(rem) * sign(b), cw);
}

inline uint64_t sqrt24(double a, uint32_t cw) {
    double r = __builtin_sqrt(a);
    if (!isFinite(r) || r == 0.0) return toBits(r);
    return finish24(r, sign(__builtin_fma(-r, r, a)), cw);
}

}  // namespace pcround
