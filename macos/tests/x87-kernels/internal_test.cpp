// Unit tests of X87Kernels.cpp internals (the TU is included so its anonymous namespace is
// visible): the multiply-only 128-bit division against an exact Knuth division, the final
// rounding against an exact reference, and every fast polynomial's absolute error against
// the 256-bit slow-path arithmetic, compared with the bound stated in the source.
//   internal_test [iterations]

#include "../../../src/platform/posix/math/X87Kernels.cpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace faf_posix::x87;
using namespace faf_posix::x87::kernels_detail;

namespace
{
  // ---- exact reference division (Knuth D, 64-bit digits) ----
  u64 DivLu(u64 u1, u64 u0, u64 v)
  {
    const u64 b = 1ull << 32;
    const u64 vn1 = v >> 32, vn0 = v & 0xFFFFFFFFull;
    const u64 un1 = u0 >> 32, un0 = u0 & 0xFFFFFFFFull;
    u64 q1 = u1 / vn1, rhat = u1 - q1 * vn1;
    while (q1 >= b || q1 * vn0 > ((rhat << 32) | un1))
    {
      --q1;
      rhat += vn1;
      if (rhat >= b)
        break;
    }
    const u64 un21 = (u1 << 32) + un1 - q1 * v;
    u64 q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= b || q0 * vn0 > ((rhat << 32) | un0))
    {
      --q0;
      rhat += vn1;
      if (rhat >= b)
        break;
    }
    return (q1 << 32) | q0;
  }

  u128 Div256Ref(u128 nhi, u128 nlo, u128 d) // floor((nhi:nlo)/d), nhi < d, d normalized
  {
    const u64 d1 = static_cast<u64>(d >> 64), d0 = static_cast<u64>(d);
    const u64 n[2] = {static_cast<u64>(nlo), static_cast<u64>(nlo >> 64)};
    u64 r2 = static_cast<u64>(nhi >> 64), r1 = static_cast<u64>(nhi);
    u64 q[2];
    for (int i = 1; i >= 0; --i)
    {
      const u64 r0 = n[i];
      u64 qhat = (r2 >= d1) ? ~0ull : DivLu(r2, r1, d1);
      const u128 plo = static_cast<u128>(qhat) * d0, phi = static_cast<u128>(qhat) * d1;
      u64 p0 = static_cast<u64>(plo);
      const u128 pm = (plo >> 64) + static_cast<u64>(phi);
      u64 p1 = static_cast<u64>(pm);
      u64 p2 = static_cast<u64>(phi >> 64) + static_cast<u64>(pm >> 64);
      while (p2 > r2 || (p2 == r2 && (p1 > r1 || (p1 == r1 && p0 > r0))))
      {
        --qhat;
        const u128 low = (static_cast<u128>(p1) << 64) | p0;
        const u128 nl = low - d;
        p2 -= (low < d) ? 1 : 0;
        p0 = static_cast<u64>(nl);
        p1 = static_cast<u64>(nl >> 64);
      }
      const u128 rem = ((static_cast<u128>(r1) << 64) | r0) - ((static_cast<u128>(p1) << 64) | p0);
      q[i] = qhat;
      r2 = static_cast<u64>(rem >> 64);
      r1 = static_cast<u64>(rem);
    }
    return (static_cast<u128>(q[1]) << 64) | q[0];
  }

  int failures = 0;
  void Fail(const char* what)
  {
    if (++failures <= 20)
      std::printf("  FAIL %s\n", what);
  }

  u128 R128(std::mt19937_64& rng) { return (static_cast<u128>(rng()) << 64) | rng(); }

  void TestDiv(long iters)
  {
    std::mt19937_64 rng(42);
    long n = 0, exact = 0, low1 = 0;
    auto one = [&](u128 A, u128 B) {
      A |= static_cast<u128>(1) << 127;
      B |= static_cast<u128>(1) << 127;
      const bool s = A < B;
      const u128 ref = s ? Div256Ref(A, 0, B) : Div256Ref(A >> 1, A << 127, B);
      const u128 z = DivQ(A, B);
      ++n;
      if (z == ref)
        ++exact;
      else if (z + 1 == ref)
        ++low1;
      else
        Fail("DivQ outside [floor(Q) - 1, floor(Q)]");
    };
    const u128 top = static_cast<u128>(1) << 127, all = ~static_cast<u128>(0);
    const u128 edges[] = {top, top + 1, all, all - 1, top | (all >> 64), top | (static_cast<u128>(~0ull) << 64),
                          (top | (all >> 2)), top + (static_cast<u128>(1) << 64), top + (static_cast<u128>(1) << 64) - 1};
    for (u128 a : edges)
      for (u128 b : edges)
        one(a, b);
    for (long i = 0; i < iters; ++i)
    {
      u128 a = R128(rng), b = R128(rng);
      switch (i & 7)
      {
      case 1: b = a ^ (rng() & 0xFF); break;                          // A ~ B
      case 2: b = (static_cast<u128>(rng()) << 64); break;             // B low half 0
      case 3: b = (static_cast<u128>(rng()) << 64) | ~0ull; break;     // B low half all ones
      case 4: a = static_cast<u128>(1) << 127; break;
      case 5: b = ~static_cast<u128>(0) - (rng() & 0xFFFF); break;     // B near 2^128
      case 6: b = (static_cast<u128>(1) << 127) + (rng() & 0xFFFF); break; // B near 2^127
      default: break;
      }
      one(a, b);
    }
    std::printf("DivQ: %ld cases, %ld exact floor, %ld one below, %d failures\n", n, exact, low1, failures);
  }

  // RoundToExt on a two-limb significand versus a direct exact rounding.
  void TestRound(long iters)
  {
    std::mt19937_64 rng(7);
    long n = 0, undecided = 0;
    for (long i = 0; i < iters; ++i)
    {
      u128 m = R128(rng) | (static_cast<u128>(1) << 127);
      if ((i & 3) == 1) // force ties and near-ties
        m = (m & ~((static_cast<u128>(1) << 64) - 1)) | (static_cast<u128>(1) << 63) | (rng() & 3);
      if ((i & 3) == 2)
        m = (m & ~((static_cast<u128>(1) << 64) - 1)) | ((static_cast<u128>(1) << 63) - (rng() & 3));
      const int e = (i & 15) == 0 ? -16382 - static_cast<int>(rng() % 70) : static_cast<int>(rng() % 200) - 100;
      const u64 err = (i & 1) ? 0 : (rng() & 7);
      const bool neg = rng() & 1;
      const u64 w[2] = {static_cast<u64>(m), static_cast<u64>(m >> 64)};
      Ext out;
      const bool ok = RoundToExt(neg, e, w, 2, err, &out);
      // reference: drop = 64 + max(0, -16382 - e)
      const int drop = 64 + (e < -16382 ? -16382 - e : 0);
      u128 kept = drop >= 128 ? 0 : (m >> drop);
      const u128 rem = drop >= 128 ? m : (m & ((static_cast<u128>(1) << drop) - 1));
      const u128 half = static_cast<u128>(1) << (drop - 1); // drop <= 129 here: guard
      bool decided = true, up;
      if (drop > 128)
      {
        up = false;
        decided = true;
      }
      else
      {
        const u128 dist = rem >= half ? rem - half : half - rem;
        if (dist <= err)
        {
          if (err != 0)
            decided = false;
          up = (kept & 1) != 0;
        }
        else
          up = rem > half;
      }
      ++n;
      if (!decided)
      {
        ++undecided;
        if (ok)
          Fail("RoundToExt decided inside its error interval");
        continue;
      }
      if (!ok)
      {
        Fail("RoundToExt undecided outside its error interval");
        continue;
      }
      if (up)
        ++kept;
      int be;
      if (e >= -16382)
      {
        be = e + 16383;
        if (kept >> 64)
        {
          kept >>= 1;
          ++be;
        }
      }
      else
        be = (kept >> 63) ? 1 : 0;
      const Ext want{static_cast<u64>(kept), static_cast<std::uint16_t>((neg ? 0x8000 : 0) | be)};
      if (!(want == out))
        Fail("RoundToExt result");
    }
    std::printf("RoundToExt: %ld cases (%ld undecided by design), %d failures so far\n", n, undecided, failures);
  }

  // |fixed - Big| in q = 2^-125 units.
  double ErrQ(i128 v, const Big& ref)
  {
    const Big b = BigSub(BigFromU128(Abs128(v), -125, v < 0), ref);
    if (b.zero)
      return 0;
    return std::ldexp(static_cast<double>(b.w[3]), b.e - 63 + 125);
  }

  Big BigFromFixed(i128 v) { return v == 0 ? BigZero() : BigFromU128(Abs128(v), -125, v < 0); }

  void TestPolys(long iters)
  {
    std::mt19937_64 rng(99);
    const Big one = BigOne();
    double mS = 0, mC = 0, mA = 0, mL = 0, mE = 0, mW = 0;
    const i128 dMax = static_cast<i128>(1) << 118; // 1/128
    for (long i = 0; i < iters; ++i)
    {
      // d in [-1/128, 1/128] (exact), w = MulQ(d, d)
      i128 d = static_cast<i128>(R128(rng) % static_cast<u128>(2 * dMax + 1)) - dMax;
      if ((i & 7) == 0)
        d = (i & 8) ? dMax : -dMax;
      if (d == 0)
        d = 1;
      const i128 w = MulQ(d, d);
      const i128 w2 = MulQU(w, w);
      const double wd = TailArg(d) * TailArg(d);
      const Big bd = BigFromFixed(d);
      Big s, c;
      SlowSinCos(BigFromFixed(d < 0 ? -d : d), &s, &c);
      const Big sref = BigDiv(s, BigFromFixed(d < 0 ? -d : d)); // sin(d)/d
      mS = std::fmax(mS, ErrQ(SinPoly(w, w2, wd), sref));
      mC = std::fmax(mC, ErrQ(CosPoly(w, w2, wd), c));
      mW = std::fmax(mW, ErrQ(w, BigMul(bd, bd)));
      // atan: u = d (as if from ToFixed), Q = atan(u)/u via SlowAtanUnit
      const Big ua = BigFromFixed(d < 0 ? -d : d);
      const Big aref = BigDiv(SlowAtanUnit(ua), ua);
      mA = std::fmax(mA, ErrQ(AtanPoly(w, w2, wd), aref));
      // log: t in [-2^-8, 2^-8] with 1 + t exact in 64 bits (lsb 2^-63);
      // ref ln(1+t)/t = log2(1+t) ln2 / t
      i128 t = (static_cast<i128>(static_cast<std::int64_t>(rng() >> 8)) >> 1) << 62; // |t| < 2^-8
      if ((i & 7) == 1)
        t = (i & 8) ? (static_cast<i128>(1) << 117) : -(static_cast<i128>(1) << 117);
      if (t == 0)
        t = static_cast<i128>(1) << 62;
      {
        // ln(1+t) via the slow path: x = 1 + t as a Dec (t has <= 125 fraction bits; use
        // the top 64 significant bits only when exact)
        const i128 onePlus = (static_cast<i128>(1) << 125) + t;
        const u128 mag = static_cast<u128>(onePlus);
        const int sh = Clz128(mag);
        const u128 norm = mag << sh;
        if (static_cast<u64>(norm) == 0) // exactly representable with a 64-bit significand
        {
          const Dec x{Kind::Finite, false, 2 - sh, static_cast<u64>(norm >> 64)};
          const Big l2 = SlowLog2(x);
          const Big lnref = BigDiv(BigMul(l2, BigFromConst(kBigLn2)), BigFromFixed(t));
          mL = std::fmax(mL, ErrQ(LogPoly(t, TailArg(t)), lnref));
        }
      }
      // expm1: g in [-2^-7.5, 2^-7.5]: g = d * 0.7
      const i128 g = MulQ(d, kLn2Q);
      {
        const Big bg = BigFromFixed(g);
        const Big f = BigDiv(bg, BigFromConst(kBigLn2)); // expm1(f ln2) = expm1(g)
        const Big eref = BigDiv(SlowExpm1Ln2(f), bg);
        mE = std::fmax(mE, ErrQ(ExpPoly(g, TailArg(g)), eref));
      }
      (void)one;
    }
    std::printf("polynomials (max |error| in q = 2^-125; analysis bound / stated bound):\n");
    std::printf("  w = d^2   %.3f  (1 / 1)\n", mW);
    std::printf("  SinPoly   %.3f  (2.6 / 6)\n", mS);
    std::printf("  CosPoly   %.3f  (2.1 / 6)\n", mC);
    std::printf("  AtanPoly  %.3f  (2.6 / 6)\n", mA);
    std::printf("  LogPoly   %.3f  (3.1 / 8)  [exact-t cases only]\n", mL);
    std::printf("  ExpPoly   %.3f  (3.3 / 8)  [g exact here, e(g) = 0]\n", mE);
    if (mW > 1 || mS > 2.6 || mC > 2.1 || mA > 2.6 || mL > 3.1 || mE > 3.3)
      Fail("polynomial error above the analysis bound");
  }
} // namespace

int main(int argc, char** argv)
{
  const long iters = argc > 1 ? std::atol(argv[1]) : 2000000;
  TestDiv(iters * 5);
  TestRound(iters);
  TestPolys(iters / 20);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
