// Layer 2 against Layer 1: the double helpers of X87Core.h (fast::*24) against the exact
// core, and every msvc8_x87_* fast path against its asm transcription (X87Emu.cpp ref::)
// on difffuzz-style inputs, edge corpora and aliasing pointers. Then the kernel-free
// transcriptions against the same chains evaluated with SoftFloat (an independent
// derivation of each chain from the asm, plus an independent arithmetic).

#include <cmath>
#include <cstring>

#include "EmuApi.h"
#include "Gen.h"

namespace x87t
{
  namespace emu = faf_posix::x87::emu;

  namespace
  {
    constexpr std::uint64_t kSeed = 0x5EED2007C0DE0002ull;

    // --- Layer 2 helper inputs ------------------------------------------------------------

    // An F24 value (24-bit significand) held in a double, with an exponent anywhere in
    // [-460, 420]: the range the routines' intermediates can reach and a margin beyond.
    [[nodiscard]] double RandomF24(Rng& r)
    {
      switch (r.Below(10)) {
      case 0:
        return static_cast<double>(difffuzz::GenFloat(r));
      case 1:
        return 0.0 * (r.Chance(50) ? 1.0 : -1.0);
      case 2:
        return std::ldexp(static_cast<double>(RandomOdd(r, 1 + static_cast<int>(r.Below(24)))), r.Between(-480, 400)) * (r.Chance(50) ? 1.0 : -1.0);
      default: {
        const double m = static_cast<double>((r.Next() >> 40) | (1ull << 23));
        return std::ldexp(m, r.Between(-460 - 23, 420 - 23)) * (r.Chance(50) ? 1.0 : -1.0);
      }
      }
    }

    // A double (F53) for the round-to-odd helpers.
    [[nodiscard]] double RandomF53(Rng& r)
    {
      switch (r.Below(8)) {
      case 0:
        return RandomF24(r);
      case 1:
        return r.Range(-1.0, 1.0);
      case 2:
        return 1.0 - std::ldexp(r.Unit(), -static_cast<int>(r.Below(60)));
      case 3:
        return std::ldexp(r.Range(-1.0, 1.0), -static_cast<int>(r.Below(1000)));
      default: {
        const std::uint64_t bits = (r.Next() & 0x800FFFFFFFFFFFFFull) | (static_cast<std::uint64_t>(1023 + r.Between(-200, 200)) << 52);
        return DFromBits(bits);
      }
      }
    }

    // Layer 1's result as a double, if it is a value the double path is specified for:
    // not NaN, and zero, infinite or inside the double normal exponent range.
    [[nodiscard]] bool Comparable(const Ext e, double& out)
    {
      if (IsNaN(e)) {
        return false;
      }
      if (!IsZero(e) && !IsInf(e)) {
        const std::int32_t unbiased = ExpFieldOf(e) - kExtBias;
        if (ExpFieldOf(e) == 0 || unbiased < -1022 || unbiased > 1023) {
          return false;
        }
      }
      out = ToDouble(e);
      return true;
    }

    // --- Routine inputs ---------------------------------------------------------------------

    [[nodiscard]] float RandomLane(Rng& r)
    {
      switch (r.Below(10)) {
      case 0:
        return difffuzz::GenEdge(r);
      case 1:
        return std::ldexp(static_cast<float>(r.Range(1.0, 2.0)), r.Between(-149, -100)) * (r.Chance(50) ? 1.0f : -1.0f);
      case 2:
        return std::ldexp(static_cast<float>(r.Range(1.0, 2.0)), r.Between(60, 127)) * (r.Chance(50) ? 1.0f : -1.0f);
      default:
        return difffuzz::GenFloat(r);
      }
    }

    void RandomVec(Rng& r, float* v)
    {
      if (r.Chance(20)) {
        for (int i = 0; i < 3; ++i) {
          v[i] = RandomLane(r);
        }
      } else {
        difffuzz::GenVec3(r, v);
      }
    }

    // A squared length the way callers form it: float arithmetic ((x*x + y*y) + z*z), or
    // anything.
    [[nodiscard]] float CallerLengthSq(Rng& r, const float* v)
    {
      if (r.Chance(70)) {
        return (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2];
      }
      return r.Chance(50) ? RandomLane(r) : std::fabs(difffuzz::GenFloat(r));
    }

    void RandomBox(Rng& r, float* box)
    {
      difffuzz::GenVec3(r, box);
      if (r.Chance(70)) {
        // Orthonormal-ish axes, near-degenerate ones, or raw lanes.
        difffuzz::GenUnitVec3(r, box + 3);
        difffuzz::GenVecPair(r, box + 3, box + 6);
        float* a2 = box + 9;
        a2[0] = box[4] * box[8] - box[5] * box[7];
        a2[1] = box[5] * box[6] - box[3] * box[8];
        a2[2] = box[3] * box[7] - box[4] * box[6];
      } else {
        for (int i = 3; i < 12; ++i) {
          box[i] = RandomLane(r);
        }
      }
      for (int i = 12; i < 15; ++i) {
        box[i] = r.Chance(60) ? static_cast<float>(r.Range(0.0, 50.0)) : RandomLane(r);
      }
    }

    void RandomAxis(Rng& r, float* axis)
    {
      if (r.Chance(40)) {
        // Cross product of near-parallel vectors: components deep in the denormal range.
        float a[3];
        float b[3];
        difffuzz::GenVecPair(r, a, b);
        axis[0] = a[1] * b[2] - a[2] * b[1];
        axis[1] = a[2] * b[0] - a[0] * b[2];
        axis[2] = a[0] * b[1] - a[1] * b[0];
        if (r.Chance(50)) {
          for (int i = 0; i < 3; ++i) {
            axis[i] = std::ldexp(axis[i], -r.Between(0, 100));
          }
        }
      } else {
        RandomVec(r, axis);
      }
    }

    [[nodiscard]] double RandomAcosArgument(Rng& r)
    {
      switch (r.Below(10)) {
      case 0:
        return r.Chance(50) ? 1.0 : -1.0;
      case 1:
        return (r.Chance(50) ? 1.0 : -1.0) * (1.0 - std::ldexp(r.Unit(), -static_cast<int>(r.Below(54))));
      case 2:
        return std::ldexp(r.Range(-1.0, 1.0), -static_cast<int>(r.Below(1080)));
      case 3:
        return static_cast<double>(difffuzz::GenFloat(r));
      case 4:
        return DFromBits(r.Next());
      case 5:
        return r.Chance(50) ? 0.0 : -0.0;
      default:
        return r.Range(-1.0, 1.0);
      }
    }

    // Runs a routine test: `body(rng, mismatches log)` returns true on a match.
    template <class Body>
    void RoutineTest(const char* name, const std::uint64_t full, const std::uint64_t salt, Body body)
    {
      MismatchLog log(name);
      const std::uint64_t n = Iters(full);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(kSeed + salt, chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          if (!body(r, log)) {
            ++mismatches;
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    }

    [[nodiscard]] bool SameLanes(const float* a, const float* b, const int n)
    {
      return std::memcmp(a, b, sizeof(float) * static_cast<std::size_t>(n)) == 0;
    }

    // Dest/source arrangement for the vector routines: separate buffers, in place, or
    // overlapping by one lane either way (the fast path must hand those to ref).
    struct Lanes
    {
      float buf[8];
      float* dest;
      float* src;
    };

    void Arrange(Lanes& l, const float* source, const std::uint32_t mode)
    {
      for (float& f : l.buf) {
        f = FFromBits(0xCDCDCDCDu);
      }
      switch (mode) {
      case 0:
        l.src = l.buf;
        l.dest = l.buf + 4;
        break;
      case 1:
        l.src = l.buf + 1;
        l.dest = l.buf + 1;
        break;
      case 2:
        l.src = l.buf + 1;
        l.dest = l.buf + 2;
        break;
      default:
        l.src = l.buf + 2;
        l.dest = l.buf + 1;
        break;
      }
      std::memcpy(l.src, source, sizeof(float) * 3);
    }

    // --- SoftFloat chains (precision 32 = PC24) -----------------------------------------

    struct Sf
    {
      static Ext L(const float f)
      {
        Ext r{};
        x87t_sf_from_f32(FBits(f), &r);
        return r;
      }
      static Ext A(const Ext a, const Ext b)
      {
        Ext r{};
        x87t_sf_add(&a, &b, &r);
        return r;
      }
      static Ext S(const Ext a, const Ext b)
      {
        Ext r{};
        x87t_sf_sub(&a, &b, &r);
        return r;
      }
      static Ext M(const Ext a, const Ext b)
      {
        Ext r{};
        x87t_sf_mul(&a, &b, &r);
        return r;
      }
      static Ext D(const Ext a, const Ext b)
      {
        Ext r{};
        x87t_sf_div(&a, &b, &r);
        return r;
      }
      static Ext Q(const Ext a)
      {
        Ext r{};
        x87t_sf_sqrt(&a, &r);
        return r;
      }
      static Ext Abs(const Ext a) { return Ext{a.signif, static_cast<std::uint16_t>(a.signExp & 0x7FFFu)}; }
      static std::uint32_t St(const Ext a) { return x87t_sf_to_f32(&a); }
    };

    [[nodiscard]] std::uint32_t SfBilinear(const float h00, const float h01, const float h10, const float h11, const float fx, const float fz)
    {
      const Ext row0 = Sf::A(Sf::L(h00), Sf::M(Sf::S(Sf::L(h01), Sf::L(h00)), Sf::L(fz)));
      const Ext row1 = Sf::A(Sf::M(Sf::L(fz), Sf::S(Sf::L(h11), Sf::L(h10))), Sf::L(h10));
      return Sf::St(Sf::A(row0, Sf::M(Sf::S(row1, row0), Sf::L(fx))));
    }

    [[nodiscard]] std::uint32_t SfBoxRadius(const float* b, const float* axis)
    {
      const Ext ax = Sf::L(axis[0]);
      const Ext ay = Sf::L(axis[1]);
      const Ext az = Sf::L(axis[2]);
      const Ext dot2 = Sf::A(Sf::A(Sf::M(Sf::L(b[11]), az), Sf::M(Sf::L(b[10]), ay)), Sf::M(Sf::L(b[9]), ax));
      const Ext dot1 = Sf::A(Sf::A(Sf::M(Sf::L(b[7]), ay), Sf::M(Sf::L(b[8]), az)), Sf::M(Sf::L(b[6]), ax));
      const Ext dot0 = Sf::A(Sf::A(Sf::M(az, Sf::L(b[5])), Sf::M(ay, Sf::L(b[4]))), Sf::M(ax, Sf::L(b[3])));
      const Ext r21 = Sf::A(Sf::M(Sf::Abs(dot2), Sf::L(b[14])), Sf::M(Sf::Abs(dot1), Sf::L(b[13])));
      return Sf::St(Sf::A(r21, Sf::M(Sf::Abs(dot0), Sf::L(b[12]))));
    }

    [[nodiscard]] std::uint32_t SfVectorLength(const float* v)
    {
      const Ext x = Sf::L(v[0]);
      const Ext y = Sf::L(v[1]);
      const Ext z = Sf::L(v[2]);
      return Sf::St(Sf::Q(Sf::A(Sf::A(Sf::M(z, z), Sf::M(y, y)), Sf::M(x, x))));
    }

    void SfScale(const float* src, const Ext scale, std::uint32_t* out)
    {
      for (int i = 0; i < 3; ++i) {
        out[i] = Sf::St(Sf::M(Sf::L(src[i]), scale));
      }
    }
  } // namespace

  void RunLayer2Helpers()
  {
    {
      const char* name = "fast::Round24 vs Layer 1 (x + 0 at PC24), all binades";
      MismatchLog log(name);
      const std::uint64_t n = Iters(100'000'000);
      std::atomic<std::uint64_t> skipped{0};
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(kSeed + 1, chunk));
        std::uint64_t mismatches = 0;
        std::uint64_t skip = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          std::uint64_t bits = (r.Next() & 0x800FFFFFFFFFFFFFull) | (static_cast<std::uint64_t>(1 + r.Below(2046)) << 52);
          if (r.Chance(30)) {
            bits = (bits & ~0x1FFFFFFFull) | (r.Chance(50) ? 0x10000000ull : (r.Next() & 0x1FFFFFFFull) | 0x0FFFFFFFull);
          }
          const double d = DFromBits(bits);
          double expected = 0.0;
          if (!Comparable(Add(FromDouble(d), core::Zero(false), Precision::P24), expected)) {
            ++skip;
            continue;
          }
          if (DBits(faf_posix::x87::fast::Round24(d)) != DBits(expected)) {
            ++mismatches;
            log.Print("%016" PRIX64, bits);
          }
        }
        skipped.fetch_add(skip);
        return mismatches;
      });
      Record(name, n, bad, "skipped (rounds past DBL_MAX): " + std::to_string(skipped.load()));
    }

    struct Helper
    {
      const char* name;
      int which;  // 0 add, 1 sub, 2 mul, 3 div, 4 sqrt, 5 add odd, 6 sub odd
    };
    static constexpr Helper kHelpers[] = {
      {"fast::Add24 (F24 operands) vs Layer 1 PC24", 0},
      {"fast::Sub24 (F24 operands) vs Layer 1 PC24", 1},
      {"fast::Mul24 (F24 operands) vs Layer 1 PC24", 2},
      {"fast::Div24 (F24 operands) vs Layer 1 PC24", 3},
      {"fast::Sqrt24 (F24 operand) vs Layer 1 PC24", 4},
      {"fast::Add24Odd (F53 operands, round-to-odd) vs Layer 1 PC24", 5},
      {"fast::Sub24Odd (F53 operands, round-to-odd) vs Layer 1 PC24", 6},
    };
    for (const Helper& helper : kHelpers) {
      MismatchLog log(helper.name);
      const std::uint64_t n = Iters(100'000'000);
      std::atomic<std::uint64_t> skipped{0};
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(kSeed + 10 + static_cast<std::uint64_t>(helper.which), chunk));
        std::uint64_t mismatches = 0;
        std::uint64_t skip = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          const bool odd = helper.which >= 5;
          double a = odd ? RandomF53(r) : RandomF24(r);
          double b = odd ? RandomF53(r) : RandomF24(r);
          switch (r.Below(6)) {
          case 0:  // cancellation
            b = -a * (1.0 + std::ldexp(static_cast<double>(r.Between(-8, 8)), odd ? -52 : -23));
            if (!odd) {
              b = faf_posix::x87::fast::Round24(b);
            }
            break;
          case 1:  // a tie at 24 bits for + and -
            b = std::ldexp(a, -24 - r.Between(0, 2)) * static_cast<double>(RandomOdd(r, 1 + static_cast<int>(r.Below(3))));
            if (!odd) {
              b = faf_posix::x87::fast::Round24(b);
            }
            break;
          case 2:  // a 25-bit product for *
            a = std::ldexp(static_cast<double>(RandomOdd(r, 12)), r.Between(-200, 200));
            b = std::ldexp(static_cast<double>(RandomOdd(r, 13 + static_cast<int>(r.Below(2)))), r.Between(-200, 200));
            break;
          default:
            break;
          }
          const Ext ea = FromDouble(a);
          const Ext eb = FromDouble(b);
          Ext exact{};
          double fast = 0.0;
          switch (helper.which) {
          case 0:
            exact = Add(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Add24(a, b);
            break;
          case 1:
            exact = Sub(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Sub24(a, b);
            break;
          case 2:
            exact = Mul(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Mul24(a, b);
            break;
          case 3:
            exact = Div(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Div24(a, b);
            break;
          case 4:
            exact = Sqrt(Fabs(ea), Precision::P24);
            fast = faf_posix::x87::fast::Sqrt24(std::fabs(a));
            break;
          case 5:
            exact = Add(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Add24Odd(a, b);
            break;
          default:
            exact = Sub(ea, eb, Precision::P24);
            fast = faf_posix::x87::fast::Sub24Odd(a, b);
            break;
          }
          double expected = 0.0;
          if (!Comparable(exact, expected)) {
            ++skip;
            continue;
          }
          if (DBits(fast) != DBits(expected)) {
            ++mismatches;
            log.Print("a %016" PRIX64 " b %016" PRIX64 " fast %016" PRIX64 " layer1 %016" PRIX64, DBits(a), DBits(b), DBits(fast), DBits(expected));
          }
        }
        skipped.fetch_add(skip);
        return mismatches;
      });
      Record(helper.name, n, bad, "skipped (NaN or outside double normal range): " + std::to_string(skipped.load()));
    }
  }

  void RunRoutines()
  {
    constexpr std::uint64_t kFull = 100'000'000;

    RoutineTest("bilinear_height fast vs transcription", kFull, 100, [](Rng& r, MismatchLog& log) {
      float h[4];
      float fx = 0.0f;
      float fz = 0.0f;
      const std::uint32_t mode = r.Below(4);
      for (float& v : h) {
        v = mode == 0 ? static_cast<float>(r.Range(0.0, 1000.0)) : (mode == 1 ? difffuzz::GenFloat(r) : RandomLane(r));
      }
      if (mode == 3) {
        fx = difffuzz::GenEdge(r);
        fz = difffuzz::GenEdge(r);
      } else {
        fx = difffuzz::GenT(r);
        fz = difffuzz::GenT(r);
      }
      const float a = emu::fast::BilinearHeight(h[0], h[1], h[2], h[3], fx, fz);
      const float b = emu::ref::BilinearHeight(h[0], h[1], h[2], h[3], fx, fz);
      if (FBits(a) != FBits(b)) {
        log.Print(
          "%08X %08X %08X %08X fx %08X fz %08X fast %08X ref %08X", FBits(h[0]), FBits(h[1]), FBits(h[2]), FBits(h[3]), FBits(fx),
          FBits(fz), FBits(a), FBits(b)
        );
        return false;
      }
      return true;
    });

    RoutineTest("box_radius_on_axis fast vs transcription", kFull, 101, [](Rng& r, MismatchLog& log) {
      float box[15];
      float axis[3];
      RandomBox(r, box);
      RandomAxis(r, axis);
      const float a = emu::fast::BoxRadiusOnAxis(box, axis);
      const float b = emu::ref::BoxRadiusOnAxis(box, axis);
      if (FBits(a) != FBits(b)) {
        log.Print("axis %08X %08X %08X fast %08X ref %08X", FBits(axis[0]), FBits(axis[1]), FBits(axis[2]), FBits(a), FBits(b));
        return false;
      }
      return true;
    });

    RoutineTest("vector_length_zyx fast vs transcription", kFull, 102, [](Rng& r, MismatchLog& log) {
      float v[3];
      RandomVec(r, v);
      if (r.Chance(20)) {
        const int e = r.Chance(50) ? r.Between(-149, -60) : r.Between(55, 127);
        for (float& f : v) {
          f = std::ldexp(static_cast<float>(r.Range(-2.0, 2.0)), e);
        }
      }
      const float a = emu::fast::VectorLengthZYX(v);
      const float b = emu::ref::VectorLengthZYX(v);
      if (FBits(a) != FBits(b)) {
        log.Print("%08X %08X %08X fast %08X ref %08X", FBits(v[0]), FBits(v[1]), FBits(v[2]), FBits(a), FBits(b));
        return false;
      }
      return true;
    });

    RoutineTest("scale_vector fast vs transcription (incl. aliasing)", kFull, 103, [](Rng& r, MismatchLog& log) {
      float v[3];
      RandomVec(r, v);
      const float scale = r.Chance(50) ? difffuzz::GenFloat(r) : RandomLane(r);
      const std::uint32_t mode = r.Below(10) < 7 ? 0 : r.Below(4);
      Lanes a{};
      Lanes b{};
      Arrange(a, v, mode);
      Arrange(b, v, mode);
      emu::fast::ScaleVector(a.dest, a.src, scale);
      emu::ref::ScaleVector(b.dest, b.src, scale);
      if (!SameLanes(a.buf, b.buf, 8)) {
        log.Print("mode %u v %08X %08X %08X scale %08X", mode, FBits(v[0]), FBits(v[1]), FBits(v[2]), FBits(scale));
        return false;
      }
      return true;
    });

    RoutineTest("scale_vector_to_length fast vs transcription (incl. aliasing)", kFull, 104, [](Rng& r, MismatchLog& log) {
      float v[3];
      RandomVec(r, v);
      const float lengthSq = CallerLengthSq(r, v);
      const float length = r.Chance(50) ? difffuzz::GenT(r) : RandomLane(r);
      const std::uint32_t mode = r.Below(10) < 7 ? 0 : r.Below(4);
      Lanes a{};
      Lanes b{};
      Arrange(a, v, mode);
      Arrange(b, v, mode);
      emu::fast::ScaleVectorToLength(a.dest, a.src, lengthSq, length);
      emu::ref::ScaleVectorToLength(b.dest, b.src, lengthSq, length);
      if (!SameLanes(a.buf, b.buf, 8)) {
        log.Print(
          "mode %u v %08X %08X %08X lenSq %08X len %08X", mode, FBits(v[0]), FBits(v[1]), FBits(v[2]), FBits(lengthSq), FBits(length)
        );
        return false;
      }
      return true;
    });

    RoutineTest("scale_vector_max_length fast vs transcription (incl. aliasing)", kFull, 105, [](Rng& r, MismatchLog& log) {
      float v[3];
      RandomVec(r, v);
      const float lengthSq = CallerLengthSq(r, v);
      const float maxLength = r.Chance(50) ? static_cast<float>(r.Range(0.0, 10.0)) : RandomLane(r);
      float a[3];
      float b[3];
      std::memcpy(a, v, sizeof a);
      std::memcpy(b, v, sizeof b);
      // Usually three distinct lanes; sometimes x and y (or y and z) alias.
      const std::uint32_t alias = r.Below(10) < 8 ? 0 : 1 + r.Below(2);
      float* ax = a;
      float* ay = alias == 1 ? a : a + 1;
      float* az = alias == 2 ? a + 1 : a + 2;
      float* bx = b;
      float* by = alias == 1 ? b : b + 1;
      float* bz = alias == 2 ? b + 1 : b + 2;
      emu::fast::ScaleVectorMaxLength(ax, ay, az, maxLength, lengthSq);
      emu::ref::ScaleVectorMaxLength(bx, by, bz, maxLength, lengthSq);
      if (!SameLanes(a, b, 3)) {
        log.Print(
          "alias %u v %08X %08X %08X max %08X lenSq %08X", alias, FBits(v[0]), FBits(v[1]), FBits(v[2]), FBits(maxLength), FBits(lengthSq)
        );
        return false;
      }
      return true;
    });

    RoutineTest("scale_vector_to_length_of fast vs transcription (incl. aliasing)", kFull, 106, [](Rng& r, MismatchLog& log) {
      float s[3];
      float t[3];
      difffuzz::GenVecPair(r, s, t);
      if (r.Chance(20)) {
        RandomVec(r, s);
        RandomVec(r, t);
      }
      if (r.Chance(10)) {
        // The overflow case X87Math.h names: a very short source against |t| ~ 100.
        for (float& f : s) {
          f = std::ldexp(f, -60);
        }
      }
      const float lengthSq = CallerLengthSq(r, s);
      const std::uint32_t mode = r.Below(10) < 7 ? 0 : r.Below(4);
      Lanes a{};
      Lanes b{};
      Arrange(a, s, mode);
      Arrange(b, s, mode);
      emu::fast::ScaleVectorToLengthOf(a.dest, a.src, t, lengthSq);
      emu::ref::ScaleVectorToLengthOf(b.dest, b.src, t, lengthSq);
      if (!SameLanes(a.buf, b.buf, 8)) {
        log.Print("mode %u s %08X %08X %08X t %08X %08X %08X lenSq %08X", mode, FBits(s[0]), FBits(s[1]), FBits(s[2]), FBits(t[0]), FBits(t[1]), FBits(t[2]), FBits(lengthSq));
        return false;
      }
      return true;
    });

    RoutineTest("acos fast (round-to-odd radicand) vs transcription", kFull, 107, [](Rng& r, MismatchLog& log) {
      const double x = RandomAcosArgument(r);
      const double a = emu::fast::Acos(x);
      const double b = emu::ref::Acos(x);
      if (DBits(a) != DBits(b)) {
        log.Print("x %016" PRIX64 " fast %016" PRIX64 " ref %016" PRIX64, DBits(x), DBits(a), DBits(b));
        return false;
      }
      return true;
    });

    RoutineTest("asin fast (round-to-odd radicand) vs transcription", kFull, 108, [](Rng& r, MismatchLog& log) {
      const double x = RandomAcosArgument(r);
      const double a = emu::fast::Asin(x);
      const double b = emu::ref::Asin(x);
      if (DBits(a) != DBits(b)) {
        log.Print("x %016" PRIX64 " fast %016" PRIX64 " ref %016" PRIX64, DBits(x), DBits(a), DBits(b));
        return false;
      }
      return true;
    });

    // Edge corpus: every combination of the edge values for the low-arity routines.
    {
      const char* name = "edge corpus: all edge-value combinations, 8 fast routines";
      MismatchLog log(name);
      constexpr std::uint32_t kE = difffuzz::kEdgeCount;
      std::uint64_t inputs = 0;
      std::uint64_t bad = 0;
      // scale_vector / _to_length / _max_length / vector_length / box radius on edge lanes.
      for (std::uint32_t i = 0; i < kE; ++i) {
        for (std::uint32_t j = 0; j < kE; ++j) {
          for (std::uint32_t k = 0; k < kE; ++k) {
            const float v[3] = {FFromBits(difffuzz::kEdgeBits[i]), FFromBits(difffuzz::kEdgeBits[j]), FFromBits(difffuzz::kEdgeBits[k])};
            ++inputs;
            if (FBits(emu::fast::VectorLengthZYX(v)) != FBits(emu::ref::VectorLengthZYX(v))) {
              ++bad;
              log.Print("vector_length %08X %08X %08X", FBits(v[0]), FBits(v[1]), FBits(v[2]));
            }
            for (std::uint32_t m = 0; m < kE; ++m) {
              const float s = FFromBits(difffuzz::kEdgeBits[m]);
              float a[3];
              float b[3];
              ++inputs;
              emu::fast::ScaleVector(a, v, s);
              emu::ref::ScaleVector(b, v, s);
              if (!SameLanes(a, b, 3)) {
                ++bad;
                log.Print("scale_vector");
              }
              const float lsq = FFromBits(difffuzz::kEdgeBits[(m * 7 + i) % kE]);
              emu::fast::ScaleVectorToLength(a, v, lsq, s);
              emu::ref::ScaleVectorToLength(b, v, lsq, s);
              if (!SameLanes(a, b, 3)) {
                ++bad;
                log.Print("scale_vector_to_length");
              }
              std::memcpy(a, v, sizeof a);
              std::memcpy(b, v, sizeof b);
              emu::fast::ScaleVectorMaxLength(a, a + 1, a + 2, s, lsq);
              emu::ref::ScaleVectorMaxLength(b, b + 1, b + 2, s, lsq);
              if (!SameLanes(a, b, 3)) {
                ++bad;
                log.Print("scale_vector_max_length");
              }
              emu::fast::ScaleVectorToLengthOf(a, v, v, s);
              emu::ref::ScaleVectorToLengthOf(b, v, v, s);
              if (!SameLanes(a, b, 3)) {
                ++bad;
                log.Print("scale_vector_to_length_of");
              }
            }
          }
        }
      }
      // bilinear: 6 inputs from a 16-value subset (16^6 combinations).
      static constexpr std::uint32_t kSub[16] = {0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u, 0x7FC12345u, 0xFFC00000u, 0x00000001u, 0x807FFFFFu,
                                                 0x7F7FFFFFu, 0xFF7FFFFFu, 0x3F800000u, 0xBF000000u, 0x00800000u, 0x3F7FFFFFu, 0x5F800000u, 0x7F800001u};
      for (std::uint32_t c = 0; c < (1u << 24); ++c) {
        float in[6];
        for (int q = 0; q < 6; ++q) {
          in[q] = FFromBits(kSub[(c >> (4 * q)) & 15u]);
        }
        ++inputs;
        if (FBits(emu::fast::BilinearHeight(in[0], in[1], in[2], in[3], in[4], in[5])) != FBits(emu::ref::BilinearHeight(in[0], in[1], in[2], in[3], in[4], in[5]))) {
          ++bad;
          log.Print("bilinear %06X", c);
        }
      }
      // box radius: every lane one edge value, the rest unit-ish.
      for (std::uint32_t lane = 0; lane < 18; ++lane) {
        for (std::uint32_t e = 0; e < kE; ++e) {
          float box[15] = {1, 2, 3, 1, 0, 0, 0, 1, 0, 0, 0, 1, 4, 5, 6};
          float axis[3] = {0.6f, -0.8f, 0.0f};
          (lane < 15 ? box[lane] : axis[lane - 15]) = FFromBits(difffuzz::kEdgeBits[e]);
          ++inputs;
          if (FBits(emu::fast::BoxRadiusOnAxis(box, axis)) != FBits(emu::ref::BoxRadiusOnAxis(box, axis))) {
            ++bad;
            log.Print("box lane %u edge %08X", lane, difffuzz::kEdgeBits[e]);
          }
        }
      }
      Record(name, inputs, bad);
    }
  }

  void RunRoutineOracle()
  {
    constexpr std::uint64_t kFull = 20'000'000;
    auto run = [&](const char* name, const std::uint64_t salt, auto&& body) {
      MismatchLog log(name);
      const std::uint64_t n = Iters(kFull);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        x87t_sf_set_precision(32);
        Rng r(Mix(kSeed + salt, chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          if (!body(r, log)) {
            ++mismatches;
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    };

    run("bilinear_height transcription vs SoftFloat chain", 200, [](Rng& r, MismatchLog& log) {
      float h[4];
      for (float& v : h) {
        v = r.Chance(50) ? difffuzz::GenFloat(r) : RandomLane(r);
      }
      const float fx = r.Chance(70) ? difffuzz::GenT(r) : RandomLane(r);
      const float fz = r.Chance(70) ? difffuzz::GenT(r) : RandomLane(r);
      const std::uint32_t ours = FBits(emu::ref::BilinearHeight(h[0], h[1], h[2], h[3], fx, fz));
      const std::uint32_t theirs = SfBilinear(h[0], h[1], h[2], h[3], fx, fz);
      if (ours != theirs) {
        log.Print("ref %08X softfloat %08X", ours, theirs);
        return false;
      }
      return true;
    });

    run("box_radius_on_axis transcription vs SoftFloat chain", 201, [](Rng& r, MismatchLog& log) {
      float box[15];
      float axis[3];
      RandomBox(r, box);
      RandomAxis(r, axis);
      const std::uint32_t ours = FBits(emu::ref::BoxRadiusOnAxis(box, axis));
      const std::uint32_t theirs = SfBoxRadius(box, axis);
      if (ours != theirs) {
        log.Print("ref %08X softfloat %08X", ours, theirs);
        return false;
      }
      return true;
    });

    run("vector_length_zyx transcription vs SoftFloat chain", 202, [](Rng& r, MismatchLog& log) {
      float v[3];
      RandomVec(r, v);
      const std::uint32_t ours = FBits(emu::ref::VectorLengthZYX(v));
      const std::uint32_t theirs = SfVectorLength(v);
      if (ours != theirs) {
        log.Print("ref %08X softfloat %08X", ours, theirs);
        return false;
      }
      return true;
    });

    run("scale_vector* transcriptions vs SoftFloat chains", 203, [](Rng& r, MismatchLog& log) {
      float v[3];
      float t[3];
      RandomVec(r, v);
      RandomVec(r, t);
      const float s = r.Chance(50) ? difffuzz::GenFloat(r) : RandomLane(r);
      const float lsq = CallerLengthSq(r, v);
      float out[3];
      std::uint32_t expect[3];
      bool ok = true;

      emu::ref::ScaleVector(out, v, s);
      SfScale(v, Sf::L(s), expect);
      ok &= FBits(out[0]) == expect[0] && FBits(out[1]) == expect[1] && FBits(out[2]) == expect[2];

      emu::ref::ScaleVectorToLength(out, v, lsq, s);
      SfScale(v, Sf::D(Sf::L(s), Sf::Q(Sf::L(lsq))), expect);
      ok &= FBits(out[0]) == expect[0] && FBits(out[1]) == expect[1] && FBits(out[2]) == expect[2];

      std::memcpy(out, v, sizeof out);
      emu::ref::ScaleVectorMaxLength(out, out + 1, out + 2, s, lsq);
      ok &= FBits(out[0]) == expect[0] && FBits(out[1]) == expect[1] && FBits(out[2]) == expect[2];

      emu::ref::ScaleVectorToLengthOf(out, v, t, lsq);
      const Ext tx = Sf::L(t[0]);
      const Ext ty = Sf::L(t[1]);
      const Ext tz = Sf::L(t[2]);
      const Ext targetSq = Sf::A(Sf::A(Sf::M(tx, tx), Sf::M(ty, ty)), Sf::M(tz, tz));
      SfScale(v, Sf::Q(Sf::D(targetSq, Sf::L(lsq))), expect);
      ok &= FBits(out[0]) == expect[0] && FBits(out[1]) == expect[1] && FBits(out[2]) == expect[2];
      if (!ok) {
        log.Print("v %08X %08X %08X s %08X lsq %08X", FBits(v[0]), FBits(v[1]), FBits(v[2]), FBits(s), FBits(lsq));
      }
      return ok;
    });
  }
} // namespace x87t
