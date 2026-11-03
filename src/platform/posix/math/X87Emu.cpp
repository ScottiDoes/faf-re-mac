// The 30 msvc8_x87_* routines legacy/math/X87Math.cpp calls on 64-bit targets, on the
// software x87 of X87Core.h (macos/docs/DETERMINISM.md §3). Windows x64 runs the same
// instruction sequences on the real x87 unit (legacy/math/X87Math64.asm); the sim must get
// the same bits here.
//
// Each routine is first a line-by-line transcription of its X87Math64.asm procedure
// against X87Stack, a model of the x87 register stack (namespace ref; each line names the
// asm instruction it models). Register forms keep MASM operand order: the first operand is
// the destination, so `fsub st(1), st(0)` is ST1 <- ST1 - ST0 and `fdivrp st(1), st(0)` is
// ST1 <- ST0 / ST1. Memory operands are read and written at the point the asm touches
// them, so aliasing pointers behave as on Windows.
//
// Precision: every routine whose asm does `fnstcw; and eax, 0fffffcffh; fldcw` runs at
// PC24. round_to_int, sinf, cosf, tanf and atan2f leave the thread's control word alone
// (Windows x64: 0x027F, PC53) and use only instructions precision control ignores.
//
// The hot F24 routines (round_to_int, bilinear_height, box_radius_on_axis,
// vector_length_zyx, the four scale_vector ones) and the acos/asin radicand also have a
// Layer 2 version (namespace fast) on doubles; it is bit-identical to the transcription
// (macos/tests/x87-core) and hands any input that produces a NaN back to it.
//
// The transcendental instructions call the kernels of X87Types.h (X87Kernels.cpp).

#include "platform/posix/math/X87Core.h"

#include <cstdint>

namespace faf_posix::x87::emu
{
  namespace
  {
    // The control word of a Windows x64 thread (0x027F): PC53, RC nearest.
    constexpr Precision kThreadPrecision = Precision::P53;

    // The x87 register stack. Reading an empty register (stack underflow, IE masked)
    // gives the real indefinite, which is what the hardware stores or computes with.
    class X87Stack
    {
    public:
      explicit X87Stack(const Precision pc) noexcept : pc_(pc) {}

      // Loads: exact.
      void fld_m32(const float value) noexcept { Push(FromFloat(value)); }
      void fld_m64(const double value) noexcept { Push(FromDouble(value)); }
      void fild_m32(const std::int32_t value) noexcept { Push(FromDouble(static_cast<double>(value))); }
      void fld_st(const int i) noexcept { Push(St(i)); }
      void fld1() noexcept { Push(kOne); }
      void fldl2e() noexcept { Push(kL2E); }
      void fldln2() noexcept { Push(kLN2); }
      void fldlg2() noexcept { Push(kLG2); }

      // Stores: one rounding to the destination format; precision control does not apply.
      [[nodiscard]] float fstp_m32() noexcept
      {
        const float value = ToFloat(St(0));
        Pop();
        return value;
      }

      [[nodiscard]] double fstp_m64() noexcept
      {
        const double value = ToDouble(St(0));
        Pop();
        return value;
      }

      [[nodiscard]] std::int32_t fistp_m32() noexcept
      {
        const std::int32_t value = ToInt32(St(0));
        Pop();
        return value;
      }

      // fstp st(i): ST(i) <- ST0, then pop.
      void fstp_st(const int i) noexcept
      {
        Set(i, St(0));
        Pop();
      }

      void fxch(const int i) noexcept
      {
        const Ext top = St(0);
        Set(0, St(i));
        Set(i, top);
      }

      // Register forms, MASM order: `fop st(dst), st(src)`.
      void fadd(const int dst, const int src) noexcept { Set(dst, Add(St(dst), St(src), pc_)); }
      void fsub(const int dst, const int src) noexcept { Set(dst, Sub(St(dst), St(src), pc_)); }
      void fsubr(const int dst, const int src) noexcept { Set(dst, Sub(St(src), St(dst), pc_)); }
      void fmul(const int dst, const int src) noexcept { Set(dst, Mul(St(dst), St(src), pc_)); }
      void fdiv(const int dst, const int src) noexcept { Set(dst, Div(St(dst), St(src), pc_)); }
      void fdivr(const int dst, const int src) noexcept { Set(dst, Div(St(src), St(dst), pc_)); }

      // `fopp st(i), st(0)`: ST(i) <- ST(i) op ST0 (reversed for the R forms), then pop.
      void faddp(const int i) noexcept
      {
        fadd(i, 0);
        Pop();
      }

      void fmulp(const int i) noexcept
      {
        fmul(i, 0);
        Pop();
      }

      void fdivp(const int i) noexcept
      {
        fdiv(i, 0);
        Pop();
      }

      void fdivrp(const int i) noexcept
      {
        fdivr(i, 0);
        Pop();
      }

      // Memory forms on ST0: `fop dword ptr [m]` is ST0 <- ST0 op m, `fopr` is m op ST0.
      void fadd_m32(const float m) noexcept { Set(0, Add(St(0), FromFloat(m), pc_)); }
      void fsubr_m32(const float m) noexcept { Set(0, Sub(FromFloat(m), St(0), pc_)); }
      void fmul_m32(const float m) noexcept { Set(0, Mul(St(0), FromFloat(m), pc_)); }
      void fdiv_m32(const float m) noexcept { Set(0, Div(St(0), FromFloat(m), pc_)); }
      void fdivr_m32(const float m) noexcept { Set(0, Div(FromFloat(m), St(0), pc_)); }

      void fsqrt() noexcept { Set(0, Sqrt(St(0), pc_)); }
      void fabs() noexcept { Set(0, Fabs(St(0))); }
      void fchs() noexcept { Set(0, Fchs(St(0))); }
      void frndint() noexcept { Set(0, Frndint(St(0))); }
      void fscale() noexcept { Set(0, Fscale(St(0), St(1))); }

      // Transcendentals (X87Kernels.cpp); precision control does not apply.
      void fsin() noexcept { Set(0, Fsin(St(0), nullptr)); }
      void fcos() noexcept { Set(0, Fcos(St(0), nullptr)); }

      // |x| >= 2^63 leaves ST0 alone and pushes nothing (C2 = 1).
      void fptan() noexcept
      {
        Ext tangent{};
        if (Fptan(St(0), &tangent)) {
          Set(0, tangent);
          Push(kOne);
        }
      }

      // ST1 <- arctan(ST1 / ST0), pop.
      void fpatan() noexcept
      {
        const Ext result = Fpatan(St(1), St(0));
        Pop();
        Set(0, result);
      }

      // ST1 <- ST1 * log2(ST0), pop.
      void fyl2x() noexcept
      {
        const Ext result = Fyl2x(St(1), St(0));
        Pop();
        Set(0, result);
      }

      void f2xm1() noexcept { Set(0, F2xm1(St(0))); }

    private:
      [[nodiscard]] Ext St(const int i) const noexcept { return i < depth_ ? regs_[depth_ - 1 - i] : kIndefinite; }
      void Set(const int i, const Ext value) noexcept { regs_[depth_ - 1 - i] = value; }
      void Push(const Ext value) noexcept { regs_[depth_++] = value; }
      void Pop() noexcept
      {
        if (depth_ > 0) {
          --depth_;
        }
      }

      Ext regs_[8]{};
      int depth_ = 0;
      Precision pc_;
    };

    // dest and source either coincide or do not overlap: then reading every source lane
    // before writing any dest lane is what the asm's interleaved loads and stores give.
    [[nodiscard]] bool SameOrDisjoint(const float* const a, const float* const b, const std::uintptr_t lanes) noexcept
    {
      const std::uintptr_t pa = reinterpret_cast<std::uintptr_t>(a);
      const std::uintptr_t pb = reinterpret_cast<std::uintptr_t>(b);
      const std::uintptr_t bytes = lanes * sizeof(float);
      return pa == pb || pa + bytes <= pb || pb + bytes <= pa;
    }

    [[nodiscard]] bool IsNaN(const double value) noexcept
    {
      return value != value;
    }
  } // namespace

  // --- Layer 1: the asm, instruction by instruction ----------------------------------------

  namespace ref
  {
    // msvc8_x87_round_to_int (no fldcw).
    std::int32_t RoundToInt(const float value) noexcept
    {
      X87Stack fpu(kThreadPrecision);
      fpu.fld_m32(value);      // movss [rsp+8], xmm0; fld dword ptr [rsp+8]
      return fpu.fistp_m32();  // fistp dword ptr [rsp+8]; mov eax, [rsp+8]
    }

    float BilinearHeight(
      const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
    ) noexcept
    {
      // h00..h11 spilled to [rsp+8..20]; fracX at [rsp+40], fracZ at [rsp+48].
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(h01);              // fld dword ptr [rsp+12]
      fpu.fld_m32(h00);              // fld dword ptr [rsp+8]
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fld_m32(fracZ);            // fld dword ptr [rsp+48]
      fpu.fmul(2, 0);                // fmul st(2), st(0)
      fpu.fxch(2);                   // fxch st(2)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(h11);              // fld dword ptr [rsp+20]
      fpu.fld_m32(h10);              // fld dword ptr [rsp+16]
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fmulp(3);                  // fmulp st(3), st(0)
      fpu.faddp(2);                  // faddp st(2), st(0)
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fmul_m32(fracX);           // fmul dword ptr [rsp+40]
      fpu.faddp(1);                  // faddp st(1), st(0)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float SubtractBilinearHeight(
      const float top, const float h00, const float h01, const float h10, const float h11, const float fracX,
      const float fracZ
    ) noexcept
    {
      // top, h00..h10 spilled to [rsp+8..20]; h11 at [rsp+40], fracX at [rsp+48], fracZ at [rsp+56].
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(h01);              // fld dword ptr [rsp+16]
      fpu.fld_m32(h00);              // fld dword ptr [rsp+12]
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fld_m32(fracZ);            // fld dword ptr [rsp+56]
      fpu.fmul(2, 0);                // fmul st(2), st(0)
      fpu.fxch(2);                   // fxch st(2)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(h11);              // fld dword ptr [rsp+40]
      fpu.fld_m32(h10);              // fld dword ptr [rsp+20]
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fmulp(3);                  // fmulp st(3), st(0)
      fpu.faddp(2);                  // faddp st(2), st(0)
      fpu.fsub(1, 0);                // fsub st(1), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fmul_m32(fracX);           // fmul dword ptr [rsp+48]
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fsubr_m32(top);            // fsubr dword ptr [rsp+8]
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float TwisterWordInRange(const float lower, const float upper, const std::uint32_t word) noexcept
    {
      // lower, upper at [rsp+8], [rsp+12]; the word at [rsp+16]; 2^32 at [rsp+20], 2^-32 at [rsp+28].
      X87Stack fpu(Precision::P24);                   // fnstcw; and eax, 0fffffcffh; fldcw
      const std::int32_t signedWord = static_cast<std::int32_t>(word);
      fpu.fild_m32(signedWord);                       // fild dword ptr [rsp+16]
      if (signedWord < 0) {                           // test r8d, r8d; jge @F
        fpu.fadd_m32(4294967296.0f);                  // fadd dword ptr [rsp+20]
      }
      fpu.fld_m32(upper);                             // fld dword ptr [rsp+12]
      fpu.fld_m32(lower);                             // fld dword ptr [rsp+8]
      fpu.fsub(1, 0);                                 // fsub st(1), st(0)
      fpu.fxch(2);                                    // fxch st(2)
      fpu.fmulp(1);                                   // fmulp st(1), st(0)
      fpu.fmul_m32(2.3283064365386962890625e-10f);   // fmul dword ptr [rsp+28]
      fpu.faddp(1);                                   // faddp st(1), st(0)
      return fpu.fstp_m32();                          // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    // msvc8_x87_sinf / cosf (no fldcw).
    float Sinf(const float x) noexcept
    {
      X87Stack fpu(kThreadPrecision);
      fpu.fld_m32(x);         // fld dword ptr [rsp+8]
      fpu.fsin();             // fsin
      return fpu.fstp_m32();  // fstp dword ptr [rsp+8]
    }

    float Cosf(const float x) noexcept
    {
      X87Stack fpu(kThreadPrecision);
      fpu.fld_m32(x);         // fld dword ptr [rsp+8]
      fpu.fcos();             // fcos
      return fpu.fstp_m32();  // fstp dword ptr [rsp+8]
    }

    // msvc8_x87_tanf (no fldcw). For |x| >= 2^63 fptan pushes nothing, `fstp st(0)` pops
    // x and the store reads an empty register: the indefinite, 0xFFC00000.
    float Tanf(const float x) noexcept
    {
      X87Stack fpu(kThreadPrecision);
      fpu.fld_m32(x);         // fld dword ptr [rsp+8]
      fpu.fptan();            // fptan: tan(x), then 1.0
      fpu.fstp_st(0);         // fstp st(0): drop the 1.0
      return fpu.fstp_m32();  // fstp dword ptr [rsp+8]
    }

    // msvc8_x87_atan2f (no fldcw).
    float Atan2f(const float y, const float x) noexcept
    {
      X87Stack fpu(kThreadPrecision);
      fpu.fld_m32(y);         // fld dword ptr [rsp+8]
      fpu.fld_m32(x);         // fld dword ptr [rsp+16]
      fpu.fpatan();           // fpatan: atan(y / x) with the quadrant of (x, y)
      return fpu.fstp_m32();  // fstp dword ptr [rsp+8]
    }

    double Acos(const double x) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m64(x);                // fld qword ptr [rsp+8]
      fpu.fld1();                    // fld1
      fpu.fadd(0, 1);                // fadd st(0), st(1)
      fpu.fld1();                    // fld1
      fpu.fsub(0, 2);                // fsub st(0), st(2)
      fpu.fmulp(1);                  // fmulp st(1), st(0)
      fpu.fsqrt();                   // fsqrt
      fpu.fxch(1);                   // fxch st(1)
      fpu.fpatan();                  // fpatan
      return fpu.fstp_m64();         // fstp qword ptr [rsp+8]; fldcw (restore)
    }

    double Asin(const double x) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m64(x);                // fld qword ptr [rsp+8]
      fpu.fld1();                    // fld1
      fpu.fadd(0, 1);                // fadd st(0), st(1)
      fpu.fld1();                    // fld1
      fpu.fsub(0, 2);                // fsub st(0), st(2)
      fpu.fmulp(1);                  // fmulp st(1), st(0)
      fpu.fsqrt();                   // fsqrt
      fpu.fpatan();                  // fpatan
      return fpu.fstp_m64();         // fstp qword ptr [rsp+8]; fldcw (restore)
    }

    double Pow(const double x, const double y) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m64(y);                // fld qword ptr [rsp+16]
      fpu.fld_m64(x);                // fld qword ptr [rsp+8]
      fpu.fyl2x();                   // fyl2x
      fpu.fld_st(0);                 // fld st(0)
      fpu.frndint();                 // frndint
      fpu.fsubr(1, 0);               // fsubr st(1), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fchs();                    // fchs
      fpu.f2xm1();                   // f2xm1
      fpu.fld1();                    // fld1
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fscale();                  // fscale
      fpu.fstp_st(1);                // fstp st(1)
      return fpu.fstp_m64();         // fstp qword ptr [rsp+8]; fldcw (restore)
    }

    float Atan2Difference(const float firstY, const float firstX, const float secondY, const float secondX) noexcept
    {
      X87Stack fpu(Precision::P24);              // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(firstY);                       // fld dword ptr [rsp+8]
      fpu.fld_m32(firstX);                       // fld dword ptr [rsp+12]
      fpu.fpatan();                              // fpatan
      const float firstAngle = fpu.fstp_m32();   // fstp dword ptr [rsp+8]
      fpu.fld_m32(secondY);                      // fld dword ptr [rsp+16]
      fpu.fld_m32(secondX);                      // fld dword ptr [rsp+20]
      fpu.fpatan();                              // fpatan
      fpu.fsubr_m32(firstAngle);                 // fsubr dword ptr [rsp+8]
      return fpu.fstp_m32();                     // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float Atan2Plus(const float y, const float x, const float addend) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(y);                // fld dword ptr [rsp+8]
      fpu.fld_m32(x);                // fld dword ptr [rsp+12]
      fpu.fpatan();                  // fpatan
      fpu.fadd_m32(addend);          // fadd dword ptr [rsp+16]
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float SinTimesCos(const float sinArg, const float cosArg) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(cosArg);           // fld dword ptr [rsp+12]
      fpu.fcos();                    // fcos
      fpu.fld_m32(sinArg);           // fld dword ptr [rsp+8]
      fpu.fsin();                    // fsin
      fpu.fmulp(1);                  // fmulp st(1), st(0)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float SinOfProductTimes(const float a, const float b, const float factor) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(a);                // fld dword ptr [rsp+8]
      fpu.fmul_m32(b);               // fmul dword ptr [rsp+12]
      fpu.fsin();                    // fsin
      fpu.fmul_m32(factor);          // fmul dword ptr [rsp+16]
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float SinTimes(const float sinArg, const float factor) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(sinArg);           // fld dword ptr [rsp+8]
      fpu.fsin();                    // fsin
      fpu.fmul_m32(factor);          // fmul dword ptr [rsp+12]
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    void SlerpSphericalBlend(
      float* const outLanes, const float* const q1Lanes, const float* const rhsLanes, const float angle,
      const float oneMinusT, const float t
    ) noexcept
    {
      // rcx = out, rdx = q1, r8 = rhs; angle spilled to [rsp+32]; oneMinusT at [rsp+40],
      // t at [rsp+48].
      X87Stack fpu(Precision::P24);      // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(angle);                // fld dword ptr [rsp+32]
      fpu.fld_st(0);                     // fld st(0)
      fpu.fsin();                        // fsin
      fpu.fld1();                        // fld1
      fpu.fdivrp(1);                     // fdivrp st(1), st(0)
      fpu.fld_st(1);                     // fld st(1)
      fpu.fmul_m32(oneMinusT);           // fmul dword ptr [rsp+40]
      fpu.fsin();                        // fsin
      fpu.fmul(0, 1);                    // fmul st(0), st(1)
      fpu.fld_m32(t);                    // fld dword ptr [rsp+48]
      fpu.fmulp(3);                      // fmulp st(3), st(0)
      fpu.fxch(2);                       // fxch st(2)
      fpu.fsin();                        // fsin
      fpu.fmulp(1);                      // fmulp st(1), st(0)
      fpu.fld_m32(q1Lanes[0]);           // fld dword ptr [rdx]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.fld_m32(rhsLanes[0]);          // fld dword ptr [r8]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.faddp(1);                      // faddp st(1), st(0)
      outLanes[0] = fpu.fstp_m32();      // fstp dword ptr [rcx]
      fpu.fld_m32(q1Lanes[1]);           // fld dword ptr [rdx+4]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.fld_m32(rhsLanes[1]);          // fld dword ptr [r8+4]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.faddp(1);                      // faddp st(1), st(0)
      outLanes[1] = fpu.fstp_m32();      // fstp dword ptr [rcx+4]
      fpu.fld_m32(q1Lanes[2]);           // fld dword ptr [rdx+8]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.fld_m32(rhsLanes[2]);          // fld dword ptr [r8+8]
      fpu.fmul(0, 2);                    // fmul st(0), st(2)
      fpu.faddp(1);                      // faddp st(1), st(0)
      outLanes[2] = fpu.fstp_m32();      // fstp dword ptr [rcx+8]
      fpu.fld_m32(q1Lanes[3]);           // fld dword ptr [rdx+12]
      fpu.fmulp(2);                      // fmulp st(2), st(0)
      fpu.fmul_m32(rhsLanes[3]);         // fmul dword ptr [r8+12]
      fpu.faddp(1);                      // faddp st(1), st(0)
      outLanes[3] = fpu.fstp_m32();      // fstp dword ptr [rcx+12]; fldcw (restore)
    }

    float IntrinsicExp(const float x) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(x);                // fld dword ptr [rsp+8]
      fpu.fldl2e();                  // fldl2e
      fpu.fmulp(1);                  // fmulp st(1), st(0)
      fpu.fld_st(0);                 // fld st(0)
      fpu.frndint();                 // frndint
      fpu.fxch(1);                   // fxch st(1)
      fpu.fsub(0, 1);                // fsub st(0), st(1)
      fpu.f2xm1();                   // f2xm1
      fpu.fld1();                    // fld1
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fscale();                  // fscale
      fpu.fstp_st(1);                // fstp st(1)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float IntrinsicLn(const float x) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(x);                // fld dword ptr [rsp+8]
      fpu.fldln2();                  // fldln2
      fpu.fxch(1);                   // fxch st(1)
      fpu.fyl2x();                   // fyl2x
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float IntrinsicLog10(const float x) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(x);                // fld dword ptr [rsp+8]
      fpu.fldlg2();                  // fldlg2
      fpu.fxch(1);                   // fxch st(1)
      fpu.fyl2x();                   // fyl2x
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float IntrinsicLnQuotient(const float x, const double base) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(x);                // fld dword ptr [rsp+8]
      fpu.fldln2();                  // fldln2
      fpu.fxch(1);                   // fxch st(1)
      fpu.fyl2x();                   // fyl2x
      fpu.fld_m64(base);             // fld qword ptr [rsp+16]
      fpu.fldln2();                  // fldln2
      fpu.fxch(1);                   // fxch st(1)
      fpu.fyl2x();                   // fyl2x
      fpu.fdivp(1);                  // fdivp st(1), st(0)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float ScaledLnQuotient(const float numerator, const float denominator, const float scale) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(numerator);        // fld dword ptr [rsp+8]
      fpu.fdiv_m32(denominator);     // fdiv dword ptr [rsp+12]
      fpu.fldln2();                  // fldln2
      fpu.fxch(1);                   // fxch st(1)
      fpu.fyl2x();                   // fyl2x
      fpu.fmul_m32(scale);           // fmul dword ptr [rsp+16]
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    float CosTimesCos(const float firstArg, const float secondArg) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(secondArg);        // fld dword ptr [rsp+12]
      fpu.fcos();                    // fcos
      fpu.fld_m32(firstArg);         // fld dword ptr [rsp+8]
      fpu.fcos();                    // fcos
      fpu.fmulp(1);                  // fmulp st(1), st(0)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+8]; fldcw (restore)
    }

    void ScaleVectorMaxLength(float* const x, float* const y, float* const z, const float maxLength, const float lengthSq) noexcept
    {
      // rcx = x, rdx = y, r8 = z; maxLength spilled to [rsp+32]; lengthSq at [rsp+40].
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(lengthSq);         // fld dword ptr [rsp+40]
      fpu.fsqrt();                   // fsqrt
      fpu.fdivr_m32(maxLength);      // fdivr dword ptr [rsp+32]
      fpu.fld_m32(*x);               // fld dword ptr [rcx]
      fpu.fmul(0, 1);                // fmul st(0), st(1)
      *x = fpu.fstp_m32();           // fstp dword ptr [rcx]
      fpu.fld_m32(*y);               // fld dword ptr [rdx]
      fpu.fmul(0, 1);                // fmul st(0), st(1)
      *y = fpu.fstp_m32();           // fstp dword ptr [rdx]
      fpu.fmul_m32(*z);              // fmul dword ptr [r8]
      *z = fpu.fstp_m32();           // fstp dword ptr [r8]; fldcw (restore)
    }

    void ScaleVectorToLength(float* const destLanes, const float* const sourceLanes, const float lengthSq, const float length) noexcept
    {
      // rcx = dest, rdx = source; lengthSq spilled to [rsp+32], length to [rsp+36].
      X87Stack fpu(Precision::P24);      // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(lengthSq);             // fld dword ptr [rsp+32]
      fpu.fsqrt();                       // fsqrt
      fpu.fdivr_m32(length);             // fdivr dword ptr [rsp+36]
      fpu.fld_m32(sourceLanes[0]);       // fld dword ptr [rdx]
      fpu.fmul(0, 1);                    // fmul st(0), st(1)
      destLanes[0] = fpu.fstp_m32();     // fstp dword ptr [rcx]
      fpu.fld_st(0);                     // fld st(0)
      fpu.fmul_m32(sourceLanes[1]);      // fmul dword ptr [rdx+4]
      destLanes[1] = fpu.fstp_m32();     // fstp dword ptr [rcx+4]
      fpu.fmul_m32(sourceLanes[2]);      // fmul dword ptr [rdx+8]
      destLanes[2] = fpu.fstp_m32();     // fstp dword ptr [rcx+8]; fldcw (restore)
    }

    void ScaleVector(float* const destLanes, const float* const sourceLanes, const float scale) noexcept
    {
      // rcx = dest, rdx = source; scale spilled to [rsp+32].
      X87Stack fpu(Precision::P24);      // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(scale);                // fld dword ptr [rsp+32]
      fpu.fld_m32(sourceLanes[0]);       // fld dword ptr [rdx]
      fpu.fmul(0, 1);                    // fmul st(0), st(1)
      destLanes[0] = fpu.fstp_m32();     // fstp dword ptr [rcx]
      fpu.fld_m32(sourceLanes[1]);       // fld dword ptr [rdx+4]
      fpu.fmul(0, 1);                    // fmul st(0), st(1)
      destLanes[1] = fpu.fstp_m32();     // fstp dword ptr [rcx+4]
      fpu.fmul_m32(sourceLanes[2]);      // fmul dword ptr [rdx+8]
      destLanes[2] = fpu.fstp_m32();     // fstp dword ptr [rcx+8]; fldcw (restore)
    }

    void ScaleVectorToLengthOf(
      float* const destLanes, const float* const sourceLanes, const float* const targetLanes, const float sourceLengthSq
    ) noexcept
    {
      // rcx = dest, rdx = source, r8 = target; sourceLengthSq spilled to [rsp+32].
      X87Stack fpu(Precision::P24);      // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(targetLanes[0]);       // fld dword ptr [r8]
      fpu.fmul(0, 0);                    // fmul st(0), st(0)
      fpu.fld_m32(targetLanes[1]);       // fld dword ptr [r8+4]
      fpu.fmul(0, 0);                    // fmul st(0), st(0)
      fpu.faddp(1);                      // faddp st(1), st(0)
      fpu.fld_m32(targetLanes[2]);       // fld dword ptr [r8+8]
      fpu.fmul(0, 0);                    // fmul st(0), st(0)
      fpu.faddp(1);                      // faddp st(1), st(0)
      fpu.fdiv_m32(sourceLengthSq);      // fdiv dword ptr [rsp+32]
      fpu.fsqrt();                       // fsqrt
      fpu.fld_m32(sourceLanes[0]);       // fld dword ptr [rdx]
      fpu.fmul(0, 1);                    // fmul st(0), st(1)
      destLanes[0] = fpu.fstp_m32();     // fstp dword ptr [rcx]
      fpu.fld_st(0);                     // fld st(0)
      fpu.fmul_m32(sourceLanes[1]);      // fmul dword ptr [rdx+4]
      destLanes[1] = fpu.fstp_m32();     // fstp dword ptr [rcx+4]
      fpu.fmul_m32(sourceLanes[2]);      // fmul dword ptr [rdx+8]
      destLanes[2] = fpu.fstp_m32();     // fstp dword ptr [rcx+8]; fldcw (restore)
    }

    float BoxRadiusOnAxis(const float* const boxLanes, const float* const axisLanes) noexcept
    {
      // rcx = box (center, axis0..2, extent: 15 lanes), rdx = axis.
      const float* const box = boxLanes;
      const float* const axis = axisLanes;
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(box[10]);          // fld dword ptr [rcx+28h]
      fpu.fld_m32(axis[1]);          // fld dword ptr [rdx+4]
      fpu.fmul(1, 0);                // fmul st(1), st(0)
      fpu.fld_m32(box[11]);          // fld dword ptr [rcx+2Ch]
      fpu.fld_m32(axis[2]);          // fld dword ptr [rdx+8]
      fpu.fmul(1, 0);                // fmul st(1), st(0)
      fpu.fxch(3);                   // fxch st(3)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(box[9]);           // fld dword ptr [rcx+24h]
      fpu.fld_m32(axis[0]);          // fld dword ptr [rdx]
      fpu.fmul(1, 0);                // fmul st(1), st(0)
      fpu.fxch(2);                   // fxch st(2)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fabs();                    // fabs
      fpu.fmul_m32(box[14]);         // fmul dword ptr [rcx+38h]
      fpu.fld_m32(box[7]);           // fld dword ptr [rcx+1Ch]
      fpu.fmul(0, 3);                // fmul st(0), st(3)
      fpu.fld_m32(box[8]);           // fld dword ptr [rcx+20h]
      fpu.fmul(0, 5);                // fmul st(0), st(5)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(box[6]);           // fld dword ptr [rcx+18h]
      fpu.fmul(0, 3);                // fmul st(0), st(3)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fabs();                    // fabs
      fpu.fmul_m32(box[13]);         // fmul dword ptr [rcx+34h]
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(box[4]);           // fld dword ptr [rcx+10h]
      fpu.fmulp(3);                  // fmulp st(3), st(0)
      fpu.fld_m32(box[5]);           // fld dword ptr [rcx+14h]
      fpu.fmulp(4);                  // fmulp st(4), st(0)
      fpu.fxch(2);                   // fxch st(2)
      fpu.faddp(3);                  // faddp st(3), st(0)
      fpu.fmul_m32(box[3]);          // fmul dword ptr [rcx+0Ch]
      fpu.faddp(2);                  // faddp st(2), st(0)
      fpu.fxch(1);                   // fxch st(1)
      fpu.fabs();                    // fabs
      fpu.fmul_m32(box[12]);         // fmul dword ptr [rcx+30h]
      fpu.faddp(1);                  // faddp st(1), st(0)
      return fpu.fstp_m32();         // fstp dword ptr [rsp+32]; movss xmm0; fldcw (restore)
    }

    float VectorLengthZYX(const float* const lanes) noexcept
    {
      X87Stack fpu(Precision::P24);  // fnstcw; and eax, 0fffffcffh; fldcw
      fpu.fld_m32(lanes[2]);         // fld dword ptr [rcx+8]
      fpu.fmul(0, 0);                // fmul st(0), st(0)
      fpu.fld_m32(lanes[1]);         // fld dword ptr [rcx+4]
      fpu.fmul(0, 0);                // fmul st(0), st(0)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fld_m32(lanes[0]);         // fld dword ptr [rcx]
      fpu.fmul(0, 0);                // fmul st(0), st(0)
      fpu.faddp(1);                  // faddp st(1), st(0)
      fpu.fsqrt();                   // fsqrt
      return fpu.fstp_m32();         // fstp dword ptr [rsp+32]; movss xmm0; fldcw (restore)
    }
  } // namespace ref

  // --- Layer 2: the same chains on doubles -------------------------------------------------
  //
  // Every operand is a float or a PC24 result (F24), so each PC24 step is a double operation
  // rounded to 24 bits (X87Core.h fast::*24). Range: for finite float inputs every non-zero
  // intermediate lies within [2^-447, 2^408] (products of up to three floats, a float sum of
  // squares over a denormal, their square roots), inside the double normal range, so the
  // x87's extended exponent changes nothing; the final (float) conversion is the store's
  // second rounding. Operations double and x87 agree on also cover infinities (x/0,
  // inf*x, inf+x, x/inf); every invalid operation, and every NaN input, gives a NaN that
  // propagates to the output, and those inputs go to the transcription, which produces
  // the x87's NaN. Proven against ref:: by macos/tests/x87-core.

  namespace fast
  {
    using x87::fast::Add24;
    using x87::fast::Add24Odd;
    using x87::fast::Div24;
    using x87::fast::Mul24;
    using x87::fast::Sqrt24;
    using x87::fast::Sub24;
    using x87::fast::Sub24Odd;

    std::int32_t RoundToInt(const float value) noexcept
    {
      // fistp under RC nearest: ties to even; NaN and |value| >= 2^31 give 0x80000000 (so
      // does -2^31 itself). Floats that large are integers, so nothing rounds into range.
      if (value >= -2147483648.0f && value < 2147483648.0f) {
        return static_cast<std::int32_t>(__builtin_rintf(value));
      }
      return static_cast<std::int32_t>(0x80000000u);
    }

    float BilinearHeight(
      const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
    ) noexcept
    {
      const double top = Sub24(h01, h00);              // fsub st(1), st(0)
      const double row0 = Add24(h00, Mul24(top, fracZ)); // fmul st(2), st(0); faddp st(1), st(0)
      const double bottom = Sub24(h11, h10);           // fsub st(1), st(0)
      const double row1 = Add24(Mul24(fracZ, bottom), h10); // fmulp st(3), st(0); faddp st(2), st(0)
      const double across = Sub24(row1, row0);         // fsub st(1), st(0)
      const double result = Add24(row0, Mul24(across, fracX)); // fmul [fracX]; faddp st(1), st(0)
      if (IsNaN(result)) {
        return ref::BilinearHeight(h00, h01, h10, h11, fracX, fracZ);
      }
      return static_cast<float>(result);
    }

    float BoxRadiusOnAxis(const float* const boxLanes, const float* const axisLanes) noexcept
    {
      // (|axis . A2| * e2 + |axis . A1| * e1) + |axis . A0| * e0, each dot as (y, z) then x,
      // in the order the transcription's stack holds them.
      const float* const b = boxLanes;
      const double ax = axisLanes[0];
      const double ay = axisLanes[1];
      const double az = axisLanes[2];
      const double dot2 = Add24(Add24(Mul24(b[11], az), Mul24(b[10], ay)), Mul24(b[9], ax));
      const double r2 = Mul24(std::fabs(dot2), b[14]);
      const double dot1 = Add24(Add24(Mul24(b[7], ay), Mul24(b[8], az)), Mul24(b[6], ax));
      const double r21 = Add24(r2, Mul24(std::fabs(dot1), b[13]));
      const double dot0 = Add24(Add24(Mul24(az, b[5]), Mul24(ay, b[4])), Mul24(ax, b[3]));
      const double result = Add24(r21, Mul24(std::fabs(dot0), b[12]));
      if (IsNaN(result)) {
        return ref::BoxRadiusOnAxis(boxLanes, axisLanes);
      }
      return static_cast<float>(result);
    }

    float VectorLengthZYX(const float* const lanes) noexcept
    {
      const double x = lanes[0];
      const double y = lanes[1];
      const double z = lanes[2];
      const double result = Sqrt24(Add24(Add24(Mul24(z, z), Mul24(y, y)), Mul24(x, x)));
      if (IsNaN(result)) {
        return ref::VectorLengthZYX(lanes);
      }
      return static_cast<float>(result);
    }

    void ScaleVector(float* const destLanes, const float* const sourceLanes, const float scale) noexcept
    {
      if (!SameOrDisjoint(destLanes, sourceLanes, 3)) {
        ref::ScaleVector(destLanes, sourceLanes, scale);
        return;
      }
      const double d0 = Mul24(sourceLanes[0], scale);
      const double d1 = Mul24(sourceLanes[1], scale);
      const double d2 = Mul24(scale, sourceLanes[2]);
      if (IsNaN(d0) || IsNaN(d1) || IsNaN(d2)) {
        ref::ScaleVector(destLanes, sourceLanes, scale);
        return;
      }
      destLanes[0] = static_cast<float>(d0);
      destLanes[1] = static_cast<float>(d1);
      destLanes[2] = static_cast<float>(d2);
    }

    void ScaleVectorToLength(float* const destLanes, const float* const sourceLanes, const float lengthSq, const float length) noexcept
    {
      if (!SameOrDisjoint(destLanes, sourceLanes, 3)) {
        ref::ScaleVectorToLength(destLanes, sourceLanes, lengthSq, length);
        return;
      }
      const double scale = Div24(length, Sqrt24(lengthSq));  // fsqrt; fdivr length
      const double d0 = Mul24(sourceLanes[0], scale);
      const double d1 = Mul24(scale, sourceLanes[1]);
      const double d2 = Mul24(scale, sourceLanes[2]);
      if (IsNaN(d0) || IsNaN(d1) || IsNaN(d2)) {
        ref::ScaleVectorToLength(destLanes, sourceLanes, lengthSq, length);
        return;
      }
      destLanes[0] = static_cast<float>(d0);
      destLanes[1] = static_cast<float>(d1);
      destLanes[2] = static_cast<float>(d2);
    }

    void ScaleVectorMaxLength(float* const x, float* const y, float* const z, const float maxLength, const float lengthSq) noexcept
    {
      if (x == y || y == z || x == z) {
        ref::ScaleVectorMaxLength(x, y, z, maxLength, lengthSq);
        return;
      }
      const double scale = Div24(maxLength, Sqrt24(lengthSq));  // fsqrt; fdivr maxLength
      const double dx = Mul24(*x, scale);
      const double dy = Mul24(*y, scale);
      const double dz = Mul24(scale, *z);
      if (IsNaN(dx) || IsNaN(dy) || IsNaN(dz)) {
        ref::ScaleVectorMaxLength(x, y, z, maxLength, lengthSq);
        return;
      }
      *x = static_cast<float>(dx);
      *y = static_cast<float>(dy);
      *z = static_cast<float>(dz);
    }

    void ScaleVectorToLengthOf(
      float* const destLanes, const float* const sourceLanes, const float* const targetLanes, const float sourceLengthSq
    ) noexcept
    {
      // The target is read completely before the first store, so only dest/source matter.
      if (!SameOrDisjoint(destLanes, sourceLanes, 3)) {
        ref::ScaleVectorToLengthOf(destLanes, sourceLanes, targetLanes, sourceLengthSq);
        return;
      }
      const double t0 = targetLanes[0];
      const double t1 = targetLanes[1];
      const double t2 = targetLanes[2];
      const double lengthSq = Add24(Add24(Mul24(t0, t0), Mul24(t1, t1)), Mul24(t2, t2));
      const double scale = Sqrt24(Div24(lengthSq, sourceLengthSq));  // fdiv; fsqrt
      const double d0 = Mul24(sourceLanes[0], scale);
      const double d1 = Mul24(scale, sourceLanes[1]);
      const double d2 = Mul24(scale, sourceLanes[2]);
      if (IsNaN(d0) || IsNaN(d1) || IsNaN(d2)) {
        ref::ScaleVectorToLengthOf(destLanes, sourceLanes, targetLanes, sourceLengthSq);
        return;
      }
      destLanes[0] = static_cast<float>(d0);
      destLanes[1] = static_cast<float>(d1);
      destLanes[2] = static_cast<float>(d2);
    }

    // acos / asin: (1 + x) and (1 - x) have the double x as an operand (F53), so they round
    // to odd at 53 bits first; the product and the square root have F24 operands. Inside
    // [-1, 1] nothing is invalid and every intermediate is 0 or at least 2^-53.
    double Acos(const double x) noexcept
    {
      if (!(x >= -1.0 && x <= 1.0)) {
        return ref::Acos(x);
      }
      const double radicand = Mul24(Add24Odd(1.0, x), Sub24Odd(1.0, x));
      const double root = Sqrt24(radicand);
      return ToDouble(Fpatan(FromDouble(root), FromDouble(x)));  // fxch st(1); fpatan
    }

    double Asin(const double x) noexcept
    {
      if (!(x >= -1.0 && x <= 1.0)) {
        return ref::Asin(x);
      }
      const double radicand = Mul24(Add24Odd(1.0, x), Sub24Odd(1.0, x));
      const double root = Sqrt24(radicand);
      return ToDouble(Fpatan(FromDouble(x), FromDouble(root)));  // fpatan
    }
  } // namespace fast
} // namespace faf_posix::x87::emu

// --- The symbols legacy/math/X87Math.cpp declares (X87Math.cpp:12-41) -------------------------

namespace emu = faf_posix::x87::emu;

extern "C" float msvc8_x87_sinf(const float x)
{
  return emu::ref::Sinf(x);
}

extern "C" float msvc8_x87_cosf(const float x)
{
  return emu::ref::Cosf(x);
}

extern "C" float msvc8_x87_tanf(const float x)
{
  return emu::ref::Tanf(x);
}

extern "C" float msvc8_x87_atan2f(const float y, const float x)
{
  return emu::ref::Atan2f(y, x);
}

extern "C" double msvc8_x87_acos(const double x)
{
  return emu::fast::Acos(x);
}

extern "C" double msvc8_x87_asin(const double x)
{
  return emu::fast::Asin(x);
}

extern "C" double msvc8_x87_pow(const double x, const double y)
{
  return emu::ref::Pow(x, y);
}

// fnstcw: a Windows x64 thread's control word. X87Math.cpp's TakesSse2Lane only reads the
// low byte (0x7F).
extern "C" unsigned int msvc8_x87_control_word()
{
  return faf_posix::x87::kDefaultControlWord;
}

extern "C" float msvc8_x87_atan2_difference(const float firstY, const float firstX, const float secondY, const float secondX)
{
  return emu::ref::Atan2Difference(firstY, firstX, secondY, secondX);
}

extern "C" float msvc8_x87_atan2_plus(const float y, const float x, const float addend)
{
  return emu::ref::Atan2Plus(y, x, addend);
}

extern "C" float msvc8_x87_sin_times_cos(const float sinArg, const float cosArg)
{
  return emu::ref::SinTimesCos(sinArg, cosArg);
}

extern "C" float msvc8_x87_sin_times(const float sinArg, const float factor)
{
  return emu::ref::SinTimes(sinArg, factor);
}

extern "C" void msvc8_x87_slerp_spherical_blend(
  float* const outLanes, const float* const q1Lanes, const float* const rhsLanes, const float angle,
  const float oneMinusT, const float t
)
{
  emu::ref::SlerpSphericalBlend(outLanes, q1Lanes, rhsLanes, angle, oneMinusT, t);
}

extern "C" float msvc8_x87_intrinsic_exp(const float x)
{
  return emu::ref::IntrinsicExp(x);
}

extern "C" float msvc8_x87_intrinsic_ln(const float x)
{
  return emu::ref::IntrinsicLn(x);
}

extern "C" float msvc8_x87_intrinsic_log10(const float x)
{
  return emu::ref::IntrinsicLog10(x);
}

extern "C" float msvc8_x87_intrinsic_ln_quotient(const float x, const double base)
{
  return emu::ref::IntrinsicLnQuotient(x, base);
}

extern "C" float msvc8_x87_scaled_ln_quotient(const float numerator, const float denominator, const float scale)
{
  return emu::ref::ScaledLnQuotient(numerator, denominator, scale);
}

extern "C" float msvc8_x87_cos_times_cos(const float firstArg, const float secondArg)
{
  return emu::ref::CosTimesCos(firstArg, secondArg);
}

extern "C" float msvc8_x87_bilinear_height(
  const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
)
{
  return emu::fast::BilinearHeight(h00, h01, h10, h11, fracX, fracZ);
}

extern "C" float msvc8_x87_subtract_bilinear_height(
  const float top, const float h00, const float h01, const float h10, const float h11, const float fracX,
  const float fracZ
)
{
  return emu::ref::SubtractBilinearHeight(top, h00, h01, h10, h11, fracX, fracZ);
}

extern "C" float msvc8_x87_sin_of_product_times(const float a, const float b, const float factor)
{
  return emu::ref::SinOfProductTimes(a, b, factor);
}

extern "C" float msvc8_x87_twister_word_in_range(const float lower, const float upper, const std::uint32_t word)
{
  return emu::ref::TwisterWordInRange(lower, upper, word);
}

extern "C" std::int32_t msvc8_x87_round_to_int(const float value)
{
  return emu::fast::RoundToInt(value);
}

extern "C" void msvc8_x87_scale_vector_max_length(
  float* const x, float* const y, float* const z, const float maxLength, const float lengthSq
)
{
  emu::fast::ScaleVectorMaxLength(x, y, z, maxLength, lengthSq);
}

extern "C" void msvc8_x87_scale_vector_to_length(
  float* const destLanes, const float* const sourceLanes, const float lengthSq, const float length
)
{
  emu::fast::ScaleVectorToLength(destLanes, sourceLanes, lengthSq, length);
}

extern "C" void msvc8_x87_scale_vector(float* const destLanes, const float* const sourceLanes, const float scale)
{
  emu::fast::ScaleVector(destLanes, sourceLanes, scale);
}

extern "C" float msvc8_x87_box_radius_on_axis(const float* const boxLanes, const float* const axisLanes)
{
  return emu::fast::BoxRadiusOnAxis(boxLanes, axisLanes);
}

extern "C" void msvc8_x87_scale_vector_to_length_of(
  float* const destLanes, const float* const sourceLanes, const float* const targetLanes, const float sourceLengthSq
)
{
  emu::fast::ScaleVectorToLengthOf(destLanes, sourceLanes, targetLanes, sourceLengthSq);
}

extern "C" float msvc8_x87_vector_length_zyx(const float* const lanes)
{
  return emu::fast::VectorLengthZYX(lanes);
}
