#pragma once

// The Layer 1 transcriptions (ref) and Layer 2 fast paths (fast) that
// src/platform/posix/math/X87Emu.cpp defines next to the msvc8_x87_* symbols. The
// signatures must match X87Emu.cpp (C++ linkage, so a mismatch fails to link).

#include <cstdint>

namespace faf_posix::x87::emu
{
  namespace ref
  {
    std::int32_t RoundToInt(float value) noexcept;
    float BilinearHeight(float h00, float h01, float h10, float h11, float fracX, float fracZ) noexcept;
    float Sinf(float x) noexcept;
    float Cosf(float x) noexcept;
    float Tanf(float x) noexcept;
    float Atan2f(float y, float x) noexcept;
    double Acos(double x) noexcept;
    double Asin(double x) noexcept;
    double Pow(double x, double y) noexcept;
    float Atan2Difference(float firstY, float firstX, float secondY, float secondX) noexcept;
    float Atan2Plus(float y, float x, float addend) noexcept;
    float SinTimesCos(float sinArg, float cosArg) noexcept;
    float SinTimes(float sinArg, float factor) noexcept;
    void SlerpSphericalBlend(float* outLanes, const float* q1Lanes, const float* rhsLanes, float angle, float oneMinusT, float t) noexcept;
    float IntrinsicExp(float x) noexcept;
    float IntrinsicLn(float x) noexcept;
    float IntrinsicLog10(float x) noexcept;
    float IntrinsicLnQuotient(float x, double base) noexcept;
    float ScaledLnQuotient(float numerator, float denominator, float scale) noexcept;
    float CosTimesCos(float firstArg, float secondArg) noexcept;
    void ScaleVectorMaxLength(float* x, float* y, float* z, float maxLength, float lengthSq) noexcept;
    void ScaleVectorToLength(float* destLanes, const float* sourceLanes, float lengthSq, float length) noexcept;
    void ScaleVector(float* destLanes, const float* sourceLanes, float scale) noexcept;
    void ScaleVectorToLengthOf(float* destLanes, const float* sourceLanes, const float* targetLanes, float sourceLengthSq) noexcept;
    float BoxRadiusOnAxis(const float* boxLanes, const float* axisLanes) noexcept;
    float VectorLengthZYX(const float* lanes) noexcept;
  } // namespace ref

  namespace fast
  {
    std::int32_t RoundToInt(float value) noexcept;
    float BilinearHeight(float h00, float h01, float h10, float h11, float fracX, float fracZ) noexcept;
    float BoxRadiusOnAxis(const float* boxLanes, const float* axisLanes) noexcept;
    float VectorLengthZYX(const float* lanes) noexcept;
    void ScaleVector(float* destLanes, const float* sourceLanes, float scale) noexcept;
    void ScaleVectorToLength(float* destLanes, const float* sourceLanes, float lengthSq, float length) noexcept;
    void ScaleVectorMaxLength(float* x, float* y, float* z, float maxLength, float lengthSq) noexcept;
    void ScaleVectorToLengthOf(float* destLanes, const float* sourceLanes, const float* targetLanes, float sourceLengthSq) noexcept;
    double Acos(double x) noexcept;
    double Asin(double x) noexcept;
  } // namespace fast
} // namespace faf_posix::x87::emu

// The extern "C" entry points (X87Math.cpp:12-41).
extern "C" {
float msvc8_x87_sinf(float x);
float msvc8_x87_cosf(float x);
float msvc8_x87_tanf(float x);
float msvc8_x87_atan2f(float y, float x);
double msvc8_x87_acos(double x);
double msvc8_x87_asin(double x);
double msvc8_x87_pow(double x, double y);
unsigned int msvc8_x87_control_word();
float msvc8_x87_atan2_difference(float firstY, float firstX, float secondY, float secondX);
float msvc8_x87_atan2_plus(float y, float x, float addend);
float msvc8_x87_sin_times_cos(float sinArg, float cosArg);
float msvc8_x87_sin_times(float sinArg, float factor);
void msvc8_x87_slerp_spherical_blend(float* outLanes, const float* q1Lanes, const float* rhsLanes, float angle, float oneMinusT, float t);
float msvc8_x87_intrinsic_exp(float x);
float msvc8_x87_intrinsic_ln(float x);
float msvc8_x87_intrinsic_log10(float x);
float msvc8_x87_intrinsic_ln_quotient(float x, double base);
float msvc8_x87_scaled_ln_quotient(float numerator, float denominator, float scale);
float msvc8_x87_cos_times_cos(float firstArg, float secondArg);
float msvc8_x87_bilinear_height(float h00, float h01, float h10, float h11, float fracX, float fracZ);
std::int32_t msvc8_x87_round_to_int(float value);
void msvc8_x87_scale_vector_max_length(float* x, float* y, float* z, float maxLength, float lengthSq);
void msvc8_x87_scale_vector_to_length(float* destLanes, const float* sourceLanes, float lengthSq, float length);
void msvc8_x87_scale_vector(float* destLanes, const float* sourceLanes, float scale);
float msvc8_x87_box_radius_on_axis(const float* boxLanes, const float* axisLanes);
void msvc8_x87_scale_vector_to_length_of(float* destLanes, const float* sourceLanes, const float* targetLanes, float sourceLengthSq);
float msvc8_x87_vector_length_zyx(const float* lanes);
}
