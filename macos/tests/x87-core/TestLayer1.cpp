// Layer 1 (X87Core.h) against Berkeley SoftFloat 3e (8086 specialization, RNE,
// extF80_roundingPrecision 32/64/80): every arithmetic operation at every precision,
// every conversion, frndint and fscale, on random and constructed inputs, plus exhaustive
// passes over all 2^32 floats and int32s.

#include "EmuApi.h"
#include "Gen.h"

namespace x87t
{
  namespace
  {
    constexpr std::uint64_t kSeed = 0x5EED2007C0DE0001ull;

    [[nodiscard]] const char* OpName(const Op op)
    {
      switch (op) {
      case Op::Add:
        return "add";
      case Op::Sub:
        return "sub";
      case Op::Mul:
        return "mul";
      case Op::Div:
        return "div";
      default:
        return "sqrt";
      }
    }

    [[nodiscard]] Ext Ours(const Op op, const Ext a, const Ext b, const Precision p)
    {
      switch (op) {
      case Op::Add:
        return Add(a, b, p);
      case Op::Sub:
        return Sub(a, b, p);
      case Op::Mul:
        return Mul(a, b, p);
      case Op::Div:
        return Div(a, b, p);
      default:
        return Sqrt(a, p);
      }
    }

    [[nodiscard]] Ext Theirs(const Op op, const Ext a, const Ext b)
    {
      Ext r{};
      switch (op) {
      case Op::Add:
        x87t_sf_add(&a, &b, &r);
        break;
      case Op::Sub:
        x87t_sf_sub(&a, &b, &r);
        break;
      case Op::Mul:
        x87t_sf_mul(&a, &b, &r);
        break;
      case Op::Div:
        x87t_sf_div(&a, &b, &r);
        break;
      default:
        x87t_sf_sqrt(&a, &r);
        break;
      }
      return r;
    }

    // FSCALE oracle: Intel's special-value table written out, SoftFloat's NaN propagation
    // (extF80_add of the two operands), and for finite operands one SoftFloat multiply of
    // ST0 normalized to [1, 2) by an exact power of two (a normal or denormal extF80), so
    // the only rounding is SoftFloat's.
    [[nodiscard]] Ext FscaleOracle(const Ext st0, const Ext st1)
    {
      if (IsNaN(st0) || IsNaN(st1)) {
        Ext r{};
        x87t_sf_add(&st0, &st1, &r);
        return r;
      }
      const bool sign0 = SignOf(st0);
      if (IsInf(st1)) {
        if (!SignOf(st1)) {
          return IsZero(st0) ? kIndefinite : core::Infinity(sign0);
        }
        return IsInf(st0) ? kIndefinite : core::Zero(sign0);
      }
      if (IsInf(st0)) {
        return core::Infinity(sign0);
      }
      if (IsZero(st0)) {
        return core::Zero(sign0);
      }
      std::int32_t n = 0;
      if (!IsZero(st1)) {
        if (ExpFieldOf(st1) - kExtBias >= 30) {
          n = SignOf(st1) ? -(1 << 30) : (1 << 30);
        } else {
          n = x87t_sf_to_i32_trunc(&st1);
        }
      }
      std::uint64_t sig = 0;
      std::int32_t exp = 0;
      core::Normalize(st0, sig, exp);
      const Ext unit = core::Pack(sign0, kExtBias, sig);
      const std::int64_t e = static_cast<std::int64_t>(exp - kExtBias) + n;
      if (e > 16383) {
        return core::Infinity(sign0);
      }
      Ext power{};
      if (e >= -16382) {
        power = core::Pack(false, static_cast<std::int32_t>(e + kExtBias), core::kIntegerBit);
      } else if (e >= -16445) {
        power = core::Pack(false, 0, 1ull << (e + 16445));
      } else if (e == -16446) {
        return sig == core::kIntegerBit ? core::Zero(sign0) : core::Pack(sign0, 0, 1);
      } else {
        return core::Zero(sign0);
      }
      Ext r{};
      x87t_sf_mul(&unit, &power, &r);
      return r;
    }

    [[nodiscard]] Ext RandomScaleExponent(Rng& r)
    {
      switch (r.Below(8)) {
      case 0:
        return FromInt32(r.Between(-70, 70));
      case 1:
        return FromInt32(r.Between(-40000, 40000));
      case 2:
        return FromDouble(r.Range(-20000.0, 20000.0));
      case 3:
        return FromDouble(r.Range(-2.0, 2.0));
      case 4:
        return RandomSpecial(r);
      case 5:
        return FromInt32(r.Chance(50) ? r.Between(16300, 16520) : -r.Between(16300, 16520));
      default:
        return RandomExt(r);
      }
    }
  } // namespace

  void RunLayer1Ops()
  {
    static constexpr Op kOps[] = {Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Sqrt};
    static constexpr Precision kPrecisions[] = {Precision::P24, Precision::P53, Precision::P64};
    for (const Op op : kOps) {
      for (const Precision p : kPrecisions) {
        const std::string name = std::string("layer1 ") + OpName(op) + " " + PName(p) + " vs SoftFloat";
        MismatchLog log(name.c_str());
        const std::uint64_t n = Iters(100'000'000);
        const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
          x87t_sf_set_precision(SfBits(p));
          Rng r(Mix(kSeed + static_cast<std::uint64_t>(op) * 16 + static_cast<std::uint64_t>(p), chunk));
          std::uint64_t mismatches = 0;
          for (std::uint64_t i = begin; i < end; ++i) {
            Ext a{};
            Ext b{};
            if (op == Op::Sqrt) {
              if (!(r.Chance(15) && TiePair(r, op, p, a, b))) {
                a = RandomExt(r);
              }
              if (r.Chance(60)) {
                a = Fabs(a);
              }
            } else {
              RandomPair(r, op, p, a, b);
            }
            const Ext ours = Ours(op, a, b, p);
            const Ext theirs = Theirs(op, a, b);
            if (!Same(ours, theirs)) {
              ++mismatches;
              log.Print("%s(%s, %s) ours %s softfloat %s", OpName(op), Hex(a).c_str(), Hex(b).c_str(), Hex(ours).c_str(), Hex(theirs).c_str());
            }
          }
          return mismatches;
        });
        Record(name, n, bad);
      }
    }
  }

  void RunConversions()
  {
    {
      const char* name = "FromDouble (fld m64) vs SoftFloat";
      MismatchLog log(name);
      const std::uint64_t n = Iters(100'000'000);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(kSeed + 101, chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          std::uint64_t bits = r.Next();
          switch (r.Below(4)) {
          case 0:
            bits &= 0x800FFFFFFFFFFFFFull;  // zero / denormal
            break;
          case 1:
            bits |= 0x7FF0000000000000ull;  // inf / NaN
            if (r.Chance(30)) {
              bits &= 0xFFF0000000000000ull;
            }
            break;
          default:
            break;
          }
          const Ext ours = FromDouble(DFromBits(bits));
          Ext theirs{};
          x87t_sf_from_f64(bits, &theirs);
          if (!Same(ours, theirs)) {
            ++mismatches;
            log.Print("%016" PRIX64 " ours %s softfloat %s", bits, Hex(ours).c_str(), Hex(theirs).c_str());
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    }

    struct Store
    {
      const char* name;
      int which;  // 0 float, 1 double, 2 int32 (fistp), 3 frndint
      std::int32_t centers[4];
      std::int32_t spread;
    };
    static constexpr Store kStores[] = {
      {"ToFloat (fstp m32) vs SoftFloat", 0, {-126, -149, 0, 127}, 30},
      {"ToDouble (fstp m64) vs SoftFloat", 1, {-1022, -1074, 0, 1023}, 60},
      {"ToInt32 (fistp m32, RNE) vs SoftFloat", 2, {-1, 15, 30, 31}, 4},
      {"Frndint (RNE) vs SoftFloat", 3, {-1, 20, 52, 63}, 6},
    };
    for (const Store& store : kStores) {
      MismatchLog log(store.name);
      const std::uint64_t n = Iters(100'000'000);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        x87t_sf_set_precision(80);
        Rng r(Mix(kSeed + 200 + static_cast<std::uint64_t>(store.which), chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          const Ext a = r.Chance(20) ? RandomExt(r) : RandomNear(r, store.centers[r.Below(4)], store.spread);
          std::uint64_t ours = 0;
          std::uint64_t theirs = 0;
          Ext oursExt{};
          Ext theirsExt{};
          switch (store.which) {
          case 0:
            ours = FBits(ToFloat(a));
            theirs = x87t_sf_to_f32(&a);
            break;
          case 1:
            ours = DBits(ToDouble(a));
            theirs = x87t_sf_to_f64(&a);
            break;
          case 2:
            ours = static_cast<std::uint32_t>(ToInt32(a));
            theirs = static_cast<std::uint32_t>(x87t_sf_to_i32(&a));
            break;
          default:
            oursExt = Frndint(a);
            x87t_sf_round_to_int(&a, &theirsExt);
            ours = Same(oursExt, theirsExt) ? 0 : 1;
            theirs = 0;
            break;
          }
          if (ours != theirs) {
            ++mismatches;
            if (store.which == 3) {
              log.Print("%s ours %s softfloat %s", Hex(a).c_str(), Hex(oursExt).c_str(), Hex(theirsExt).c_str());
            } else {
              log.Print("%s ours %" PRIX64 " softfloat %" PRIX64, Hex(a).c_str(), ours, theirs);
            }
          }
        }
        return mismatches;
      });
      Record(store.name, n, bad);
    }

    {
      const char* name = "Fscale vs Intel table + SoftFloat power-of-two multiply";
      MismatchLog log(name);
      const std::uint64_t n = Iters(100'000'000);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        x87t_sf_set_precision(80);
        Rng r(Mix(kSeed + 300, chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          const Ext st0 = RandomExt(r);
          const Ext st1 = RandomScaleExponent(r);
          const Ext ours = Fscale(st0, st1);
          const Ext theirs = FscaleOracle(st0, st1);
          if (!Same(ours, theirs)) {
            ++mismatches;
            log.Print("fscale(%s, %s) ours %s oracle %s", Hex(st0).c_str(), Hex(st1).c_str(), Hex(ours).c_str(), Hex(theirs).c_str());
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    }

    {
      // FCHS / FABS touch only the sign, NaNs included.
      const char* name = "Fchs / Fabs sign-only";
      MismatchLog log(name);
      const std::uint64_t n = Iters(10'000'000);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(kSeed + 400, chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          const Ext a = RandomExt(r);
          const Ext c = Fchs(a);
          const Ext f = Fabs(a);
          if (c.signif != a.signif || c.signExp != (a.signExp ^ 0x8000u) || f.signif != a.signif || f.signExp != (a.signExp & 0x7FFFu)) {
            ++mismatches;
            log.Print("%s", Hex(a).c_str());
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    }
  }

  void RunExhaustive()
  {
    // One pass over all 2^32 bit patterns: every float through fld/fstp/fistp/frndint/
    // fsqrt P24 against SoftFloat, every int32 through fild, and the Layer 2
    // round_to_int against its transcription.
    enum
    {
      kFromFloat,
      kRoundTrip,
      kFistp,
      kFrndint,
      kSqrt24,
      kFromInt32,
      kRoundToIntFast,
      kCount
    };
    static constexpr const char* kNames[kCount] = {
      "exhaustive FromFloat (fld m32), all 2^32 floats",
      "exhaustive ToFloat(FromFloat(f)), all 2^32 floats",
      "exhaustive ToInt32 (fistp) of every float",
      "exhaustive Frndint of every float",
      "exhaustive Sqrt P24 of every float (then fstp m32)",
      "exhaustive FromInt32 (fild m32), all 2^32 int32",
      "exhaustive round_to_int Layer 2 vs Layer 1, all floats",
    };
    std::atomic<std::uint64_t> counts[kCount] = {};
    std::vector<MismatchLog> logs;
    for (const char* name : kNames) {
      logs.emplace_back(name);
    }
    const std::uint64_t total = QuickMode() ? (1ull << 26) : (1ull << 32);
    ParallelChunks(total, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
      x87t_sf_set_precision(32);
      std::uint64_t local[kCount] = {};
      // In quick mode, stride across the whole space instead of the low 2^26 patterns.
      const std::uint64_t stride = total == (1ull << 32) ? 1 : 64;
      for (std::uint64_t i = begin; i < end; ++i) {
        const std::uint32_t bits = static_cast<std::uint32_t>((i * stride) ^ ((i * 0x9E3779B1ull) & (stride - 1)));
        const float f = FFromBits(bits);
        const Ext ours = FromFloat(f);
        Ext theirs{};
        x87t_sf_from_f32(bits, &theirs);
        if (!Same(ours, theirs)) {
          ++local[kFromFloat];
          logs[kFromFloat].Print("%08X ours %s softfloat %s", bits, Hex(ours).c_str(), Hex(theirs).c_str());
        }
        if (FBits(ToFloat(ours)) != x87t_sf_to_f32(&theirs)) {
          ++local[kRoundTrip];
          logs[kRoundTrip].Print("%08X", bits);
        }
        const std::int32_t fistp = ToInt32(ours);
        if (fistp != x87t_sf_to_i32(&theirs)) {
          ++local[kFistp];
          logs[kFistp].Print("%08X ours %d softfloat %d", bits, fistp, x87t_sf_to_i32(&theirs));
        }
        Ext rounded{};
        x87t_sf_round_to_int(&theirs, &rounded);
        if (!Same(Frndint(ours), rounded)) {
          ++local[kFrndint];
          logs[kFrndint].Print("%08X", bits);
        }
        Ext root{};
        x87t_sf_sqrt(&theirs, &root);
        const Ext ourRoot = Sqrt(ours, Precision::P24);
        if (!Same(ourRoot, root) || FBits(ToFloat(ourRoot)) != x87t_sf_to_f32(&root)) {
          ++local[kSqrt24];
          logs[kSqrt24].Print("%08X ours %s softfloat %s", bits, Hex(ourRoot).c_str(), Hex(root).c_str());
        }
        const std::int32_t integer = static_cast<std::int32_t>(bits);
        Ext fild{};
        x87t_sf_from_i32(integer, &fild);
        if (!Same(FromInt32(integer), fild)) {
          ++local[kFromInt32];
          logs[kFromInt32].Print("%d", integer);
        }
        if (faf_posix::x87::emu::fast::RoundToInt(f) != faf_posix::x87::emu::ref::RoundToInt(f)) {
          ++local[kRoundToIntFast];
          logs[kRoundToIntFast].Print("%08X", bits);
        }
      }
      for (int k = 0; k < kCount; ++k) {
        counts[k].fetch_add(local[k]);
      }
      return std::uint64_t{0};
    });
    for (int k = 0; k < kCount; ++k) {
      Record(kNames[k], total, counts[k].load());
    }
  }
} // namespace x87t
