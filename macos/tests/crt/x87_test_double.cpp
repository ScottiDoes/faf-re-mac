// Test double for the 27 msvc8_x87_* routines, so the crt tests can link SelfTest.cpp and
// X87Math.cpp before the software x87 (src/platform/posix/math) exists. NOT the emulator.
//
// - The PC24 arithmetic routines the self-test uses (scale_vector, scale_vector_to_length,
//   vector_length_zyx, round_to_int, control_word) are an independent double-based model:
//   every product of two floats is exact in double, and for +, -, *, /, sqrt of 24-bit
//   operands one double rounding followed by a 24-bit rounding equals the single 24-bit
//   rounding (Figueroa: 53 >= 2*24+2); the 24-bit rounding keeps the extended exponent by
//   rounding the frexp mantissa. So these cross-check kat_oracle.py's Fraction model.
// - The transcendental ones return the documented hardware answer for the self-test's
//   inputs only (a lookup: this checks the harness, not an implementation) and abort on any
//   other input.
// - Built with -DFAF_X87_TEST_WRONG, every answer is one ulp off, so the self-test must fail.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
#if defined(FAF_X87_TEST_WRONG)
  constexpr std::uint32_t kSkew = 1;
#else
  constexpr std::uint32_t kSkew = 0;
#endif

  std::uint32_t Bits(const float x)
  {
    std::uint32_t b;
    std::memcpy(&b, &x, 4);
    return b;
  }

  float F32(const std::uint32_t b)
  {
    float x;
    std::memcpy(&x, &b, 4);
    return x;
  }

  float Skew(const float x)
  {
    return F32(Bits(x) + kSkew);
  }

  [[noreturn]] void Unexpected(const char* what)
  {
    std::fprintf(stderr, "x87_test_double: %s called with an input it has no answer for\n", what);
    std::abort();
  }

  // Round to a 24-bit significand with an unbounded exponent (PC24), from a double that is
  // the exact result or its innocuous RN53 rounding.
  double Pc24(const double v)
  {
    if (v == 0.0 || !std::isfinite(v)) {
      return v;
    }
    int e = 0;
    const double m = std::frexp(v, &e);              // [0.5, 1), exact
    const double r = static_cast<double>(static_cast<float>(m)); // RN24 of the mantissa
    return std::ldexp(r, e);                          // exact: well inside double range here
  }
} // namespace

extern "C" {
  unsigned int msvc8_x87_control_word() { return 0x027Fu + kSkew; }

  std::int32_t msvc8_x87_round_to_int(float value)
  {
    if (!(value >= -2147483648.0f && value < 2147483648.0f)) {
      return INT32_MIN;
    }
    return static_cast<std::int32_t>(std::nearbyint(value)) + static_cast<std::int32_t>(kSkew);
  }

  void msvc8_x87_scale_vector(float* dest, const float* source, float scale)
  {
    for (int i = 0; i < 3; ++i) {
      dest[i] = Skew(static_cast<float>(Pc24(static_cast<double>(source[i]) * scale)));
    }
  }

  void msvc8_x87_scale_vector_to_length(float* dest, const float* source, float lengthSq, float length)
  {
    const double root = Pc24(std::sqrt(static_cast<double>(lengthSq)));
    const double k = Pc24(static_cast<double>(length) / root);
    for (int i = 0; i < 3; ++i) {
      dest[i] = Skew(static_cast<float>(Pc24(static_cast<double>(source[i]) * k)));
    }
  }

  float msvc8_x87_vector_length_zyx(const float* lanes)
  {
    const double z = lanes[2];
    const double y = lanes[1];
    const double x = lanes[0];
    double acc = Pc24(Pc24(z * z) + Pc24(y * y));
    acc = Pc24(acc + Pc24(x * x));
    return Skew(static_cast<float>(Pc24(std::sqrt(acc))));
  }

  // --- lookups ---------------------------------------------------------------------------
  float msvc8_x87_sinf(float x)
  {
    if (Bits(x) == 0x5F000000u) return Skew(x);
    if (Bits(x) == 0) return Skew(0.0f);
    Unexpected("sinf");
  }

  float msvc8_x87_cosf(float x)
  {
    if (Bits(x) == 0x5F000000u) return Skew(x);
    if (Bits(x) == 0) return Skew(1.0f);
    Unexpected("cosf");
  }

  float msvc8_x87_tanf(float x)
  {
    if (Bits(x) == 0x5F000000u) return Skew(F32(0xFFC00000u));
    Unexpected("tanf");
  }

  float msvc8_x87_atan2f(float y, float x)
  {
    if (y == 1.0f && x == 1.0f) return Skew(F32(0x3F490FDBu));
    Unexpected("atan2f");
  }

  double msvc8_x87_asin(double x)
  {
    if (x == static_cast<double>(F32(0x3F052EAAu))) return Skew(F32(0x3F0C1118u));
    Unexpected("asin");
  }

  double msvc8_x87_pow(double x, double y)
  {
    if (x == static_cast<double>(F32(0x41AB54C2u)) && y == 2.0) return Skew(F32(0x43E554AFu));
    Unexpected("pow");
  }

  float msvc8_x87_intrinsic_exp(float x)
  {
    if (x == 0.0f) return Skew(1.0f);
    Unexpected("intrinsic_exp");
  }

  float msvc8_x87_intrinsic_ln(float x)
  {
    if (x == 1.0f) return Skew(0.0f);
    Unexpected("intrinsic_ln");
  }

  // --- the rest exist only so X87Math.cpp links -------------------------------------------
  double msvc8_x87_acos(double) { Unexpected("acos"); }
  float msvc8_x87_atan2_difference(float, float, float, float) { Unexpected("atan2_difference"); }
  float msvc8_x87_atan2_plus(float, float, float) { Unexpected("atan2_plus"); }
  float msvc8_x87_sin_times_cos(float, float) { Unexpected("sin_times_cos"); }
  float msvc8_x87_sin_times(float, float) { Unexpected("sin_times"); }
  void msvc8_x87_slerp_spherical_blend(float*, const float*, const float*, float, float, float)
  {
    Unexpected("slerp_spherical_blend");
  }
  float msvc8_x87_intrinsic_log10(float) { Unexpected("intrinsic_log10"); }
  float msvc8_x87_intrinsic_ln_quotient(float, double) { Unexpected("intrinsic_ln_quotient"); }
  float msvc8_x87_scaled_ln_quotient(float, float, float) { Unexpected("scaled_ln_quotient"); }
  float msvc8_x87_cos_times_cos(float, float) { Unexpected("cos_times_cos"); }
  float msvc8_x87_bilinear_height(float, float, float, float, float, float) { Unexpected("bilinear_height"); }
  void msvc8_x87_scale_vector_max_length(float*, float*, float*, float, float) { Unexpected("scale_vector_max_length"); }
  float msvc8_x87_box_radius_on_axis(const float*, const float*) { Unexpected("box_radius_on_axis"); }
  void msvc8_x87_scale_vector_to_length_of(float*, const float*, const float*, float) { Unexpected("scale_vector_to_length_of"); }

  // MathReflection.cpp's; X87Math.cpp reads it.
  int global_mode_sse2 = 0;
}
