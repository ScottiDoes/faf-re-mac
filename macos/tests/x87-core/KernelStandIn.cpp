// TEST-ONLY STAND-IN for src/platform/posix/math/X87Kernels.cpp (package x87-kernels).
//
// NOT x87-exact and never linked into the engine: the values come from the host's double
// libm, so they carry 53 bits instead of the hardware's 64 and use true pi instead of
// Intel's 66-bit pi. It exists so X87Emu.cpp links in this test and the arithmetic
// around the transcendental instructions can be compared (Layer 1 transcription vs
// Layer 2 fast path, both calling this same stand-in). Special operands follow the
// X87Types.h contract closely enough for that: NaN propagation, infinities invalid,
// |x| >= 2^63 out of range.

#include <cmath>

#include "platform/posix/math/X87Core.h"

namespace faf_posix::x87
{
  namespace
  {
    [[nodiscard]] bool OutOfTrigRange(const Ext x) noexcept
    {
      return ExpFieldOf(x) >= kExtBias + 63 && !IsNaN(x);
    }
  } // namespace

  Ext Fsin(const Ext x, bool* const outOfRange) noexcept
  {
    if (outOfRange != nullptr) {
      *outOfRange = false;
    }
    if (IsNaN(x)) {
      return core::QuietNaN(x);
    }
    if (IsInf(x)) {
      return kIndefinite;
    }
    if (OutOfTrigRange(x)) {
      if (outOfRange != nullptr) {
        *outOfRange = true;
      }
      return x;
    }
    return FromDouble(std::sin(ToDouble(x)));
  }

  Ext Fcos(const Ext x, bool* const outOfRange) noexcept
  {
    if (outOfRange != nullptr) {
      *outOfRange = false;
    }
    if (IsNaN(x)) {
      return core::QuietNaN(x);
    }
    if (IsInf(x)) {
      return kIndefinite;
    }
    if (OutOfTrigRange(x)) {
      if (outOfRange != nullptr) {
        *outOfRange = true;
      }
      return x;
    }
    return FromDouble(std::cos(ToDouble(x)));
  }

  bool Fptan(const Ext x, Ext* const tangent) noexcept
  {
    if (IsNaN(x)) {
      *tangent = core::QuietNaN(x);
      return true;
    }
    if (IsInf(x)) {
      *tangent = kIndefinite;
      return true;
    }
    if (OutOfTrigRange(x)) {
      return false;
    }
    *tangent = FromDouble(std::tan(ToDouble(x)));
    return true;
  }

  Ext Fpatan(const Ext y, const Ext x) noexcept
  {
    if (IsNaN(y) || IsNaN(x)) {
      return core::PropagateNaN(y, x);
    }
    return FromDouble(std::atan2(ToDouble(y), ToDouble(x)));
  }

  Ext Fyl2x(const Ext y, const Ext x) noexcept
  {
    if (IsNaN(y) || IsNaN(x)) {
      return core::PropagateNaN(y, x);
    }
    if (SignOf(x) && !IsZero(x)) {
      return kIndefinite;
    }
    const double lx = std::log2(ToDouble(x));
    const double yy = ToDouble(y);
    if ((yy == 0.0 && std::isinf(lx)) || (std::isinf(yy) && lx == 0.0)) {
      return kIndefinite;
    }
    return FromDouble(yy * lx);
  }

  Ext F2xm1(const Ext x) noexcept
  {
    if (IsNaN(x)) {
      return core::QuietNaN(x);
    }
    return FromDouble(std::exp2(ToDouble(x)) - 1.0);
  }
} // namespace faf_posix::x87
