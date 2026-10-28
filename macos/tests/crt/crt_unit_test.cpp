// Unit tests for src/platform/posix/crt (faf_crtmath, trace mode, MSVC rand). Built with the
// engine's flags (engine_cc.py), so std::sin(float), std::pow(double, double), ... call the
// faf_crtmath definitions exactly as engine code does. Run by run_tests.sh.
//
//   crt_unit_test                 all tests (spawns itself for the trace tests)
//   crt_unit_test --trace-child   the traced workload (run with FAF_CRTMATH_TRACE=1)

#include "platform/posix/crt/CrtMath.h"

#include <dlfcn.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace
{
  int g_checks = 0;
  int g_failures = 0;

  void Check(const bool ok, const std::string& what)
  {
    ++g_checks;
    if (!ok) {
      ++g_failures;
      std::fprintf(stderr, "FAIL %s\n", what.c_str());
    }
  }

  std::uint64_t B(const double x)
  {
    std::uint64_t b;
    std::memcpy(&b, &x, 8);
    return b;
  }

  std::uint32_t B(const float x)
  {
    std::uint32_t b;
    std::memcpy(&b, &x, 4);
    return b;
  }

  double D(const std::uint64_t b)
  {
    double x;
    std::memcpy(&x, &b, 8);
    return x;
  }

  float Fl(const std::uint32_t b)
  {
    float x;
    std::memcpy(&x, &b, 4);
    return x;
  }

  std::string Hex(const std::uint64_t v)
  {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016" PRIX64, v);
    return buf;
  }

  std::string Hex32(const std::uint32_t v)
  {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08" PRIX32, v);
    return buf;
  }

  void Expect64(const char* what, const double got, const std::uint64_t want)
  {
    Check(B(got) == want, std::string(what) + ": got " + Hex(B(got)) + ", want " + Hex(want));
  }

  void Expect32(const char* what, const float got, const std::uint32_t want)
  {
    Check(B(got) == want, std::string(what) + ": got " + Hex32(B(got)) + ", want " + Hex32(want));
  }

  constexpr std::uint64_t kDNan = 0xFFF8000000000000ull;    // x86 default NaN
  constexpr std::uint32_t kDNanF = 0xFFC00000u;
  constexpr std::uint64_t kPi = 0x400921FB54442D18ull;
  constexpr std::uint64_t kHalfPi = 0x3FF921FB54442D18ull;
  constexpr std::uint64_t kQuarterPi = 0x3FE921FB54442D18ull;
  constexpr std::uint64_t kThreeQuarterPi = 0x4002D97C7F3321D2ull;
  constexpr std::uint64_t kInf = 0x7FF0000000000000ull;
  constexpr std::uint64_t kNegInf = 0xFFF0000000000000ull;
  constexpr std::uint64_t kOne = 0x3FF0000000000000ull;
  constexpr std::uint64_t kNegZero = 0x8000000000000000ull;

  // Volatile so nothing is folded at compile time: every call goes through the entry points.
  volatile double vInf = D(kInf);
  volatile double vNegInf = D(kNegInf);
  volatile double vZero = 0.0;
  volatile double vNegZero = D(kNegZero);
  volatile double vNan = D(0x7FF8000000000000ull);
  volatile double vNanPayload = D(0xFFF8000000001234ull); // quiet, negative, payload
  volatile double vSNan = D(0x7FF0000000000001ull);       // signalling
  volatile float vInfF = Fl(0x7F800000u);
  volatile float vZeroF = 0.0f;
  volatile float vNanPayloadF = Fl(0xFFC01234u);

  // --- Annex F special values and the UCRT NaN results ------------------------------------

  void TestSpecialValues()
  {
    // Domain errors: the x86 default NaN.
    Expect64("acos(2)", std::acos(2.0 + vZero), kDNan);
    Expect64("asin(-2)", std::asin(-2.0 + vZero), kDNan);
    Expect64("log(-1)", std::log(-1.0 + vZero), kDNan);
    Expect64("log(-inf)", std::log(vNegInf), kDNan);
    Expect64("log10(-1)", std::log10(-1.0 + vZero), kDNan);
    Expect64("sin(inf)", std::sin(vInf), kDNan);
    Expect64("cos(-inf)", std::cos(vNegInf), kDNan);
    Expect64("tan(inf)", std::tan(vInf), kDNan);
    Expect64("pow(-2, 0.5)", std::pow(-2.0 + vZero, 0.5), kDNan);
    Expect64("fmod(1, 0)", std::fmod(1.0 + vZero, vZero), kDNan);
    Expect64("fmod(inf, 1)", std::fmod(vInf, 1.0), kDNan);
    Expect64("sqrt(-1)", std::sqrt(-1.0 + vZero), kDNan);
    Expect64("sqrt(-inf)", std::sqrt(vNegInf), kDNan);
    Expect32("acosf(2)", std::acos(2.0f + vZeroF), kDNanF);
    Expect32("asinf(2)", std::asin(2.0f + vZeroF), kDNanF);
    Expect32("logf(-1)", std::log(-1.0f + vZeroF), kDNanF);
    Expect32("log2f(-1)", std::log2(-1.0f + vZeroF), kDNanF);
    Expect32("log10f(-1)", std::log10(-1.0f + vZeroF), kDNanF);
    Expect32("sinf(inf)", std::sin(vInfF), kDNanF);
    Expect32("cosf(inf)", std::cos(vInfF), kDNanF);
    Expect32("tanf(inf)", std::tan(vInfF), kDNanF);
    Expect32("powf(-2, 0.5)", std::pow(-2.0f + vZeroF, 0.5f), kDNanF);
    Expect32("sqrtf(-1)", std::sqrt(-1.0f + vZeroF), kDNanF);

    // NaN operands come back quieted with sign and payload (first NaN operand of two).
    Expect64("sin(-nan payload)", std::sin(vNanPayload), 0xFFF8000000001234ull);
    Expect64("exp(sNaN)", std::exp(vSNan), 0x7FF8000000000001ull);
    Expect64("atan2(nan payload, nan)", std::atan2(vNanPayload, vNan), 0xFFF8000000001234ull);
    Expect64("atan2(nan, nan payload)", std::atan2(vNan, vNanPayload), 0x7FF8000000000000ull);
    Expect64("atan2(1, nan payload)", std::atan2(1.0, vNanPayload), 0xFFF8000000001234ull);
    Expect64("pow(nan payload, 2)", std::pow(vNanPayload, 2.0), 0xFFF8000000001234ull);
    Expect64("pow(2, nan payload)", std::pow(2.0, vNanPayload), 0xFFF8000000001234ull);
    Expect64("fmod(nan payload, 1)", std::fmod(vNanPayload, 1.0), 0xFFF8000000001234ull);
    Expect64("sqrt(nan payload)", std::sqrt(vNanPayload), 0xFFF8000000001234ull);
    Expect32("sinf(-nan payload)", std::sin(vNanPayloadF), 0xFFC01234u);
    Expect32("atan2f(1, nan payload)", std::atan2(1.0f, vNanPayloadF), 0xFFC01234u);
    Expect32("powf(nan payload, 3)", std::pow(vNanPayloadF, 3.0f), 0xFFC01234u);

    // Annex F values that are not NaN.
    Expect64("pow(nan, 0)", std::pow(vNanPayload, vZero), kOne);
    Expect64("pow(1, nan)", std::pow(1.0, vNanPayload), kOne);
    Expect64("pow(-1, inf)", std::pow(-1.0, vInf), kOne);
    Expect64("pow(-0, -1)", std::pow(vNegZero, -1.0), kNegInf);
    Expect64("pow(+0, -1)", std::pow(vZero, -1.0), kInf);
    Expect64("pow(-0, 3)", std::pow(vNegZero, 3.0), kNegZero);
    Expect64("pow(0.5, -inf)", std::pow(0.5, vNegInf), kInf);
    Expect64("pow(2, -inf)", std::pow(2.0, vNegInf), 0);
    Expect64("pow(-inf, -3)", std::pow(vNegInf, -3.0), kNegZero);
    Expect64("pow(-inf, 3)", std::pow(vNegInf, 3.0), kNegInf);
    Expect64("pow(-inf, 2.5)", std::pow(vNegInf, 2.5), kInf);
    Expect64("acos(1)", std::acos(1.0 + vZero), 0);
    Expect64("asin(-0)", std::asin(vNegZero), kNegZero);
    Expect64("atan(inf)", std::atan(vInf), kHalfPi);
    Expect64("atan(-0)", std::atan(vNegZero), kNegZero);
    Expect64("atan2(+0, -0)", std::atan2(vZero, vNegZero), kPi);
    Expect64("atan2(-0, -0)", std::atan2(vNegZero, vNegZero), kPi | (1ull << 63));
    Expect64("atan2(+0, +0)", std::atan2(vZero, vZero), 0);
    Expect64("atan2(-0, +0)", std::atan2(vNegZero, vZero), kNegZero);
    Expect64("atan2(-0, -1)", std::atan2(vNegZero, -1.0), kPi | (1ull << 63));
    Expect64("atan2(1, -0)", std::atan2(1.0, vNegZero), kHalfPi);
    Expect64("atan2(-1, +0)", std::atan2(-1.0, vZero), kHalfPi | (1ull << 63));
    Expect64("atan2(1, -inf)", std::atan2(1.0, vNegInf), kPi);
    Expect64("atan2(-1, inf)", std::atan2(-1.0, vInf), kNegZero);
    Expect64("atan2(inf, 3)", std::atan2(vInf, 3.0), kHalfPi);
    Expect64("atan2(inf, -inf)", std::atan2(vInf, vNegInf), kThreeQuarterPi);
    Expect64("atan2(-inf, inf)", std::atan2(vNegInf, vInf), kQuarterPi | (1ull << 63));
    Expect64("cos(-0)", std::cos(vNegZero), kOne);
    Expect64("sin(-0)", std::sin(vNegZero), kNegZero);
    Expect64("tan(-0)", std::tan(vNegZero), kNegZero);
    Expect64("cosh(-inf)", std::cosh(vNegInf), kInf);
    Expect64("sinh(-inf)", std::sinh(vNegInf), kNegInf);
    Expect64("tanh(-inf)", std::tanh(vNegInf), 0xBFF0000000000000ull);
    Expect64("tanh(-0)", std::tanh(vNegZero), kNegZero);
    Expect64("exp(-inf)", std::exp(vNegInf), 0);
    Expect64("exp(inf)", std::exp(vInf), kInf);
    Expect64("exp(1000)", std::exp(1000.0 + vZero), kInf);
    Expect64("exp(-1000)", std::exp(-1000.0 + vZero), 0);
    Expect64("exp(-0)", std::exp(vNegZero), kOne);
    Expect64("log(-0)", std::log(vNegZero), kNegInf);
    Expect64("log(1)", std::log(1.0 + vZero), 0);
    Expect64("log(inf)", std::log(vInf), kInf);
    Expect64("log10(0)", std::log10(vZero), kNegInf);
    Expect64("fmod(-0, 3)", std::fmod(vNegZero, 3.0), kNegZero);
    Expect64("fmod(5, inf)", std::fmod(5.0 + vZero, vInf), 0x4014000000000000ull);
    Expect64("fmod(-5.5, 2)", std::fmod(-5.5 + vZero, 2.0), 0xBFF8000000000000ull);
    Expect64("sqrt(-0)", std::sqrt(vNegZero), kNegZero);
    Expect64("sqrt(inf)", std::sqrt(vInf), kInf);
    Expect32("exp2f(-inf)", std::exp2(-vInfF), 0);
    Expect32("exp2f(3)", std::exp2(3.0f + vZeroF), 0x41000000u);
    Expect32("log2f(0)", std::log2(vZeroF), 0xFF800000u);
    Expect32("log2f(8)", std::log2(8.0f + vZeroF), 0x40400000u);
    Expect32("expf(-inf)", std::exp(-vInfF), 0);
    Expect32("coshf(0)", std::cosh(vZeroF), 0x3F800000u);
    // Exact +-pi, +-pi/2: the nearest float (Apple alone rounds these toward zero).
    Expect32("atan2f(-0, -0)", std::atan2(-vZeroF, -vZeroF), 0xC0490FDBu);
    Expect32("atan2f(+0, -1)", std::atan2(vZeroF, -1.0f), 0x40490FDBu);
    Expect32("atan2f(1, -inf)", std::atan2(1.0f + vZeroF, -vInfF), 0x40490FDBu);
    Expect32("atan2f(-inf, 2)", std::atan2(-vInfF, 2.0f), 0xBFC90FDBu);
    Expect32("atan2f(1, 0)", std::atan2(1.0f + vZeroF, vZeroF), 0x3FC90FDBu);
    Expect32("atan2f(inf, -inf)", std::atan2(vInfF, -vInfF), 0x4016CBE4u);
    Expect32("atanf(-inf)", std::atan(-vInfF), 0xBFC90FDBu);
    Expect32("asinf(1)", std::asin(1.0f + vZeroF), 0x3FC90FDBu);
    Expect32("asinf(-1)", std::asin(-1.0f + vZeroF), 0xBFC90FDBu);
    Expect32("acosf(-1)", std::acos(-1.0f + vZeroF), 0x40490FDBu);
    Expect32("acosf(0)", std::acos(vZeroF), 0x3FC90FDBu);
    Expect32("atan2f(1e-30, -1) stays Apple's (clone TODO)", std::atan2(1e-30f + vZeroF, -1.0f),
             B(reinterpret_cast<float (*)(float, float)>(dlsym(RTLD_NEXT, "atan2f"))(1e-30f, -1.0f)));
    Expect32("powf(1, nan)", std::pow(1.0f, vNanPayloadF), 0x3F800000u);

    Expect64("atan(1.0) = RN(pi/4)", std::atan(1.0 + vZero), kQuarterPi);
  }

  // --- Same results as Apple libm wherever the result is not NaN --------------------------

  // Apple's pi or pi/2 rounded toward zero, replaced by the nearest float (exact cases only).
  bool PiCorrected(const float libm, const float ours)
  {
    const std::uint32_t m = B(libm) & 0x7FFFFFFFu;
    return (m == 0x40490FDAu || m == 0x3FC90FDAu) && B(ours) == B(libm) + 1;
  }

  template <class T>
  T Random(std::mt19937_64& rng)
  {
    // Mostly ordinary magnitudes, some anything-goes bit patterns.
    if ((rng() & 7) == 0) {
      if constexpr (sizeof(T) == 8) {
        return D(rng());
      } else {
        return Fl(static_cast<std::uint32_t>(rng()));
      }
    }
    std::uniform_real_distribution<double> mag(-40.0, 40.0);
    const double v = std::ldexp(static_cast<double>(rng() >> 11) / 9007199254740992.0, static_cast<int>(mag(rng) / 4)) *
                     ((rng() & 1) ? -1.0 : 1.0);
    return static_cast<T>(v);
  }

  void TestAgainstLibm()
  {
    std::mt19937_64 rng(0x5EED2007);
    const auto compareD1 = [&](const char* name, double (*ours)(double)) {
      auto* const libm = reinterpret_cast<double (*)(double)>(dlsym(RTLD_NEXT, name));
      Check(libm != nullptr && reinterpret_cast<void*>(libm) != reinterpret_cast<void*>(ours),
            std::string(name) + ": libm resolves to another function");
      int bad = 0;
      for (int i = 0; i < 200000 && libm != nullptr; ++i) {
        const double x = Random<double>(rng);
        const double want = libm(x);
        const double got = ours(x);
        const bool ok = std::isnan(want) ? (std::isnan(x) ? B(got) == (B(x) | 0x0008000000000000ull) : B(got) == kDNan)
                                         : B(got) == B(want);
        if (!ok && bad++ < 3) {
          std::fprintf(stderr, "  %s(%s): ours %s libm %s\n", name, Hex(B(x)).c_str(), Hex(B(got)).c_str(),
                       Hex(B(want)).c_str());
        }
      }
      Check(bad == 0, std::string(name) + " equals libm on 200000 inputs (NaN rules aside)");
    };
    const auto compareD2 = [&](const char* name, double (*ours)(double, double)) {
      auto* const libm = reinterpret_cast<double (*)(double, double)>(dlsym(RTLD_NEXT, name));
      int bad = 0;
      for (int i = 0; i < 200000 && libm != nullptr; ++i) {
        const double x = Random<double>(rng);
        const double y = Random<double>(rng);
        const double want = libm(x, y);
        const double got = ours(x, y);
        bool ok = B(got) == B(want);
        if (std::isnan(want)) {
          const std::uint64_t expected = std::isnan(x)   ? (B(x) | 0x0008000000000000ull)
                                         : std::isnan(y) ? (B(y) | 0x0008000000000000ull)
                                                         : kDNan;
          ok = B(got) == expected;
        }
        if (!ok && bad++ < 3) {
          std::fprintf(stderr, "  %s(%s, %s): ours %s libm %s\n", name, Hex(B(x)).c_str(), Hex(B(y)).c_str(),
                       Hex(B(got)).c_str(), Hex(B(want)).c_str());
        }
      }
      Check(libm != nullptr && bad == 0, std::string(name) + " equals libm on 200000 input pairs (NaN rules aside)");
    };
    const auto compareF1 = [&](const char* name, float (*ours)(float)) {
      auto* const libm = reinterpret_cast<float (*)(float)>(dlsym(RTLD_NEXT, name));
      int bad = 0;
      for (int i = 0; i < 200000 && libm != nullptr; ++i) {
        const float x = Random<float>(rng);
        const float want = libm(x);
        const float got = ours(x);
        bool ok = std::isnan(want) ? (std::isnan(x) ? B(got) == (B(x) | 0x00400000u) : B(got) == kDNanF)
                                   : B(got) == B(want);
        ok = ok || PiCorrected(want, got);
        if (!ok && bad++ < 3) {
          std::fprintf(stderr, "  %s(%s): ours %s libm %s\n", name, Hex32(B(x)).c_str(), Hex32(B(got)).c_str(),
                       Hex32(B(want)).c_str());
        }
      }
      Check(libm != nullptr && bad == 0, std::string(name) + " equals libm on 200000 inputs (NaN rules aside)");
    };
    const auto compareF2 = [&](const char* name, float (*ours)(float, float)) {
      auto* const libm = reinterpret_cast<float (*)(float, float)>(dlsym(RTLD_NEXT, name));
      int bad = 0;
      for (int i = 0; i < 200000 && libm != nullptr; ++i) {
        const float x = Random<float>(rng);
        const float y = Random<float>(rng);
        const float want = libm(x, y);
        const float got = ours(x, y);
        bool ok = B(got) == B(want);
        if (std::isnan(want)) {
          const std::uint32_t expected = std::isnan(x)   ? (B(x) | 0x00400000u)
                                         : std::isnan(y) ? (B(y) | 0x00400000u)
                                                         : kDNanF;
          ok = B(got) == expected;
        }
        ok = ok || PiCorrected(want, got);
        if (!ok && bad++ < 3) {
          std::fprintf(stderr, "  %s(%s, %s): ours %s libm %s\n", name, Hex32(B(x)).c_str(), Hex32(B(y)).c_str(),
                       Hex32(B(got)).c_str(), Hex32(B(want)).c_str());
        }
      }
      Check(libm != nullptr && bad == 0, std::string(name) + " equals libm on 200000 input pairs (NaN rules aside)");
    };

    compareD1("acos", static_cast<double (*)(double)>(&::acos));
    compareD1("asin", static_cast<double (*)(double)>(&::asin));
    compareD1("atan", static_cast<double (*)(double)>(&::atan));
    compareD1("cos", static_cast<double (*)(double)>(&::cos));
    compareD1("sin", static_cast<double (*)(double)>(&::sin));
    compareD1("tan", static_cast<double (*)(double)>(&::tan));
    compareD1("cosh", static_cast<double (*)(double)>(&::cosh));
    compareD1("sinh", static_cast<double (*)(double)>(&::sinh));
    compareD1("tanh", static_cast<double (*)(double)>(&::tanh));
    compareD1("exp", static_cast<double (*)(double)>(&::exp));
    compareD1("log", static_cast<double (*)(double)>(&::log));
    compareD1("log10", static_cast<double (*)(double)>(&::log10));
    compareD1("sqrt", static_cast<double (*)(double)>(&::sqrt));
    compareD2("atan2", static_cast<double (*)(double, double)>(&::atan2));
    compareD2("pow", static_cast<double (*)(double, double)>(&::pow));
    compareD2("fmod", static_cast<double (*)(double, double)>(&::fmod));
    compareF1("acosf", &::acosf);
    compareF1("asinf", &::asinf);
    compareF1("atanf", &::atanf);
    compareF1("cosf", &::cosf);
    compareF1("sinf", &::sinf);
    compareF1("tanf", &::tanf);
    compareF1("coshf", &::coshf);
    compareF1("sinhf", &::sinhf);
    compareF1("tanhf", &::tanhf);
    compareF1("expf", &::expf);
    compareF1("exp2f", &::exp2f);
    compareF1("logf", &::logf);
    compareF1("log2f", &::log2f);
    compareF1("log10f", &::log10f);
    compareF1("sqrtf", &::sqrtf);
    compareF2("atan2f", &::atan2f);
    compareF2("powf", &::powf);
  }

  void TestBindings()
  {
    char bad[64] = "";
    const int count = faf_crtmathCheckBindings(bad, sizeof bad);
    Check(count == 0, std::string("faf_crtmathCheckBindings: ") + std::to_string(count) + " bad, first " + bad);
    Check(faf_crtmathFunctionCount() == 33, "33 entry points");
    for (std::size_t i = 0; i < faf_crtmathFunctionCount(); ++i) {
      // Each name is a symbol this executable defines (not libSystem's).
      void* const self = dlsym(RTLD_SELF, faf_crtmathFunctionName(i));
      Dl_info info{};
      Dl_info mine{};
      const bool ok = self != nullptr && dladdr(self, &info) != 0 &&
                      dladdr(reinterpret_cast<void*>(&TestBindings), &mine) != 0 && info.dli_fbase == mine.dli_fbase;
      Check(ok, std::string(faf_crtmathFunctionName(i)) + " is defined in this executable");
    }
  }

  // --- MSVC rand ---------------------------------------------------------------------------

  void TestRand()
  {
    // This thread has not called rand yet: state 1.
    const int want[5] = {41, 18467, 6334, 26500, 19169};
    int got[5];
    for (int& v : got) {
      v = std::rand();
    }
    Check(std::memcmp(got, want, sizeof want) == 0, "rand from state 1: 41 18467 6334 26500 19169");
    std::srand(1);
    for (int& v : got) {
      v = std::rand();
    }
    Check(std::memcmp(got, want, sizeof want) == 0, "srand(1) restarts the sequence");
    Check(RAND_MAX == 0x7FFF, "RAND_MAX is 0x7FFF (compat/Math.h)");

    // Reference LCG over a long run and odd seeds (wrap-around of the 32-bit state).
    for (const unsigned seed : {0u, 2u, 12345u, 0x7FFFFFFFu, 0xFFFFFFFFu, 2028178766u}) {
      std::srand(seed);
      std::uint32_t s = seed;
      bool ok = true;
      for (int i = 0; i < 100000; ++i) {
        s = s * 214013u + 2531011u;
        const int expected = static_cast<int>((s >> 16) & 0x7FFF);
        if (std::rand() != expected) {
          ok = false;
          break;
        }
      }
      Check(ok, "rand matches the MSVC LCG for 100000 draws from seed " + std::to_string(seed));
    }

    // Per-thread state: each new thread starts at 1 and does not disturb the others.
    std::srand(777);
    const int mainFirst = std::rand();
    std::atomic<int> threadsOk{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
      threads.emplace_back([&threadsOk, t] {
        if (t % 2 == 1) {
          std::srand(1000u + static_cast<unsigned>(t));
        }
        std::uint32_t s = (t % 2 == 1) ? 1000u + static_cast<unsigned>(t) : 1u;
        bool ok = true;
        for (int i = 0; i < 50000; ++i) {
          s = s * 214013u + 2531011u;
          ok = ok && std::rand() == static_cast<int>((s >> 16) & 0x7FFF);
        }
        if (ok) {
          threadsOk.fetch_add(1);
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
    Check(threadsOk.load() == 8, "8 threads each see their own MSVC sequence (state 1 or their seed)");
    std::uint32_t s = 777;
    s = s * 214013u + 2531011u;
    Check(mainFirst == static_cast<int>((s >> 16) & 0x7FFF), "srand(777) first value");
    s = s * 214013u + 2531011u;
    Check(std::rand() == static_cast<int>((s >> 16) & 0x7FFF), "main thread state untouched by the other threads");

    // Lua's math.random: rand() % 0x7FFF scaled by 0x38000100 (LuaObject.cpp math_random).
    std::srand(1);
    const float sample = static_cast<float>(std::rand() % 0x7FFF) * Fl(0x38000100u);
    Expect32("math.random() first sample after randomseed(1)", sample, B(static_cast<float>(41) * Fl(0x38000100u)));
  }

  // --- Trace mode ----------------------------------------------------------------------------

  // Distinct call sites for the trace child.
  [[gnu::noinline]] double WrapAcos(const double x)
  {
    const double r = std::acos(x);
    __asm__ __volatile__("" ::: "memory"); // keep the call a non-tail call
    return r;
  }

  [[gnu::noinline]] double CallerOne(const double x)
  {
    const double r = WrapAcos(x);
    __asm__ __volatile__("" ::: "memory");
    return r;
  }

  [[gnu::noinline]] double CallerTwo(const double x)
  {
    const double r = WrapAcos(x * 0.5);
    __asm__ __volatile__("" ::: "memory");
    return r;
  }

  // A static initializer that calls atan: the trace must see calls before main().
  const double g_staticPi = 4.0 * std::atan(1.0 + vZero);

  int TraceChild()
  {
    volatile double sink = g_staticPi;
#pragma clang loop unroll(disable)
    for (int i = 0; i < 100; ++i) {
      sink = sink + std::sin(static_cast<double>(i % 10) + vZero); // 100 calls, 10 distinct
    }
#pragma clang loop unroll(disable)
    for (int i = 0; i < 3; ++i) {
      sink = sink + std::asin(2.0 + vZero); // 3 domain errors
    }
#pragma clang loop unroll(disable)
    for (int i = 0; i < 2; ++i) {
      sink = sink + std::sin(vNan); // 2 NaN operands
    }
#pragma clang loop unroll(disable)
    for (int i = 0; i < 5; ++i) {
      sink = sink + std::atan2(static_cast<float>(i) + vZeroF, 2.0f); // atan2f, 5 calls
    }
#pragma clang loop unroll(disable)
    for (int i = 0; i < 7; ++i) {
      sink = sink + CallerOne(0.25 * i / 7.0);
    }
#pragma clang loop unroll(disable)
    for (int i = 0; i < 9; ++i) {
      sink = sink + CallerTwo(0.25 * i / 9.0);
    }
    // Four threads, one cos call site, 1000 distinct operands each (4000 in all).
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([t] {
        volatile double local = 0;
    #pragma clang loop unroll(disable)
    for (int i = 0; i < 10000; ++i) {
          local = local + std::cos(static_cast<double>(t * 1000 + i % 1000) * 0.001);
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
    // The trace path was fixed at the first call: changing directory must not move it.
    if (chdir("/") != 0) {
      return 3;
    }
    return faf_crtmathTraceEnabled() ? 0 : 2;
  }

  int RunChild(const std::string& self, const std::string& cwd, const std::vector<std::string>& env)
  {
    std::vector<std::string> envStrings;
    for (char** e = environ; *e != nullptr; ++e) {
      if (std::strncmp(*e, "FAF_CRTMATH_", 12) != 0) {
        envStrings.emplace_back(*e);
      }
    }
    for (const auto& e : env) {
      envStrings.push_back(e);
    }
    std::vector<char*> envp;
    for (auto& e : envStrings) {
      envp.push_back(e.data());
    }
    envp.push_back(nullptr);
    std::string arg0 = self;
    std::string arg1 = "--trace-child";
    char* argv[] = {arg0.data(), arg1.data(), nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, self.c_str(), &actions, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
      return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  struct TraceSiteRow
  {
    std::string fn;
    long long calls = 0;
    double distinct = 0;
    long long domain = 0;
    long long nanOperand = 0;
    std::string ra0;
    std::string ra1;
    std::vector<std::string> extra;
  };

  struct TraceFile
  {
    std::map<std::string, std::vector<long long>> functions; // calls sites domain nan dropped
    std::vector<TraceSiteRow> sites;
  };

  bool ParseTrace(const std::string& path, TraceFile& out)
  {
    std::ifstream in(path);
    if (!in) {
      return false;
    }
    std::string line;
    enum
    {
      kNone,
      kFunctions,
      kSites
    } section = kNone;
    while (std::getline(in, line)) {
      if (line == "## Functions") {
        section = kFunctions;
        continue;
      }
      if (line == "## Call sites") {
        section = kSites;
        continue;
      }
      if (line.empty() || line[0] == '#') {
        continue;
      }
      if (section == kFunctions) {
        std::istringstream row(line);
        std::string name;
        row >> name;
        std::vector<long long> values;
        long long v;
        while (row >> v) {
          values.push_back(v);
        }
        out.functions[name] = values;
      } else if (section == kSites) {
        if (line.rfind("  ra0 ", 0) == 0) {
          out.sites.back().ra0 = line.substr(6, line.find(' ', 6) - 6);
        } else if (line.rfind("  ra1 ", 0) == 0) {
          out.sites.back().ra1 = line.substr(6, line.find(' ', 6) - 6);
        } else if (line.rfind("  ", 0) == 0) {
          out.sites.back().extra.push_back(line.substr(2));
        } else {
          TraceSiteRow site;
          char fn[32] = {};
          std::sscanf(line.c_str(), "%31s\tcalls %lld\tdistinct~ %lf\tdomain %lld\tnan_operand %lld", fn, &site.calls,
                      &site.distinct, &site.domain, &site.nanOperand);
          site.fn = fn;
          out.sites.push_back(site);
        }
      }
    }
    return true;
  }

  void TestTrace(const std::string& self, const std::string& workDir)
  {
    const std::string traceDir = workDir + "/trace";
    const std::string cwdDir = workDir + "/trace_cwd";
    mkdir(traceDir.c_str(), 0755);
    mkdir(cwdDir.c_str(), 0755);
    const std::string explicitFile = traceDir + "/explicit.txt";
    std::remove(explicitFile.c_str());
    std::remove((cwdDir + "/faf_crtmath_trace.txt").c_str());
    std::remove((traceDir + "/faf_crtmath_trace.txt").c_str());

    // Off: no file anywhere.
    Check(RunChild(self, traceDir, {}) == 2, "trace child without FAF_CRTMATH_TRACE: tracing reports off");
    struct stat st{};
    Check(stat((traceDir + "/faf_crtmath_trace.txt").c_str(), &st) != 0, "no trace file when tracing is off");
    Check(RunChild(self, traceDir, {"FAF_CRTMATH_TRACE=0"}) == 2, "FAF_CRTMATH_TRACE=0 is off");

    // On, default file: the directory the process started in, even after chdir("/").
    Check(RunChild(self, cwdDir, {"FAF_CRTMATH_TRACE=1"}) == 0, "trace child with FAF_CRTMATH_TRACE=1 exits 0");
    Check(stat((cwdDir + "/faf_crtmath_trace.txt").c_str(), &st) == 0,
          "default trace file is ./faf_crtmath_trace.txt of the starting directory");

    // On, explicit file.
    Check(RunChild(self, cwdDir, {"FAF_CRTMATH_TRACE=1", "FAF_CRTMATH_TRACE_FILE=" + explicitFile}) == 0,
          "trace child with FAF_CRTMATH_TRACE_FILE exits 0");
    TraceFile trace;
    Check(ParseTrace(explicitFile, trace), "trace file written to FAF_CRTMATH_TRACE_FILE");

    const auto calls = [&](const char* fn) {
      const auto it = trace.functions.find(fn);
      return it == trace.functions.end() || it->second.empty() ? -1LL : it->second[0];
    };
    const auto column = [&](const char* fn, std::size_t i) {
      const auto it = trace.functions.find(fn);
      return it == trace.functions.end() || it->second.size() <= i ? -1LL : it->second[i];
    };
    Check(trace.functions.size() == faf_crtmathFunctionCount(), "the function table lists every entry point");
    Check(calls("sin") == 102, "sin: 100 + 2 calls (got " + std::to_string(calls("sin")) + ")");
    Check(column("sin", 3) == 2, "sin: 2 NaN operands");
    Check(calls("asin") == 3 && column("asin", 2) == 3, "asin: 3 calls, 3 domain errors");
    Check(calls("atan2f") == 5, "atan2f: 5 calls (std::atan2(float, float))");
    Check(calls("acos") == 16, "acos: 7 + 9 calls");
    Check(column("acos", 1) == 2, "acos: two sites (same return address, different callers)");
    Check(calls("cos") == 40000, "cos: 4 threads x 10000 calls, none lost");
    Check(calls("atan") >= 1, "atan: the static initializer's call is traced");
    Check(column("cos", 4) == 0, "nothing dropped");

    // The loops are not unrolled, so each is one call site.
    int sinSites100 = 0;
    for (const auto& site : trace.sites) {
      sinSites100 += site.fn == "sin" && site.calls == 100;
    }
    Check(sinSites100 == 1, "one sin site with the 100 calls");
    int acosSites = 0;
    std::string acosRa0;
    std::vector<std::string> acosRa1;
    for (const auto& site : trace.sites) {
      if (site.fn == "acos") {
        ++acosSites;
        acosRa1.push_back(site.ra1);
        Check(acosRa0.empty() || acosRa0 == site.ra0, "acos sites share the return address inside WrapAcos");
        acosRa0 = site.ra0;
        Check(site.calls == 7 || site.calls == 9, "acos site counts are 7 and 9");
      }
      if (site.fn == "cos") {
        Check(site.distinct > 3600 && site.distinct < 4400,
              "cos distinct~ about 4000 (got " + std::to_string(site.distinct) + ")");
      }
      if (site.fn == "sin" && site.calls == 100) {
        Check(site.distinct > 8 && site.distinct < 12, "sin distinct~ about 10");
        int samples = 0;
        for (const auto& e : site.extra) {
          samples += e.rfind("sample ", 0) == 0;
        }
        Check(samples == 8, "sin site keeps 8 distinct samples");
      }
      if (site.fn == "asin") {
        bool first = false;
        for (const auto& e : site.extra) {
          first = first || e.find("first domain error 0x4000000000000000") != std::string::npos;
        }
        Check(first && site.domain == 3, "asin site records its first domain error operand (2.0)");
      }
    }
    Check(acosSites == 2 && acosRa1.size() == 2 && acosRa1[0] != acosRa1[1] && acosRa1[0] != "-" &&
            acosRa1[1] != "-",
          "acos: the two sites differ by the caller's return address");
  }

  // --- Cost with tracing off ---------------------------------------------------------------

  void ReportCost()
  {
    auto* const libmSin = reinterpret_cast<double (*)(double)>(dlsym(RTLD_NEXT, "sin"));
    double (*volatile oursSin)(double) = &::sin;
    double (*volatile directSin)(double) = libmSin;
    constexpr int kCalls = 2000000;
    const auto time = [&](double (*const fn)(double)) {
      volatile double sink = 0;
      const auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < kCalls; ++i) {
        sink = sink + fn(static_cast<double>(i & 1023) * 0.001);
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / kCalls;
    };
    time(directSin);
    const double direct = time(directSin);
    const double ours = time(oursSin);
    std::printf("cost (informational, machine load matters): libm sin %.2f ns/call, faf_crtmath sin (trace off) %.2f ns/call\n",
                direct, ours);
  }
} // namespace

int main(int argc, char** argv)
{
  if (argc > 1 && std::strcmp(argv[1], "--trace-child") == 0) {
    return TraceChild();
  }
  char selfPath[PATH_MAX];
  char workPath[PATH_MAX];
  if (realpath(argv[0], selfPath) == nullptr || realpath(argc > 1 ? argv[1] : ".", workPath) == nullptr) {
    std::fprintf(stderr, "cannot resolve the test paths\n");
    return 2;
  }
  const std::string self = selfPath;     // absolute: the children start in other directories
  const std::string workDir = workPath;

  TestBindings();
  TestSpecialValues();
  TestAgainstLibm();
  TestRand();
  Check(!faf_crtmathTraceEnabled(), "tracing is off in the test process");
  TestTrace(self, workDir);
  ReportCost();

  std::printf("crt_unit_test: %d of %d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
