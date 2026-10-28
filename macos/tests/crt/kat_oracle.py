#!/usr/bin/env python3 -I
"""Exact oracle for the crt self-test's known answers (src/platform/posix/crt/SelfTest.cpp).

Python stdlib only (fractions, decimal): every value below is derived from exact
arithmetic, never from a libm.

  python3 -I macos/tests/crt/kat_oracle.py          # print every known answer
  python3 -I macos/tests/crt/kat_oracle.py --check  # exit 1 if a value differs from the
                                                    # table copied into SelfTest.cpp

x87 model (macos/docs/DETERMINISM.md §3.1): under precision control 24 an FADD/FSUB/FMUL/
FDIV/FSQRT rounds the exact result once to a 24-bit significand, nearest-even, with the
extended exponent range (no denormals, no overflow at FLT_MAX). `fstp dword` then rounds
once more to binary32 with denormals (grid 2^-149) and overflow. The routines modelled are
transcribed from src/legacy/math/X87Math64.asm.
"""
import math
import struct
import sys
from decimal import Decimal, getcontext
from fractions import Fraction as F

getcontext().prec = 90


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f32_from_bits(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def f64_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def f64_from_bits(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def floor_log2(x):
    """floor(log2(x)) for a positive Fraction, exactly."""
    x = F(x)
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if F(2) ** e > x:
        e -= 1
    while F(2) ** (e + 1) <= x:
        e += 1
    return e


def round_quantum(x, q):
    """x rounded to a multiple of 2^q, nearest-even."""
    scaled = F(x) / F(2) ** q
    n = scaled.numerator // scaled.denominator
    r = scaled - n
    if r > F(1, 2) or (r == F(1, 2) and n % 2 == 1):
        n += 1
    return n * F(2) ** q


def rn(x, bits, emin=None):
    """Round to `bits` significant bits; with `emin`, the quantum is at least 2^emin."""
    x = F(x)
    if x == 0:
        return F(0)
    sign = -1 if x < 0 else 1
    q = floor_log2(abs(x)) - (bits - 1)
    if emin is not None and q < emin:
        q = emin
    return sign * round_quantum(abs(x), q)


INF = float("inf")


def store32(x):
    """`fstp dword`: binary32 with denormals and overflow (to +-inf at RN)."""
    r = rn(x, 24, emin=-149)
    if abs(r) >= F(2) ** 128 - F(2) ** 103:  # rounds past FLT_MAX
        return math.copysign(INF, r)
    return float(r)


def pc24(x):
    """A PC24 arithmetic result: 24-bit significand, extended exponent."""
    return rn(x, 24)


def pc24_sqrt(x):
    """FSQRT under PC24 of a non-negative exact value."""
    x = F(x)
    if x == 0:
        return F(0)
    e = floor_log2(x) // 2
    q = e - 23
    y = x / F(4) ** q  # sqrt(y) = sqrt(x) / 2^q, in [2^23, 2^25)
    n0 = math.isqrt(y.numerator // y.denominator)
    if n0 >= 2 ** 24:  # one bit too many: requantize
        q += 1
        y = x / F(4) ** q
        n0 = math.isqrt(y.numerator // y.denominator)
    half = F(2 * n0 + 1, 2)
    if y > half * half or (y == half * half and n0 % 2 == 1):
        n0 += 1
    return n0 * F(2) ** q


def F32(b):
    return F(f32_from_bits(b))


# --- x87 routines (X87Math64.asm) --------------------------------------------------------

def scale_vector(src, scale):
    """msvc8_x87_scale_vector: dest[i] = store32(PC24(src[i] * scale))."""
    return [store32(pc24(F(s) * F(scale))) for s in src]


def scale_vector_to_length(src, length_sq, length):
    """msvc8_x87_scale_vector_to_length: fsqrt(lengthSq); fdivr length; 3 x fmul; store."""
    root = pc24_sqrt(F(length_sq))
    k = pc24(F(length) / root)
    return [store32(pc24(F(s) * k)) for s in src]


def vector_length_zyx(lanes):
    """msvc8_x87_vector_length_zyx: sqrt((z*z + y*y) + x*x), every step PC24."""
    x, y, z = (F(v) for v in lanes)
    acc = pc24(pc24(z * z) + pc24(y * y))
    acc = pc24(acc + pc24(x * x))
    return store32(pc24_sqrt(acc))


def multiply_integer(integer, factor):
    """msvc8::MultiplyIntegerX87: `fild; fmul` under PC24, stored to float."""
    return store32(pc24(F(integer) * F(factor)))


# --- searches for double-rounding cases ----------------------------------------------------

def find_scale_vector_double_rounding():
    """A float pair whose PC24 product, stored, differs from the single-rounded float product
    (denormal range: 24-bit rounding, then the 2^-149 grid)."""
    seed = 0x5EED2007
    state = seed
    for _ in range(2_000_000):
        state = (state * 6364136223846793005 + 1442695040888963407) & (2 ** 64 - 1)
        ma = (state >> 40) | 0x800000
        state = (state * 6364136223846793005 + 1442695040888963407) & (2 ** 64 - 1)
        mb = (state >> 40) | 0x800000
        a = F(ma, 2 ** 23) * F(2) ** -70   # ~1e-21
        b = F(mb, 2 ** 23) * F(2) ** -70   # product ~2^-140: 9 bits on the denormal grid
        exact = a * b
        twice = store32(pc24(exact))
        once = store32(exact)
        if twice != once:
            return float(a), float(b), twice, once
    return None


def find_scale_vector_to_length_double_rounding():
    state = 0xC0FFEE
    for _ in range(2_000_000):
        state = (state * 6364136223846793005 + 1442695040888963407) & (2 ** 64 - 1)
        ms = (state >> 40) | 0x800000
        src = F(ms, 2 ** 23) * F(2) ** -60
        length_sq = F(1)   # sqrt = 1 exactly, so k = PC24(length)
        state = (state * 6364136223846793005 + 1442695040888963407) & (2 ** 64 - 1)
        ml = (state >> 40) | 0x800000
        length = F(ml, 2 ** 23) * F(2) ** -80
        pc = scale_vector_to_length([float(src), 0.0, 0.0], float(length_sq), float(length))[0]
        once = store32(src * length)
        if pc != once:
            return float(src), float(length_sq), float(length), pc, once
    return None


# --- Wm3::Math constants (dependencies/WildMagic3p8/Foundation/Math/Wm3Math.cpp) ----------

def f32_op(x):
    """One binary32 arithmetic result (normal range here)."""
    return float(rn(F(x), 24, emin=-149))


def wm3_constants():
    pi_d = math.pi  # 4.0*atan(1.0): atan(1.0) = RN53(pi/4), exact times 4
    assert f64_bits(pi_d / 4) == 0x3FE921FB54442D18
    pi_f = f32_op(F(pi_d))
    two_pi_f = f32_op(2 * F(pi_f))
    half_pi_f = f32_op(F(1, 2) * F(pi_f))
    out = {
        "float PI": f32_bits(pi_f),
        "float TWO_PI": f32_bits(two_pi_f),
        "float HALF_PI": f32_bits(half_pi_f),
        "float INV_PI": f32_bits(f32_op(1 / F(pi_f))),
        "float INV_TWO_PI": f32_bits(f32_op(1 / F(two_pi_f))),
        "float DEG_TO_RAD": f32_bits(f32_op(F(pi_f) / 180)),
        "float RAD_TO_DEG": f32_bits(f32_op(180 / F(pi_f))),
        "double PI": f64_bits(pi_d),
        "double TWO_PI": f64_bits(2.0 * pi_d),
        "double HALF_PI": f64_bits(0.5 * pi_d),
        "double INV_PI": f64_bits(1.0 / pi_d),
        "double INV_TWO_PI": f64_bits(1.0 / (2.0 * pi_d)),
        "double DEG_TO_RAD": f64_bits(pi_d / 180.0),
        "double RAD_TO_DEG": f64_bits(180.0 / pi_d),
    }
    return out


# --- (float)sin/cos of the header quaternion constants' half angles ------------------------

def dsin(x):
    x = Decimal(x)
    term, total, n = x, x, 1
    while True:
        term = -term * x * x / ((2 * n) * (2 * n + 1))
        n += 1
        if abs(term) < Decimal(10) ** -85:
            return total
        total += term


def dcos(x):
    x = Decimal(x)
    term, total, n = Decimal(1), Decimal(1), 1
    while True:
        term = -term * x * x / ((2 * n - 1) * (2 * n))
        n += 1
        if abs(term) < Decimal(10) ** -85:
            return total
        total += term


def float_with_margin(value):
    """RN binary32 of a Decimal, and its distance from the nearest rounding tie in float ulps."""
    fr = F(value)
    q = floor_log2(abs(fr)) - 23
    scaled = abs(fr) / F(2) ** q
    n = scaled.numerator // scaled.denominator
    rem = scaled - n
    margin = abs(rem - F(1, 2))
    return f32_bits(float(rn(fr, 24))), float(margin)


def trig_constants():
    rows = []
    for bits in (0x3FE921FB60000000, 0xBFE921FB60000000, 0x3FD921FB60000000, 0xBFD921FB60000000):
        x = f64_from_bits(bits)
        s, sm = float_with_margin(dsin(x))
        c, cm = float_with_margin(dcos(x))
        rows.append((bits, s, sm, c, cm))
    return rows


def msvc_rand(seed, count):
    out = []
    s = seed & 0xFFFFFFFF
    for _ in range(count):
        s = (s * 214013 + 2531011) & 0xFFFFFFFF
        out.append((s >> 16) & 0x7FFF)
    return out


# The values SelfTest.cpp carries (kept in sync by --check).
EXPECTED = {
    "rand(1)": [41, 18467, 6334, 26500, 19169],
    "rand(12345)": [7584, 19164, 25795, 22125, 5828],
    "multiply_integer": 0x4B83C107,
    "vector_length_zyx": 0x15F79688,
    "scale_vector": (0x1C9CCD65, 0x1CE928EA, 0x00000476, 0x00000477),
    "scale_vector_to_length": (0x21E2F5E4, 0x3F800000, 0x17D20296, 0x000005D2, 0x000005D1),
    "scale_vector_to_length_overflow": (0x00000001, 0x7149F2CA, 0x0DA24260, 0x64B504F4),
    "wm3": {
        "float PI": 0x40490FDB, "float TWO_PI": 0x40C90FDB, "float HALF_PI": 0x3FC90FDB,
        "float INV_PI": 0x3EA2F983, "float INV_TWO_PI": 0x3E22F983,
        "float DEG_TO_RAD": 0x3C8EFA35, "float RAD_TO_DEG": 0x42652EE0,
        "double PI": 0x400921FB54442D18, "double TWO_PI": 0x401921FB54442D18,
        "double HALF_PI": 0x3FF921FB54442D18, "double INV_PI": 0x3FD45F306DC9C883,
        "double INV_TWO_PI": 0x3FC45F306DC9C883, "double DEG_TO_RAD": 0x3F91DF46A2529D39,
        "double RAD_TO_DEG": 0x404CA5DC1A63C1F8,
    },
    "trig": [
        (0x3FE921FB60000000, 0x3F3504F3, 0x3F3504F3),
        (0xBFE921FB60000000, 0xBF3504F3, 0x3F3504F3),
        (0x3FD921FB60000000, 0x3EC3EF16, 0x3F6C835E),
        (0xBFD921FB60000000, 0xBEC3EF16, 0x3F6C835E),
    ],
}


def compute():
    got = {}
    got["rand(1)"] = msvc_rand(1, 5)
    got["rand(12345)"] = msvc_rand(12345, 5)
    sample = f32_op(F(279) * F32(0x38000100))
    assert f32_bits(sample) == 0x3C0B8117
    got["multiply_integer"] = f32_bits(multiply_integer(2028178766, sample))
    got["vector_length_zyx"] = f32_bits(vector_length_zyx([f32_from_bits(f32_bits(1e-25)), 0.0, 0.0]))

    a, b, twice, once = find_scale_vector_double_rounding()
    got["scale_vector"] = (f32_bits(a), f32_bits(b), f32_bits(twice), f32_bits(once))

    src, length_sq, length, pc, once = find_scale_vector_to_length_double_rounding()
    got["scale_vector_to_length"] = (f32_bits(src), f32_bits(length_sq), f32_bits(length), f32_bits(pc),
                                     f32_bits(once))

    # Extended exponent: length / sqrt(lengthSq) exceeds FLT_MAX on the x87 stack and comes
    # back into range in the product; float arithmetic gives +inf.
    length_sq = f32_from_bits(0x00000001)   # 2^-149: sqrt = 2^-74.5
    length = f32_from_bits(0x7149F2CA)      # 1e30
    src = f32_from_bits(0x0DA24260)         # 1e-30
    pc = scale_vector_to_length([src, 0.0, 0.0], length_sq, length)[0]
    got["scale_vector_to_length_overflow"] = (0x00000001, 0x7149F2CA, 0x0DA24260, f32_bits(pc))
    assert f32_bits(1e30) == 0x7149F2CA and f32_bits(1e-30) == 0x0DA24260

    got["wm3"] = wm3_constants()
    got["trig"] = [(b, s, c) for (b, s, sm, c, cm) in trig_constants()]
    return got


def main():
    got = compute()
    for key, value in got.items():
        if isinstance(value, dict):
            print(key)
            for k, v in value.items():
                print("  %-18s 0x%X" % (k, v))
        elif isinstance(value, (list, tuple)) and value and isinstance(value[0], tuple):
            print(key)
            for row in value:
                print("  " + " ".join("0x%X" % v for v in row))
        elif isinstance(value, (list, tuple)):
            print("%-32s %s" % (key, " ".join(("0x%08X" % v) if key.startswith("scale") else str(v)
                                               for v in value)))
        else:
            print("%-32s 0x%08X" % (key, value))
    print("trig margins (float ulps from a tie; > 2^-29 means every faithful double libm agrees):")
    for bits, s, sm, c, cm in trig_constants():
        print("  0x%016X sin %.4f cos %.4f" % (bits, sm, cm))
    if "--check" in sys.argv:
        bad = [k for k in EXPECTED if EXPECTED[k] != got[k]]
        if bad:
            for k in bad:
                print("MISMATCH %s: SelfTest has %r, oracle gives %r" % (k, EXPECTED[k], got[k]))
            return 1
        print("all %d known-answer groups match SelfTest.cpp's table" % len(EXPECTED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
