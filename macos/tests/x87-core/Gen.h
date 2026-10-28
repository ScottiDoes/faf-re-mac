#pragma once

// Input generators: extended operands for the Layer 1 vs SoftFloat tests, and a port of
// the difffuzz float generators (tools/difffuzz/src/gen.cpp) for the routine tests.

#include <cmath>

#include "TestCommon.h"

namespace x87t
{
  using core::Pack;

  // An odd k-bit number with its top bit set (k in [1, 64]).
  [[nodiscard]] inline std::uint64_t RandomOdd(Rng& r, const int k) noexcept
  {
    if (k <= 1) {
      return 1;
    }
    return (r.Next() >> (64 - k)) | (1ull << (k - 1)) | 1ull;
  }

  // m * 2^e2 for m != 0, if that is a normal extended value.
  [[nodiscard]] inline bool ScaledInt(const bool sign, const std::uint64_t m, const std::int32_t e2, Ext& out) noexcept
  {
    const int shift = std::countl_zero(m);
    const std::int32_t exp = kExtBias + 63 - shift + e2;
    if (exp < 1 || exp > 0x7FFE) {
      return false;
    }
    out = Pack(sign, exp, m << shift);
    return true;
  }

  [[nodiscard]] inline std::int32_t ClampExp(const std::int32_t e) noexcept
  {
    return e < 1 ? 1 : (e > 0x7FFE ? 0x7FFE : e);
  }

  // A significand from a mix of shapes: random, short (k bits), all-ones runs, and patterns
  // that sit exactly at or next to a rounding tie for P24 / P53 / P64.
  [[nodiscard]] inline std::uint64_t RandomSignificand(Rng& r) noexcept
  {
    constexpr std::uint64_t kTop = core::kIntegerBit;
    switch (r.Below(8)) {
    case 0:
    case 1:
      return r.Next() | kTop;
    case 2: {
      const int k = 1 + static_cast<int>(r.Below(64));
      return RandomOdd(r, k) << (64 - k);
    }
    case 3:
      return ~0ull << r.Below(64);
    case 4: {
      // Exactly half an ulp at a precision width, optionally +-1 at the bottom.
      static constexpr int kWidths[] = {24, 53, 64, 23, 25, 52, 54};
      const int width = kWidths[r.Below(7)];
      std::uint64_t sig = (r.Next() | kTop) & (width >= 64 ? ~0ull : ~((1ull << (64 - width)) - 1));
      if (width < 64) {
        sig |= 1ull << (63 - width);
      }
      const std::uint32_t tweak = r.Below(3);
      return tweak == 0 ? sig : (tweak == 1 ? sig + 1 : sig - 1) | kTop;
    }
    case 5:
      return kTop | (r.Next() >> (1 + r.Below(63)));
    case 6:
      return kTop;
    default:
      return (r.Next() | kTop) & ~((1ull << (r.Below(64))) - 1);
    }
  }

  [[nodiscard]] inline Ext RandomSpecial(Rng& r) noexcept
  {
    const bool s = r.Below(2) != 0;
    switch (r.Below(6)) {
    case 0:
      return Pack(s, 0, 0);
    case 1:
      return Pack(s, 0x7FFF, core::kIntegerBit);
    case 2:
      return Pack(s, 0x7FFF, 0xC000000000000000ull | (r.Next() >> r.Below(64) >> 2));
    case 3:
      return Pack(s, 0x7FFF, 0x8000000000000000ull | ((r.Next() >> 2 >> r.Below(62)) | 1ull));
    case 4:
      return kIndefinite;
    default:
      return Pack(s, 0x7FFF, 0xC000000000000000ull);
    }
  }

  // One extended operand: F24 / F53 / F64 classes over the whole range, denormals of every
  // format, near-overflow and near-underflow exponents, specials, small integers.
  [[nodiscard]] inline Ext RandomExt(Rng& r) noexcept
  {
    const bool s = r.Below(2) != 0;
    switch (r.Below(20)) {
    case 0:
    case 1:
    case 2: {
      const std::uint32_t bits = (r.U32() & 0x807FFFFFu) | ((1u + r.Below(254)) << 23);
      return FromFloat(FFromBits(bits));
    }
    case 3:
      return FromFloat(FFromBits((r.U32() & 0x807FFFFFu) | 1u));
    case 4:
    case 5: {
      const std::uint64_t bits = (r.Next() & 0x800FFFFFFFFFFFFFull) | (static_cast<std::uint64_t>(1 + r.Below(2046)) << 52);
      return FromDouble(DFromBits(bits));
    }
    case 6:
      return FromDouble(DFromBits((r.Next() & 0x800FFFFFFFFFFFFFull) | 1ull));
    case 7:
    case 8:
    case 9:
      return Pack(s, kExtBias + r.Between(-80, 80), RandomSignificand(r));
    case 10:
      return Pack(s, 1 + static_cast<std::int32_t>(r.Below(0x7FFE)), RandomSignificand(r));
    case 11:
      return Pack(s, 0x7FFE - static_cast<std::int32_t>(r.Below(16)), RandomSignificand(r));
    case 12:
      return Pack(s, 1 + static_cast<std::int32_t>(r.Below(70)), RandomSignificand(r));
    case 13:
      return Pack(s, 0, (r.Next() >> (1 + r.Below(63))) | 1ull);
    case 14:
      return RandomSpecial(r);
    case 15:
      return FromInt32(r.Between(-40, 40));
    case 16:
    case 17: {
      // Short significands at moderate exponents: products and sums land on ties.
      const int k = 1 + static_cast<int>(r.Below(r.Chance(50) ? 26 : 64));
      return Pack(s, kExtBias + r.Between(-40, 40), RandomOdd(r, k) << (64 - k));
    }
    default:
      return Pack(s, kExtBias + r.Between(-200, 200), RandomSignificand(r));
    }
  }

  enum class Op
  {
    Add,
    Sub,
    Mul,
    Div,
    Sqrt,
  };

  // Ties for the operation at precision p, built exactly.
  [[nodiscard]] inline bool TiePair(Rng& r, const Op op, const Precision p, Ext& a, Ext& b) noexcept
  {
    const int width = static_cast<int>(p);
    const bool sa = r.Below(2) != 0;
    const bool sb = r.Below(2) != 0;
    switch (op) {
    case Op::Add:
    case Op::Sub: {
      const std::int32_t ea = kExtBias + r.Between(-60, 60);
      const std::uint64_t keep = width >= 64 ? ~0ull : ~((1ull << (64 - width)) - 1);
      a = Pack(sa, ea, (r.Next() | core::kIntegerBit) & keep);
      const std::uint64_t k = RandomOdd(r, 1 + static_cast<int>(r.Below(8)));
      return ScaledInt(sb, k, ea - kExtBias - width, b);
    }
    case Op::Mul: {
      const int k1 = 1 + static_cast<int>(r.Below(static_cast<std::uint32_t>(width)));
      const int k2 = width + 1 + static_cast<int>(r.Below(2)) - k1;
      if (k2 < 1 || k2 > 64) {
        return false;
      }
      return ScaledInt(sa, RandomOdd(r, k1), r.Between(-80, 80), a) && ScaledInt(sb, RandomOdd(r, k2), r.Between(-80, 80), b);
    }
    case Op::Div: {
      if (width >= 64) {
        return false;
      }
      const std::uint64_t q = RandomOdd(r, width + 1);
      const std::uint64_t d = RandomOdd(r, 1 + static_cast<int>(r.Below(static_cast<std::uint32_t>(63 - width))));
      return ScaledInt(sa, q * d, r.Between(-80, 80), a) && ScaledInt(sb, d, r.Between(-80, 80), b);
    }
    case Op::Sqrt: {
      const std::uint64_t root = RandomOdd(r, 1 + static_cast<int>(r.Below(32)));
      const std::uint64_t square = root * root + (r.Chance(30) ? (r.Chance(50) ? 1 : ~0ull) : 0);
      b = Ext{};
      return square != 0 && ScaledInt(false, square, 2 * r.Between(-60, 60), a);
    }
    }
    return false;
  }

  // An operand pair for a binary operation: independent, close (cancellation), equal or
  // opposite, exponent differences at the 64/128-bit alignment edges, exact ties.
  inline void RandomPair(Rng& r, const Op op, const Precision p, Ext& a, Ext& b) noexcept
  {
    a = RandomExt(r);
    const std::int32_t ea = ExpFieldOf(a);
    const bool finiteA = ea != 0x7FFF && ea != 0;
    switch (r.Below(12)) {
    case 5:
      if (finiteA) {
        const std::uint64_t flip = r.Next() >> r.Below(64);
        b = Pack(r.Below(2) != 0, ClampExp(ea + r.Between(-1, 1)), (a.signif ^ flip) | core::kIntegerBit);
        return;
      }
      break;
    case 6:
      b = r.Below(2) != 0 ? a : Fchs(a);
      return;
    case 7:
      if (finiteA) {
        static constexpr int kGaps[] = {62, 63, 64, 65, 66, 126, 127, 128, 129, 130, 1, 2, 24, 25, 53, 54};
        const int gap = kGaps[r.Below(16)];
        b = Pack(r.Below(2) != 0, ClampExp(r.Chance(50) ? ea - gap : ea + gap), RandomSignificand(r));
        return;
      }
      break;
    case 8:
    case 9:
      if (TiePair(r, op, p, a, b)) {
        if (r.Chance(50)) {
          const Ext t = a;
          a = b;
          b = t;
        }
        return;
      }
      break;
    case 10:
      b = FromInt32(r.Between(-5, 5));
      return;
    case 11:
      if (finiteA) {
        b = Pack(r.Below(2) != 0, ea, RandomSignificand(r));
        return;
      }
      break;
    default:
      break;
    }
    b = RandomExt(r);
  }

  // A value near 2^center (spread binades either side), for the conversions.
  [[nodiscard]] inline Ext RandomNear(Rng& r, const std::int32_t center, const std::int32_t spread) noexcept
  {
    if (r.Chance(5)) {
      return RandomSpecial(r);
    }
    if (r.Chance(5)) {
      return RandomExt(r);
    }
    return Pack(r.Below(2) != 0, ClampExp(kExtBias + center + r.Between(-spread, spread)), RandomSignificand(r));
  }

  // --- difffuzz generators (tools/difffuzz/src/gen.cpp) ---------------------------------------

  namespace difffuzz
  {
    constexpr std::uint32_t kEdgeBits[] = {
      0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7FC12345u,
      0x00800000u, 0x80800000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x7F7FFFFFu,
      0xFF7FFFFFu, 0x3F800000u, 0xBF800000u, 0x3F000000u, 0xBF000000u, 0x40000000u, 0xC0000000u,
      0x358637BDu, 0xB58637BDu, 0x3A83126Fu, 0xBA83126Fu, 0x3FC90FDBu, 0xBFC90FDBu, 0x40490FDBu,
      0xC0490FDBu, 0x40C90FDBu, 0xC0C90FDBu, 0x1E3CE508u, 0x60AD78ECu, 0x3F800001u, 0x3F7FFFFFu,
      0x5F800000u, 0x1F800000u,
      // Added for the native tests: an SNaN, and the x87-range probes 2^+-63/64 neighbours.
      0x7F800001u, 0xFFA00000u, 0x5F000000u, 0x1F000000u, 0x7F000000u, 0x00400000u,
    };
    constexpr std::uint32_t kEdgeCount = sizeof(kEdgeBits) / sizeof(kEdgeBits[0]);

    [[nodiscard]] inline float GenEdge(Rng& rng) { return FFromBits(kEdgeBits[rng.Below(kEdgeCount)]); }

    [[nodiscard]] inline float Sign(Rng& rng, const double v) { return static_cast<float>(rng.Chance(50) ? -v : v); }

    [[nodiscard]] inline float GenPlain(Rng& rng, const float lim) { return static_cast<float>(rng.Range(-lim, lim)); }

    [[nodiscard]] inline float GenFloat(Rng& rng)
    {
      switch (rng.Below(12)) {
      case 0:
      case 1:
        return GenPlain(rng, 1.0f);
      case 2:
        return GenPlain(rng, 1000.0f);
      case 3:
        return GenPlain(rng, 1.0e6f);
      case 4:
        return Sign(rng, std::ldexp(rng.Range(1.0, 2.0), static_cast<int>(rng.Below(280)) - 150));
      case 5:
        return FFromBits(rng.U32());
      case 6:
      case 7:
        return GenEdge(rng);
      case 8:
        return FFromBits(kEdgeBits[rng.Below(kEdgeCount)] + rng.Below(7) - 3);
      case 9:
        return Sign(rng, std::ldexp(rng.Range(1.0, 2.0), -40 - static_cast<int>(rng.Below(110))));
      case 10:
        return Sign(rng, std::ldexp(rng.Range(1.0, 2.0), 40 + static_cast<int>(rng.Below(88))));
      default:
        return static_cast<float>(rng.Below(9)) - 4.0f;
      }
    }

    inline void NormalizeD(double* v, const int n)
    {
      double s = 0.0;
      for (int i = 0; i < n; ++i) {
        s += v[i] * v[i];
      }
      s = std::sqrt(s);
      for (int i = 0; i < n; ++i) {
        v[i] /= s;
      }
    }

    [[nodiscard]] inline double Perturbation(Rng& rng)
    {
      static constexpr double kEps[] = {0.0, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2};
      return kEps[rng.Below(8)] * rng.Range(-1.0, 1.0);
    }

    inline void GenUnitVec3(Rng& rng, float* v)
    {
      double d[3];
      do {
        d[0] = rng.Range(-1, 1);
        d[1] = rng.Range(-1, 1);
        d[2] = rng.Range(-1, 1);
      } while (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1e-6);
      NormalizeD(d, 3);
      for (int i = 0; i < 3; ++i) {
        v[i] = static_cast<float>(d[i]);
      }
    }

    inline void GenVec3(Rng& rng, float* v)
    {
      const std::uint32_t mode = rng.Below(100);
      if (mode < 35) {
        GenUnitVec3(rng, v);
      } else if (mode < 60) {
        for (int i = 0; i < 3; ++i) {
          v[i] = GenFloat(rng);
        }
      } else if (mode < 75) {
        for (int i = 0; i < 3; ++i) {
          v[i] = GenPlain(rng, 100.0f);
        }
      } else if (mode < 88) {
        static constexpr float kLane[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.0f, 0.0f};
        for (int i = 0; i < 3; ++i) {
          v[i] = rng.Chance(20) ? GenPlain(rng, 10.0f) : kLane[rng.Below(6)];
        }
      } else {
        GenUnitVec3(rng, v);
        const double mag = std::ldexp(rng.Range(1.0, 2.0), static_cast<int>(rng.Below(200)) - 100);
        for (int i = 0; i < 3; ++i) {
          v[i] = static_cast<float>(v[i] * mag);
        }
      }
    }

    inline void GenVecPair(Rng& rng, float* v1, float* v2)
    {
      GenVec3(rng, v1);
      const std::uint32_t mode = rng.Below(100);
      if (mode < 30) {
        GenVec3(rng, v2);
        return;
      }
      const double sign = mode < 65 ? 1.0 : -1.0;
      const double scale = rng.Chance(50) ? 1.0 : std::ldexp(rng.Range(1.0, 2.0), static_cast<int>(rng.Below(20)) - 10);
      for (int i = 0; i < 3; ++i) {
        v2[i] = static_cast<float>(sign * scale * v1[i] + Perturbation(rng));
      }
    }

    [[nodiscard]] inline float GenT(Rng& rng)
    {
      const std::uint32_t mode = rng.Below(100);
      if (mode < 50) {
        return static_cast<float>(rng.Unit());
      }
      if (mode < 65) {
        static constexpr std::uint32_t kT[] = {0x00000000u, 0x80000000u, 0x3F800000u, 0x3F000000u, 0x3F7FFFFFu, 0x00000001u};
        return FFromBits(kT[rng.Below(6)]);
      }
      if (mode < 80) {
        return static_cast<float>(rng.Range(-0.5, 1.5));
      }
      return GenFloat(rng);
    }
  } // namespace difffuzz
} // namespace x87t
