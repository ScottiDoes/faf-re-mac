// Software x87 transcendental instructions: FSIN, FCOS, FPTAN, FPATAN, FYL2X, F2XM1.
// Contract: X87Types.h. Design: macos/docs/DETERMINISM.md §3.4 and §3.6.
//
// Model of the hardware result: the function value rounded to nearest at a 64-bit
// significand (extended exponent range, denormals at 2^-16445). FSIN/FCOS/FPTAN first reduce
// the argument with Intel's 66-bit pi exactly: r = x - k*(pi66/2), k = nearest integer, then
// take sin/cos/tan of r per quadrant. FPATAN, FYL2X and F2XM1 have no reduction constant: the
// model is the exact atan2(y, x), y*log2(x) (one rounding) and 2^x - 1.
//
// Implementation (Ziv's strategy, every bound rigorous):
//  1. Fast path: Q2.125 fixed point (signed 128-bit, value v * 2^-125) and a 128-bit floating
//     type F128 that carries an error bound in units of its last bit. Table-based argument
//     reduction, short polynomials whose low-order tail is evaluated in double (the analysis
//     next to each tail shows its error is below 2^-126 absolute). The result is ~2^-115
//     relative or better; RoundToExt rounds it when the error interval contains no rounding
//     boundary of the 64-bit result (fails with probability ~2^-50 per call).
//  2. Slow path: 256-bit floating arithmetic (Big) with plain Taylor/atanh series, error below
//     2^-245 relative; decides every case except a near-tie within 2^-231 relative, which no
//     64-bit input is known to produce (it would be rounded as if exact and counted).
// Exact results (x = 0, x = 2^n for FYL2X, x = +-1 for F2XM1, ...) are handled exactly.
//
// Portable C++20 with unsigned __int128 (clang on arm64 and x86_64; clang-cl not tried yet);
// no 128-bit division or 128-bit/floating conversion is used, so no compiler-rt helper is
// needed. The x86_64 build gives bit-identical results (macos/porting/x87-kernels.md).
// No libm. Pure functions, no state (the test hooks exist only under FAF_X87_KERNELS_TESTING).

#include "X87Types.h"
#include "X87KernelsTesting.h"

#include <cstdint>
#include <cstring>

#if defined(FAF_X87_KERNELS_TESTING)
#include <atomic>
#endif

namespace faf_posix::x87::kernels_detail
{
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;
  using u64 = std::uint64_t;
  using u128 = unsigned __int128;
  using i128 = __int128;

  constexpr i128 Q(u64 hi, u64 lo)
  {
    return static_cast<i128>((static_cast<u128>(hi) << 64) | lo);
  }

  struct BigConst
  {
    u64 w[4];
    int e;
    bool zero;
  };
} // namespace faf_posix::x87::kernels_detail

#include "X87KernelsTables.h"

#if defined(FAF_X87_KERNELS_TESTING)
namespace faf_posix::x87::kernels_testing
{
  namespace
  {
    std::atomic<int> g_mode{0};
    thread_local FastApprox g_last{};
    std::atomic<std::uint64_t> g_fastUndecided{0};
    std::atomic<std::uint64_t> g_slowUndecided{0};
  } // namespace

  void SetMode(Mode mode) noexcept { g_mode.store(static_cast<int>(mode), std::memory_order_relaxed); }
  FastApprox LastFast() noexcept { return g_last; }
  void ClearLastFast() noexcept { g_last = FastApprox{}; }
  std::uint64_t FastUndecided() noexcept { return g_fastUndecided.load(std::memory_order_relaxed); }
  std::uint64_t SlowUndecided() noexcept { return g_slowUndecided.load(std::memory_order_relaxed); }
  void ResetCounters() noexcept
  {
    g_fastUndecided.store(0, std::memory_order_relaxed);
    g_slowUndecided.store(0, std::memory_order_relaxed);
  }
} // namespace faf_posix::x87::kernels_testing
#endif

namespace faf_posix::x87::kernels_detail
{
  namespace
  {
    // ======================================================================================
    // Integer helpers
    // ======================================================================================

    inline int Clz64(u64 v) noexcept { return __builtin_clzll(v); } // v != 0

    inline int Clz128(u128 v) noexcept // v != 0
    {
      const u64 hi = static_cast<u64>(v >> 64);
      return hi != 0 ? Clz64(hi) : 64 + Clz64(static_cast<u64>(v));
    }

    inline u128 Abs128(i128 v) noexcept
    {
      return v < 0 ? u128(0) - static_cast<u128>(v) : static_cast<u128>(v);
    }

    struct U256
    {
      u128 hi;
      u128 lo;
    };

    // Exact 128 x 128 -> 256-bit product.
    inline U256 Mul128(u128 a, u128 b) noexcept
    {
      const u64 a0 = static_cast<u64>(a), a1 = static_cast<u64>(a >> 64);
      const u64 b0 = static_cast<u64>(b), b1 = static_cast<u64>(b >> 64);
      const u128 p00 = static_cast<u128>(a0) * b0;
      const u128 p01 = static_cast<u128>(a0) * b1;
      const u128 p10 = static_cast<u128>(a1) * b0;
      const u128 p11 = static_cast<u128>(a1) * b1;
      const u128 mid = (p00 >> 64) + static_cast<u64>(p01) + static_cast<u64>(p10);
      U256 r;
      r.hi = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);
      r.lo = (mid << 64) | static_cast<u64>(p00);
      return r;
    }

    // Q2.125 product, magnitude truncated: |error| < 2^-125 (one "q"). Requires |a*b| < 4.
    inline i128 MulQ(i128 a, i128 b) noexcept
    {
      const U256 p = Mul128(Abs128(a), Abs128(b));
      const i128 r = static_cast<i128>((p.hi << 3) | (p.lo >> 125));
      return ((a < 0) != (b < 0)) ? -r : r;
    }

    // Q2.125 product of nonnegative operands (same error as MulQ, shorter latency).
    inline i128 MulQU(i128 a, i128 b) noexcept
    {
      const U256 p = Mul128(static_cast<u128>(a), static_cast<u128>(b));
      return static_cast<i128>((p.hi << 3) | (p.lo >> 125));
    }

    // ======================================================================================
    // Double helpers (no libm)
    // ======================================================================================

    inline double Pow2(int k) noexcept // 2^k, -1022 <= k <= 1023
    {
      const u64 bits = static_cast<u64>(k + 1023) << 52;
      double d;
      std::memcpy(&d, &bits, sizeof d);
      return d;
    }

    // double -> Q2.125, truncated (|d| < 4).
    inline i128 DoubleToFixed(double d) noexcept
    {
      u64 bits;
      std::memcpy(&bits, &d, sizeof bits);
      const int be = static_cast<int>((bits >> 52) & 0x7FF);
      if (be == 0)
        return 0;
      const u64 mant = (bits & ((1ull << 52) - 1)) | (1ull << 52);
      const int k = be - 1075 + 125;
      u128 mag;
      if (k >= 0)
        mag = static_cast<u128>(mant) << k;
      else if (k > -64)
        mag = mant >> (-k);
      else
        mag = 0;
      const i128 r = static_cast<i128>(mag);
      return (bits >> 63) ? -r : r;
    }

    // Q2.125 -> double for the polynomial tails: the top 64 bits only (|v| < 2^127), absolute
    // error < 2^-61 plus one rounding. Every tail argument tolerates that (the tail's
    // derivative times 2^-61, times the power of the argument in front of the tail, is below
    // 2^-140 everywhere).
    inline double TailArg(i128 v) noexcept
    {
      return static_cast<double>(static_cast<std::int64_t>(v >> 64)) * 0x1p-61;
    }

    // ======================================================================================
    // Polynomials (Estrin's scheme in Q2.125, low-order tail in double)
    // ======================================================================================
    //
    // Errors are absolute, in q = 2^-125. A MulQ/MulQU truncates (< 1 q); a table or
    // coefficient entry is rounded to nearest (0.5 q). A double tail T with |T| ~ c has an
    // error below c * 2^-52 (rounded coefficients, two or three roundings, the 2^-52 relative
    // error of the double argument); it is multiplied by the power of the argument in front of
    // it, which makes it negligible in every polynomial below (the largest is sin: 0.35 q).

    // sin(d)/d = S(w), w = d^2 in [0, 2^-14], w2 = w^2:
    //   S = (1 - c1 w) + w2 ((c2 - c3 w) + w2 T), T = c4 - c5 w + c6 w^2 (double).
    //   Error: 1 + e(w) c1 (first pair) + 1 + e(w2) / 120 (outer product) + 0.35 (tail)
    //   < 2.6 q for e(w), e(w2) <= 1.02. Stated: 6 q.
    inline i128 SinPoly(i128 w, i128 w2, double wd) noexcept
    {
      const i128 A = kSinCoef[0] - MulQU(w, kSinCoef[1]);
      const i128 B = kSinCoef[2] - MulQU(w, kSinCoef[3]);
      const double t = kSinTail[0] - wd * (kSinTail[1] - wd * kSinTail[2]);
      return A + MulQU(w2, B + MulQU(w2, DoubleToFixed(t)));
    }

    // cos(d) = C(w): C = (1 - w/2) + w2 ((c2 - c3 w) + w2 (c4 - w T)), T = c5 - c6 w.
    //   Error: 0.5 + e(w)/2 (the shift) + 1 + e(w2)/24 < 2.1 q. Stated: 6 q.
    inline i128 CosPoly(i128 w, i128 w2, double wd) noexcept
    {
      const i128 A = kCosCoef[0] - (w >> 1);
      const i128 B = kCosCoef[2] - MulQU(w, kCosCoef[3]);
      const double t = kCosTail[0] - wd * kCosTail[1];
      const i128 D = kCosCoef[4] - MulQU(w, DoubleToFixed(t));
      return A + MulQU(w2, B + MulQU(w2, D));
    }

    // atan(u)/u = Q(w), w = u^2 <= 2^-14 (+):
    //   Q = (1 - c1 w) + w2 ((c2 - c3 w) + w2 ((c4 - c5 w) + w2 T)), T = c6 - c7 w + c8 w^2.
    //   Error: 1 + e(w)/3 + 1 + e(w2)/5 < 2.6 q (e(w) <= 1.03). Stated: 6 q.
    inline i128 AtanPoly(i128 w, i128 w2, double wd) noexcept
    {
      const i128 A = kAtanCoef[0] - MulQU(w, kAtanCoef[1]);
      const i128 B = kAtanCoef[2] - MulQU(w, kAtanCoef[3]);
      const i128 C = kAtanCoef[4] - MulQU(w, kAtanCoef[5]);
      const double t = kAtanTail[0] - wd * (kAtanTail[1] - wd * kAtanTail[2]);
      return A + MulQU(w2, B + MulQU(w2, C + MulQU(w2, DoubleToFixed(t))));
    }

    // ln(1+t)/t = P(t) = sum_{n<10} (-t)^n/(n+1) + t^10 T, |t| <= 2^-8, t exact:
    //   pairs p_i = c_2i - c_2i+1 t (p0 = 1 - t/2 by a shift), t2 = t^2, t4 = t2^2,
    //   P = (p0 + t2 p1) + t4 ((p2 + t2 p3) + t4 (p4 + t2 T)).
    //   Error: 0.5 (shift) + 1 + e(t2)/3 + 1 + e(t4)/5 < 3.1 q. Stated: 8 q.
    inline i128 LogPoly(i128 t, double td) noexcept
    {
      const i128 t2 = MulQ(t, t);
      const i128 t4 = MulQU(t2, t2);
      const i128 p0 = kLogCoef[0] - (t >> 1);
      const i128 p1 = kLogCoef[2] - MulQ(t, kLogCoef[3]);
      const i128 p2 = kLogCoef[4] - MulQ(t, kLogCoef[5]);
      const i128 p3 = kLogCoef[6] - MulQ(t, kLogCoef[7]);
      const i128 p4 = kLogCoef[8] - MulQ(t, kLogCoef[9]);
      double tl = kLogTail[5];
      for (int k = 4; k >= 0; --k)
        tl = kLogTail[k] - td * tl;
      const i128 q0 = p0 + MulQU(t2, p1);
      const i128 q1 = p2 + MulQU(t2, p3);
      const i128 q2 = p4 + MulQU(t2, DoubleToFixed(tl));
      return q0 + MulQU(t4, q1 + MulQU(t4, q2));
    }

    // expm1(g)/g = E(g) = sum_{n<8} g^n/(n+1)! + g^8 T, |g| <= 2^-7.5, e(g) <= 1.01 q:
    //   pairs p_i = c_2i + c_2i+1 g (p0 = 1 + g/2 by a shift), g2 = g^2, g4 = g2^2,
    //   E = (p0 + g2 p1) + g4 ((p2 + g2 p3) + g4 T).
    //   Error: 0.5 + e(g)/2 + 1 + e(g2)/6 + 1 + e(g4)/24 < 3.3 q. Stated: 8 q.
    inline i128 ExpPoly(i128 g, double gd) noexcept
    {
      const i128 g2 = MulQ(g, g);
      const i128 g4 = MulQU(g2, g2);
      const i128 p0 = kExpCoef[0] + (g >> 1);
      const i128 p1 = kExpCoef[2] + MulQ(g, kExpCoef[3]);
      const i128 p2 = kExpCoef[4] + MulQ(g, kExpCoef[5]);
      const i128 p3 = kExpCoef[6] + MulQ(g, kExpCoef[7]);
      double te = kExpTail[4];
      for (int k = 3; k >= 0; --k)
        te = kExpTail[k] + gd * te;
      const i128 q0 = p0 + MulQU(g2, p1);
      const i128 q1 = p2 + MulQU(g2, p3);
      return q0 + MulQU(g4, q1 + MulQU(g4, DoubleToFixed(te)));
    }

    // ======================================================================================
    // Extended-format operands
    // ======================================================================================

    enum class Kind : unsigned char
    {
      Zero,
      Finite,
      Inf,
      QNaN,
      SNaN,
      Invalid, // unsupported encoding (unnormal, pseudo-NaN, pseudo-infinity): #IA
    };

    struct Dec
    {
      Kind kind;
      bool neg;
      int e;  // Finite: value = m * 2^(e - 63)
      u64 m;  // Finite: normalized (bit 63 set)
    };

    inline Dec Decode(Ext x) noexcept
    {
      Dec d{Kind::Invalid, (x.signExp & 0x8000) != 0, 0, 0};
      const int be = x.signExp & 0x7FFF;
      const u64 s = x.signif;
      if (be == 0x7FFF)
      {
        if (!(s >> 63))
          d.kind = Kind::Invalid;
        else if ((s << 1) == 0)
          d.kind = Kind::Inf;
        else
          d.kind = ((s >> 62) & 1) ? Kind::QNaN : Kind::SNaN;
        return d;
      }
      if (be == 0)
      {
        if (s == 0)
        {
          d.kind = Kind::Zero;
          return d;
        }
        // Denormal (integer bit clear) or pseudo-denormal (set): both are 2^-16382 * 0.s.
        const int sh = Clz64(s);
        d.kind = Kind::Finite;
        d.e = -16382 - sh;
        d.m = s << sh;
        return d;
      }
      if (!(s >> 63))
        return d; // unnormal
      d.kind = Kind::Finite;
      d.e = be - 16383;
      d.m = s;
      return d;
    }

    inline bool IsNaN(const Dec& d) noexcept { return d.kind == Kind::QNaN || d.kind == Kind::SNaN; }
    inline Ext Quiet(Ext x) noexcept { return Ext{x.signif | 0x4000000000000000ull, x.signExp}; }
    inline Ext WithSign(Ext x, bool neg) noexcept
    {
      return Ext{x.signif, static_cast<u16>((x.signExp & 0x7FFF) | (neg ? 0x8000 : 0))};
    }
    inline Ext SignedZero(bool neg) noexcept { return Ext{0, static_cast<u16>(neg ? 0x8000 : 0)}; }

    // x itself as a result: a pseudo-denormal (exponent 0, integer bit set) is the value
    // 2^-16382 * 1.f, which the rounding unit writes with exponent 1.
    inline Ext Canonical(Ext x) noexcept
    {
      if ((x.signExp & 0x7FFF) == 0 && (x.signif >> 63))
        return Ext{x.signif, static_cast<u16>(x.signExp | 1)};
      return x;
    }
    inline Ext SignedInf(bool neg) noexcept
    {
      return Ext{0x8000000000000000ull, static_cast<u16>(neg ? 0xFFFF : 0x7FFF)};
    }

    constexpr Ext kPiOver2Ext{kPi.signif, 0x3FFF};
    constexpr Ext kPiOver4Ext{kPi.signif, 0x3FFE};

    // Two-operand NaN result (Intel SDM Vol. 1 Table 4-7; SoftFloat's 8086 rule): a QNaN
    // operand beats an SNaN; two NaNs of the same kind give the larger significand (equal
    // significands: the positive one); the result is quiet. At least one operand is a NaN.
    inline Ext PropagateNaN2(Ext a, const Dec& da, Ext b, const Dec& db) noexcept
    {
      const bool aNaN = IsNaN(da), bNaN = IsNaN(db);
      const bool aS = da.kind == Kind::SNaN, bS = db.kind == Kind::SNaN;
      if (aS != bS)
      {
        if (aS)
          return bNaN ? b : Quiet(a);
        return aNaN ? a : Quiet(b);
      }
      if (!aNaN)
        return Quiet(b);
      if (!bNaN)
        return Quiet(a);
      if (a.signif < b.signif)
        return Quiet(b);
      if (b.signif < a.signif)
        return Quiet(a);
      return a.signExp < b.signExp ? Quiet(a) : Quiet(b);
    }

    // ======================================================================================
    // Final rounding
    // ======================================================================================

    // Bits [pos, pos + 64) of the little-endian n-limb integer w (pos may be negative).
    inline u64 Extract64(const u64* w, int n, int pos) noexcept
    {
      if (pos >= 64 * n || pos <= -64)
        return 0;
      if (pos < 0)
        return w[0] << (-pos);
      const int li = pos / 64, bi = pos % 64;
      u64 r = w[li] >> bi;
      if (bi != 0 && li + 1 < n)
        r |= w[li + 1] << (64 - bi);
      return r;
    }

    // sig: nl little-endian limbs (2 <= nl <= 4), normalized (top bit set) or zero.
    // value = (-1)^neg * sig * 2^(e - (64 nl - 1)); |true - sig| <= err units of the last bit.
    // Rounds to nearest in the x87 extended format (64-bit significand, denormals on the
    // 2^-16445 grid, overflow to infinity). Returns false when the error interval contains a
    // rounding boundary. err == 0 means exact: a tie then rounds to even.
    bool RoundToExt(bool neg, int e, const u64* w, int nl, u64 err, Ext* out) noexcept
    {
      const u16 sign = neg ? 0x8000 : 0;
      bool zero = true;
      for (int i = 0; i < nl; ++i)
        zero = zero && w[i] == 0;
      if (zero)
      {
        if (err != 0)
          return false;
        *out = Ext{0, sign};
        return true;
      }
      const int total = 64 * nl;
      const bool tiny = e < -16382;
      int drop = total - 64;
      if (tiny)
      {
        const long long extra = -16382LL - e;
        drop = extra >= 65 ? total + 1 : drop + static_cast<int>(extra);
      }
      // rem = sig mod 2^drop, mid = 2^(drop - 1); both in nl + 1 limbs.
      u64 rem[5] = {0, 0, 0, 0, 0};
      for (int i = 0; i < nl; ++i)
        rem[i] = w[i];
      u64 kept = 0;
      if (drop < total)
      {
        kept = Extract64(w, nl, drop);
        const int li = drop / 64, bi = drop % 64;
        rem[li] &= bi != 0 ? ((1ull << bi) - 1) : 0;
        for (int i = li + 1; i < nl; ++i)
          rem[i] = 0;
      }
      u64 mid[5] = {0, 0, 0, 0, 0};
      mid[(drop - 1) / 64] = 1ull << ((drop - 1) % 64);
      // diff = rem - mid (two's complement over nl + 1 limbs)
      u64 diff[5];
      u64 borrow = 0;
      for (int i = 0; i <= nl; ++i)
      {
        const u64 a = rem[i], b = mid[i];
        const u64 t = a - b;
        const u64 b1 = a < b ? 1 : 0;
        diff[i] = t - borrow;
        borrow = b1 | (t < borrow ? 1 : 0);
      }
      const bool below = borrow != 0; // rem < mid
      if (below)
      {
        u64 carry = 1;
        for (int i = 0; i <= nl; ++i)
        {
          diff[i] = ~diff[i] + carry;
          carry = (carry != 0 && diff[i] == 0) ? 1 : 0;
        }
      }
      bool highZero = true;
      for (int i = 1; i <= nl; ++i)
        highZero = highZero && diff[i] == 0;
      bool up;
      if (highZero && diff[0] <= err)
      {
        if (err != 0)
          return false;
        up = (kept & 1) != 0; // exact tie: to even
      }
      else
      {
        up = !below;
      }
      int be;
      if (!tiny)
      {
        be = e + 16383;
        if (up)
        {
          ++kept;
          if (kept == 0)
          {
            kept = 1ull << 63;
            ++be;
          }
        }
        if (be >= 0x7FFF)
        {
          *out = SignedInf(neg);
          return true;
        }
      }
      else
      {
        kept += up ? 1 : 0; // kept < 2^63 before the increment
        be = (kept >> 63) ? 1 : 0;
      }
      *out = Ext{kept, static_cast<u16>(sign | be)};
      return true;
    }

    // ======================================================================================
    // F128: 128-bit floating value with an error bound
    // ======================================================================================

    // value = (-1)^neg * m * 2^(e - 127), m normalized (bit 127 set); |true - m| <= err units
    // of m's last bit. Inputs are never zero where F128 is used.
    struct F128
    {
      u128 m;
      int e;
      bool neg;
      u64 err;
    };

    constexpr u64 kErrSat = 1ull << 62; // "unknown": RoundToExt then always fails

    inline u64 ErrAdd(u64 a, u64 b) noexcept
    {
      const u64 s = a + b;
      return s > kErrSat ? kErrSat : s;
    }

    inline u64 ErrShl(u64 a, int sh) noexcept
    {
      if (a == 0)
        return 0;
      if (sh >= 62 || a > (kErrSat >> sh))
        return kErrSat;
      return a << sh;
    }

    // From Q2.125 with absolute error errQ (units of 2^-125).
    inline F128 FromFixed(i128 v, u64 errQ) noexcept
    {
      F128 r;
      r.neg = v < 0;
      const u128 mag = Abs128(v);
      if (mag == 0) // never expected: make the rounding fail so the slow path decides
        return F128{0, 0, r.neg, kErrSat};
      const int sh = Clz128(mag);
      r.m = mag << sh;
      r.e = 2 - sh;
      r.err = ErrShl(errQ, sh);
      return r;
    }

    // Exact v * 2^scale (v != 0).
    inline F128 FromU128(u128 v, int scale, bool neg) noexcept
    {
      const int sh = Clz128(v);
      return F128{v << sh, scale - sh + 127, neg, 0};
    }

    inline F128 FromDec(const Dec& d) noexcept
    {
      return F128{static_cast<u128>(d.m) << 64, d.e, d.neg, 0};
    }

    // F128 -> Q2.125, truncated: error < 1 q plus the input's error scaled (|value| < 4).
    inline i128 ToFixed(const F128& f) noexcept
    {
      const int sh = 2 - f.e;
      u128 mag;
      if (sh >= 128)
        mag = 0;
      else if (sh <= 0)
        mag = f.m; // |value| >= 4 never happens here
      else
        mag = f.m >> sh;
      const i128 r = static_cast<i128>(mag);
      return f.neg ? -r : r;
    }

    // Product. Error: inputs' relative errors add; the result's last bit is at least
    // 2^-128 relative, so err <= 2 (ea + eb) + 1 (truncation) + 1 (second order).
    inline F128 MulF(const F128& a, const F128& b) noexcept
    {
      const U256 p = Mul128(a.m, b.m);
      F128 r;
      r.neg = a.neg != b.neg;
      if (p.hi >> 127)
      {
        r.m = p.hi;
        r.e = a.e + b.e + 1;
        r.err = ErrAdd(ErrAdd(a.err, b.err), 2);
      }
      else
      {
        r.m = (p.hi << 1) | (p.lo >> 127);
        r.e = a.e + b.e;
        r.err = ErrAdd(ErrShl(ErrAdd(a.err, b.err), 1), 2);
      }
      return r;
    }

    // Q = X / B for X = A * 2^k (k = 127 when A >= B, else 128) and A, B in [2^127, 2^128):
    // Q in [2^127, 2^128). Returns Z with Q - 1 - 2^-35 <= Z <= Q (Z >= 2^127). Only
    // multiplications and one double division:
    //  1. V0 = 2^126 / double(B >> 64) (an integer in (2^62, 2^63]): V0 = (2^190/B)(1 + e0),
    //     |e0| <= 2^-53 (B's rounding) + 2^-53 (division) + 2^-63 (B's low half) < 2^-51.9.
    //  2. One Newton step, exact but for two truncations: E = 2^190 - B V0 (|E| < 2^138.1),
    //     V1 = V0 2^64 + floor(V0 floor(E / 2^76) / 2^50). Then V* - 2^23.3 <= V1 <= V*,
    //     V* = 2^254/B (Newton from below: V* e0^2 <= 2^127 2^-103.8; truncations < 2^13 + 1).
    //  3. Z = floor(A V1 / 2^(254-k)) <= Q, Q - Z <= 2^128 2^23.3 / 2^126 + 1 < 2^25.4.
    //  4. R = X - Z B (exact, 0 <= R < 2^153.5); Q - Z = R/B. The correction
    //     c = floor((R >> 90)(V1 >> 64) / 2^100) is R/B minus at most 1 + 2^-36 + 2^-36 + 2^-77.
    inline u128 DivQ(u128 A, u128 B) noexcept
    {
      const bool shift128 = A < B;
      const u64 Bh = static_cast<u64>(B >> 64);
      const u64 V0 = static_cast<u64>(0x1p126 / static_cast<double>(Bh));
      // E = 2^190 - B V0 over 256 bits (B V0 < 2^191).
      const u128 plo = static_cast<u128>(static_cast<u64>(B)) * V0;
      const u128 phi = static_cast<u128>(Bh) * V0 + (plo >> 64); // B V0 = phi 2^64 + low(plo)
      const u128 Plo = (phi << 64) | static_cast<u64>(plo);      // bits 0..127
      const u128 Phi = phi >> 64;                                 // bits 128..191
      const u128 Elo = u128(0) - Plo;
      const u128 Ehi = (static_cast<u128>(1) << 62) - Phi - (Plo != 0 ? 1 : 0);
      // floor(E / 2^76) fits in 64 bits (|E| < 2^138.1): bits 76..139 of the two's complement.
      const std::int64_t Et = static_cast<std::int64_t>(static_cast<u64>(Elo >> 76) | (static_cast<u64>(Ehi) << 52));
      const i128 corr = (static_cast<i128>(V0) * Et) >> 50;
      const u128 V1 = (static_cast<u128>(V0) << 64) + static_cast<u128>(corr);
      // Z = floor(A V1 / 2^(254 - k)).
      const U256 av = Mul128(A, V1);
      const int zs = shift128 ? 126 : 127;
      u128 Z = (av.hi << (128 - zs)) | (av.lo >> zs);
      // R = X - Z B.
      const u128 Xhi = shift128 ? A : (A >> 1);
      const u128 Xlo = shift128 ? u128(0) : (A << 127);
      const U256 zb = Mul128(Z, B);
      const u128 Rlo = Xlo - zb.lo;
      const u128 Rhi = Xhi - zb.hi - (Xlo < zb.lo ? 1 : 0);
      const u64 Rt = static_cast<u64>(Rlo >> 90) | static_cast<u64>(Rhi << 38); // R < 2^154
      const u128 c = (static_cast<u128>(Rt) * static_cast<u64>(V1 >> 64)) >> 100;
      Z += c;
      if (!(Z >> 127))
        Z = static_cast<u128>(1) << 127;
      return Z;
    }

    // Quotient: |m - true| <= 2 (ea + eb) (relative input errors; the last bit is at least
    // 2^-128 relative) + 1 (DivQ) + 2 (second order, generous).
    inline F128 DivF(const F128& a, const F128& b) noexcept
    {
      F128 r;
      r.neg = a.neg != b.neg;
      r.m = DivQ(a.m, b.m);
      r.e = (a.m >= b.m) ? a.e - b.e : a.e - b.e - 1;
      r.err = ErrAdd(ErrShl(ErrAdd(a.err, b.err), 1), 3);
      return r;
    }

    // ======================================================================================
    // Test hooks
    // ======================================================================================

#if defined(FAF_X87_KERNELS_TESTING)
    inline kernels_testing::Mode TestMode() noexcept
    {
      return static_cast<kernels_testing::Mode>(kernels_testing::g_mode.load(std::memory_order_relaxed));
    }
    inline bool ForceSlow() noexcept { return TestMode() == kernels_testing::Mode::ForceSlow; }
#else
    constexpr bool ForceSlow() noexcept { return false; }
#endif

    // RoundToExt for a normalized 128-bit significand with err > 0 in the normal exponent
    // range (the common case), without loops.
    inline bool RoundF128(const F128& v, bool neg, Ext* out) noexcept
    {
      if (v.e < -16382 || v.e > 16383 || v.err == 0 || !(v.m >> 127))
      {
        const u64 w[2] = {static_cast<u64>(v.m), static_cast<u64>(v.m >> 64)};
        return RoundToExt(neg, v.e, w, 2, v.err, out);
      }
      const u64 lo = static_cast<u64>(v.m);
      const u64 half = 1ull << 63;
      const u64 dist = lo >= half ? lo - half : half - lo;
      if (dist <= v.err)
        return false;
      u64 kept = static_cast<u64>(v.m >> 64) + (lo > half ? 1 : 0);
      int be = v.e + 16383;
      if (kept == 0)
      {
        kept = half;
        ++be;
      }
      if (be >= 0x7FFF)
      {
        *out = SignedInf(neg);
        return true;
      }
      *out = Ext{kept, static_cast<u16>((neg ? 0x8000 : 0) | be)};
      return true;
    }

    // Rounds a fast-path result; false sends the caller to the slow path.
    inline bool FinishFast(const F128& v, bool neg, Ext* out) noexcept
    {
#if defined(FAF_X87_KERNELS_TESTING)
      const u64 w[2] = {static_cast<u64>(v.m), static_cast<u64>(v.m >> 64)};
      kernels_testing::g_last = kernels_testing::FastApprox{true, neg, v.e, w[1], w[0], v.err};
      if (RoundF128(v, neg, out))
        return true;
      kernels_testing::g_fastUndecided.fetch_add(1, std::memory_order_relaxed);
      if (TestMode() == kernels_testing::Mode::FastOnly)
      {
        RoundToExt(neg, v.e, w, 2, 0, out);
        return true;
      }
      return false;
#else
      return RoundF128(v, neg, out);
#endif
    }

    // ======================================================================================
    // Slow path: 256-bit floating arithmetic
    // ======================================================================================

    // value = (-1)^neg * W * 2^(e - 255), W normalized (bit 255 set), or zero.
    // Every operation truncates: relative error < 2^-254 each.
    struct Big
    {
      u64 w[4];
      int e;
      bool neg;
      bool zero;
    };

    inline Big BigZero() noexcept
    {
      Big b{};
      b.zero = true;
      return b;
    }

    inline Big BigFromConst(const BigConst& c) noexcept
    {
      Big b{};
      for (int i = 0; i < 4; ++i)
        b.w[i] = c.w[i];
      b.e = c.e;
      b.zero = c.zero;
      return b;
    }

    // Normalizes the n-limb magnitude L (value L * 2^scale) into a Big, truncating.
    inline Big BigFromLimbs(const u64* l, int n, int scale, bool neg) noexcept
    {
      int top = -1;
      for (int i = n - 1; i >= 0; --i)
      {
        if (l[i] != 0)
        {
          top = 64 * i + 63 - Clz64(l[i]);
          break;
        }
      }
      if (top < 0)
        return BigZero();
      Big b{};
      for (int k = 0; k < 4; ++k)
        b.w[k] = Extract64(l, n, top - 255 + 64 * k);
      b.e = scale + top;
      b.neg = neg;
      b.zero = false;
      return b;
    }

    inline Big BigFromU128(u128 v, int scale, bool neg) noexcept
    {
      const u64 l[2] = {static_cast<u64>(v), static_cast<u64>(v >> 64)};
      return BigFromLimbs(l, 2, scale, neg);
    }

    inline Big BigFromDec(const Dec& d) noexcept { return BigFromU128(d.m, d.e - 63, d.neg); }

    inline Big BigOne() noexcept { return BigFromU128(1, 0, false); }

    inline Big BigNeg(Big a) noexcept
    {
      a.neg = !a.neg;
      return a;
    }

    inline int BigCmpMag(const Big& a, const Big& b) noexcept
    {
      if (a.zero || b.zero)
        return a.zero ? (b.zero ? 0 : -1) : 1;
      if (a.e != b.e)
        return a.e < b.e ? -1 : 1;
      for (int i = 3; i >= 0; --i)
        if (a.w[i] != b.w[i])
          return a.w[i] < b.w[i] ? -1 : 1;
      return 0;
    }

    Big BigMul(const Big& a, const Big& b) noexcept
    {
      if (a.zero || b.zero)
        return BigZero();
      u64 p[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      for (int i = 0; i < 4; ++i)
      {
        u64 carry = 0;
        for (int j = 0; j < 4; ++j)
        {
          const u128 t = static_cast<u128>(a.w[i]) * b.w[j] + p[i + j] + carry;
          p[i + j] = static_cast<u64>(t);
          carry = static_cast<u64>(t >> 64);
        }
        p[i + 4] = carry;
      }
      return BigFromLimbs(p, 8, a.e + b.e - 510, a.neg != b.neg);
    }

    Big BigAdd(const Big& a, const Big& b) noexcept
    {
      if (a.zero)
        return b;
      if (b.zero)
        return a;
      const bool aBig = BigCmpMag(a, b) >= 0;
      const Big& x = aBig ? a : b;
      const Big& y = aBig ? b : a;
      const int de = x.e - y.e;
      // Six limbs: x * 2^64 (one guard limb below, one carry limb above), y aligned to it.
      u64 X[6] = {0, x.w[0], x.w[1], x.w[2], x.w[3], 0};
      u64 Y[6];
      for (int k = 0; k < 6; ++k)
        Y[k] = (de > 64 * 6) ? 0 : Extract64(y.w, 4, de + 64 * (k - 1));
      u64 S[6];
      if (x.neg == y.neg)
      {
        u64 carry = 0;
        for (int k = 0; k < 6; ++k)
        {
          const u128 t = static_cast<u128>(X[k]) + Y[k] + carry;
          S[k] = static_cast<u64>(t);
          carry = static_cast<u64>(t >> 64);
        }
      }
      else
      {
        u64 borrow = 0;
        for (int k = 0; k < 6; ++k)
        {
          const u64 t = X[k] - Y[k];
          const u64 b1 = X[k] < Y[k] ? 1 : 0;
          S[k] = t - borrow;
          borrow = b1 | (t < borrow ? 1 : 0);
        }
      }
      return BigFromLimbs(S, 6, x.e - 255 - 64, x.neg);
    }

    inline Big BigSub(const Big& a, const Big& b) noexcept { return BigAdd(a, BigNeg(b)); }

    // Restoring division: 257 quotient bits.
    Big BigDiv(const Big& a, const Big& b) noexcept
    {
      if (a.zero)
        return BigZero();
      u64 R[5] = {a.w[0], a.w[1], a.w[2], a.w[3], 0};
      const u64 B[5] = {b.w[0], b.w[1], b.w[2], b.w[3], 0};
      u64 Qd[5] = {0, 0, 0, 0, 0};
      for (int i = 256; i >= 0; --i)
      {
        int c = 0;
        for (int k = 4; k >= 0; --k)
        {
          if (R[k] != B[k])
          {
            c = R[k] < B[k] ? -1 : 1;
            break;
          }
        }
        if (c >= 0)
        {
          u64 borrow = 0;
          for (int k = 0; k < 5; ++k)
          {
            const u64 t = R[k] - B[k];
            const u64 b1 = R[k] < B[k] ? 1 : 0;
            R[k] = t - borrow;
            borrow = b1 | (t < borrow ? 1 : 0);
          }
          Qd[i / 64] |= 1ull << (i % 64);
        }
        for (int k = 4; k > 0; --k)
          R[k] = (R[k] << 1) | (R[k - 1] >> 63);
        R[0] <<= 1;
      }
      return BigFromLimbs(Qd, 5, a.e - b.e - 256, a.neg != b.neg);
    }

    // a / d for 0 < d < 2^32 (32-bit digits, 64-bit hardware division).
    Big BigDivSmall(const Big& a, u32 d) noexcept
    {
      if (a.zero)
        return BigZero();
      // Dividend W * 2^64 as 10 half-limbs, quotient likewise.
      u64 Qd[5] = {0, 0, 0, 0, 0};
      u64 r = 0;
      for (int k = 9; k >= 0; --k)
      {
        const u64 h = k >= 2 ? ((a.w[(k - 2) / 2] >> (32 * ((k - 2) % 2))) & 0xFFFFFFFFull) : 0;
        const u64 cur = (r << 32) | h;
        const u64 q = cur / d;
        r = cur - q * d;
        Qd[k / 2] |= q << (32 * (k % 2));
      }
      return BigFromLimbs(Qd, 5, a.e - 255 - 64, a.neg);
    }

    inline Big BigScale2(Big a, int k) noexcept
    {
      a.e += k;
      return a;
    }

    // The slow path's error is below 2^-245 relative (at most a few hundred truncating
    // operations, no cancellation): 2^24 units of 2^-255 leaves a margin of 2^14.
    constexpr u64 kSlowErr = 1ull << 24;

    Ext FinishSlow(const Big& v, bool neg) noexcept
    {
      Ext out;
      if (v.zero)
        return SignedZero(neg);
      if (RoundToExt(neg, v.e, v.w, 4, kSlowErr, &out))
        return out;
      // A near-tie within 2^-231 relative: beyond this implementation's precision.
#if defined(FAF_X87_KERNELS_TESTING)
      kernels_testing::g_slowUndecided.fetch_add(1, std::memory_order_relaxed);
#endif
      RoundToExt(neg, v.e, v.w, 4, 0, &out);
      return out;
    }

    // sin(r), cos(r) for 0 < r <= pi66/4 (Taylor, nested form 1 - w/((2k)(2k+1)) * (...)).
    void SlowSinCos(const Big& r, Big* s, Big* c) noexcept
    {
      const Big one = BigOne();
      const Big w = BigMul(r, r);
      Big S = one;
      for (u32 k = 30; k >= 1; --k)
        S = BigSub(one, BigDivSmall(BigMul(w, S), (2 * k) * (2 * k + 1)));
      *s = BigMul(r, S);
      Big C = one;
      for (u32 k = 30; k >= 1; --k)
        C = BigSub(one, BigDivSmall(BigMul(w, C), (2 * k - 1) * (2 * k)));
      *c = C;
    }

    // atan(t), 0 < t <= 1: atan(k/16) + atan(u), u = (t - k/16) / (1 + t k/16), |u| <= 1/32.
    Big SlowAtanUnit(const Big& t) noexcept
    {
      const Big one = BigOne();
      u32 k = 0;
      if (t.e >= -5)
      {
        const u64 floor32t = t.w[3] >> (58 - t.e); // floor(32 t), t.e <= 0
        k = static_cast<u32>((floor32t + 1) >> 1);
      }
      Big u = t;
      if (k != 0)
      {
        const Big c = BigFromU128(k, -4, false);
        u = BigDiv(BigSub(t, c), BigAdd(one, BigMul(t, c)));
      }
      const Big w = BigMul(u, u);
      constexpr u32 K = 26;
      Big A = BigDivSmall(one, 2 * K + 1);
      for (u32 n = K; n-- > 0;)
        A = BigSub(BigDivSmall(one, 2 * n + 1), BigMul(w, A));
      const Big at = BigMul(u, A);
      return k != 0 ? BigAdd(BigFromConst(kBigAtan16[k]), at) : at;
    }

    // log2(x) for finite positive x that is not a power of two:
    // x = 2^I * m, m in [sqrt(1/2), sqrt(2)), ln m = 2 atanh(s), s = (m - 1)/(m + 1).
    Big SlowLog2(const Dec& x) noexcept
    {
      const Big one = BigOne();
      const u64 M = x.m;
      int I = x.e;
      const bool half = M > 0xB504F333F9DE6484ull; // m > sqrt(2) (to 64 bits)
      u128 numMag, den;
      bool numNeg;
      if (half)
      {
        numMag = (static_cast<u128>(1) << 64) - M;
        numNeg = true;
        den = (static_cast<u128>(1) << 64) + M;
        I += 1;
      }
      else
      {
        numMag = M - (1ull << 63);
        numNeg = false;
        den = static_cast<u128>(M) + (1ull << 63);
      }
      const Big s = BigDiv(BigFromU128(numMag, 0, numNeg), BigFromU128(den, 0, false));
      const Big w = BigMul(s, s);
      constexpr u32 K = 52;
      Big A = BigDivSmall(one, 2 * K + 1);
      for (u32 n = K; n-- > 0;)
        A = BigAdd(BigDivSmall(one, 2 * n + 1), BigMul(w, A));
      const Big lnm = BigScale2(BigMul(s, A), 1);
      const Big log2m = BigMul(lnm, BigFromConst(kBigLog2e));
      if (I == 0)
        return log2m;
      const Big bi = BigFromU128(static_cast<u128>(I < 0 ? -static_cast<long long>(I) : I), 0, I < 0);
      return BigAdd(bi, log2m);
    }

    // expm1(f * ln 2) for |f| <= 1: g * sum g^n/(n+1)!.
    Big SlowExpm1Ln2(const Big& f) noexcept
    {
      if (f.zero)
        return BigZero();
      const Big one = BigOne();
      const Big g = BigMul(f, BigFromConst(kBigLn2));
      Big E = one;
      for (u32 n = 56; n >= 1; --n)
        E = BigAdd(one, BigDivSmall(BigMul(g, E), n + 1));
      return BigMul(g, E);
    }

    // ======================================================================================
    // FSIN / FCOS / FPTAN
    // ======================================================================================

    enum class Trig : unsigned char
    {
      Sin,
      Cos,
      Tan,
    };

    // |r| in Q2.125 (exact, 0 < |r| < pi66/4), quadrant k mod 4, sign of r.
    struct TrigArg
    {
      u128 rq;
      unsigned quadrant;
      bool rneg;
    };

    // Requires a finite d with -33 <= d.e <= 62.
    inline TrigArg ReduceTrig(const Dec& d) noexcept
    {
      TrigArg a{0, 0, false};
      // |x| < pi66/4 needs no reduction: |x| < 1/2, or |x| < 1 and 2X < Cp below.
      if (d.e < -1)
      {
        a.rq = static_cast<u128>(d.m) << (d.e + 62);
        return a;
      }
      // X = |x| in units of 2^-65 (exact: d.e >= -1); Cp = pi66/2 in the same units.
      const u128 X = static_cast<u128>(d.m) << (d.e + 2);
      if (d.e == -1 && 2 * X < kPi66Units)
      {
        a.rq = static_cast<u128>(d.m) << (d.e + 62);
        return a;
      }
      // k0 = floor(X * floor(2^190/Cp) / 2^190) is floor(X/Cp) or one less (X < 2^128).
      const U256 p = Mul128(X, kPi66Recip190);
      u64 k = static_cast<u64>(p.hi >> 62);
      u128 R = X - static_cast<u128>(k) * kPi66Units; // exact: true value in [0, 2 Cp)
      if (R >= kPi66Units)
      {
        R -= kPi66Units;
        ++k;
      }
      // Nearest k (2R == Cp is impossible: Cp has 66 significant bits, X at most 64).
      if (2 * R > kPi66Units)
      {
        R = kPi66Units - R;
        ++k;
        a.rneg = true;
      }
      a.rq = R << 60;
      a.quadrant = static_cast<unsigned>(k & 3);
      return a;
    }

    // Which of sin(|r|), cos(|r|) the result uses, and its sign before x's sign.
    struct TrigSel
    {
      bool useCos;  // Sin/Cos: the value is cos(|r|) (else sin(|r|)); Tan: cos/sin (else sin/cos)
      bool negate;
    };

    inline TrigSel SelectTrig(Trig fn, const TrigArg& a, bool xneg) noexcept
    {
      const unsigned q = a.quadrant;
      TrigSel s{false, false};
      switch (fn)
      {
      case Trig::Sin: // sin r, cos r, -sin r, -cos r
        s.useCos = (q & 1) != 0;
        s.negate = (q == 0) ? a.rneg : (q == 1) ? false : (q == 2) ? !a.rneg : true;
        s.negate = s.negate != xneg;
        break;
      case Trig::Cos: // cos r, -sin r, -cos r, sin r
        s.useCos = (q & 1) == 0;
        s.negate = (q == 0) ? false : (q == 1) ? !a.rneg : (q == 2) ? true : a.rneg;
        break;
      case Trig::Tan: // tan r, -cot r
        s.useCos = (q & 1) != 0;
        s.negate = ((q & 1) ? !a.rneg : a.rneg) != xneg;
        break;
      }
      return s;
    }

    // sin(|r|) and/or cos(|r|) as F128 (positive). rq = |r| in Q2.125, 0 < rq < pi66/4.
    //
    // r = j/64 + d, j = round(64 |r|) in [0, 50], |d| <= 1/128, w = d^2 <= 2^-14 (e(w) < 1 q,
    // d exact), S = SinPoly (< 2.6 q), C = CosPoly (< 2.1 q).
    // j = 0: sin = d * S (relative product), cos = C; stated 6 q each.
    // j >= 1, table values s0 = sin(j/64), c0 = cos(j/64) (0.5 q each):
    //   sin = s0 C + (c0 d) S: (1 + 0.5 + 0.71 * 2.1) + (1 + 1.004 + 2^-7 * 2.6) < 5.1 q,
    //   cos = c0 C - (s0 d) S: (1 + 0.5 + 2.1) + (1 + 1.004 + 2^-7 * 2.6) < 5.7 q; stated 16 q.
    inline void FastSinCos(u128 rq, bool needSin, bool needCos, F128* sinOut, F128* cosOut) noexcept
    {
      const i128 r = static_cast<i128>(rq);
      const int j = static_cast<int>((r + (static_cast<i128>(1) << 118)) >> 119);
      const i128 d = r - (static_cast<i128>(j) << 119);
      const i128 w = MulQ(d, d);
      const i128 w2 = MulQU(w, w);
      const double dd = TailArg(d);
      const double wd = dd * dd;
      if (j == 0)
      {
        if (needSin)
          *sinOut = MulF(FromFixed(d, 0), FromFixed(SinPoly(w, w2, wd), 6));
        if (needCos)
          *cosOut = FromFixed(CosPoly(w, w2, wd), 6);
        return;
      }
      const i128 S = SinPoly(w, w2, wd);
      const i128 C = CosPoly(w, w2, wd);
      if (needSin)
        *sinOut = FromFixed(MulQU(kSinTable[j], C) + MulQ(MulQ(kCosTable[j], d), S), 16);
      if (needCos)
        *cosOut = FromFixed(MulQU(kCosTable[j], C) - MulQ(MulQ(kSinTable[j], d), S), 16);
    }

    Ext SlowTrig(Trig fn, const TrigArg& a, bool xneg) noexcept
    {
      const Big r = BigFromU128(a.rq, -125, false);
      Big s, c;
      SlowSinCos(r, &s, &c);
      const TrigSel sel = SelectTrig(fn, a, xneg);
      Big v;
      if (fn == Trig::Tan)
        v = sel.useCos ? BigDiv(c, s) : BigDiv(s, c);
      else
        v = sel.useCos ? c : s;
      return FinishSlow(v, sel.negate);
    }

    Ext TrigGeneral(Trig fn, const Dec& d) noexcept
    {
      const TrigArg a = ReduceTrig(d);
      if (!ForceSlow())
      {
        const TrigSel sel = SelectTrig(fn, a, d.neg);
        const bool both = fn == Trig::Tan;
        F128 s, c;
        FastSinCos(a.rq, both || !sel.useCos, both || sel.useCos, &s, &c);
        F128 v;
        if (fn == Trig::Tan)
          v = sel.useCos ? DivF(c, s) : DivF(s, c);
        else
          v = sel.useCos ? c : s;
        Ext out;
        if (FinishFast(v, sel.negate, &out))
          return out;
      }
      return SlowTrig(fn, a, d.neg);
    }

    // ======================================================================================
    // FPATAN
    // ======================================================================================

    inline bool MagGreater(const Dec& a, const Dec& b) noexcept
    {
      return a.e != b.e ? a.e > b.e : a.m > b.m;
    }

    // atan2 for finite nonzero y, x. theta = atan(t), t = min/max of |y|, |x| (<= 1);
    // swap (|y| > |x|): pi/2 - theta; x < 0: pi - theta; sign of y.
    //
    // t = j/64 + ..., u = (t - j/64)/(1 + t j/64) = N/D with exact integers N, D (j from a
    // double estimate: |t - j/64| <= 1/128 + 2^-50), |u| <= 2^-7 (+), w = u^2 <= 2^-14 (+).
    // Q(w) = atan(u)/u = AtanPoly (< 2.6 q).
    // u: DivF (err <= 3 lsb), uq = ToFixed(u): error <= 1 q + tiny.
    // j = 0, no swap, x > 0: result u * Q (relative). Otherwise theta in fixed point:
    //   atan(j/64) (0.5) + uq Q (1 + 1.01 + 0.02) + pi/2 (0.5) + pi (0.5) < 3.6 q.
    // Stated bounds are twice or more the analysis: Q 6 q, theta 16 q.
    Ext AtanGeneral(const Dec& dy, const Dec& dx) noexcept
    {
      const bool swap = MagGreater(dy, dx);
      const Dec& num = swap ? dx : dy;
      const Dec& den = swap ? dy : dx;
      const int delta = den.e - num.e; // >= 0
      if (!ForceSlow())
      {
        int j = 0;
        if (delta <= 8)
        {
          const double td = static_cast<double>(num.m) / static_cast<double>(den.m) * Pow2(-delta);
          j = static_cast<int>(td * 64.0 + 0.5);
        }
        F128 u;
        bool uzero = false;
        if (j == 0)
        {
          u = DivF(F128{static_cast<u128>(num.m) << 64, num.e, false, 0},
                   F128{static_cast<u128>(den.m) << 64, den.e, false, 0});
        }
        else
        {
          const u128 mn = num.m, md = static_cast<u128>(den.m) << delta;
          const i128 N = static_cast<i128>(64 * mn) - static_cast<i128>(static_cast<u128>(j) * md);
          const u128 D = 64 * md + static_cast<u128>(j) * mn;
          if (N == 0)
            uzero = true;
          else
            u = DivF(FromU128(Abs128(N), 0, N < 0), FromU128(D, 0, false));
        }
        const i128 uq = uzero ? 0 : ToFixed(u);
        const i128 w = MulQ(uq, uq);
        const double ud = TailArg(uq);
        const i128 Qv = AtanPoly(w, MulQU(w, w), ud * ud);

        F128 v;
        if (j == 0 && !swap && !dx.neg)
        {
          v = MulF(u, FromFixed(Qv, 6));
        }
        else
        {
          i128 th = kAtanTable[j] + MulQ(uq, Qv);
          if (swap)
            th = kPiOver2Q - th;
          if (dx.neg)
            th = kPiQ - th;
          v = FromFixed(th, 16);
        }
        Ext out;
        if (FinishFast(v, dy.neg, &out))
          return out;
      }
      // Slow path.
      const Big t = BigDiv(BigFromDec(Dec{Kind::Finite, false, num.e, num.m}),
                           BigFromDec(Dec{Kind::Finite, false, den.e, den.m}));
      Big th = SlowAtanUnit(t);
      if (swap)
        th = BigSub(BigFromConst(kBigPiOver2), th);
      if (dx.neg)
        th = BigSub(BigFromConst(kBigPi), th);
      return FinishSlow(th, dy.neg);
    }

    // ======================================================================================
    // FYL2X
    // ======================================================================================

    // y * log2(x) for finite nonzero y and finite positive x that is not a power of two.
    //
    // x = 2^E * m, m in [1, 2); idx = the 8 bits after the leading one.
    //   idx = 0:   t = m - 1 in (0, 2^-8), I = E.
    //   idx = 255: t = m/2 - 1 in [-2^-9, 0), I = E + 1.
    //   else:      t = m * inv - 1 (exact, |t| < 2^-9), I = E, l = -log2(inv) (table, 0.5 q).
    // log2(x) = I + l + log2e * t * P(t), P(t) = ln(1+t)/t = LogPoly (< 3.1 q).
    // I = 0 with idx 0/255: relative product t * P * log2e. Otherwise F = l + log2e*(t P):
    //   (t P: 1 + 2^-8 * 3.1) -> 1 + 1.4427 * 1.02 + 0.5 * 2^-8 + 0.5 (l) < 3 q; then I + F in a
    //   fixed point scaled by 2^-s (s = bit length of |I|): +1 unit of truncation, bound 4.
    //   |I + F| >= 2^-8.5 whenever this branch is taken (no catastrophic cancellation).
    // Stated bounds are twice the analysis or more: P 8 q, F and I + F 8 units.
    Ext Yl2xGeneral(const Dec& dy, const Dec& dx) noexcept
    {
      if (!ForceSlow())
      {
        const u64 M = dx.m;
        const int idx = static_cast<int>((M >> 55) & 0xFF);
        i128 tq;
        i128 lq = 0;
        int I = dx.e;
        if (idx == 0)
        {
          tq = static_cast<i128>(M - (1ull << 63)) << 62;
        }
        else if (idx == 255)
        {
          tq = -(static_cast<i128>(u64(0) - M) << 61);
          I += 1;
        }
        else
        {
          tq = static_cast<i128>((static_cast<u128>(M) * kLogInv[idx]) << 38) - (static_cast<i128>(1) << 125);
          lq = kLogMinusLog2Inv[idx];
        }
        const i128 P = LogPoly(tq, TailArg(tq));

        F128 L;
        if (I == 0 && lq == 0)
        {
          L = MulF(MulF(FromFixed(tq, 0), FromFixed(P, 8)), F128{kLog2eFM, kLog2eFE, false, 1});
        }
        else
        {
          const i128 F = lq + MulQ(MulQ(tq, P), kLog2eQ);
          if (I == 0)
          {
            L = FromFixed(F, 8);
          }
          else
          {
            const unsigned mag = static_cast<unsigned>(I < 0 ? -I : I);
            const int s = 32 - __builtin_clz(mag);
            const i128 Lq = (static_cast<i128>(I) << (125 - s)) + (F >> s);
            L = FromFixed(Lq, 8);
            L.e += s;
          }
        }
        const F128 v = MulF(FromDec(dy), L);
        Ext out;
        if (FinishFast(v, v.neg, &out))
          return out;
      }
      const Big v = BigMul(BigFromDec(dy), SlowLog2(dx));
      return FinishSlow(v, v.neg);
    }

    // ======================================================================================
    // F2XM1
    // ======================================================================================

    // 2^x - 1 outside [-1, 1] (undefined on the hardware; the game never goes there): the
    // true value, as 2^n (1 + expm1(f ln 2)) - 1 with n = trunc(x).
    Ext F2xm1Outside(const Dec& d) noexcept
    {
      if (d.e >= 14) // |x| >= 16384
        return d.neg ? WithSign(kOne, true) : SignedInf(false);
      if (d.neg && d.e >= 7) // x <= -128: 2^x - 1 rounds to -1
        return WithSign(kOne, true);
      const int fb = 63 - d.e; // fraction bits
      const u64 n = d.m >> fb;
      const u64 fbits = d.m & ((1ull << fb) - 1);
      const Big one = BigOne();
      const Big em = fbits ? SlowExpm1Ln2(BigFromU128(fbits, -fb, d.neg)) : BigZero();
      const Big p = BigScale2(BigAdd(one, em), d.neg ? -static_cast<int>(n) : static_cast<int>(n));
      const Big v = BigSub(p, one);
      return FinishSlow(v, v.neg);
    }

    // 2^x - 1 for finite nonzero |x| <= 1.
    //
    // |x| < 2^-7 (j = 0): g = x ln2 (F128, err 4), gq = ToFixed(g); expm1 = g * E(gq).
    // Else x = j/64 + f (exact, |f| <= 1/128), g = f ln2 in fixed point (1.01 q),
    //   2^x - 1 = (T_j - 1) + T_j * expm1(g), T_j = 2^(j/64) (table, 0.5 q).
    // E(g) = expm1(g)/g = ExpPoly (< 3.3 q).
    // expm1 = g E: 1 + 2^-7.5 * 3.3 + 1.01 * 1.01 < 2.06; result: 0.5 + 1 + 0.01 + 2 * 2.06
    //   < 5.7 q. Stated bounds: E 8 q (j = 0), result 32 q;
    //   |result| >= 2^(1/128) - 1 > 2^-7.6 in this branch.
    Ext F2xm1General(const Dec& d) noexcept
    {
      if (!ForceSlow())
      {
        F128 v;
        if (d.e <= -8)
        {
          const F128 g = MulF(FromDec(d), F128{kLn2FM, kLn2FE, false, 1});
          const i128 gq = ToFixed(g);
          v = MulF(g, FromFixed(ExpPoly(gq, TailArg(gq)), 8));
        }
        else
        {
          const i128 mag = static_cast<i128>(static_cast<u128>(d.m) << (d.e + 62));
          const i128 xq = d.neg ? -mag : mag;
          const int j = static_cast<int>((xq + (static_cast<i128>(1) << 118)) >> 119);
          const i128 f = xq - (static_cast<i128>(j) << 119);
          const i128 gq = MulQ(f, kLn2Q);
          const i128 em1 = MulQ(gq, ExpPoly(gq, TailArg(f) * 0x1.62e42fefa39efp-1));
          const i128 T = kExp2Table[j + 64];
          const i128 res = (T - (static_cast<i128>(1) << 125)) + MulQ(T, em1);
          v = FromFixed(res, 32);
        }
        Ext out;
        if (FinishFast(v, v.neg, &out))
          return out;
      }
      const Big v = SlowExpm1Ln2(BigFromDec(d));
      return FinishSlow(v, v.neg);
    }
  } // namespace
} // namespace faf_posix::x87::kernels_detail

namespace faf_posix::x87
{
  using namespace kernels_detail;

  Ext Fsin(Ext x, bool* outOfRange) noexcept
  {
    if (outOfRange)
      *outOfRange = false;
    const Dec d = Decode(x);
    switch (d.kind)
    {
    case Kind::Zero:
    case Kind::QNaN:
      return x;
    case Kind::SNaN:
      return Quiet(x);
    case Kind::Inf:
    case Kind::Invalid:
      return kIndefinite;
    case Kind::Finite:
      break;
    }
    if (d.e >= 63)
    {
      if (outOfRange)
        *outOfRange = true;
      return x;
    }
    if (d.e <= -34) // |x| < 2^-33: sin x rounds to x
      return Canonical(x);
    return TrigGeneral(Trig::Sin, d);
  }

  Ext Fcos(Ext x, bool* outOfRange) noexcept
  {
    if (outOfRange)
      *outOfRange = false;
    const Dec d = Decode(x);
    switch (d.kind)
    {
    case Kind::Zero:
      return kOne;
    case Kind::QNaN:
      return x;
    case Kind::SNaN:
      return Quiet(x);
    case Kind::Inf:
    case Kind::Invalid:
      return kIndefinite;
    case Kind::Finite:
      break;
    }
    if (d.e >= 63)
    {
      if (outOfRange)
        *outOfRange = true;
      return x;
    }
    if (d.e <= -34) // 1 - x^2/2 rounds to 1
      return kOne;
    return TrigGeneral(Trig::Cos, d);
  }

  bool Fptan(Ext x, Ext* tangent) noexcept
  {
    const Dec d = Decode(x);
    switch (d.kind)
    {
    case Kind::Zero:
    case Kind::QNaN:
      *tangent = x;
      return true;
    case Kind::SNaN:
      *tangent = Quiet(x);
      return true;
    case Kind::Inf: // #IA, masked: the indefinite (then 1.0 is pushed)
    case Kind::Invalid:
      *tangent = kIndefinite;
      return true;
    case Kind::Finite:
      break;
    }
    if (d.e >= 63)
    {
      *tangent = x;
      return false;
    }
    if (d.e <= -34) // tan x rounds to x
    {
      *tangent = Canonical(x);
      return true;
    }
    *tangent = TrigGeneral(Trig::Tan, d);
    return true;
  }

  Ext Fpatan(Ext y, Ext x) noexcept
  {
    const Dec dy = Decode(y), dx = Decode(x);
    if (dy.kind == Kind::Invalid || dx.kind == Kind::Invalid)
      return kIndefinite;
    if (IsNaN(dy) || IsNaN(dx))
      return PropagateNaN2(y, dy, x, dx);
    // Intel's table (SDM FPATAN): no invalid case; 0/0 and inf/inf have defined values.
    if (dy.kind == Kind::Zero)
      return dx.neg ? WithSign(kPi, dy.neg) : SignedZero(dy.neg);
    if (dy.kind == Kind::Inf)
    {
      if (dx.kind == Kind::Inf)
        return WithSign(dx.neg ? kExt3PiOver4 : kPiOver4Ext, dy.neg);
      return WithSign(kPiOver2Ext, dy.neg);
    }
    if (dx.kind == Kind::Zero)
      return WithSign(kPiOver2Ext, dy.neg);
    if (dx.kind == Kind::Inf)
      return dx.neg ? WithSign(kPi, dy.neg) : SignedZero(dy.neg);
    return AtanGeneral(dy, dx);
  }

  Ext Fyl2x(Ext y, Ext x) noexcept
  {
    const Dec dy = Decode(y), dx = Decode(x);
    if (dy.kind == Kind::Invalid || dx.kind == Kind::Invalid)
      return kIndefinite;
    if (IsNaN(dy) || IsNaN(dx))
      return PropagateNaN2(y, dy, x, dx);
    // Intel's table (SDM FYL2X).
    if (dx.neg && dx.kind != Kind::Zero) // x < 0 (-F, -inf)
      return kIndefinite;
    if (dx.kind == Kind::Zero) // log2(+-0) = -inf: 0 * inf invalid, else divide-by-zero
      return dy.kind == Kind::Zero ? kIndefinite : SignedInf(!dy.neg);
    if (dx.kind == Kind::Inf)
      return dy.kind == Kind::Zero ? kIndefinite : SignedInf(dy.neg);
    // x finite positive
    const bool xIsPow2 = dx.m == (1ull << 63);
    if (xIsPow2 && dx.e == 0) // x = 1: +-0 with y's sign; inf * 0 invalid
      return dy.kind == Kind::Inf ? kIndefinite : SignedZero(dy.neg);
    const bool logNeg = dx.e < 0; // x < 1
    if (dy.kind == Kind::Inf)
      return SignedInf(dy.neg != logNeg);
    if (dy.kind == Kind::Zero)
      return SignedZero(dy.neg != logNeg);
    if (xIsPow2) // y * E exactly, one rounding (ties to even)
    {
      const unsigned E = static_cast<unsigned>(logNeg ? -dx.e : dx.e);
      const u128 prod = static_cast<u128>(dy.m) * E;
      const F128 v = FromU128(prod, dy.e - 63, dy.neg != logNeg);
      const u64 w[2] = {static_cast<u64>(v.m), static_cast<u64>(v.m >> 64)};
      Ext out;
      RoundToExt(v.neg, v.e, w, 2, 0, &out);
      return out;
    }
    return Yl2xGeneral(dy, dx);
  }

  Ext F2xm1(Ext x) noexcept
  {
    const Dec d = Decode(x);
    switch (d.kind)
    {
    case Kind::Zero:
    case Kind::QNaN:
      return x;
    case Kind::SNaN:
      return Quiet(x);
    case Kind::Invalid:
      return kIndefinite;
    case Kind::Inf: // outside the documented domain: the limits
      return d.neg ? WithSign(kOne, true) : x;
    case Kind::Finite:
      break;
    }
    if (d.e >= 0)
    {
      if (d.m == (1ull << 63) && d.e == 0) // x = +-1: exactly 1 or -1/2
        return d.neg ? Ext{0x8000000000000000ull, 0xBFFE} : kOne;
      return F2xm1Outside(d);
    }
    return F2xm1General(d);
  }
} // namespace faf_posix::x87
