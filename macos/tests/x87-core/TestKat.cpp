// Known answers that need no transcendental kernel (macos/docs/DETERMINISM.md §6.1.4), the
// expected values from legacy/math/X87Math.h and from x87_model.py (an exact Fraction
// model of the PC24 chains, independent of the C++). Each answer is checked on the
// extern "C" entry point (the shipped path) and on the Layer 1 transcription.

#include <cmath>
#include <limits>

#include "EmuApi.h"
#include "TestCommon.h"

namespace x87t
{
  namespace emu = faf_posix::x87::emu;

  namespace
  {
    int gChecks = 0;
    int gFailures = 0;

    void Check(const bool ok, const char* what, const std::uint64_t got, const std::uint64_t want)
    {
      ++gChecks;
      if (!ok) {
        ++gFailures;
        std::printf("  KAT FAIL %s: got %08" PRIX64 " want %08" PRIX64 "\n", what, got, want);
      }
    }

    void CheckF(const float got, const std::uint32_t want, const char* what)
    {
      Check(FBits(got) == want, what, FBits(got), want);
    }

    void CheckI(const std::int32_t got, const std::int32_t want, const char* what)
    {
      Check(got == want, what, static_cast<std::uint32_t>(got), static_cast<std::uint32_t>(want));
    }

    void CheckE(const Ext got, const Ext want, const char* what)
    {
      ++gChecks;
      if (!Same(got, want)) {
        ++gFailures;
        std::printf("  KAT FAIL %s: got %s want %s\n", what, Hex(got).c_str(), Hex(want).c_str());
      }
    }
  } // namespace

  void RunKnownAnswers()
  {
    gChecks = 0;
    gFailures = 0;

    // --- VectorLengthZYX(1e-25) -> 0x15F79688 (X87Math.h; retail). Float arithmetic gives 0.
    {
      const float e = FFromBits(0x15F79688u);
      const float lanes[3][3] = {{e, 0, 0}, {0, e, 0}, {0, 0, e}};
      for (const auto& v : lanes) {
        CheckF(msvc8_x87_vector_length_zyx(v), 0x15F79688u, "vector_length_zyx(1e-25)");
        CheckF(emu::ref::VectorLengthZYX(v), 0x15F79688u, "ref vector_length_zyx(1e-25)");
      }
      const float all[3] = {e, e, e};
      CheckF(msvc8_x87_vector_length_zyx(all), 0x16566AE2u, "vector_length_zyx(1e-25 x3) (x87_model.py)");
      // Lanes above ~1.8e19 keep a finite length (float squares overflow).
      const float big[3] = {3e19f, 0.0f, 0.0f};
      CheckF(msvc8_x87_vector_length_zyx(big), FBits(3e19f), "vector_length_zyx(3e19)");
    }

    // --- ScaleVectorToLength: denormal outputs (two roundings: PC24, then the store) and a
    // scale that overflows float but not the x87 register (x87_model.py).
    {
      struct Case
      {
        std::uint32_t src[3];
        std::uint32_t lengthSq;
        std::uint32_t length;
        std::uint32_t want[3];
      };
      static constexpr Case kCases[] = {
        {{0xBF183AA3u, 0x3EA57228u, 0x3F3B7DC1u}, 0x3FED5E35u, 0x00076CF4u, {0x80033E16u, 0x0001C314u, 0x0003FE5Eu}},
        {{0xBF5E1AD1u, 0xBD71BAB2u, 0x3F369194u}, 0x3FAFCCFEu, 0x00000409u, {0x800002FDu, 0x80000034u, 0x00000275u}},
        {{0x3EE218E5u, 0x3F40C2EDu, 0x3EBAFEDCu}, 0x3FC55745u, 0x00006283u, {0x00002309u, 0x00003BBDu, 0x00001CFAu}},
        {{0x3F05CC31u, 0xBF1B638Fu, 0xBC4463CAu}, 0x3FF2A5D7u, 0x00001E9Fu, {0x00000BA0u, 0x80000D80u, 0x80000044u}},
        {{0x0DA24260u, 0x8E224260u, 0x0E736390u}, 0x000116C2u, 0x7149F2CAu, {0x60AD790Au, 0xE12D790Au, 0x61821AC8u}},
      };
      for (const Case& c : kCases) {
        const float src[3] = {FFromBits(c.src[0]), FFromBits(c.src[1]), FFromBits(c.src[2])};
        float out[3];
        msvc8_x87_scale_vector_to_length(out, src, FFromBits(c.lengthSq), FFromBits(c.length));
        for (int i = 0; i < 3; ++i) {
          CheckF(out[i], c.want[i], "scale_vector_to_length (x87_model.py)");
        }
        emu::ref::ScaleVectorToLength(out, src, FFromBits(c.lengthSq), FFromBits(c.length));
        for (int i = 0; i < 3; ++i) {
          CheckF(out[i], c.want[i], "ref scale_vector_to_length (x87_model.py)");
        }
      }
    }

    // --- round_to_int (fld; fistp, RC nearest): ties to even, the integer indefinite.
    {
      struct Case
      {
        std::uint32_t in;
        std::int32_t want;
      };
      constexpr std::int32_t kInd = std::numeric_limits<std::int32_t>::min();
      static constexpr Case kCases[] = {
        {0x3F000000u, 0},           // 0.5
        {0xBF000000u, 0},           // -0.5
        {0x3FC00000u, 2},           // 1.5
        {0x40200000u, 2},           // 2.5
        {0xC0200000u, -2},          // -2.5
        {0xC0600000u, -4},          // -3.5
        {0x3EFFFFFFu, 0},           // 0.49999997
        {0x3F000001u, 1},           // 0.50000006
        {0x3F7FFFFFu, 1},           // 0.99999994
        {0x4B000001u, 8388609},     // 2^23 + 1
        {0x4AFFFFFFu, 8388608},     // 8388607.5 -> even
        {0x4EFFFFFFu, 2147483520},  // largest float below 2^31
        {0x4F000000u, kInd},        // 2^31
        {0xCF000000u, kInd},        // -2^31 (the indefinite's own bits)
        {0xCF000001u, kInd},        // below -2^31
        {0x7F800000u, kInd},        // +inf
        {0xFF800000u, kInd},        // -inf
        {0x7FC00000u, kInd},        // NaN
        {0xFFC00000u, kInd},        // indefinite NaN
        {0x7F800001u, kInd},        // SNaN
        {0x00000001u, 0},           // denormal
        {0x80000000u, 0},           // -0
        {0xBF800000u, -1},
        {0x7F7FFFFFu, kInd},        // FLT_MAX
      };
      for (const Case& c : kCases) {
        CheckI(msvc8_x87_round_to_int(FFromBits(c.in)), c.want, "round_to_int");
        CheckI(emu::ref::RoundToInt(FFromBits(c.in)), c.want, "ref round_to_int");
      }
    }

    // --- control word: Windows x64's 0x027F (TakesSse2Lane reads the low byte 0x7F).
    Check(msvc8_x87_control_word() == 0x027Fu, "control_word", msvc8_x87_control_word(), 0x027F);

    // --- tanf with |x| >= 2^63: fptan pushes nothing, so `fstp st(0); fstp m32` stores the
    // indefinite from an empty register (DETERMINISM.md §3.2 #5). Needs only the range
    // check, which the stand-in kernel shares with the contract.
    CheckF(msvc8_x87_tanf(FFromBits(0x5F000000u)), 0xFFC00000u, "tanf(2^63)");
    CheckF(msvc8_x87_tanf(FFromBits(0xDF800000u)), 0xFFC00000u, "tanf(-2^64)");

    // --- Layer 1 basics.
    CheckE(FromFloat(1.0f), kOne, "fld1 == fld 1.0f");
    CheckE(Sqrt(FromInt32(-1), Precision::P24), kIndefinite, "fsqrt(-1) -> indefinite");
    CheckE(Sqrt(core::Zero(true), Precision::P24), core::Zero(true), "fsqrt(-0) -> -0");
    CheckE(Mul(core::Infinity(false), core::Zero(false), Precision::P24), kIndefinite, "inf * 0 -> indefinite");
    CheckE(Sub(core::Infinity(false), core::Infinity(false), Precision::P64), kIndefinite, "inf - inf -> indefinite");
    CheckE(Div(core::Zero(false), core::Zero(true), Precision::P24), kIndefinite, "0 / 0 -> indefinite");
    CheckE(Div(FromInt32(1), core::Zero(true), Precision::P24), core::Infinity(true), "1 / -0 -> -inf");
    CheckE(Sub(FromInt32(3), FromInt32(3), Precision::P24), core::Zero(false), "x - x -> +0");
    Check(FBits(ToFloat(kIndefinite)) == 0xFFC00000u, "store32(indefinite)", FBits(ToFloat(kIndefinite)), 0xFFC00000u);
    Check(DBits(ToDouble(kIndefinite)) == 0xFFF8000000000000ull, "store64(indefinite)", DBits(ToDouble(kIndefinite)), 0xFFF8000000000000ull);
    // The larger significand wins between two NaNs, whatever the operand order.
    {
      const Ext small = FromFloat(FFromBits(0x7FC00001u));
      const Ext large = FromFloat(FFromBits(0xFFC12345u));
      CheckE(Add(small, large, Precision::P24), large, "NaN + NaN: larger significand");
      CheckE(Add(large, small, Precision::P24), large, "NaN + NaN: larger significand (swapped)");
      CheckF(ToFloat(Add(FromFloat(FFromBits(0x7F800001u)), FromInt32(1), Precision::P24)), 0x7FC00001u, "SNaN + 1 -> quieted");
    }
    // PC24 keeps the extended exponent: FLT_MAX * 2 / 4 is finite, 2^-149 * 2^-149 * 2^149 is 2^-149.
    {
      const Ext flt_max = FromFloat(FFromBits(0x7F7FFFFFu));
      const Ext twice = Mul(flt_max, FromInt32(2), Precision::P24);
      CheckF(ToFloat(Div(twice, FromInt32(4), Precision::P24)), 0x7EFFFFFFu, "PC24 extended range (overflow side)");
      const Ext tiny = FromFloat(FFromBits(0x00000001u));
      const Ext tinier = Mul(tiny, tiny, Precision::P24);
      CheckF(ToFloat(Mul(tinier, FromFloat(std::ldexp(1.0f, 127)), Precision::P24)), 0x00000000u, "PC24 extended range: 2^-298 * 2^127 -> 0 at the store");
      CheckF(ToFloat(Div(tinier, tiny, Precision::P24)), 0x00000001u, "PC24 extended range (underflow side)");
    }
    // frndint and fscale.
    CheckE(Frndint(FromFloat(2.5f)), FromInt32(2), "frndint(2.5) = 2");
    CheckE(Frndint(FromFloat(-0.25f)), core::Zero(true), "frndint(-0.25) = -0");
    CheckE(Fscale(FromFloat(1.5f), FromFloat(-3.9f)), FromFloat(0.1875f), "fscale(1.5, -3.9) = 1.5 * 2^-3");
    CheckE(Fscale(core::Zero(false), core::Infinity(false)), kIndefinite, "fscale(0, +inf) invalid");
    // The constants (X87Types.h) against their double values.
    Check(ToDouble(kL2E) == 1.4426950408889634, "fldl2e", DBits(ToDouble(kL2E)), DBits(1.4426950408889634));
    Check(ToDouble(kLN2) == 0.6931471805599453, "fldln2", DBits(ToDouble(kLN2)), DBits(0.6931471805599453));
    Check(ToDouble(kLG2) == 0.3010299956639812, "fldlg2", DBits(ToDouble(kLG2)), DBits(0.3010299956639812));
    Check(ToDouble(kPi) == 3.141592653589793, "fldpi", DBits(ToDouble(kPi)), DBits(3.141592653589793));

    Record("known answers (DETERMINISM.md §6.1.4, X87Math.h, x87_model.py)", static_cast<std::uint64_t>(gChecks), static_cast<std::uint64_t>(gFailures));
  }
} // namespace x87t
