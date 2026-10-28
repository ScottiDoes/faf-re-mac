#!/usr/bin/env python3
# Reference vectors for the x87 transcendental kernels (src/platform/posix/math/X87Kernels.cpp).
# Python 3 stdlib only: decimal (>= 64 digits) for the function values, exact integer
# arithmetic for argument reduction and rounding. Written from the hardware model in
# macos/docs/DETERMINISM.md §3.4 and the Intel SDM special-value tables, independently of the
# C++ (different algorithms: atan by Euler's series, logs by decimal ln, exp by decimal exp).
#
#   python3 -I oracle.py <kernel> <count-scale> <out-file> [workers]
#   kernel: trig | atan | yl2x | f2xm1     (trig = FSIN, FCOS and FPTAN of the same inputs)
#
# Output lines (Ext = 20 hex digits: 4 of sign/exponent, 16 of significand):
#   trig : x sin cos tan oor truthSin truthCos truthTan
#   atan : y x res truth           yl2x : y x res truth           f2xm1 : x res truth
# truth = "s:E:T" (sign, exponent, 160-bit significand T as 40 hex digits; true value
# ~ (-1)^s * T * 2^(E-159)) when the result is an approximated transcendental value, else "-".

import multiprocessing
import os
import random
import struct
import sys
from decimal import Decimal, getcontext, localcontext

P = 72               # working precision (digits)
EPS_DIGITS = P - 8   # assumed relative accuracy of every computed value: 10^-EPS_DIGITS
getcontext().prec = P
getcontext().Emax = 999999
getcontext().Emin = -999999

BIAS = 16383
INDEFINITE = (0xFFFF, 0xC000000000000000)
ONE = (0x3FFF, 1 << 63)

# Intel's internal pi: top 68 bits C90FDAA2 2168C234 C (SDM Vol. 1 §8.3.10).
PI66_TOP68 = 0xC90FDAA22168C234C
# value of those 68 bits: PI66_TOP68 * 2^-66; half of it = PI66_TOP68 * 2^-67
HALF_PI66_NUM = PI66_TOP68  # P = HALF_PI66_NUM / 2^67
HALF_PI66_SHIFT = 67

# ---------------------------------------------------------------------------------- decimal


def pi_dec(prec):
    with localcontext() as c:
        c.prec = prec + 5
        three = Decimal(3)
        lasts, t, s, n, na, d, da = 0, three, 3, 1, 0, 0, 24
        while s != lasts:
            lasts = s
            n, na = n + na, na + 8
            d, da = d + da, da + 32
            t = (t * n) / d
            s += t
    with localcontext() as c:
        c.prec = prec
        return +s


_CONST = {}


def pi_at():
    """pi to P + 10 digits (cached per working precision)."""
    key = ("pi", P)
    if key not in _CONST:
        _CONST[key] = pi_dec(P + 10)
    return _CONST[key]


def ln2():
    key = ("ln2", P)
    if key not in _CONST:
        with localcontext() as c:
            c.prec = P + 10
            _CONST[key] = Decimal(2).ln()
    return _CONST[key]


def d_sin(x):
    with localcontext() as c:
        c.prec = P + 4
        i, lasts, s, fact, num, sign = 1, 0, x, 1, x, 1
        while s != lasts:
            lasts = s
            i += 2
            fact *= i * (i - 1)
            num *= x * x
            sign *= -1
            s += num / fact * sign
    return s


def d_cos(x):
    with localcontext() as c:
        c.prec = P + 4
        i, lasts, s, fact, num, sign = 0, 0, 1, 1, 1, 1
        while s != lasts:
            lasts = s
            i += 2
            fact *= i * (i - 1)
            num *= x * x
            sign *= -1
            s += num / fact * sign
    return s


def d_atan(t):
    """atan(t), 0 < t <= 1: one halving t -> t/(1 + sqrt(1 + t^2)), then Euler's series
    atan(z) = sum 2^(2n) (n!)^2 / (2n+1)! * z^(2n+1) / (1+z^2)^(n+1)."""
    with localcontext() as c:
        c.prec = P + 6
        z = t / (1 + (1 + t * t).sqrt())
        z2 = z * z
        y = z2 / (1 + z2)
        term = z / (1 + z2)
        s = term
        n = 0
        while True:
            n += 1
            term = term * y * (2 * n) / (2 * n + 1)
            s2 = s + term
            if s2 == s:
                break
            s = s2
        return 2 * s


# ------------------------------------------------------------------------------ Ext helpers


def ext_hex(e):
    return "%04X%016X" % e


def dec_ext(e):
    """Returns (kind, neg, M, E) with value M * 2^(E-63) for 'fin' (M may be unnormalized
    for denormals; normalized here)."""
    se, sig = e
    neg = (se >> 15) & 1
    be = se & 0x7FFF
    if be == 0x7FFF:
        if not (sig >> 63):
            return ("invalid", neg, 0, 0)
        if (sig << 1) & ((1 << 64) - 1) == 0:
            return ("inf", neg, 0, 0)
        return ("qnan" if (sig >> 62) & 1 else "snan", neg, 0, 0)
    if be == 0:
        if sig == 0:
            return ("zero", neg, 0, 0)
        E = -16382
        M = sig
        while not (M >> 63):
            M <<= 1
            E -= 1
        return ("fin", neg, M, E)
    if not (sig >> 63):
        return ("invalid", neg, 0, 0)
    return ("fin", neg, sig, be - BIAS)


def quiet(e):
    return (e[0], e[1] | (1 << 62))


def signed(e, neg):
    return ((e[0] & 0x7FFF) | (0x8000 if neg else 0), e[1])


def from_float(f):
    b = struct.unpack("<I", struct.pack("<f", f))[0]
    return from_f32_bits(b)


def from_f32_bits(b):
    s = b >> 31
    ex = (b >> 23) & 0xFF
    fr = b & 0x7FFFFF
    if ex == 0xFF:
        if fr == 0:
            return ((s << 15) | 0x7FFF, 1 << 63)
        return ((s << 15) | 0x7FFF, (1 << 63) | (1 << 62) | (fr << 40))
    if ex == 0:
        if fr == 0:
            return (s << 15, 0)
        E = -126
        M = fr
        while not (M >> 23):
            M <<= 1
            E -= 1
        return ((s << 15) | (E + BIAS), M << 40)
    return ((s << 15) | (ex - 127 + BIAS), ((1 << 23) | fr) << 40)


def from_f64_bits(b):
    s = b >> 63
    ex = (b >> 52) & 0x7FF
    fr = b & ((1 << 52) - 1)
    if ex == 0x7FF:
        if fr == 0:
            return ((s << 15) | 0x7FFF, 1 << 63)
        return ((s << 15) | 0x7FFF, (1 << 63) | (1 << 62) | (fr << 11))
    if ex == 0:
        if fr == 0:
            return (s << 15, 0)
        E = -1022
        M = fr
        while not (M >> 52):
            M <<= 1
            E -= 1
        return ((s << 15) | (E + BIAS), M << 11)
    return ((s << 15) | (ex - 1023 + BIAS), ((1 << 52) | fr) << 11)


def from_double(d):
    return from_f64_bits(struct.unpack("<Q", struct.pack("<d", d))[0])


def to_float32(d):
    return struct.unpack("<f", struct.pack("<f", d))[0]


def make_ext(neg, M, E):
    """Finite Ext from a normalized 64-bit M and exponent E (must be in the normal range)."""
    assert M >> 63 and -16382 <= E <= 16383
    return (((1 if neg else 0) << 15) | (E + BIAS), M)


# ----------------------------------------------------------------------------- rounding

class Undecided(Exception):
    pass


def _ilog2_frac(num, den):
    """floor(log2(num/den)) for positive integers."""
    E = num.bit_length() - den.bit_length()
    # adjust so that 2^E <= num/den < 2^(E+1)
    if E >= 0:
        if num < (den << E):
            E -= 1
    else:
        if (num << -E) < den:
            E -= 1
    if E >= 0:
        assert (den << E) <= num < (den << (E + 1))
    else:
        assert den <= (num << -E) < (den << 1)
    return E


def round_rational(neg, num, den, eps_num, eps_den):
    """Round (-1)^neg * num/den to the x87 extended format (nearest, ties even, denormals,
    overflow to inf). The true value is within a relative eps = eps_num/eps_den of num/den
    (eps = 0: exact). Returns (ext, truth) or raises Undecided."""
    if num == 0:
        return (((1 if neg else 0) << 15, 0), "-")
    E = _ilog2_frac(num, den)
    q = max(E - 63, -16445)  # quantum exponent
    # N = num/den / 2^q
    if q >= 0:
        n_num, n_den = num, den << q
    else:
        n_num, n_den = num << -q, den
    n = n_num // n_den
    rem = n_num - n * n_den
    # frac = rem/n_den; distance from 1/2 versus the error eps * N (< eps * 2^64)
    d2 = 2 * rem - n_den  # sign of frac - 1/2, scaled by n_den
    if eps_num:
        # |true N - N| <= eps * N <= eps * 2^64 (N < 2^64); undecided if |frac-1/2| <= that
        if abs(d2) * eps_den <= 2 * n_den * eps_num * (1 << 64):
            raise Undecided()
        up = d2 > 0
    else:
        up = d2 > 0 or (d2 == 0 and (n & 1))
    if up:
        n += 1
    if n == 1 << 64:
        n >>= 1
        q += 1
    sign = (1 if neg else 0) << 15
    if n >> 63:
        Eres = q + 63
        if Eres > 16383:
            return ((sign | 0x7FFF, 1 << 63), "-")
        ext = (sign | (Eres + BIAS), n)
    else:
        ext = (sign, n)  # denormal (q == -16445)
    truth = "-"
    if eps_num:
        # 160-bit truth at the same binade as the exact value: T = floor(v * 2^(159 - E))
        sh = 159 - E
        if sh >= 0:
            T = (num << sh) // den
        else:
            T = num // (den << -sh)
        truth = "%d:%d:%040X" % (1 if neg else 0, E, T)
    return ext, truth


def dec_to_rational(v):
    s, digits, exp = v.as_tuple()
    D = 0
    for dg in digits:
        D = D * 10 + dg
    if exp >= 0:
        return s, D * 10 ** exp, 1
    return s, D, 10 ** (-exp)


EPS_NUM, EPS_DEN = 1, 10 ** EPS_DIGITS


def round_dec(v, flip=False):
    """Round a Decimal approximation (relative error <= 10^-EPS_DIGITS)."""
    s, num, den = dec_to_rational(v)
    neg = bool(s) != flip
    return round_rational(neg, num, den, EPS_NUM, EPS_DEN)


def with_retry(fn, *args):
    global P, EPS_DIGITS, EPS_NUM, EPS_DEN
    try:
        return fn(*args)
    except Undecided:
        old = P
        P, EPS_DIGITS = 160, 150
        EPS_DEN = 10 ** EPS_DIGITS
        getcontext().prec = P
        try:
            sys.stderr.write("near-tie: %s %s (retrying at %d digits)\n" % (fn.__name__, args, P))
            return fn(*args)
        finally:
            P, EPS_DIGITS = old, old - 8
            EPS_DEN = 10 ** EPS_DIGITS
            getcontext().prec = P


def dec_exact_pow2(M, k):
    """Decimal M * 2^k, rounded to P+20 digits."""
    with localcontext() as c:
        c.prec = P + 20
        if k >= 0:
            return Decimal(M * (1 << k)) if k < 4000 else Decimal(M) * (Decimal(2) ** k)
        if -k < 4000:
            return Decimal(M) / Decimal(1 << -k)
        return Decimal(M) * (Decimal(2) ** k)


# ---------------------------------------------------------------------------- the models


def model_trig(x):
    """Returns (sin, cos, tan, oor, truths)."""
    kind, neg, M, E = dec_ext(x)
    if kind == "zero":
        return x, ONE, x, 0, ("-", "-", "-")
    if kind == "qnan":
        return x, x, x, 0, ("-", "-", "-")
    if kind == "snan":
        return quiet(x), quiet(x), quiet(x), 0, ("-", "-", "-")
    if kind in ("inf", "invalid"):
        return INDEFINITE, INDEFINITE, INDEFINITE, 0, ("-", "-", "-")
    if E >= 63:
        return x, x, x, 1, ("-", "-", "-")
    # |x| = M * 2^(E-63); reduce by P = HALF_PI66_NUM * 2^-67 exactly: work in units 2^-S
    S = max(67, 63 - E)
    X = M << (S - (63 - E))  # |x| * 2^S
    Pn = HALF_PI66_NUM << (S - 67)  # P * 2^S
    k = (2 * X + Pn) // (2 * Pn)  # nearest (ties impossible)
    R = X - k * Pn  # r * 2^S, signed
    quad = k & 3
    rneg = R < 0
    with localcontext() as c:
        c.prec = P + 20
        ra = Decimal(abs(R)) / Decimal(1 << S) if S < 4000 else Decimal(abs(R)) * (Decimal(2) ** -S)
    s = d_sin(ra)
    co = d_cos(ra)
    # quadrant functions (signs before x's sign)
    def sgn(v, n):
        return -v if n else v
    sinv = [sgn(s, rneg), co, sgn(s, not rneg), -co][quad]
    cosv = [co, sgn(s, not rneg), -co, sgn(s, rneg)][quad]
    with localcontext() as c:
        c.prec = P + 4
        tanv = (sgn(s, rneg) / co) if quad % 2 == 0 else (sgn(co, not rneg) / s)
    if neg:
        sinv, tanv = -sinv, -tanv
    rs, ts = round_dec(sinv)
    rc, tc = round_dec(cosv)
    rt, tt = round_dec(tanv)
    return rs, rc, rt, 0, (ts, tc, tt)


PI_RN = (0x4000, 0xC90FDAA22168C235)


def rn_const(v):
    return round_dec(v)[0]


def model_atan(y, x):
    ky, ny, My, Ey = dec_ext(y)
    kx, nx, Mx, Ex = dec_ext(x)
    if ky == "invalid" or kx == "invalid":
        return INDEFINITE, "-"
    yn = ky in ("qnan", "snan")
    xn = kx in ("qnan", "snan")
    if yn or xn:
        return nan2(y, ky, x, kx), "-"
    pi = pi_at()
    if ky == "zero":
        return (signed(PI_RN, ny) if nx else ((ny << 15), 0)), "-"
    if ky == "inf":
        if kx == "inf":
            return signed(rn_const(3 * pi / 4 if nx else pi / 4), ny), "-"
        return signed(rn_const(pi / 2), ny), "-"
    if kx == "zero":
        return signed(rn_const(pi / 2), ny), "-"
    if kx == "inf":
        return (signed(PI_RN, ny) if nx else ((ny << 15), 0)), "-"
    # finite nonzero: a = |y|, b = |x| as integers over a common power of two
    shift = Ey - Ex
    if shift >= 0:
        a, b = My << shift, Mx
    else:
        a, b = My, Mx << -shift
    with localcontext() as c:
        c.prec = P + 6
        if a <= b:
            th = d_atan(Decimal(a) / Decimal(b))
        else:
            th = pi / 2 - d_atan(Decimal(b) / Decimal(a))
        if nx:
            th = pi - th
    return round_dec(th, flip=bool(ny))


def nan2(a, ka, b, kb):
    an = ka in ("qnan", "snan")
    bn = kb in ("qnan", "snan")
    if ka == "snan" and kb != "snan":
        return b if bn else quiet(a)
    if kb == "snan" and ka != "snan":
        return a if an else quiet(b)
    if not an:
        return quiet(b)
    if not bn:
        return quiet(a)
    if a[1] < b[1]:
        return quiet(b)
    if b[1] < a[1]:
        return quiet(a)
    return quiet(a) if a[0] < b[0] else quiet(b)


def inf(neg):
    return (((1 if neg else 0) << 15) | 0x7FFF, 1 << 63)


def zero(neg):
    return (((1 if neg else 0) << 15), 0)


def model_yl2x(y, x):
    ky, ny, My, Ey = dec_ext(y)
    kx, nx, Mx, Ex = dec_ext(x)
    if ky == "invalid" or kx == "invalid":
        return INDEFINITE, "-"
    if ky in ("qnan", "snan") or kx in ("qnan", "snan"):
        return nan2(y, ky, x, kx), "-"
    if nx and kx != "zero":
        return INDEFINITE, "-"
    if kx == "zero":
        if ky == "zero":
            return INDEFINITE, "-"
        return inf(not ny), "-"
    if kx == "inf":
        if ky == "zero":
            return INDEFINITE, "-"
        return inf(ny), "-"
    # x finite positive
    if Mx == 1 << 63 and Ex == 0:
        if ky == "inf":
            return INDEFINITE, "-"
        return zero(ny), "-"
    lneg = Ex < 0
    if ky == "inf":
        return inf(ny != lneg), "-"
    if ky == "zero":
        return zero(ny != lneg), "-"
    if Mx == 1 << 63:
        # exact: y * Ex
        num = My * abs(Ex)
        k = Ey - 63
        if k >= 0:
            return round_rational(ny != lneg, num << k, 1, 0, 1)[0], "-"
        return round_rational(ny != lneg, num, 1 << -k, 0, 1)[0], "-"
    # log2(x) = E' + ln(m)/ln2, m in [0.75, 1.5)
    with localcontext() as c:
        c.prec = P + 10
        if Mx >= 3 << 62:
            m = Decimal(Mx) / Decimal(1 << 64)
            Ep = Ex + 1
        else:
            m = Decimal(Mx) / Decimal(1 << 63)
            Ep = Ex
        l2 = m.ln() / ln2()
        if Ep != 0:
            l2 = Ep + l2
        yv = dec_exact_pow2(My, Ey - 63)
        v = yv * l2
    return round_dec(v, flip=bool(ny))


def model_f2xm1(x):
    kind, neg, M, E = dec_ext(x)
    if kind in ("zero", "qnan"):
        return x, "-"
    if kind == "snan":
        return quiet(x), "-"
    if kind == "invalid":
        return INDEFINITE, "-"
    if kind == "inf":
        return ((0xBFFF, 1 << 63) if neg else x), "-"
    if M == 1 << 63 and E == 0:
        return ((0xBFFE, 1 << 63) if neg else ONE), "-"
    with localcontext() as c:
        c.prec = P + 10
        xv = dec_exact_pow2(M, E - 63)
        if neg:
            xv = -xv
        g = xv * ln2()
        if abs(g) < Decimal("1e-3"):
            # expm1 series
            s, term, n = g, g, 1
            while True:
                n += 1
                term = term * g / n
                s2 = s + term
                if s2 == s:
                    break
                s = s2
            v = s
        else:
            if E >= 15:  # |x| >= 32768: overflow / -1
                if neg:
                    return (0xBFFF, 1 << 63), "-"
                return inf(False), "-"
            v = g.exp() - 1
    return round_dec(v)


# ---------------------------------------------------------------------------- generators

KLN2 = (0x3FFE, 0xB17217F7D1CF79AC)
KLG2 = (0x3FFD, 0x9A209A84FBCFF799)


def rnd_float_exp(rng, emin, emax, signed_=True):
    E = rng.randint(emin, emax)
    M = (1 << 23) | rng.getrandbits(23)
    neg = signed_ and rng.random() < 0.5
    return make_ext(neg, M << 40, E)


def rnd_double_exp(rng, emin, emax, signed_=True):
    E = rng.randint(emin, emax)
    M = (1 << 52) | rng.getrandbits(52)
    neg = signed_ and rng.random() < 0.5
    return make_ext(neg, M << 11, E)


def rnd_ext_exp(rng, emin, emax, signed_=True):
    E = rng.randint(emin, emax)
    M = (1 << 63) | rng.getrandbits(63)
    neg = signed_ and rng.random() < 0.5
    return make_ext(neg, M, E)


def float_uniform(rng, lo, hi):
    return from_float(to_float32(rng.uniform(lo, hi)))


def round_to_bits(num, den, bits):
    """Nearest value with a `bits`-bit significand to num/den > 0: returns (M64, E)."""
    E = _ilog2_frac(num, den)
    q = E - (bits - 1)
    if q >= 0:
        n = (2 * num + (den << q)) // (2 * (den << q))
    else:
        n = (2 * (num << -q) + den) // (2 * den)
    if n >> bits:
        n >>= 1
        q += 1
    return n << (64 - bits), q + bits - 1


def near_multiple(rng, k, half, bits, spread):
    """An Ext with a `bits`-bit significand near k * P (or (k + 1/2) * P), offset by up to
    `spread` ulps."""
    num = HALF_PI66_NUM * (2 * k + (1 if half else 0))
    den = 1 << (HALF_PI66_SHIFT + 1)
    M, E = round_to_bits(num, den, bits)
    off = rng.randint(-spread, spread) << (64 - bits)
    M2 = M + off
    if not (M2 >> 63) or M2 >> 64:
        M2 = M
    return make_ext(rng.random() < 0.5, M2, E)


SPECIAL_SINGLE = [
    (0x0000, 0), (0x8000, 0),
    (0x7FFF, 1 << 63), (0xFFFF, 1 << 63),
    (0x7FFF, 0xC000000000000000), (0xFFFF, 0xC000000000000000),
    (0x7FFF, 0xC000000000000001), (0xFFFF, 0xC123456789ABCDEF),
    (0x7FFF, 0x8000000000000001), (0xFFFF, 0x8123456789ABCDEF),  # SNaN
    (0x7FFF, 0xA000000000000000),
    (0x7FFF, 0x4000000000000000), (0x7FFF, 0x0000000000000001),  # pseudo-NaN / pseudo-inf
    (0x7FFF, 0),
    (0x3FFF, 0x4000000000000000), (0x0001, 0),  # unnormals
    (0x0000, 1), (0x8000, 0x0000000012345678), (0x0000, 0x7FFFFFFFFFFFFFFF),  # denormals
    (0x0000, 0x8000000000000000), (0x8000, 0xC000000000000000),  # pseudo-denormals
    (0x0001, 0x8000000000000000), (0x7FFE, 0xFFFFFFFFFFFFFFFF), (0xFFFE, 0xFFFFFFFFFFFFFFFF),
    ONE, (0xBFFF, 1 << 63), (0x4000, 0xA000000000000000), (0xC000, 0xA000000000000000),
    (0x3FFE, 1 << 63), (0xBFFE, 1 << 63), (0x3F9E, 0xF0BDC21ABB48DB20), (0xBF9E, 0x8000000000000000),
    from_float(1e-30), from_float(-3e-41), from_float(1e30), from_float(-7.5),
    from_double(1e-300), from_double(-2.5e300),
]


def gen_trig(rng, scale):
    out = list(SPECIAL_SINGLE)
    # around the tiny threshold and the 2^63 limit
    for E in range(-40, -25):
        for M in ((1 << 63), (1 << 63) + 1, (1 << 64) - 1, 0xC90FDAA22168C235):
            for neg in (False, True):
                out.append(make_ext(neg, M, E))
    for M in ((1 << 63), (1 << 63) + 1, (1 << 64) - 1):
        for E in (61, 62, 63, 64, 100, 16383):
            out.append(make_ext(False, M, E))
            out.append(make_ext(True, M, E))
    for f in (1e19, -1e19, 9.2e18, 9.3e18, 3.4e38, 1e10, 1e15, 123456789.0):
        out.append(from_float(f))
        out.append(from_double(f))
    # pi multiples as the game would pass them
    for i in range(-64, 65):
        out.append(from_float(to_float32(i * 3.141592653589793 / 4)))
        out.append(from_double(i * 3.141592653589793 / 4))
    n = int(scale)
    for _ in range(int(0.30 * n)):
        out.append(rnd_float_exp(rng, -34, 30))
    for _ in range(int(0.20 * n)):
        out.append(float_uniform(rng, -8 * 3.14159265, 8 * 3.14159265))
    for _ in range(int(0.15 * n)):
        r = rng.random()
        k = rng.randint(1, 1000) if r < 0.5 else rng.randint(1, 1 << 20) if r < 0.8 else rng.randint(1, 1 << 40)
        out.append(near_multiple(rng, k, rng.random() < 0.3, 24, 3))
    for _ in range(int(0.10 * n)):
        out.append(rnd_double_exp(rng, -30, 62))
    for _ in range(int(0.10 * n)):
        out.append(rnd_ext_exp(rng, -40, 62))
    for _ in range(int(0.10 * n)):
        k = int(2 ** rng.uniform(0, 61.5))
        out.append(near_multiple(rng, max(k, 1), rng.random() < 0.3, 64, 4))
    for _ in range(int(0.05 * n)):
        k = rng.randint(0, 3000)
        out.append(near_multiple(rng, k, True, 24, 2))
    return [(x,) for x in out]


def gen_atan(rng, scale):
    sp = list(SPECIAL_SINGLE) + [from_float(2.5), from_float(-2.5)]
    out = [(a, b) for a in sp for b in sp]
    n = int(scale)
    for _ in range(int(0.30 * n)):
        out.append((rnd_float_exp(rng, -40, 40), rnd_float_exp(rng, -40, 40)))
    for _ in range(int(0.20 * n)):
        out.append((float_uniform(rng, -1000, 1000), float_uniform(rng, -1000, 1000)))
    for _ in range(int(0.075 * n)):
        xd = rng.uniform(-1, 1)
        s = to_float32(((1 + xd) * (1 - xd)) ** 0.5)
        out.append((from_float(s), from_double(xd)))  # acos: fpatan(y = s, x = x)
    for _ in range(int(0.075 * n)):
        xd = rng.uniform(-1, 1)
        s = to_float32(((1 + xd) * (1 - xd)) ** 0.5)
        out.append((from_double(xd), from_float(s)))  # asin: fpatan(y = x, x = s)
    for _ in range(int(0.10 * n)):
        # ratios near the table points and boundaries j/64, (2j+1)/128, and near 1
        xb = rnd_float_exp(rng, -20, 20, signed_=False)
        kind, _, Mx, Ex = dec_ext(xb)
        j = rng.randint(0, 128)
        c_num = j * (1 << 40) + rng.randint(-1000, 1000)
        if c_num <= 0:
            c_num = 1
        M, E = round_to_bits(Mx * c_num, 1 << 47, 24 if rng.random() < 0.5 else 64)
        y = make_ext(rng.random() < 0.5, M, E + Ex - 63)
        xs = signed(xb, rng.random() < 0.5)
        out.append((y, xs) if rng.random() < 0.5 else (xs, y))
    for _ in range(int(0.10 * n)):
        out.append((rnd_ext_exp(rng, -60, 60), rnd_ext_exp(rng, -60, 60)))
    for _ in range(int(0.05 * n)):
        e1 = rng.randint(-1000, 1000)
        d = rng.randint(20, 2000) * (1 if rng.random() < 0.5 else -1)
        if rng.random() < 0.5:
            out.append((rnd_double_exp(rng, e1, e1), rnd_double_exp(rng, e1 + d, e1 + d)))
        else:
            out.append((rnd_ext_exp(rng, e1, e1), rnd_ext_exp(rng, e1 + d, e1 + d)))
    return out


def gen_yl2x(rng, scale):
    sp = list(SPECIAL_SINGLE) + [KLN2, KLG2, from_float(0.5), from_float(3.0)]
    out = [(a, b) for a in sp for b in sp]
    n = int(scale)
    for _ in range(int(0.30 * n)):
        y = KLN2 if rng.random() < 0.5 else KLG2
        b = rng.randint(1, 0x7F7FFFFF)  # any positive finite float incl. denormals
        out.append((y, from_f32_bits(b)))
    for _ in range(int(0.15 * n)):
        y = KLN2 if rng.random() < 0.5 else KLG2
        b = 0x3F800000 + rng.randint(-(1 << 16), 1 << 16)
        out.append((y, from_f32_bits(b)))
    for _ in range(int(0.20 * n)):
        out.append((rnd_double_exp(rng, -10, 10), rnd_double_exp(rng, -100, 100, signed_=False)))
    for _ in range(int(0.10 * n)):
        b = 0x3FF0000000000000 + rng.randint(-(1 << 20), 1 << 20)
        out.append((rnd_double_exp(rng, -10, 10), from_f64_bits(b)))
    for _ in range(int(0.10 * n)):
        out.append((rnd_ext_exp(rng, -60, 60), rnd_ext_exp(rng, -16000, 16000, signed_=False)))
    for _ in range(int(0.05 * n)):
        E = rng.randint(-16445, 16383)
        if E < -16382:
            x = (0, 1 << (63 - (-16382 - E)))
        else:
            x = make_ext(False, 1 << 63, E)
        y = rnd_ext_exp(rng, -30, 30) if rng.random() < 0.7 else (KLN2 if rng.random() < 0.5 else KLG2)
        out.append((y, x))
    for _ in range(int(0.05 * n)):
        idx = rng.randint(0, 256)
        M = (1 << 63) + (idx << 55) + rng.randint(-(1 << 20), 1 << 20)
        M = min(max(M, 1 << 63), (1 << 64) - 1)
        E = rng.choice([-1, 0, 1, rng.randint(-200, 200)])
        y = KLN2 if rng.random() < 0.5 else rnd_ext_exp(rng, -5, 5)
        out.append((y, make_ext(False, M, E)))
    for _ in range(int(0.05 * n)):
        M = (1 << 63) + rng.randint(1, 1 << 40) if rng.random() < 0.5 else (1 << 64) - rng.randint(1, 1 << 40)
        E = 0 if M < (1 << 64) - (1 << 41) and M > (1 << 63) + (1 << 41) else (0 if M < (3 << 62) else -1)
        y = KLN2 if rng.random() < 0.3 else rnd_ext_exp(rng, -5, 5)
        out.append((y, make_ext(False, M, E)))
    return out


def gen_f2xm1(rng, scale):
    out = list(SPECIAL_SINGLE)
    for v in (0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.0, -2.0, 10.25, -10.25, 100.5, -100.5, 200.0, -200.0,
              16383.0, 16384.0, 16385.0, -16384.0, 40000.0):
        out.append(from_double(v))
    for j in range(-128, 129):
        out.append(from_double(j / 128.0))
    n = int(scale)
    for _ in range(int(0.40 * n)):
        out.append(float_uniform(rng, -0.5, 0.5))
    for _ in range(int(0.20 * n)):
        out.append(rnd_float_exp(rng, -40, -2))
    for _ in range(int(0.15 * n)):
        out.append(float_uniform(rng, -1, 1))
    for _ in range(int(0.15 * n)):
        out.append(rnd_ext_exp(rng, -70, -1))
    for _ in range(int(0.05 * n)):
        j = rng.randint(-64, 63)  # x near (2j+1)/128, where round(64 x) changes
        num = (2 * j + 1) * (1 << 40) + rng.randint(-1000, 1000)
        if num == 0:
            num = 1
        M, E = round_to_bits(abs(num), 1 << 47, 24 if rng.random() < 0.5 else 64)
        out.append(make_ext(num < 0, M, E))
    for _ in range(int(0.04 * n)):
        if rng.random() < 0.5:
            M = (1 << 64) - rng.randint(1, 1 << 30)
            out.append(make_ext(rng.random() < 0.5, M, -1))
        else:
            b = 0x3F800000 - rng.randint(1, 1 << 12)
            e = from_f32_bits(b)
            out.append(signed(e, rng.random() < 0.5))
    for _ in range(int(0.01 * n)):
        E = rng.randint(-16445, -60)
        if E < -16382:
            out.append(((0x8000 if rng.random() < 0.5 else 0), rng.getrandbits(63 - (-16382 - E)) | 1))
        else:
            out.append(rnd_ext_exp(rng, E, E))
    return [(x,) for x in out]


# ------------------------------------------------------------------------------- driver


def work(args):
    kernel, items = args
    lines = []
    for it in items:
        if kernel == "trig":
            (x,) = it
            s, c, t, oor, tr = with_retry(model_trig, x)
            lines.append("%s %s %s %s %d %s %s %s" % (ext_hex(x), ext_hex(s), ext_hex(c), ext_hex(t), oor, tr[0], tr[1], tr[2]))
        elif kernel == "atan":
            y, x = it
            r, tr = with_retry(model_atan, y, x)
            lines.append("%s %s %s %s" % (ext_hex(y), ext_hex(x), ext_hex(r), tr))
        elif kernel == "yl2x":
            y, x = it
            r, tr = with_retry(model_yl2x, y, x)
            lines.append("%s %s %s %s" % (ext_hex(y), ext_hex(x), ext_hex(r), tr))
        elif kernel == "f2xm1":
            (x,) = it
            r, tr = with_retry(model_f2xm1, x)
            lines.append("%s %s %s" % (ext_hex(x), ext_hex(r), tr))
    return lines


def main():
    kernel, scale, path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    workers = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    rng = random.Random({"trig": 1, "atan": 2, "yl2x": 3, "f2xm1": 4}[kernel] * 7919 + 87)
    gen = {"trig": gen_trig, "atan": gen_atan, "yl2x": gen_yl2x, "f2xm1": gen_f2xm1}[kernel]
    items = gen(rng, scale)
    chunk = 2000
    jobs = [(kernel, items[i:i + chunk]) for i in range(0, len(items), chunk)]
    with open(path, "w") as f, multiprocessing.Pool(workers) as pool:
        done = 0
        for lines in pool.imap(work, jobs):
            f.write("\n".join(lines) + "\n")
            done += len(lines)
    print("%s: %d vectors -> %s" % (kernel, len(items), path))


if __name__ == "__main__":
    main()
