// faf_crtmath entry points: see CrtMath.h for what they are and why. The list is
// FAF_CRTMATH_FUNCTIONS in CrtMathInternal.h.
//
// Each entry point calls Apple's libm through a pointer resolved once with
// dlsym(RTLD_NEXT, name): RTLD_NEXT searches the images loaded after this one, so it never
// returns these definitions (that would recurse); Resolve() aborts if it ever did. The
// pointers are constant-initialized, so the entry points work from the engine's static
// initializers (Wm3::Math<float>::PI = (float)(4.0*atan(1.0)), the header quaternion
// constants built with sin/cos), whatever their order.
//
// Fast path: load the pointer, store the operands to a stack slot, call libm, then one
// predicted branch (NaN result, a pi candidate, or tracing not off) to the out-of-line
// SlowPath. Nothing stays in a callee-saved register across the libm call: keeping the
// operand in d8 made the caller's own d8 wait for the restore after libm returned, which
// serialized back-to-back calls in a loop (13 ns against libm's 3.7 ns per sin, measured).

#include "platform/posix/crt/CrtMath.h"
#include "platform/posix/crt/CrtMathInternal.h"

#include <dlfcn.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace faf_posix::crt::detail
{
  // Defined here, next to its hot readers; CrtMathTrace.cpp decides it.
  constinit std::atomic<int> g_traceState{kTraceUnknown};

  namespace
  {
    using D1 = double (*)(double);
    using D2 = double (*)(double, double);
    using F1 = float (*)(float);
    using F2 = float (*)(float, float);

    constinit std::atomic<void*> g_next[kFnCount] = {};

    [[nodiscard]] void* LookupNext(const Fn fn) noexcept
    {
      return dlsym(RTLD_NEXT, kFnNames[static_cast<std::size_t>(fn)]);
    }

    [[gnu::noinline, gnu::cold]] void* Resolve(const Fn fn, void* const self) noexcept
    {
      void* const next = LookupNext(fn);
      if (next == nullptr || next == self) {
        // No libm behind this entry point, or the lookup came back to the entry point itself:
        // either way every result would be wrong (or the call would recurse forever).
        std::fprintf(stderr, "faf_crtmath: cannot bind %s to libSystem (dlsym gave %p, entry point %p)\n",
                     kFnNames[static_cast<std::size_t>(fn)], next, self);
        std::abort();
      }
      g_next[static_cast<std::size_t>(fn)].store(next, std::memory_order_release);
      return next;
    }

    [[nodiscard]] inline std::uint64_t Bits(const double x) noexcept
    {
      std::uint64_t bits;
      std::memcpy(&bits, &x, sizeof bits);
      return bits;
    }

    [[nodiscard]] inline std::uint32_t Bits(const float x) noexcept
    {
      std::uint32_t bits;
      std::memcpy(&bits, &x, sizeof bits);
      return bits;
    }

    [[nodiscard]] inline double DoubleFromBits(const std::uint64_t bits) noexcept
    {
      double x;
      std::memcpy(&x, &bits, sizeof x);
      return x;
    }

    [[nodiscard]] inline float FloatFromBits(const std::uint32_t bits) noexcept
    {
      float x;
      std::memcpy(&x, &bits, sizeof x);
      return x;
    }

    // Bit tests, so no compiler NaN folklore is involved.
    [[nodiscard]] inline bool IsNan(const double x) noexcept
    {
      return (Bits(x) & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull;
    }

    [[nodiscard]] inline bool IsNan(const float x) noexcept
    {
      return (Bits(x) & 0x7FFFFFFFu) > 0x7F800000u;
    }

    // A NaN operand as x86 returns it: quiet bit set, sign and payload kept.
    [[nodiscard]] inline double Quiet(const double x) noexcept
    {
      return DoubleFromBits(Bits(x) | 0x0008000000000000ull);
    }

    [[nodiscard]] inline float Quiet(const float x) noexcept
    {
      return FloatFromBits(Bits(x) | 0x00400000u);
    }

    template <class T>
    [[nodiscard]] inline T DefaultNan() noexcept
    {
      if constexpr (sizeof(T) == 8) {
        return DoubleFromBits(kX86DefaultNan64);
      } else {
        return FloatFromBits(kX86DefaultNan32);
      }
    }

    // The UCRT's NaN results over Apple's (CrtMath.h). A non-NaN result is Apple's as is.
    // FAF_POSIX_TODO(determinism): which NaN the UCRT returns when *both* operands of
    // atan2/pow/fmod are NaN (here the first) is not verified against 10.0.22000.
    template <class T>
    [[nodiscard]] T Finish(const T x, const T result) noexcept
    {
      if (!IsNan(result)) {
        return result;
      }
      return IsNan(x) ? Quiet(x) : DefaultNan<T>();
    }

    template <class T>
    [[nodiscard]] T Finish(const T x, const T y, const T result) noexcept
    {
      if (!IsNan(result)) {
        return result;
      }
      if (IsNan(x)) {
        return Quiet(x);
      }
      return IsNan(y) ? Quiet(y) : DefaultNan<T>();
    }

    // Annex F's exact float results +-pi and +-pi/2 (atan2f(+-0, -0 or x < 0), atan2f(y, -inf),
    // atan2f(+-inf, x), atanf(+-inf), asinf(+-1), acosf(-1)): Apple's libm returns them rounded
    // toward zero (0x40490FDA, 0x3FC90FDA, measured 27/10/2026), everything else, the UCRT's
    // double-evaluated float functions included, the nearest float (0x40490FDB, 0x3FC90FDB),
    // which Apple's own atan2f(1, 0) and acosf(0) return too. Only these operand cases are
    // corrected. FAF_POSIX_TODO(determinism): Apple's atan2f also stays at or below
    // 0x40490FDA for |result| just under pi, where the nearest float is 0x40490FDB; the
    // UCRT 22000 clone settles those (atan2f is render-only per DETERMINISM.md §2.1).
    [[nodiscard]] inline float NearestPiMultiple(const float result) noexcept
    {
      const std::uint32_t bits = Bits(result);
      const std::uint32_t magnitude = bits & 0x7FFFFFFFu;
      if (magnitude == 0x40490FDAu || magnitude == 0x3FC90FDAu) {
        return FloatFromBits(bits + 1); // same sign, next magnitude up
      }
      return result;
    }

    [[nodiscard]] inline bool IsInf(const float x) noexcept
    {
      return (Bits(x) & 0x7FFFFFFFu) == 0x7F800000u;
    }

    // Per-function corrections after Finish(): none except the pi cases above.
    template <Fn fn>
    [[nodiscard]] inline float Adjust(const float x, const float result) noexcept
    {
      if constexpr (fn == Fn::atanf) {
        return IsInf(x) ? NearestPiMultiple(result) : result;
      } else if constexpr (fn == Fn::asinf) {
        return (x == 1.0f || x == -1.0f) ? NearestPiMultiple(result) : result;
      } else if constexpr (fn == Fn::acosf) {
        return x == -1.0f ? NearestPiMultiple(result) : result;
      } else {
        return result;
      }
    }

    template <Fn fn>
    [[nodiscard]] inline float Adjust(const float y, const float x, const float result) noexcept
    {
      if constexpr (fn == Fn::atan2f) {
        const bool exactPi = (y == 0.0f && (x < 0.0f || (x == 0.0f && (Bits(x) >> 31) != 0))) ||
                             (IsInf(x) && x < 0.0f && !IsInf(y) && !IsNan(y));
        const bool exactHalfPi = IsInf(y) && !IsInf(x) && !IsNan(x);
        return (exactPi || exactHalfPi) ? NearestPiMultiple(result) : result;
      } else {
        return result;
      }
    }

    // A result the fast path must not return as is: NaN (UCRT NaN rules), a value the pi
    // correction may change, or tracing not known to be off.
    template <Fn fn, class T>
    [[nodiscard]] inline bool NeedsSlowPath(const T result) noexcept
    {
      bool slow = result != result || g_traceState.load(std::memory_order_relaxed) != kTraceOff;
      if constexpr (fn == Fn::atanf || fn == Fn::asinf || fn == Fn::acosf || fn == Fn::atan2f) {
        const std::uint32_t magnitude = Bits(result) & 0x7FFFFFFFu;
        slow = slow || magnitude == 0x40490FDAu || magnitude == 0x3FC90FDAu;
      }
      return slow;
    }

    // The caller's return address, from the entry point's own frame record (every entry point
    // has one: it calls libm), so no register has to keep it across that call.
    [[nodiscard]] inline void* CallerAddress(void* const frame) noexcept
    {
      return static_cast<void* const*>(frame)[1];
    }

    template <Fn fn, class T>
    [[gnu::noinline]] T SlowPath(void* const returnAddress, const T x, T result) noexcept
    {
      if constexpr (sizeof(T) == 4) {
        result = Adjust<fn>(x, Finish(x, result));
      } else {
        result = Finish(x, result);
      }
      if (TraceOn()) {
        TraceRecord(fn, returnAddress, Bits(x), 0, Bits(result));
      }
      return result;
    }

    template <Fn fn, class T>
    [[gnu::noinline]] T SlowPath(void* const returnAddress, const T x, const T y, T result) noexcept
    {
      if constexpr (sizeof(T) == 4) {
        result = Adjust<fn>(x, y, Finish(x, y, result));
      } else {
        result = Finish(x, y, result);
      }
      if (TraceOn()) {
        TraceRecord(fn, returnAddress, Bits(x), Bits(y), Bits(result));
      }
      return result;
    }

    // The first call of an entry point: resolve, then as SlowPath.
    template <Fn fn, class T>
    [[gnu::noinline, gnu::cold]] T FirstCall(void* const returnAddress, void* const self, const T x) noexcept
    {
      const auto next = reinterpret_cast<T (*)(T)>(Resolve(fn, self));
      return SlowPath<fn>(returnAddress, x, next(x));
    }

    template <Fn fn, class T>
    [[gnu::noinline, gnu::cold]] T FirstCall(void* const returnAddress, void* const self, const T x, const T y) noexcept
    {
      const auto next = reinterpret_cast<T (*)(T, T)>(Resolve(fn, self));
      return SlowPath<fn>(returnAddress, x, y, next(x, y));
    }
  } // namespace
} // namespace faf_posix::crt::detail

// --- The entry points --------------------------------------------------------------------
// C linkage, no exception specification (they redeclare <math.h>'s). The operands go to a
// volatile stack slot (read only on the slow path) instead of a callee-saved register.

#define FAF_CRTMATH_DEFINE_1(name, T, Ptr)                                                           \
  extern "C" T name(T x)                                                                             \
  {                                                                                                  \
    using namespace faf_posix::crt::detail;                                                          \
    void* const next = g_next[static_cast<std::size_t>(Fn::name)].load(std::memory_order_relaxed);   \
    if (__builtin_expect(next == nullptr, 0)) {                                                      \
      return FirstCall<Fn::name>(CallerAddress(__builtin_frame_address(0)),                          \
                                 reinterpret_cast<void*>(static_cast<Ptr>(&::name)), x);             \
    }                                                                                                \
    volatile T operand = x;                                                                          \
    const T result = reinterpret_cast<Ptr>(next)(x);                                                 \
    if (__builtin_expect(NeedsSlowPath<Fn::name>(result), 0)) {                                      \
      return SlowPath<Fn::name>(CallerAddress(__builtin_frame_address(0)), static_cast<T>(operand),  \
                                result);                                                             \
    }                                                                                                \
    return result;                                                                                   \
  }

#define FAF_CRTMATH_DEFINE_2(name, T, Ptr)                                                           \
  extern "C" T name(T x, T y)                                                                        \
  {                                                                                                  \
    using namespace faf_posix::crt::detail;                                                          \
    void* const next = g_next[static_cast<std::size_t>(Fn::name)].load(std::memory_order_relaxed);   \
    if (__builtin_expect(next == nullptr, 0)) {                                                      \
      return FirstCall<Fn::name>(CallerAddress(__builtin_frame_address(0)),                          \
                                 reinterpret_cast<void*>(static_cast<Ptr>(&::name)), x, y);          \
    }                                                                                                \
    volatile T operands[2] = {x, y};                                                                 \
    const T result = reinterpret_cast<Ptr>(next)(x, y);                                              \
    if (__builtin_expect(NeedsSlowPath<Fn::name>(result), 0)) {                                      \
      return SlowPath<Fn::name>(CallerAddress(__builtin_frame_address(0)), static_cast<T>(operands[0]), \
                                static_cast<T>(operands[1]), result);                                \
    }                                                                                                \
    return result;                                                                                   \
  }

#define FAF_CRTMATH_DEFINE_D1(name) FAF_CRTMATH_DEFINE_1(name, double, D1)
#define FAF_CRTMATH_DEFINE_D2(name) FAF_CRTMATH_DEFINE_2(name, double, D2)
#define FAF_CRTMATH_DEFINE_F1(name) FAF_CRTMATH_DEFINE_1(name, float, F1)
#define FAF_CRTMATH_DEFINE_F2(name) FAF_CRTMATH_DEFINE_2(name, float, F2)

#define FAF_CRTMATH_DEFINE(name, kind) FAF_CRTMATH_DEFINE_##kind(name)
FAF_CRTMATH_FUNCTIONS(FAF_CRTMATH_DEFINE)
#undef FAF_CRTMATH_DEFINE

// --- Introspection --------------------------------------------------------------------------

extern "C" int faf_crtmathCheckBindings(char* const firstBad, const std::size_t firstBadSize) noexcept
{
  using namespace faf_posix::crt::detail;
  void* const self[kFnCount] = {
#define FAF_CRTMATH_SELF_D1(name) reinterpret_cast<void*>(static_cast<D1>(&::name)),
#define FAF_CRTMATH_SELF_D2(name) reinterpret_cast<void*>(static_cast<D2>(&::name)),
#define FAF_CRTMATH_SELF_F1(name) reinterpret_cast<void*>(static_cast<F1>(&::name)),
#define FAF_CRTMATH_SELF_F2(name) reinterpret_cast<void*>(static_cast<F2>(&::name)),
#define FAF_CRTMATH_SELF(name, kind) FAF_CRTMATH_SELF_##kind(name)
    FAF_CRTMATH_FUNCTIONS(FAF_CRTMATH_SELF)
#undef FAF_CRTMATH_SELF
#undef FAF_CRTMATH_SELF_D1
#undef FAF_CRTMATH_SELF_D2
#undef FAF_CRTMATH_SELF_F1
#undef FAF_CRTMATH_SELF_F2
  };

  int bad = 0;
  for (std::size_t i = 0; i < kFnCount; ++i) {
    const Fn fn = static_cast<Fn>(i);
    void* const next = LookupNext(fn);
    // The bound pointer, if a call already resolved it, must be the same libm function.
    void* const bound = g_next[i].load(std::memory_order_acquire);
    // The libm function must live in another image than the entry point (libsystem_m).
    Dl_info nextInfo{};
    Dl_info selfInfo{};
    const bool sameImage = next != nullptr && dladdr(next, &nextInfo) != 0 && dladdr(self[i], &selfInfo) != 0 &&
                           nextInfo.dli_fbase == selfInfo.dli_fbase;
    if (next == nullptr || next == self[i] || sameImage || (bound != nullptr && bound != next)) {
      if (bad == 0 && firstBad != nullptr && firstBadSize != 0) {
        std::snprintf(firstBad, firstBadSize, "%s", kFnNames[i]);
      }
      ++bad;
    }
  }
  return bad;
}

extern "C" std::size_t faf_crtmathFunctionCount() noexcept
{
  return faf_posix::crt::detail::kFnCount;
}

extern "C" const char* faf_crtmathFunctionName(const std::size_t index) noexcept
{
  using namespace faf_posix::crt::detail;
  return index < kFnCount ? kFnNames[index] : nullptr;
}
