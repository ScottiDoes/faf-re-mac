#pragma once

// Software x87 for the native build: the value type, the constants, and the contract
// between the arithmetic core (X87Core.h), the transcendental kernels (X87Kernels.cpp)
// and the msvc8_x87_* routines (X87Emu.cpp). Design: macos/docs/DETERMINISM.md §3.
//
// Lead-owned: the core and kernel owners implement against this header and do not
// change it (report needed changes instead). Pure C++20 with no platform dependency, so
// the same code also builds into a Windows x64 test that compares it with the real x87
// unit (DETERMINISM.md §6.2).

#include <cstdint>

namespace faf_posix::x87
{
  // An x87 register value in the 80-bit extended encoding, the layout `fstp tbyte` stores
  // and SoftFloat's extFloat80_t uses (LITTLEENDIAN): 64-bit significand with an explicit
  // integer bit (bit 63), then sign (bit 15) and biased exponent (bits 0-14, bias 16383).
  // Precision control 24/53 rounds the significand only; the exponent range stays the
  // extended one until a store.
  struct Ext
  {
    std::uint64_t signif;
    std::uint16_t signExp;

    friend constexpr bool operator==(const Ext&, const Ext&) = default;
  };

  constexpr int kExtBias = 16383;

  // Precision control (FPU control word PC field). The game runs every routine but five
  // at P24 (_PC_24); the five use only PC-independent instructions.
  enum class Precision : std::uint8_t
  {
    P24 = 24,
    P53 = 53,
    P64 = 64,
  };

  // The control word the routines report (msvc8_x87_control_word): Windows x64's default.
  constexpr unsigned int kDefaultControlWord = 0x027Fu;

  // Constant loads, each the hardware's 64-bit round-to-nearest value (DETERMINISM.md §3.3;
  // they match QEMU's floatx80 constants).
  constexpr Ext kOne{0x8000000000000000ull, 0x3FFF};   // fld1
  constexpr Ext kL2E{0xB8AA3B295C17F0BCull, 0x3FFF};   // fldl2e  log2(e)
  constexpr Ext kLN2{0xB17217F7D1CF79ACull, 0x3FFE};   // fldln2  ln(2)
  constexpr Ext kLG2{0x9A209A84FBCFF799ull, 0x3FFD};   // fldlg2  log10(2)
  constexpr Ext kPi{0xC90FDAA22168C235ull, 0x4000};    // fldpi

  // The x87 "real indefinite": the QNaN an invalid operation produces (stores as
  // 0xFFC00000 / 0xFFF8000000000000).
  constexpr Ext kIndefinite{0xC000000000000000ull, 0xFFFF};

  // --- Transcendental kernels (X87Kernels.cpp) ----------------------------------------
  // Each returns what the instruction leaves in the register: the function value rounded
  // to nearest at 64 significand bits (the hardware is faithful to < 1 ulp64; we model it
  // as correctly rounded after Intel's argument reduction, see DETERMINISM.md §3.4, §3.6).
  // Precision control does not apply to these instructions. Operands follow Intel's
  // stack naming: ST0 is the top, ST1 the one below.

  // FSIN / FCOS on ST0. For |x| >= 2^63 the instruction sets C2 and leaves ST0
  // unchanged: the kernel returns x and sets *outOfRange (may be null). ±inf gives the
  // indefinite. Reduction uses Intel's 66-bit pi (DETERMINISM.md §3.4).
  Ext Fsin(Ext x, bool* outOfRange) noexcept;
  Ext Fcos(Ext x, bool* outOfRange) noexcept;

  // FPTAN on ST0. Returns false for |x| >= 2^63 (C2 set, nothing pushed, ST0 unchanged);
  // otherwise stores tan(x) in *tangent and returns true (the instruction then pushes 1.0).
  bool Fptan(Ext x, Ext* tangent) noexcept;

  // FPATAN: arctan(ST1 / ST0) with the quadrant from both signs, Intel's special-value
  // table for zeros and infinities. y = ST1, x = ST0.
  Ext Fpatan(Ext y, Ext x) noexcept;

  // FYL2X: ST1 * log2(ST0), computed as one operation and rounded once. y = ST1, x = ST0.
  Ext Fyl2x(Ext y, Ext x) noexcept;

  // F2XM1: 2^ST0 - 1 for |ST0| <= 1 (outside that the hardware result is undefined; the
  // game never goes there).
  Ext F2xm1(Ext x) noexcept;
} // namespace faf_posix::x87
