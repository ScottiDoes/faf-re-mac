#pragma once

// Software x87 arithmetic for the native build (macos/docs/DETERMINISM.md §3.3).
//
// Layer 1 (namespace faf_posix::x87): the exact reference. Add, Sub, Mul, Div and Sqrt
// form the exact result in unsigned __int128 plus a sticky bit and round it once, to
// nearest-even, to the precision-control width (24, 53 or 64 significand bits) while the
// exponent keeps the 15-bit extended range: no overflow at FLT_MAX and no denormal below
// FLT_MIN until a store. Stores (ToFloat, ToDouble, ToInt32) round once more, with
// denormals and overflow, as `fstp dword` / `fstp qword` / `fistp dword` do. NaN rules are
// the x87's: an invalid operation gives the real indefinite (kIndefinite), a signalling
// NaN is quieted, and of two NaNs the one with the larger significand wins.
//
// The rounding step transcribes Berkeley SoftFloat 3e's softfloat_roundPackToExtF80 for
// round-to-nearest-even (BSD-3; dependencies/berkeley-softfloat-3), which also pins down
// what happens at the edges of the extended range the game never reaches (overflow to
// infinity at 2^16384, extended denormals below 2^-16382). SoftFloat with the "8086"
// specialization is the test oracle (macos/tests/x87-core): Layer 1 matches it bit for bit
// on every operation and conversion.
//
// Only rounding control "nearest" exists here: the game never changes RC (§3.1).
// Operands are expected in canonical encodings (what fld and these functions produce).
// Unnormals and pseudo-denormals are taken at their numeric value; a real 387+ rejects
// unnormals as invalid, but nothing here can produce one.
//
// Layer 2 (namespace faf_posix::x87::fast): double-based fast paths for PC24 operations
// whose operands fit in a double and whose results stay in the double normal range.
// F24 operands (floats and PC24 results) need only a double operation then a 24-bit
// rounding of its bit pattern: double rounding is innocuous at 53 >= 2*24 + 2 bits
// (Figueroa). An F53 operand (a double argument) needs round-to-odd at 53 bits first
// (PcRoundCore.h; Boldo-Melquiond). A routine may use Layer 2 only after it is shown to
// be bit-identical to its Layer 1 transcription (promotion rule, §3.3).
//
// Pure C++20 with no platform dependency, so the same header also builds into the Windows
// x64 differential test against the real x87 unit (§6.2). Needs unsigned __int128 (clang,
// clang-cl, GCC) but never divides 128-bit values, so no compiler-rt helper is required.

#include <bit>
#include <cmath>
#include <cstdint>

#include "PcRoundCore.h"
#include "X87Types.h"

#if !defined(__SIZEOF_INT128__)
#error "X87Core.h needs unsigned __int128 (clang, clang-cl or GCC)"
#endif

namespace faf_posix::x87
{
  // --- Classification ---------------------------------------------------------------

  [[nodiscard]] constexpr bool SignOf(const Ext a) noexcept
  {
    return (a.signExp & 0x8000u) != 0;
  }

  [[nodiscard]] constexpr std::int32_t ExpFieldOf(const Ext a) noexcept
  {
    return a.signExp & 0x7FFF;
  }

  [[nodiscard]] constexpr bool IsNaN(const Ext a) noexcept
  {
    return ExpFieldOf(a) == 0x7FFF && (a.signif & 0x7FFFFFFFFFFFFFFFull) != 0;
  }

  [[nodiscard]] constexpr bool IsSignalingNaN(const Ext a) noexcept
  {
    return IsNaN(a) && (a.signif & 0x4000000000000000ull) == 0;
  }

  [[nodiscard]] constexpr bool IsInf(const Ext a) noexcept
  {
    return ExpFieldOf(a) == 0x7FFF && (a.signif & 0x7FFFFFFFFFFFFFFFull) == 0;
  }

  // Zero value: canonical zeros, and the pseudo-zeros an x87 never produces.
  [[nodiscard]] constexpr bool IsZero(const Ext a) noexcept
  {
    return a.signif == 0 && ExpFieldOf(a) != 0x7FFF;
  }

  namespace core
  {
    using u128 = unsigned __int128;

    constexpr std::uint64_t kIntegerBit = 0x8000000000000000ull;
    constexpr std::uint64_t kQuietBit = 0x4000000000000000ull;
    constexpr std::int32_t kMaxExpField = 0x7FFF;

    [[nodiscard]] constexpr Ext Pack(const bool sign, const std::int32_t expField, const std::uint64_t signif) noexcept
    {
      return Ext{signif, static_cast<std::uint16_t>((sign ? 0x8000u : 0u) | static_cast<std::uint32_t>(expField))};
    }

    [[nodiscard]] constexpr Ext Infinity(const bool sign) noexcept
    {
      return Pack(sign, kMaxExpField, kIntegerBit);
    }

    [[nodiscard]] constexpr Ext Zero(const bool sign) noexcept
    {
      return Pack(sign, 0, 0);
    }

    [[nodiscard]] constexpr std::uint64_t Hi(const u128 v) noexcept
    {
      return static_cast<std::uint64_t>(v >> 64);
    }

    [[nodiscard]] constexpr std::uint64_t Lo(const u128 v) noexcept
    {
      return static_cast<std::uint64_t>(v);
    }

    [[nodiscard]] constexpr int CountLeadingZeros128(const u128 v) noexcept
    {
      return Hi(v) != 0 ? std::countl_zero(Hi(v)) : 64 + std::countl_zero(Lo(v));
    }

    // SoftFloat softfloat_shiftRightJam64 (dist >= 1).
    [[nodiscard]] constexpr std::uint64_t ShiftRightJam64(const std::uint64_t a, const std::uint32_t dist) noexcept
    {
      return dist < 63 ? (a >> dist) | static_cast<std::uint64_t>((a << (-dist & 63)) != 0) : static_cast<std::uint64_t>(a != 0);
    }

    // A finite non-zero value as a significand with the integer bit set and a biased
    // exponent that may leave the encodable range: value = sig * 2^(exp - kExtBias - 63).
    // Exponent field 0 counts as 1 (denormals), as on the x87 and in SoftFloat.
    constexpr void Normalize(const Ext a, std::uint64_t& sig, std::int32_t& exp) noexcept
    {
      std::int32_t e = ExpFieldOf(a);
      std::uint64_t s = a.signif;
      if (e == 0) {
        e = 1;
      }
      if ((s & kIntegerBit) == 0) {
        const int shift = std::countl_zero(s);
        s <<= shift;
        e -= shift;
      }
      sig = s;
      exp = e;
    }

    // Rounds sign * (sig + sigExtra / 2^64) / 2^63 * 2^(exp - kExtBias) to `p` significand
    // bits, nearest-even, and packs it: SoftFloat 3e softfloat_roundPackToExtF80 with
    // roundingMode = near_even and roundingPrecision = 32 / 64 / 80. `sig` has its integer
    // bit set; `sigExtra` holds the next 64 bits with any further sticky bit ORed into it.
    // Overflow past the extended range gives infinity; below it, an extended denormal
    // rounded at the same bit positions (as SoftFloat does).
    [[nodiscard]] constexpr Ext RoundPack(
      const bool sign, std::int32_t exp, std::uint64_t sig, std::uint64_t sigExtra, const Precision p
    ) noexcept
    {
      if (p != Precision::P64) {
        std::uint64_t roundIncrement = p == Precision::P53 ? 0x0000000000000400ull : 0x0000008000000000ull;
        std::uint64_t roundMask = p == Precision::P53 ? 0x00000000000007FFull : 0x000000FFFFFFFFFFull;
        sig |= static_cast<std::uint64_t>(sigExtra != 0);
        std::uint64_t roundBits = sig & roundMask;
        if (0x7FFDu <= static_cast<std::uint32_t>(exp - 1)) {
          if (exp <= 0) {
            sig = ShiftRightJam64(sig, static_cast<std::uint32_t>(1 - exp));
            roundBits = sig & roundMask;
            sig += roundIncrement;
            exp = (sig & kIntegerBit) != 0 ? 1 : 0;
            roundIncrement = roundMask + 1;
            if ((roundBits << 1) == roundIncrement) {
              roundMask |= roundIncrement;
            }
            sig &= ~roundMask;
            return Pack(sign, exp, sig);
          }
          if (0x7FFE < exp || (exp == 0x7FFE && static_cast<std::uint64_t>(sig + roundIncrement) < sig)) {
            return Infinity(sign);
          }
        }
        sig += roundIncrement;
        if (sig < roundIncrement) {
          ++exp;
          sig = kIntegerBit;
        }
        roundIncrement = roundMask + 1;
        if ((roundBits << 1) == roundIncrement) {
          roundMask |= roundIncrement;
        }
        sig &= ~roundMask;
        return Pack(sign, exp, sig);
      }

      bool doIncrement = kIntegerBit <= sigExtra;
      if (0x7FFDu <= static_cast<std::uint32_t>(exp - 1)) {
        if (exp <= 0) {
          const std::uint32_t dist = static_cast<std::uint32_t>(1 - exp);
          const bool extraSticky = sigExtra != 0;
          if (dist < 64) {
            sigExtra = sig << (-dist & 63);
            sig >>= dist;
          } else {
            sigExtra = dist == 64 ? sig : static_cast<std::uint64_t>(sig != 0);
            sig = 0;
          }
          sigExtra |= static_cast<std::uint64_t>(extraSticky);
          exp = 0;
          doIncrement = kIntegerBit <= sigExtra;
          if (doIncrement) {
            ++sig;
            sig &= ~static_cast<std::uint64_t>((sigExtra & 0x7FFFFFFFFFFFFFFFull) == 0);
            exp = (sig & kIntegerBit) != 0 ? 1 : 0;
          }
          return Pack(sign, exp, sig);
        }
        if (0x7FFE < exp || (exp == 0x7FFE && sig == ~0ull && doIncrement)) {
          return Infinity(sign);
        }
      }
      if (doIncrement) {
        ++sig;
        if (sig == 0) {
          ++exp;
          sig = kIntegerBit;
        } else {
          sig &= ~static_cast<std::uint64_t>((sigExtra & 0x7FFFFFFFFFFFFFFFull) == 0);
        }
      }
      return Pack(sign, exp, sig);
    }

    // x87 operand NaN rules (SoftFloat 8086 softfloat_propagateNaNExtF80UI): a signalling
    // NaN is quieted; an SNaN against a QNaN gives the QNaN; otherwise the larger
    // significand wins, and for equal significands the positive one. At least one of a, b
    // is a NaN. Symmetric in a and b.
    [[nodiscard]] constexpr Ext PropagateNaN(const Ext a, const Ext b) noexcept
    {
      const bool sigNaNA = IsSignalingNaN(a);
      const bool sigNaNB = IsSignalingNaN(b);
      const Ext quietA{a.signif | 0xC000000000000000ull, a.signExp};
      const Ext quietB{b.signif | 0xC000000000000000ull, b.signExp};
      if (sigNaNA || sigNaNB) {
        if (sigNaNA && !sigNaNB) {
          return IsNaN(b) ? quietB : quietA;
        }
        if (!sigNaNA) {
          return IsNaN(a) ? quietA : quietB;
        }
      }
      const std::uint32_t magA = a.signExp & 0x7FFFu;
      const std::uint32_t magB = b.signExp & 0x7FFFu;
      if (magA < magB) {
        return quietB;
      }
      if (magB < magA) {
        return quietA;
      }
      if (a.signif < b.signif) {
        return quietB;
      }
      if (b.signif < a.signif) {
        return quietA;
      }
      return a.signExp < b.signExp ? quietA : quietB;
    }

    [[nodiscard]] constexpr Ext QuietNaN(const Ext a) noexcept
    {
      return Ext{a.signif | 0xC000000000000000ull, a.signExp};
    }

    // (u1:u0) / v for u1 < v and v >= 2^63 (Hacker's Delight divlu with 32-bit digits):
    // the quotient, and the remainder in `rem`. Portable: no 128-bit division.
    [[nodiscard]] constexpr std::uint64_t DivideNormalized(
      const std::uint64_t u1, const std::uint64_t u0, const std::uint64_t v, std::uint64_t& rem
    ) noexcept
    {
      constexpr std::uint64_t kBase = 1ull << 32;
      const std::uint64_t vn1 = v >> 32;
      const std::uint64_t vn0 = v & 0xFFFFFFFFull;
      const std::uint64_t un1 = u0 >> 32;
      const std::uint64_t un0 = u0 & 0xFFFFFFFFull;

      std::uint64_t q1 = u1 / vn1;
      std::uint64_t rhat = u1 - q1 * vn1;
      while (q1 >= kBase || q1 * vn0 > kBase * rhat + un1) {
        --q1;
        rhat += vn1;
        if (rhat >= kBase) {
          break;
        }
      }
      const std::uint64_t un21 = u1 * kBase + un1 - q1 * v;

      std::uint64_t q0 = un21 / vn1;
      rhat = un21 - q0 * vn1;
      while (q0 >= kBase || q0 * vn0 > kBase * rhat + un0) {
        --q0;
        rhat += vn1;
        if (rhat >= kBase) {
          break;
        }
      }
      rem = un21 * kBase + un0 - q0 * v;
      return q1 * kBase + q0;
    }

    // floor(sqrt(n)) and n - root^2 for n in [2^126, 2^128): a double estimate, one
    // Newton step in integers, then an exact fix-up.
    [[nodiscard]] inline std::uint64_t SquareRoot128(const u128 n, u128& rem) noexcept
    {
      const double estimate = std::sqrt(static_cast<double>(Hi(n))) * 4294967296.0;
      std::uint64_t r = estimate >= 18446744073709551615.0 ? ~0ull : static_cast<std::uint64_t>(estimate);
      if (r < kIntegerBit) {
        r = kIntegerBit;
      }
      const u128 square = static_cast<u128>(r) * r;
      std::uint64_t unused = 0;
      if (square > n) {
        const u128 d = square - n;
        const std::uint64_t q = DivideNormalized(Hi(d), Lo(d), r, unused);
        r -= q >> 1;
      } else {
        const u128 d = n - square;
        const std::uint64_t step = DivideNormalized(Hi(d), Lo(d), r, unused) >> 1;
        r = step > ~0ull - r ? ~0ull : r + step;
      }
      while (static_cast<u128>(r) * r > n) {
        --r;
      }
      while (r != ~0ull && static_cast<u128>(r + 1) * (r + 1) <= n) {
        ++r;
      }
      rem = n - static_cast<u128>(r) * r;
      return r;
    }

    // The bits below a quotient's last digit, from the remainder: 0, below half, exactly
    // half, above half. Enough for any rounding at bit 64 or above (plus sticky shifts).
    [[nodiscard]] constexpr std::uint64_t FractionBits(const std::uint64_t rem, const std::uint64_t divisor) noexcept
    {
      if (rem == 0) {
        return 0;
      }
      const std::uint64_t other = divisor - rem;
      if (rem > other) {
        return 0xC000000000000000ull;
      }
      return rem == other ? 0x8000000000000000ull : 0x4000000000000000ull;
    }

    [[nodiscard]] constexpr Ext AddSigned(const Ext a, const Ext b, const bool negateB, const Precision p) noexcept
    {
      if (IsNaN(a) || IsNaN(b)) {
        return PropagateNaN(a, b);
      }
      bool signA = SignOf(a);
      bool signB = SignOf(b) != negateB;
      if (IsInf(a)) {
        if (IsInf(b) && signA != signB) {
          return kIndefinite;
        }
        return Infinity(signA);
      }
      if (IsInf(b)) {
        return Infinity(signB);
      }
      const bool zeroA = IsZero(a);
      const bool zeroB = IsZero(b);
      if (zeroA && zeroB) {
        return Zero(signA && signB);
      }

      std::uint64_t sigA = 0;
      std::uint64_t sigB = 0;
      std::int32_t expA = 0;
      std::int32_t expB = 0;
      if (zeroB || zeroA) {
        // x + 0 still rounds x to the precision-control width.
        const bool useA = zeroB;
        Normalize(useA ? a : b, sigA, expA);
        return RoundPack(useA ? signA : signB, expA, sigA, 0, p);
      }
      Normalize(a, sigA, expA);
      Normalize(b, sigB, expB);
      if (expA < expB || (expA == expB && sigA < sigB)) {
        std::uint64_t ts = sigA;
        sigA = sigB;
        sigB = ts;
        std::int32_t te = expA;
        expA = expB;
        expB = te;
        bool tg = signA;
        signA = signB;
        signB = tg;
      }

      // |a| >= |b|: align b below a in 128 bits, the bits shifted out jammed into bit 0.
      const u128 big = static_cast<u128>(sigA) << 64;
      u128 small = static_cast<u128>(sigB) << 64;
      const std::uint32_t diff = static_cast<std::uint32_t>(expA - expB);
      if (diff != 0) {
        if (diff < 128) {
          const bool lost = (small << (128 - diff)) != 0;
          small = (small >> diff) | static_cast<u128>(lost);
        } else {
          small = 1;
        }
      }

      std::int32_t expZ = expA;
      u128 z = 0;
      if (signA == signB) {
        z = big + small;
        if (z < big) {
          z = (z >> 1) | (z & 1) | (static_cast<u128>(1) << 127);
          ++expZ;
        }
      } else {
        z = big - small;
        if (z == 0) {
          return Zero(false);
        }
        const int shift = CountLeadingZeros128(z);
        z <<= shift;
        expZ -= shift;
      }
      return RoundPack(signA, expZ, Hi(z), Lo(z), p);
    }
  } // namespace core

  // --- Layer 1: arithmetic under precision control -----------------------------------

  // a + b (FADD).
  [[nodiscard]] constexpr Ext Add(const Ext a, const Ext b, const Precision p) noexcept
  {
    return core::AddSigned(a, b, false, p);
  }

  // a - b (FSUB with a the minuend; FSUBR swaps the operands).
  [[nodiscard]] constexpr Ext Sub(const Ext a, const Ext b, const Precision p) noexcept
  {
    return core::AddSigned(a, b, true, p);
  }

  // a * b (FMUL).
  [[nodiscard]] constexpr Ext Mul(const Ext a, const Ext b, const Precision p) noexcept
  {
    if (IsNaN(a) || IsNaN(b)) {
      return core::PropagateNaN(a, b);
    }
    const bool signZ = SignOf(a) != SignOf(b);
    if (IsInf(a) || IsInf(b)) {
      if (IsZero(a) || IsZero(b)) {
        return kIndefinite;
      }
      return core::Infinity(signZ);
    }
    if (IsZero(a) || IsZero(b)) {
      return core::Zero(signZ);
    }
    std::uint64_t sigA = 0;
    std::uint64_t sigB = 0;
    std::int32_t expA = 0;
    std::int32_t expB = 0;
    core::Normalize(a, sigA, expA);
    core::Normalize(b, sigB, expB);
    core::u128 product = static_cast<core::u128>(sigA) * sigB;
    std::int32_t expZ = expA + expB - kExtBias + 1;
    if ((product >> 127) == 0) {
      product <<= 1;
      --expZ;
    }
    return core::RoundPack(signZ, expZ, core::Hi(product), core::Lo(product), p);
  }

  // a / b (FDIV with a the dividend; FDIVR swaps the operands). Division by zero gives a
  // signed infinity (ZE masked).
  [[nodiscard]] constexpr Ext Div(const Ext a, const Ext b, const Precision p) noexcept
  {
    if (IsNaN(a) || IsNaN(b)) {
      return core::PropagateNaN(a, b);
    }
    const bool signZ = SignOf(a) != SignOf(b);
    if (IsInf(a)) {
      return IsInf(b) ? kIndefinite : core::Infinity(signZ);
    }
    if (IsInf(b)) {
      return core::Zero(signZ);
    }
    if (IsZero(b)) {
      return IsZero(a) ? kIndefinite : core::Infinity(signZ);
    }
    if (IsZero(a)) {
      return core::Zero(signZ);
    }
    std::uint64_t sigA = 0;
    std::uint64_t sigB = 0;
    std::int32_t expA = 0;
    std::int32_t expB = 0;
    core::Normalize(a, sigA, expA);
    core::Normalize(b, sigB, expB);
    std::int32_t expZ = expA - expB + kExtBias;
    std::uint64_t rem = 0;
    std::uint64_t q = 0;
    if (sigA >= sigB) {
      q = core::DivideNormalized(sigA >> 1, sigA << 63, sigB, rem);
    } else {
      q = core::DivideNormalized(sigA, 0, sigB, rem);
      --expZ;
    }
    return core::RoundPack(signZ, expZ, q, core::FractionBits(rem, sigB), p);
  }

  // sqrt(a) (FSQRT). sqrt(-0) is -0; any other negative operand is invalid.
  [[nodiscard]] inline Ext Sqrt(const Ext a, const Precision p) noexcept
  {
    if (IsNaN(a)) {
      return core::QuietNaN(a);
    }
    if (IsZero(a)) {
      return core::Zero(SignOf(a));
    }
    if (SignOf(a)) {
      return kIndefinite;
    }
    if (IsInf(a)) {
      return core::Infinity(false);
    }
    std::uint64_t sig = 0;
    std::int32_t exp = 0;
    core::Normalize(a, sig, exp);
    const std::int32_t unbiased = exp - kExtBias;
    const std::int32_t expZ = (unbiased >> 1) + kExtBias;
    const core::u128 n = static_cast<core::u128>(sig) << (63 + (unbiased & 1));
    core::u128 rem = 0;
    const std::uint64_t root = core::SquareRoot128(n, rem);
    // sqrt(n) - root >= 1/2 exactly when rem > root; it is never exactly 1/2.
    const std::uint64_t fraction = rem == 0 ? 0 : (rem > root ? 0xC000000000000000ull : 0x4000000000000000ull);
    return core::RoundPack(false, expZ, root, fraction, p);
  }

  // FCHS / FABS: sign only, never a NaN quieting.
  [[nodiscard]] constexpr Ext Fchs(const Ext a) noexcept
  {
    return Ext{a.signif, static_cast<std::uint16_t>(a.signExp ^ 0x8000u)};
  }

  [[nodiscard]] constexpr Ext Fabs(const Ext a) noexcept
  {
    return Ext{a.signif, static_cast<std::uint16_t>(a.signExp & 0x7FFFu)};
  }

  // FRNDINT under RC = nearest: round to an integer, ties to even. Precision control does
  // not apply. SoftFloat extF80_roundToInt(near_even).
  [[nodiscard]] constexpr Ext Frndint(const Ext a) noexcept
  {
    if (IsNaN(a)) {
      return core::QuietNaN(a);
    }
    const bool sign = SignOf(a);
    if (IsInf(a)) {
      return core::Infinity(sign);
    }
    if (IsZero(a)) {
      return core::Zero(sign);
    }
    std::uint64_t sig = 0;
    std::int32_t exp = 0;
    core::Normalize(a, sig, exp);
    if (0x403E <= exp) {
      return core::Pack(sign, exp, sig);
    }
    if (exp <= 0x3FFE) {
      if (exp == 0x3FFE && (sig & 0x7FFFFFFFFFFFFFFFull) != 0) {
        return core::Pack(sign, 0x3FFF, core::kIntegerBit);
      }
      return core::Zero(sign);
    }
    const std::uint64_t lastBitMask = 1ull << (0x403E - exp);
    const std::uint64_t roundBitsMask = lastBitMask - 1;
    std::uint64_t sigZ = sig + (lastBitMask >> 1);
    if ((sigZ & roundBitsMask) == 0) {
      sigZ &= ~lastBitMask;
    }
    sigZ &= ~roundBitsMask;
    if (sigZ == 0) {
      return core::Pack(sign, exp + 1, core::kIntegerBit);
    }
    return core::Pack(sign, exp, sigZ);
  }

  // FSCALE: ST0 * 2^trunc(ST1), exact apart from leaving the extended range (overflow to
  // infinity, extended denormals); precision control does not apply. Special operands
  // follow Intel's FSCALE table: 0 * 2^+inf and inf * 2^-inf are invalid.
  [[nodiscard]] constexpr Ext Fscale(const Ext st0, const Ext st1) noexcept
  {
    if (IsNaN(st0) || IsNaN(st1)) {
      return core::PropagateNaN(st0, st1);
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
    // trunc(ST1), clamped where the result saturates anyway (|n| > 2^20 leaves any
    // finite ST0 beyond the extended range).
    std::int32_t n = 0;
    if (!IsZero(st1)) {
      std::uint64_t sig1 = 0;
      std::int32_t exp1 = 0;
      core::Normalize(st1, sig1, exp1);
      const std::int32_t unbiased = exp1 - kExtBias;
      if (unbiased >= 0) {
        n = unbiased > 20 ? (1 << 21) : static_cast<std::int32_t>(sig1 >> (63 - unbiased));
        if (SignOf(st1)) {
          n = -n;
        }
      }
    }
    std::uint64_t sig = 0;
    std::int32_t exp = 0;
    core::Normalize(st0, sig, exp);
    return core::RoundPack(sign0, exp + n, sig, 0, Precision::P64);
  }

  // --- Layer 1: loads -----------------------------------------------------------------

  // FLD m32: exact; a signalling NaN is quieted (invalid masked).
  [[nodiscard]] constexpr Ext FromFloat(const float value) noexcept
  {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const bool sign = (bits >> 31) != 0;
    const std::int32_t exp = static_cast<std::int32_t>((bits >> 23) & 0xFFu);
    const std::uint64_t mant = bits & 0x7FFFFFu;
    if (exp == 0xFF) {
      if (mant != 0) {
        return core::Pack(sign, core::kMaxExpField, 0xC000000000000000ull | (mant << 40));
      }
      return core::Infinity(sign);
    }
    if (exp == 0) {
      if (mant == 0) {
        return core::Zero(sign);
      }
      const int top = 63 - std::countl_zero(mant);
      return core::Pack(sign, kExtBias + top - 149, mant << (63 - top));
    }
    return core::Pack(sign, exp - 127 + kExtBias, (core::kIntegerBit | (mant << 40)));
  }

  // FLD m64: exact; a signalling NaN is quieted.
  [[nodiscard]] constexpr Ext FromDouble(const double value) noexcept
  {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    const bool sign = (bits >> 63) != 0;
    const std::int32_t exp = static_cast<std::int32_t>((bits >> 52) & 0x7FFu);
    const std::uint64_t mant = bits & 0x000FFFFFFFFFFFFFull;
    if (exp == 0x7FF) {
      if (mant != 0) {
        return core::Pack(sign, core::kMaxExpField, 0xC000000000000000ull | (mant << 11));
      }
      return core::Infinity(sign);
    }
    if (exp == 0) {
      if (mant == 0) {
        return core::Zero(sign);
      }
      const int top = 63 - std::countl_zero(mant);
      return core::Pack(sign, kExtBias + top - 1074, mant << (63 - top));
    }
    return core::Pack(sign, exp - 1023 + kExtBias, core::kIntegerBit | (mant << 11));
  }

  // FILD m32: exact; 0 loads as +0.
  [[nodiscard]] constexpr Ext FromInt32(const std::int32_t value) noexcept
  {
    if (value == 0) {
      return core::Zero(false);
    }
    const bool sign = value < 0;
    const std::uint64_t magnitude =
      sign ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(value)) : static_cast<std::uint64_t>(value);
    const int shift = std::countl_zero(magnitude);
    return core::Pack(sign, kExtBias + 63 - shift, magnitude << shift);
  }

  // --- Layer 1: stores ----------------------------------------------------------------

  namespace core
  {
    // Rounds to a binary interchange format with `precisionBits` significand bits and
    // `expBits` exponent bits, nearest-even, with denormals and overflow to infinity; NaNs
    // keep their sign and top payload bits and come out quiet (FSTP m32 / m64).
    [[nodiscard]] constexpr std::uint64_t StoreBinary(const Ext a, const int precisionBits, const int expBits) noexcept
    {
      const int fracBits = precisionBits - 1;
      const int dropBits = 64 - precisionBits;
      const std::int32_t bias = (1 << (expBits - 1)) - 1;
      const std::int32_t maxField = (1 << expBits) - 1;
      const std::uint64_t fracMask = (1ull << fracBits) - 1;
      const std::uint64_t signBit = static_cast<std::uint64_t>(SignOf(a)) << (fracBits + expBits);
      const std::uint64_t infBits = signBit | (static_cast<std::uint64_t>(maxField) << fracBits);

      if (ExpFieldOf(a) == kMaxExpField) {
        if (IsNaN(a)) {
          return infBits | (1ull << (fracBits - 1)) | ((a.signif >> dropBits) & fracMask);
        }
        return infBits;
      }
      if (IsZero(a)) {
        return signBit;
      }
      std::uint64_t sig = 0;
      std::int32_t exp = 0;
      Normalize(a, sig, exp);
      std::int32_t field = exp - kExtBias + bias;
      if (field >= 1) {
        std::uint64_t keep = sig >> dropBits;
        const std::uint64_t rem = sig & ((1ull << dropBits) - 1);
        const std::uint64_t half = 1ull << (dropBits - 1);
        if (rem > half || (rem == half && (keep & 1) != 0)) {
          ++keep;
        }
        if ((keep >> precisionBits) != 0) {
          keep >>= 1;
          ++field;
        }
        if (field >= maxField) {
          return infBits;
        }
        return signBit | (static_cast<std::uint64_t>(field) << fracBits) | (keep & fracMask);
      }
      // Denormal result: value / 2^(1 - bias - fracBits) = sig / 2^shift.
      const std::int32_t shift = dropBits + 1 - field;
      if (shift > 64) {
        return signBit;
      }
      std::uint64_t keep = 0;
      std::uint64_t rem = 0;
      std::uint64_t half = 0;
      if (shift == 64) {
        rem = sig;
        half = kIntegerBit;
      } else {
        keep = sig >> shift;
        rem = sig & ((1ull << shift) - 1);
        half = 1ull << (shift - 1);
      }
      if (rem > half || (rem == half && (keep & 1) != 0)) {
        ++keep;
      }
      return signBit | keep;
    }
  } // namespace core

  // FSTP m32.
  [[nodiscard]] constexpr float ToFloat(const Ext a) noexcept
  {
    return std::bit_cast<float>(static_cast<std::uint32_t>(core::StoreBinary(a, 24, 8)));
  }

  // FSTP m64.
  [[nodiscard]] constexpr double ToDouble(const Ext a) noexcept
  {
    return std::bit_cast<double>(core::StoreBinary(a, 53, 11));
  }

  // FISTP m32 under RC = nearest: ties to even; NaN, infinity or a result outside the
  // int32 range gives the integer indefinite 0x80000000 (IE masked).
  [[nodiscard]] constexpr std::int32_t ToInt32(const Ext a) noexcept
  {
    constexpr std::int32_t kIntegerIndefinite = static_cast<std::int32_t>(0x80000000u);
    if (ExpFieldOf(a) == core::kMaxExpField) {
      return kIntegerIndefinite;
    }
    if (IsZero(a)) {
      return 0;
    }
    std::uint64_t sig = 0;
    std::int32_t exp = 0;
    core::Normalize(a, sig, exp);
    const std::int32_t unbiased = exp - kExtBias;
    if (unbiased >= 31) {
      return kIntegerIndefinite; // |a| >= 2^31: out of range, or exactly -2^31
    }
    if (unbiased < -1) {
      return 0;
    }
    std::uint64_t whole = 0;
    std::uint64_t fraction = sig;
    if (unbiased >= 0) {
      whole = sig >> (63 - unbiased);
      fraction = sig << (unbiased + 1);
    }
    if (fraction > core::kIntegerBit || (fraction == core::kIntegerBit && (whole & 1) != 0)) {
      ++whole;
    }
    if (whole > 0x7FFFFFFFull) {
      return kIntegerIndefinite; // 2^31 after rounding; -2^31 is the same bits
    }
    return SignOf(a) ? -static_cast<std::int32_t>(whole) : static_cast<std::int32_t>(whole);
  }

  // --- Layer 2: double fast paths ------------------------------------------------------

  namespace fast
  {
    // Round a double to a 24-bit significand, nearest-even, on its bit pattern (add and
    // mask at bit 29). For zeros, infinities and normal doubles; a carry out of the
    // significand moves to the next binade, as it should.
    [[nodiscard]] inline double Round24(const double value) noexcept
    {
      std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
      bits += 0x0FFFFFFFull + ((bits >> 29) & 1);
      return std::bit_cast<double>(bits & ~0x1FFFFFFFull);
    }

    // PC24 operations on F24 operands (24-bit significands, held in doubles) whose exact
    // result lies in the double normal range: RN24(RN53(x)) == RN24(x) because
    // 53 >= 2 * 24 + 2 (Figueroa, "When is double rounding innocuous?", 1995).
    [[nodiscard]] inline double Add24(const double a, const double b) noexcept
    {
      return Round24(a + b);
    }

    [[nodiscard]] inline double Sub24(const double a, const double b) noexcept
    {
      return Round24(a - b);
    }

    [[nodiscard]] inline double Mul24(const double a, const double b) noexcept
    {
      return Round24(a * b);
    }

    [[nodiscard]] inline double Div24(const double a, const double b) noexcept
    {
      return Round24(a / b);
    }

    [[nodiscard]] inline double Sqrt24(const double a) noexcept
    {
      return Round24(std::sqrt(a));
    }

    // PC24 add / subtract with an F53 operand (a double argument): round-to-odd at 53
    // bits from the exact TwoSum error, then RN24 (PcRoundCore.h).
    [[nodiscard]] inline double Add24Odd(const double a, const double b) noexcept
    {
      return std::bit_cast<double>(pcround::add24(a, b, 0));
    }

    [[nodiscard]] inline double Sub24Odd(const double a, const double b) noexcept
    {
      return std::bit_cast<double>(pcround::add24(a, -b, 0));
    }
  } // namespace fast
} // namespace faf_posix::x87
