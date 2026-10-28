// faf_crtmath trace mode (FAF_CRTMATH_TRACE=1): which UCRT functions the sim reaches, from
// where, and with how many distinct operands (macos/docs/DETERMINISM.md §6.4). See CrtMath.h.
//
// A call site is (function, return address, the caller's return address): the second
// address separates the callers of a wrapper such as msvc8::acos, whose fallback lane is
// the one std::acos call site. Sites live in a fixed lock-free open-addressing table, so
// recording works from static initializers and from every sim thread at once. Per site:
// call count, NaN operands, domain errors (NaN from non-NaN operands, given the x86 default
// NaN), operand range, the first few distinct operands with their results, and a 4096-bit
// linear-counting sketch of the distinct operands.

#include "platform/posix/crt/CrtMath.h"
#include "platform/posix/crt/CrtMathInternal.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace faf_posix::crt::detail
{
  namespace
  {
    constexpr std::size_t kSiteCount = 4096; // power of two
    constexpr std::uint32_t kSamples = 8;
    constexpr std::size_t kSketchBits = 4096;
    constexpr std::size_t kSketchWords = kSketchBits / 64;

    constexpr std::uint64_t kPlusInfBits = 0x7FF0000000000000ull;
    constexpr std::uint64_t kMinusInfBits = 0xFFF0000000000000ull;

    enum : std::uint32_t
    {
      kSiteEmpty = 0,
      kSiteClaimed = 1,
      kSitePublished = 2,
    };

    // A plain integer used atomically. Trivially default-constructible, so the table below
    // is zero-initialized storage with no constructor to run: it works from the first
    // static initializer on (std::atomic's constexpr constructors exceed the constant
    // evaluator's step limit for a table this size).
    template <class T>
    struct Atomic
    {
      T raw;

      [[nodiscard]] T load(const int order = __ATOMIC_RELAXED) const noexcept { return __atomic_load_n(&raw, order); }
      void store(const T value, const int order = __ATOMIC_RELAXED) noexcept { __atomic_store_n(&raw, value, order); }
      T fetch_add(const T value, const int order = __ATOMIC_RELAXED) noexcept
      {
        return __atomic_fetch_add(&raw, value, order);
      }
      T fetch_or(const T value, const int order = __ATOMIC_RELAXED) noexcept
      {
        return __atomic_fetch_or(&raw, value, order);
      }
      bool compare_exchange(T& expected, const T desired, const int success = __ATOMIC_ACQ_REL,
                            const int failure = __ATOMIC_ACQUIRE) noexcept
      {
        return __atomic_compare_exchange_n(&raw, &expected, desired, false, success, failure);
      }
    };

    struct Sample
    {
      Atomic<std::uint32_t> ready;
      Atomic<std::uint64_t> a;
      Atomic<std::uint64_t> b;
      Atomic<std::uint64_t> result;
    };

    // Operand range, stored XOR-encoded so that zero storage means "none yet": the minimum
    // as bits ^ +inf, the maximum as bits ^ -inf.
    struct Range
    {
      Atomic<std::uint64_t> minEncoded;
      Atomic<std::uint64_t> maxEncoded;
    };

    struct Site
    {
      Atomic<std::uint32_t> state;
      std::uint32_t fn;
      std::uintptr_t ra0;
      std::uintptr_t ra1;
      Atomic<std::uint64_t> calls;
      Atomic<std::uint64_t> nanOperand;
      Atomic<std::uint64_t> domain;
      Range rangeA;
      Range rangeB;
      Atomic<std::uint32_t> sampleCount;
      Sample samples[kSamples];
      Sample firstDomain;
      Atomic<std::uint64_t> sketch[kSketchWords];
    };

    Site g_sites[kSiteCount];
    Atomic<std::uint64_t> g_dropped[kFnCount];
    char g_tracePath[PATH_MAX];

    [[nodiscard]] double DoubleFromBits(const std::uint64_t bits) noexcept
    {
      double x;
      std::memcpy(&x, &bits, sizeof x);
      return x;
    }

    [[nodiscard]] std::uint64_t Bits(const double x) noexcept
    {
      std::uint64_t bits;
      std::memcpy(&bits, &x, sizeof bits);
      return bits;
    }

    [[nodiscard]] bool IsFloatKind(const Kind kind) noexcept
    {
      return kind == Kind::F1 || kind == Kind::F2;
    }

    [[nodiscard]] bool IsBinaryKind(const Kind kind) noexcept
    {
      return kind == Kind::D2 || kind == Kind::F2;
    }

    // The operand as a double (a float operand's bits are in the low 32 bits).
    [[nodiscard]] double OperandValue(const std::uint64_t bits, const bool isFloat) noexcept
    {
      if (!isFloat) {
        return DoubleFromBits(bits);
      }
      const auto low = static_cast<std::uint32_t>(bits);
      float x;
      std::memcpy(&x, &low, sizeof x);
      return static_cast<double>(x);
    }

    [[nodiscard]] bool IsNanBits(const std::uint64_t bits, const bool isFloat) noexcept
    {
      if (isFloat) {
        return (static_cast<std::uint32_t>(bits) & 0x7FFFFFFFu) > 0x7F800000u;
      }
      return (bits & 0x7FFFFFFFFFFFFFFFull) > kPlusInfBits;
    }

    [[nodiscard]] std::uint64_t Mix(std::uint64_t h) noexcept
    {
      h ^= h >> 33;
      h *= 0xFF51AFD7ED558CCDull;
      h ^= h >> 33;
      h *= 0xC4CEB9FE1A85EC53ull;
      h ^= h >> 33;
      return h;
    }

    [[nodiscard]] std::uintptr_t StripAddress(const std::uintptr_t address) noexcept
    {
      // Plain arm64 (not arm64e) has no pointer authentication on return addresses; keep the
      // 48-bit address space only, in case.
      return address & 0x0000FFFFFFFFFFFFull;
    }

    // The return address one level above `returnAddress`: walk the frame records (Darwin
    // arm64 keeps them in every non-leaf function) to the one that holds `returnAddress`,
    // the entry point's, and read its caller's. 0 if the chain looks wrong.
    [[gnu::noinline]] std::uintptr_t CallerOfCaller(const std::uintptr_t returnAddress) noexcept
    {
      const pthread_t self = pthread_self();
      const auto top = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(self));
      const std::size_t size = pthread_get_stacksize_np(self);
      const std::uintptr_t bottom = top - size;
      const auto valid = [&](const std::uintptr_t fp) noexcept {
        return fp >= bottom && fp + 2 * sizeof(std::uintptr_t) <= top && (fp & 7u) == 0;
      };

      auto fp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
      for (int depth = 0; depth < 8 && valid(fp); ++depth) {
        const auto* const record = reinterpret_cast<const std::uintptr_t*>(fp);
        const std::uintptr_t savedFp = record[0];
        const std::uintptr_t savedLr = StripAddress(record[1]);
        if (savedFp <= fp || !valid(savedFp)) {
          return 0;
        }
        if (savedLr == returnAddress) {
          return StripAddress(reinterpret_cast<const std::uintptr_t*>(savedFp)[1]);
        }
        fp = savedFp;
      }
      return 0;
    }

    [[nodiscard]] Site* FindSite(const std::uint32_t fn, const std::uintptr_t ra0, const std::uintptr_t ra1) noexcept
    {
      std::size_t index = Mix((static_cast<std::uint64_t>(fn) << 56) ^ ra0 ^ Mix(ra1)) & (kSiteCount - 1);
      for (std::size_t probe = 0; probe < kSiteCount; ++probe, index = (index + 1) & (kSiteCount - 1)) {
        Site& site = g_sites[index];
        std::uint32_t state = site.state.load(__ATOMIC_ACQUIRE);
        if (state == kSiteEmpty) {
          std::uint32_t expected = kSiteEmpty;
          if (site.state.compare_exchange(expected, kSiteClaimed, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
            site.fn = fn;
            site.ra0 = ra0;
            site.ra1 = ra1;
            site.state.store(kSitePublished, __ATOMIC_RELEASE);
            return &site;
          }
          state = expected;
        }
        while (state == kSiteClaimed) {
          // Another thread is writing the key: three stores.
          state = site.state.load(__ATOMIC_ACQUIRE);
        }
        if (site.fn == fn && site.ra0 == ra0 && site.ra1 == ra1) {
          return &site;
        }
      }
      return nullptr;
    }

    // Encoded so that zero storage means "none yet" (Range).
    void UpdateMin(Range& range, const double value) noexcept
    {
      std::uint64_t current = range.minEncoded.load(__ATOMIC_RELAXED);
      while (value < DoubleFromBits(current ^ kPlusInfBits) &&
             !range.minEncoded.compare_exchange(current, Bits(value) ^ kPlusInfBits, __ATOMIC_RELAXED,
                                                __ATOMIC_RELAXED)) {
      }
    }

    void UpdateMax(Range& range, const double value) noexcept
    {
      std::uint64_t current = range.maxEncoded.load(__ATOMIC_RELAXED);
      while (value > DoubleFromBits(current ^ kMinusInfBits) &&
             !range.maxEncoded.compare_exchange(current, Bits(value) ^ kMinusInfBits, __ATOMIC_RELAXED,
                                                __ATOMIC_RELAXED)) {
      }
    }

    [[nodiscard]] double RangeMin(const Range& range) noexcept
    {
      return DoubleFromBits(range.minEncoded.load(__ATOMIC_RELAXED) ^ kPlusInfBits);
    }

    [[nodiscard]] double RangeMax(const Range& range) noexcept
    {
      return DoubleFromBits(range.maxEncoded.load(__ATOMIC_RELAXED) ^ kMinusInfBits);
    }

    void StoreSample(Sample& sample, const std::uint64_t a, const std::uint64_t b, const std::uint64_t result) noexcept
    {
      sample.a.store(a, __ATOMIC_RELAXED);
      sample.b.store(b, __ATOMIC_RELAXED);
      sample.result.store(result, __ATOMIC_RELAXED);
      sample.ready.store(1, __ATOMIC_RELEASE);
    }

    void RecordSample(Site& site, const std::uint64_t a, const std::uint64_t b, const std::uint64_t result) noexcept
    {
      const std::uint32_t count = site.sampleCount.load(__ATOMIC_ACQUIRE);
      if (count >= kSamples) {
        return;
      }
      for (std::uint32_t i = 0; i < count; ++i) {
        const Sample& sample = site.samples[i];
        if (sample.ready.load(__ATOMIC_ACQUIRE) != 0 && sample.a.load(__ATOMIC_RELAXED) == a &&
            sample.b.load(__ATOMIC_RELAXED) == b) {
          return;
        }
      }
      // Two threads can both add the same new operand here; harmless for a sample.
      const std::uint32_t slot = site.sampleCount.fetch_add(1, __ATOMIC_ACQ_REL);
      if (slot < kSamples) {
        StoreSample(site.samples[slot], a, b, result);
      }
    }

    // ln(x) for the distinct-count estimate, without calling the traced `log`.
    [[nodiscard]] double NaturalLog(const double x) noexcept
    {
      int exponent = 0;
      const double m = std::frexp(x, &exponent); // libSystem's: exact, not traced
      const double t = (m - 1.0) / (m + 1.0);    // m in [0.5, 1): |t| <= 1/3
      double term = t;
      double sum = 0.0;
      for (int k = 1; k < 60; k += 2) {
        sum += term / k;
        term *= t * t;
      }
      return 2.0 * sum + exponent * 0.69314718055994530942;
    }

    // Linear counting over the sketch: n ~ -m ln(zero bits / m). Returns -1 when saturated.
    [[nodiscard]] double DistinctEstimate(const Site& site) noexcept
    {
      std::size_t zeros = 0;
      for (const auto& word : site.sketch) {
        zeros += static_cast<std::size_t>(64 - __builtin_popcountll(word.load(__ATOMIC_RELAXED)));
      }
      if (zeros == 0) {
        return -1.0;
      }
      const double m = static_cast<double>(kSketchBits);
      return -m * NaturalLog(static_cast<double>(zeros) / m);
    }

    void FormatOperand(char* out, const std::size_t size, const std::uint64_t bits, const bool isFloat) noexcept
    {
      if (isFloat) {
        std::snprintf(out, size, "0x%08" PRIX32 "(%.9g)", static_cast<std::uint32_t>(bits), OperandValue(bits, true));
      } else {
        std::snprintf(out, size, "0x%016" PRIX64 "(%.17g)", bits, OperandValue(bits, false));
      }
    }

    void FormatAddress(char* out, const std::size_t size, const std::uintptr_t address) noexcept
    {
      if (address == 0) {
        std::snprintf(out, size, "-");
        return;
      }
      Dl_info info{};
      if (dladdr(reinterpret_cast<const void*>(address), &info) == 0 || info.dli_fname == nullptr) {
        std::snprintf(out, size, "0x%" PRIxPTR, address);
        return;
      }
      const char* image = std::strrchr(info.dli_fname, '/');
      image = image != nullptr ? image + 1 : info.dli_fname;
      const auto base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
      int written = std::snprintf(out, size, "0x%" PRIxPTR " %s+0x%" PRIxPTR, address, image, address - base);
      if (written < 0 || static_cast<std::size_t>(written) >= size) {
        return;
      }
      if (info.dli_sname != nullptr && info.dli_saddr != nullptr) {
        // Nearest *exported* symbol: in the main executable that may be far off; use atos.
        int status = 0;
        char* const demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        const char* const name = (status == 0 && demangled != nullptr) ? demangled : info.dli_sname;
        std::snprintf(out + written, size - static_cast<std::size_t>(written), " [%s+0x%" PRIxPTR "]", name,
                      address - reinterpret_cast<std::uintptr_t>(info.dli_saddr));
        std::free(demangled);
      }
    }

    // "[min, max]" of the non-NaN operands, or "none" when every operand was NaN.
    void PrintRange(std::FILE* const out, const char* const label, const Range& range) noexcept
    {
      const double low = RangeMin(range);
      const double high = RangeMax(range);
      if (low > high) {
        std::fprintf(out, "  %s none", label);
      } else {
        std::fprintf(out, "  %s [%.17g, %.17g]", label, low, high);
      }
    }

    void DumpAtExit() noexcept
    {
      faf_crtmathTraceDump();
    }
  } // namespace

  bool TraceInit() noexcept
  {
    const char* const value = std::getenv("FAF_CRTMATH_TRACE");
    const bool on = value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    int expected = kTraceUnknown;
    if (!g_traceState.compare_exchange_strong(expected, on ? kTraceOn : kTraceOff, std::memory_order_acq_rel)) {
      return expected == kTraceOn; // another thread decided first, from the same environment
    }
    if (on) {
      // Fixed now: the engine may change directory before exit.
      const char* file = std::getenv("FAF_CRTMATH_TRACE_FILE");
      if (file == nullptr || file[0] == '\0') {
        file = "faf_crtmath_trace.txt";
      }
      if (file[0] == '/') {
        std::snprintf(g_tracePath, sizeof g_tracePath, "%s", file);
      } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) == nullptr) {
          std::snprintf(cwd, sizeof cwd, ".");
        }
        std::snprintf(g_tracePath, sizeof g_tracePath, "%s/%s", cwd, file);
      }
      std::atexit(DumpAtExit);
      std::fprintf(stderr, "faf_crtmath: tracing on, writing %s at exit\n", g_tracePath);
    }
    return on;
  }

  void TraceRecord(const Fn fn, void* const returnAddress, const std::uint64_t a, const std::uint64_t b,
                   const std::uint64_t result) noexcept
  {
    const auto fnIndex = static_cast<std::size_t>(fn);
    const Kind kind = kFnKinds[fnIndex];
    const bool isFloat = IsFloatKind(kind);
    const bool binary = IsBinaryKind(kind);

    const std::uintptr_t ra0 = StripAddress(reinterpret_cast<std::uintptr_t>(returnAddress));
    const std::uintptr_t ra1 = CallerOfCaller(ra0);
    Site* const site = FindSite(static_cast<std::uint32_t>(fnIndex), ra0, ra1);
    if (site == nullptr) {
      g_dropped[fnIndex].fetch_add(1, __ATOMIC_RELAXED);
      return;
    }
    site->calls.fetch_add(1, __ATOMIC_RELAXED);

    const bool nanA = IsNanBits(a, isFloat);
    const bool nanB = binary && IsNanBits(b, isFloat);
    if (nanA || nanB) {
      site->nanOperand.fetch_add(1, __ATOMIC_RELAXED);
    } else if (IsNanBits(result, isFloat)) {
      if (site->domain.fetch_add(1, __ATOMIC_RELAXED) == 0) {
        StoreSample(site->firstDomain, a, b, result);
      }
    }
    if (!nanA) {
      UpdateMin(site->rangeA, OperandValue(a, isFloat));
      UpdateMax(site->rangeA, OperandValue(a, isFloat));
    }
    if (binary && !nanB) {
      UpdateMin(site->rangeB, OperandValue(b, isFloat));
      UpdateMax(site->rangeB, OperandValue(b, isFloat));
    }

    const std::uint64_t bit = Mix(a ^ Mix(b + 0x9E3779B97F4A7C15ull)) & (kSketchBits - 1);
    auto& word = site->sketch[bit / 64];
    const std::uint64_t mask = std::uint64_t{1} << (bit % 64);
    if ((word.load(__ATOMIC_RELAXED) & mask) == 0) {
      word.fetch_or(mask, __ATOMIC_RELAXED);
    }

    RecordSample(*site, a, b, result);
  }
} // namespace faf_posix::crt::detail

extern "C" bool faf_crtmathTraceEnabled() noexcept
{
  return faf_posix::crt::detail::TraceOn();
}

extern "C" bool faf_crtmathTraceDump() noexcept
{
  using namespace faf_posix::crt::detail;
  if (!TraceOn() || g_tracePath[0] == '\0') {
    return false;
  }
  std::FILE* const out = std::fopen(g_tracePath, "w");
  if (out == nullptr) {
    std::fprintf(stderr, "faf_crtmath: cannot write the trace to %s: %s\n", g_tracePath, std::strerror(errno));
    return false;
  }

  std::vector<const Site*> sites;
  for (const Site& site : g_sites) {
    if (site.state.load(__ATOMIC_ACQUIRE) == kSitePublished) {
      sites.push_back(&site);
    }
  }
  std::sort(sites.begin(), sites.end(), [](const Site* x, const Site* y) {
    if (x->fn != y->fn) {
      return x->fn < y->fn;
    }
    return x->calls.load(__ATOMIC_RELAXED) > y->calls.load(__ATOMIC_RELAXED);
  });

  std::fprintf(out, "# faf_crtmath trace (FAF_CRTMATH_TRACE=1), pid %d\n", static_cast<int>(getpid()));
  std::fprintf(out, "# Main image: %s, load address 0x%" PRIxPTR ", slide 0x%" PRIxPTR "\n", _dyld_get_image_name(0),
               reinterpret_cast<std::uintptr_t>(_dyld_get_image_header(0)),
               static_cast<std::uintptr_t>(_dyld_get_image_vmaddr_slide(0)));
  std::fprintf(out, "# Symbolize: atos -o <image> -l <load address> <address>...  (addresses below are runtime)\n");
  std::fprintf(out, "# ra0 = the entry point's return address (its caller), ra1 = the caller's caller.\n");
  std::fprintf(out, "# domain = NaN result from non-NaN operands (returned as the x86 default NaN).\n");
  std::fprintf(out, "# distinct~ = linear-counting estimate of distinct operands per site (-1: > ~30000).\n\n");

  std::fprintf(out, "## Functions\n# function\tcalls\tsites\tdomain\tnan_operand\tdropped\n");
  for (std::size_t fn = 0; fn < kFnCount; ++fn) {
    std::uint64_t calls = 0;
    std::uint64_t domain = 0;
    std::uint64_t nanOperand = 0;
    std::size_t count = 0;
    for (const Site* site : sites) {
      if (site->fn == fn) {
        calls += site->calls.load(__ATOMIC_RELAXED);
        domain += site->domain.load(__ATOMIC_RELAXED);
        nanOperand += site->nanOperand.load(__ATOMIC_RELAXED);
        ++count;
      }
    }
    const std::uint64_t dropped = g_dropped[fn].load(__ATOMIC_RELAXED);
    std::fprintf(out, "%s\t%" PRIu64 "\t%zu\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n", kFnNames[fn], calls + dropped,
                 count, domain, nanOperand, dropped);
  }

  std::fprintf(out, "\n## Call sites\n");
  char ra0[512];
  char ra1[512];
  char x[96];
  char y[96];
  char r[96];
  for (const Site* site : sites) {
    const Kind kind = kFnKinds[site->fn];
    const bool isFloat = IsFloatKind(kind);
    const bool binary = IsBinaryKind(kind);
    FormatAddress(ra0, sizeof ra0, site->ra0);
    FormatAddress(ra1, sizeof ra1, site->ra1);
    std::fprintf(out, "%s\tcalls %" PRIu64 "\tdistinct~ %.0f\tdomain %" PRIu64 "\tnan_operand %" PRIu64 "\n",
                 kFnNames[site->fn], site->calls.load(__ATOMIC_RELAXED), DistinctEstimate(*site),
                 site->domain.load(__ATOMIC_RELAXED), site->nanOperand.load(__ATOMIC_RELAXED));
    std::fprintf(out, "  ra0 %s\n  ra1 %s\n", ra0, ra1);
    PrintRange(out, "range a", site->rangeA);
    if (binary) {
      PrintRange(out, "  b", site->rangeB);
    }
    std::fprintf(out, "\n");
    const std::uint32_t samples = std::min(site->sampleCount.load(__ATOMIC_ACQUIRE), kSamples);
    for (std::uint32_t i = 0; i < samples; ++i) {
      const Sample& sample = site->samples[i];
      if (sample.ready.load(__ATOMIC_ACQUIRE) == 0) {
        continue;
      }
      FormatOperand(x, sizeof x, sample.a.load(__ATOMIC_RELAXED), isFloat);
      FormatOperand(r, sizeof r, sample.result.load(__ATOMIC_RELAXED), isFloat);
      if (binary) {
        FormatOperand(y, sizeof y, sample.b.load(__ATOMIC_RELAXED), isFloat);
        std::fprintf(out, "  sample %s, %s -> %s\n", x, y, r);
      } else {
        std::fprintf(out, "  sample %s -> %s\n", x, r);
      }
    }
    if (site->firstDomain.ready.load(__ATOMIC_ACQUIRE) != 0) {
      FormatOperand(x, sizeof x, site->firstDomain.a.load(__ATOMIC_RELAXED), isFloat);
      FormatOperand(r, sizeof r, site->firstDomain.result.load(__ATOMIC_RELAXED), isFloat);
      if (binary) {
        FormatOperand(y, sizeof y, site->firstDomain.b.load(__ATOMIC_RELAXED), isFloat);
        std::fprintf(out, "  first domain error %s, %s -> %s\n", x, y, r);
      } else {
        std::fprintf(out, "  first domain error %s -> %s\n", x, r);
      }
    }
  }
  const bool ok = std::fclose(out) == 0;
  std::fprintf(stderr, "faf_crtmath: trace of %zu call sites written to %s\n", sites.size(), g_tracePath);
  return ok;
}
