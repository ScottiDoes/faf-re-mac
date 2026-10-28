#pragma once

// Shared between CrtMath.cpp (entry points) and CrtMathTrace.cpp (trace mode). Not for
// engine code: the engine calls the C names. See CrtMath.h.

#include <atomic>
#include <cstddef>
#include <cstdint>

// Every entry point faf_crtmath defines: (name, kind). Kinds: D1 double(double),
// D2 double(double, double), F1 float(float), F2 float(float, float).
//
// - The UCRT 10.0.22000 objects the x64 reference build links (DETERMINISM.md §1): acos asin
//   atan atan2 atan2f cosh sinh tanh sqrt sqrtf cos cosf sin sinf tan tanf exp log log10 logf
//   pow powf fmod.
// - Referenced by the engine objects as well (nm, 27/10/2026): exp2f log2f (CameraImpl).
// - The float twins libc++'s std:: float overloads call under -fmath-errno (std::exp(float)
//   is expf, ...), so a new call site is traced instead of silently reaching libSystem:
//   acosf asinf atanf expf log10f coshf sinhf tanhf.
// Exact functions (floor, ceil, ldexp, frexp, lrint, fma, nextafter, ...) stay libSystem's:
// they are correctly rounded by definition on both sides. sqrt and fmod are exact too but
// are defined here for their NaN results (the engine's inline fsqrt calls sqrt only for a
// NaN result, and uses that call's value).
#define FAF_CRTMATH_FUNCTIONS(X) \
  X(acos, D1)                    \
  X(asin, D1)                    \
  X(atan, D1)                    \
  X(atan2, D2)                   \
  X(cos, D1)                     \
  X(sin, D1)                     \
  X(tan, D1)                     \
  X(cosh, D1)                    \
  X(sinh, D1)                    \
  X(tanh, D1)                    \
  X(exp, D1)                     \
  X(log, D1)                     \
  X(log10, D1)                   \
  X(pow, D2)                     \
  X(fmod, D2)                    \
  X(sqrt, D1)                    \
  X(acosf, F1)                   \
  X(asinf, F1)                   \
  X(atanf, F1)                   \
  X(atan2f, F2)                  \
  X(cosf, F1)                    \
  X(sinf, F1)                    \
  X(tanf, F1)                    \
  X(coshf, F1)                   \
  X(sinhf, F1)                   \
  X(tanhf, F1)                   \
  X(expf, F1)                    \
  X(exp2f, F1)                   \
  X(logf, F1)                    \
  X(log2f, F1)                   \
  X(log10f, F1)                  \
  X(powf, F2)                    \
  X(sqrtf, F1)

namespace faf_posix::crt::detail
{
  enum class Fn : std::uint8_t
  {
#define FAF_CRTMATH_ENUM(name, kind) name,
    FAF_CRTMATH_FUNCTIONS(FAF_CRTMATH_ENUM)
#undef FAF_CRTMATH_ENUM
    kCount
  };

  enum class Kind : std::uint8_t
  {
    D1,
    D2,
    F1,
    F2
  };

  inline constexpr std::size_t kFnCount = static_cast<std::size_t>(Fn::kCount);

  inline constexpr const char* kFnNames[kFnCount] = {
#define FAF_CRTMATH_NAME(name, kind) #name,
    FAF_CRTMATH_FUNCTIONS(FAF_CRTMATH_NAME)
#undef FAF_CRTMATH_NAME
  };

  inline constexpr Kind kFnKinds[kFnCount] = {
#define FAF_CRTMATH_KIND(name, kind) Kind::kind,
    FAF_CRTMATH_FUNCTIONS(FAF_CRTMATH_KIND)
#undef FAF_CRTMATH_KIND
  };

  // Trace state: unknown until the first call reads FAF_CRTMATH_TRACE, then off or on for
  // the rest of the process.
  enum : int
  {
    kTraceUnknown = 0,
    kTraceOff = 1,
    kTraceOn = 2,
  };
  extern constinit std::atomic<int> g_traceState;

  // Reads the environment once (the first caller to finish decides) and returns whether
  // tracing is on. Registers the at-exit dump when it is.
  bool TraceInit() noexcept;

  inline bool TraceOn() noexcept
  {
    const int state = g_traceState.load(std::memory_order_relaxed);
    if (__builtin_expect(state == kTraceOff, 1)) {
      return false;
    }
    return state == kTraceOn || TraceInit();
  }

  // Records one call. Operands and result are raw bits (a float's in the low 32 bits;
  // `b` is 0 for one-operand functions). `returnAddress` is the entry point's return address
  // (into its caller, read from the entry point's frame record); the caller's caller is found
  // by walking the frame chain from there.
  void TraceRecord(Fn fn, void* returnAddress, std::uint64_t a, std::uint64_t b, std::uint64_t result) noexcept;
} // namespace faf_posix::crt::detail
