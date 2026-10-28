// x87-core tests: Layer 1 (X87Core.h) against Berkeley SoftFloat 3e, Layer 2 against
// Layer 1, the msvc8_x87_* fast paths against their asm transcriptions, known answers,
// and timings. Notes and results: macos/porting/x87-core.md.
//
//   x87core_tests [--quick] [--threads N] [suite ...]
//   suites: ops conv exhaustive helpers routines oracle kat integration bench
//   (default: all but bench; integration needs x87core_tests_kernels, the real kernels)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "TestCommon.h"

namespace x87t
{
  namespace
  {
    bool gQuick = false;
    unsigned gThreads = 0;
  } // namespace

  Report& GlobalReport()
  {
    static Report report;
    return report;
  }

  void Record(const std::string& name, const std::uint64_t inputs, const std::uint64_t mismatches, const std::string& note)
  {
    Report& report = GlobalReport();
    std::lock_guard<std::mutex> guard(report.lock);
    char line[512];
    std::snprintf(
      line, sizeof line, "%-4s %-58s %14" PRIu64 " inputs %10" PRIu64 " mismatches%s%s", mismatches == 0 ? "ok" : "FAIL",
      name.c_str(), inputs, mismatches, note.empty() ? "" : "  ", note.c_str()
    );
    std::printf("%s\n", line);
    std::fflush(stdout);
    report.lines.push_back(line);
    if (mismatches != 0) {
      ++report.failures;
    }
  }

  std::uint64_t ParallelChunks(
    const std::uint64_t total, const std::function<std::uint64_t(std::uint64_t, std::uint64_t, std::uint64_t)>& body
  )
  {
    constexpr std::uint64_t kChunk = 1ull << 20;
    const std::uint64_t chunks = (total + kChunk - 1) / kChunk;
    std::atomic<std::uint64_t> next{0};
    std::atomic<std::uint64_t> mismatches{0};
    const unsigned threads = gThreads != 0 ? gThreads : std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
      pool.emplace_back([&] {
        for (;;) {
          const std::uint64_t chunk = next.fetch_add(1);
          if (chunk >= chunks) {
            return;
          }
          const std::uint64_t begin = chunk * kChunk;
          const std::uint64_t end = std::min(total, begin + kChunk);
          mismatches.fetch_add(body(chunk, begin, end));
        }
      });
    }
    for (std::thread& thread : pool) {
      thread.join();
    }
    return mismatches.load();
  }

  bool QuickMode()
  {
    return gQuick;
  }

  std::uint64_t Iters(const std::uint64_t full)
  {
    return gQuick ? std::max<std::uint64_t>(full / 100, 1) : full;
  }
} // namespace x87t

int main(int argc, char** argv)
{
  using namespace x87t;
  std::vector<std::string> suites;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) {
      gQuick = true;
    } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      gThreads = static_cast<unsigned>(std::atoi(argv[++i]));
    } else {
      suites.emplace_back(argv[i]);
    }
  }
  if (suites.empty()) {
    suites = {"kat", "integration", "ops", "conv", "exhaustive", "helpers", "routines", "oracle"};
  }

  const auto start = std::chrono::steady_clock::now();
  for (const std::string& suite : suites) {
    std::printf("== %s\n", suite.c_str());
    std::fflush(stdout);
    if (suite == "ops") {
      RunLayer1Ops();
    } else if (suite == "conv") {
      RunConversions();
    } else if (suite == "exhaustive") {
      RunExhaustive();
    } else if (suite == "helpers") {
      RunLayer2Helpers();
    } else if (suite == "routines") {
      RunRoutines();
    } else if (suite == "oracle") {
      RunRoutineOracle();
    } else if (suite == "kat") {
      RunKnownAnswers();
    } else if (suite == "integration") {
      RunIntegration();
    } else if (suite == "bench") {
      RunBench();
    } else {
      std::printf("unknown suite %s\n", suite.c_str());
      return 2;
    }
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("== %d failing test(s), %.1f s\n", GlobalReport().failures, seconds);
  return GlobalReport().failures == 0 ? 0 : 1;
}
