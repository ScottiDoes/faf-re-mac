#!/usr/bin/env python3
"""Exact model of the x87 PC24 chains, for the known-answer tests (TestKat.cpp).

Independent of the C++ code: Python's fractions.Fraction holds every value exactly; a
PC24 operation rounds the exact result to a 24-bit significand (nearest-even, exponent
unbounded) and `fstp dword` rounds that once more to binary32 with denormals and overflow.
Prints the inputs and expected outputs TestKat.cpp checks. Python 3.9 stdlib only:

    python3 -I macos/tests/x87-core/x87_model.py
"""

import random
import struct
from fractions import Fraction as F


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def bits32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def exact(bits):
    return F(f32(bits))


def floor_log2(a):
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if F(2) ** e > a:
        e -= 1
    return e


def round_half_even(q):
    n = q.numerator // q.denominator
    r = q - n
    if r > F(1, 2) or (r == F(1, 2) and n % 2 == 1):
        n += 1
    return n


def rn(x, p):
    """Round to p significant bits, nearest-even, unbounded exponent (x87 PC24 register)."""
    if x == 0:
        return x
    sign = -1 if x < 0 else 1
    a = abs(x)
    ulp = F(2) ** (floor_log2(a) - p + 1)
    return sign * round_half_even(a / ulp) * ulp


def store32(x):
    """fstp dword: one rounding to binary32 with denormals; returns the bits."""
    if x == 0:
        return 0
    sign = 0x80000000 if x < 0 else 0
    a = abs(x)
    e = floor_log2(a)
    ulp = F(2) ** (-149) if e < -126 else F(2) ** (e - 23)
    v = round_half_even(a / ulp) * ulp
    if v >= F(2) ** 128:
        return sign | 0x7F800000
    return sign | bits32(float(v))


def sqrt_rn(x, p):
    """sqrt rounded to p bits (x > 0): exact integer square root on a scaled value."""
    e = floor_log2(x)
    shift = 2 * (p + 4) - e
    shift += shift % 2
    scaled = x * F(2) ** shift
    n = scaled.numerator // scaled.denominator
    lo, hi = 0, 1
    while hi * hi <= n:
        hi *= 2
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if mid * mid <= n:
            lo = mid
        else:
            hi = mid
    root = F(lo) / F(2) ** (shift // 2)
    inexact = lo * lo != n or scaled.denominator != 1 or scaled.numerator % scaled.denominator != 0
    # lo has >= p + 4 bits, so a strict sticky nudge decides any tie correctly.
    if inexact:
        root += F(1, 2) ** (shift // 2 + 1)
    return rn(root, p)


def vector_length_zyx(x, y, z):
    s = rn(rn(z * z, 24) + rn(y * y, 24), 24)
    s = rn(s + rn(x * x, 24), 24)
    return store32(sqrt_rn(s, 24)) if s != 0 else 0


def scale_vector_to_length(src, length_sq, length):
    scale = rn(length / sqrt_rn(length_sq, 24), 24)
    return [store32(rn(s * scale, 24)) for s in src]


def single_rounding(src, length_sq, length):
    """What float SSE arithmetic would give: sqrtf, divide, multiply, each rounded to float."""
    def f(x):
        return F(f32(store32(x)))
    scale = f(F(length) / f(F(sqrt_rn(length_sq, 60))))
    return [store32(s * scale) for s in src]


def main():
    one_e_25 = bits32(1e-25)
    print('float(1e-25) = %08X' % one_e_25)
    x = exact(one_e_25)
    for lanes in ((x, 0, 0), (0, x, 0), (0, 0, x), (x, x, x)):
        print('vector_length_zyx(%s) = %08X' % (['%08X' % bits32(float(v)) for v in lanes], vector_length_zyx(*lanes)))

    # scale_vector_to_length: find inputs whose outputs land on the denormal grid, where the
    # PC24 rounding then the store differs from float arithmetic; plus the float-overflow case.
    rng = random.Random(2007)
    found = []
    while len(found) < 4:
        src_bits = [bits32(rng.uniform(-1, 1)) for _ in range(3)]
        ls_bits = bits32(rng.uniform(0.5, 2.0))
        len_bits = bits32(rng.uniform(1, 2) * 2.0 ** rng.randint(-140, -127))
        src = [exact(b) for b in src_bits]
        ours = scale_vector_to_length(src, exact(ls_bits), exact(len_bits))
        plain = single_rounding(src, exact(ls_bits), exact(len_bits))
        if ours != plain:
            found.append((src_bits, ls_bits, len_bits, ours, plain))
    for src_bits, ls_bits, len_bits, ours, plain in found:
        print('scale_vector_to_length src %s lenSq %08X len %08X -> %s (float math %s)' % (
            ' '.join('%08X' % b for b in src_bits), ls_bits, len_bits,
            ' '.join('%08X' % b for b in ours), ' '.join('%08X' % b for b in plain)))

    # Float overflow of the scale: lengthSq denormal, length large, source tiny.
    src_bits = [bits32(1e-30), bits32(-2e-30), bits32(3e-30)]
    ls_bits, len_bits = bits32(1e-40), bits32(1e30)
    ours = scale_vector_to_length([exact(b) for b in src_bits], exact(ls_bits), exact(len_bits))
    print('scale_vector_to_length src %s lenSq %08X len %08X -> %s' % (
        ' '.join('%08X' % b for b in src_bits), ls_bits, len_bits, ' '.join('%08X' % b for b in ours)))


if __name__ == '__main__':
    main()
