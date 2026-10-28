#pragma once

// Shared pieces of the x87-core tests: RNG, Ext helpers, the SoftFloat oracle's C entry
// points, a deterministic parallel runner and the pass/fail report.

#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/posix/math/X87Core.h"

extern "C" {
void x87t_sf_set_precision(unsigned bits);
void x87t_sf_add(const faf_posix::x87::Ext* a, const faf_posix::x87::Ext* b, faf_posix::x87::Ext* out);
void x87t_sf_sub(const faf_posix::x87::Ext* a, const faf_posix::x87::Ext* b, faf_posix::x87::Ext* out);
void x87t_sf_mul(const faf_posix::x87::Ext* a, const faf_posix::x87::Ext* b, faf_posix::x87::Ext* out);
void x87t_sf_div(const faf_posix::x87::Ext* a, const faf_posix::x87::Ext* b, faf_posix::x87::Ext* out);
void x87t_sf_sqrt(const faf_posix::x87::Ext* a, faf_posix::x87::Ext* out);
void x87t_sf_from_f32(std::uint32_t bits, faf_posix::x87::Ext* out);
void x87t_sf_from_f64(std::uint64_t bits, faf_posix::x87::Ext* out);
void x87t_sf_from_i32(std::int32_t value, faf_posix::x87::Ext* out);
std::uint32_t x87t_sf_to_f32(const faf_posix::x87::Ext* a);
std::uint64_t x87t_sf_to_f64(const faf_posix::x87::Ext* a);
std::int32_t x87t_sf_to_i32(const faf_posix::x87::Ext* a);
std::int32_t x87t_sf_to_i32_trunc(const faf_posix::x87::Ext* a);
void x87t_sf_round_to_int(const faf_posix::x87::Ext* a, faf_posix::x87::Ext* out);
}

namespace x87t
{
  using namespace faf_posix::x87;

  // splitmix64, the same generator as tools/difffuzz (fuzz.h).
  struct Rng
  {
    std::uint64_t s;

    explicit Rng(const std::uint64_t seed) noexcept : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    std::uint64_t Next() noexcept
    {
      std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
    }
    std::uint32_t U32() noexcept { return static_cast<std::uint32_t>(Next() >> 32); }
    std::uint32_t Below(const std::uint32_t n) noexcept { return static_cast<std::uint32_t>((Next() >> 32) % n); }
    bool Chance(const std::uint32_t percent) noexcept { return Below(100) < percent; }
    double Unit() noexcept { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
    double Range(const double lo, const double hi) noexcept { return lo + (hi - lo) * Unit(); }
    std::int32_t Between(const std::int32_t lo, const std::int32_t hi) noexcept
    {
      return lo + static_cast<std::int32_t>(Below(static_cast<std::uint32_t>(hi - lo + 1)));
    }
  };

  [[nodiscard]] inline std::uint64_t Mix(std::uint64_t a, const std::uint64_t b) noexcept
  {
    Rng r(a ^ (b * 0xD1B54A32D192ED03ull));
    r.Next();
    return r.Next();
  }

  [[nodiscard]] inline std::uint32_t FBits(const float f) noexcept { return std::bit_cast<std::uint32_t>(f); }
  [[nodiscard]] inline float FFromBits(const std::uint32_t u) noexcept { return std::bit_cast<float>(u); }
  [[nodiscard]] inline std::uint64_t DBits(const double d) noexcept { return std::bit_cast<std::uint64_t>(d); }
  [[nodiscard]] inline double DFromBits(const std::uint64_t u) noexcept { return std::bit_cast<double>(u); }

  [[nodiscard]] inline bool Same(const Ext a, const Ext b) noexcept
  {
    return a.signif == b.signif && a.signExp == b.signExp;
  }

  [[nodiscard]] inline std::string Hex(const Ext a)
  {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04X:%016" PRIX64, a.signExp, a.signif);
    return buf;
  }

  [[nodiscard]] inline unsigned SfBits(const Precision p) noexcept
  {
    return p == Precision::P24 ? 32u : p == Precision::P53 ? 64u : 80u;
  }

  [[nodiscard]] inline const char* PName(const Precision p) noexcept
  {
    return p == Precision::P24 ? "P24" : p == Precision::P53 ? "P53" : "P64";
  }

  // --- Report ------------------------------------------------------------------------------

  struct Report
  {
    std::mutex lock;
    int failures = 0;
    std::vector<std::string> lines;
  };

  Report& GlobalReport();

  // Prints a mismatch line (at most `limit` per test) under the report lock.
  class MismatchLog
  {
  public:
    explicit MismatchLog(const char* test, const int limit = 8) : test_(test), limit_(limit) {}

    void Print(const char* fmt, ...) __attribute__((format(printf, 2, 3)))
    {
      std::lock_guard<std::mutex> guard(GlobalReport().lock);
      if (printed_ >= limit_) {
        return;
      }
      ++printed_;
      std::printf("  MISMATCH %s: ", test_);
      va_list args;
      va_start(args, fmt);
      std::vprintf(fmt, args);
      va_end(args);
      std::printf("\n");
      std::fflush(stdout);
    }

  private:
    const char* test_;
    int limit_;
    int printed_ = 0;
  };

  // Records one test's totals; a test passes with zero mismatches.
  void Record(const std::string& name, std::uint64_t inputs, std::uint64_t mismatches, const std::string& note = {});

  // Runs body(chunkIndex, begin, end) over [0, total) in chunks of 2^20 on every core.
  // Each chunk seeds its own RNG from its index, so results do not depend on the thread
  // count. Returns the summed mismatch counts.
  std::uint64_t ParallelChunks(std::uint64_t total, const std::function<std::uint64_t(std::uint64_t, std::uint64_t, std::uint64_t)>& body);

  // Test scale: --quick divides the iteration counts by 100.
  std::uint64_t Iters(std::uint64_t full);
  bool QuickMode();

  // Suites.
  void RunLayer1Ops();
  void RunConversions();
  void RunExhaustive();
  void RunLayer2Helpers();
  void RunRoutines();
  void RunRoutineOracle();
  void RunKnownAnswers();
  void RunBench();
  void RunIntegration();
} // namespace x87t
