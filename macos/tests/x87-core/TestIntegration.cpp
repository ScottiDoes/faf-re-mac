// With the real kernels (target x87core_tests_kernels, which links
// src/platform/posix/math/X87Kernels.cpp instead of KernelStandIn.cpp): the known answers
// of DETERMINISM.md §6.1.4 that need a transcendental, the out-of-range trig behaviour,
// and the acos/asin fast paths against their transcriptions on the real fpatan.

#include <cmath>
#include <limits>

#include "EmuApi.h"
#include "Gen.h"

namespace x87t
{
  namespace emu = faf_posix::x87::emu;

  void RunIntegration()
  {
#if defined(X87T_REAL_KERNELS)
    int checks = 0;
    int failures = 0;
    auto check = [&](const std::uint32_t got, const std::uint32_t want, const char* what) {
      ++checks;
      if (got != want) {
        ++failures;
        std::printf("  KAT FAIL %s: got %08X want %08X\n", what, got, want);
      }
    };

    // legacy/math/X87Math.h: the x87 asin lane at PC24 (callers round the double to float).
    check(FBits(static_cast<float>(msvc8_x87_asin(static_cast<double>(FFromBits(0x3F052EAAu))))), 0x3F0C1118u, "asin(0x3F052EAA) -> 0x3F0C1118");
    // pow(0x41AB54C2, 2) -> 0x43E554AF where the exact square rounds to 0x43E554AE.
    check(FBits(static_cast<float>(msvc8_x87_pow(static_cast<double>(FFromBits(0x41AB54C2u)), 2.0))), 0x43E554AFu, "pow(0x41AB54C2, 2) -> 0x43E554AF");
    // |x| >= 2^63: fsin/fcos leave x (C2 = 1); fptan pushes nothing and the store reads an
    // empty register.
    check(FBits(msvc8_x87_sinf(FFromBits(0x5F000000u))), 0x5F000000u, "sinf(2^63) -> 2^63");
    check(FBits(msvc8_x87_cosf(FFromBits(0xDF800000u))), 0xDF800000u, "cosf(-2^64) -> -2^64");
    check(FBits(msvc8_x87_tanf(FFromBits(0x5F000000u))), 0xFFC00000u, "tanf(2^63) -> indefinite");
    check(FBits(msvc8_x87_sinf(FFromBits(0x7F800000u))), 0xFFC00000u, "sinf(+inf) -> indefinite");
    check(FBits(msvc8_x87_tanf(FFromBits(0xFF800000u))), 0xFFC00000u, "tanf(-inf) -> indefinite");
    check(FBits(msvc8_x87_sinf(FFromBits(0x7FC12345u))), 0x7FC12345u, "sinf(NaN) keeps the payload");
    check(FBits(msvc8_x87_atan2f(0.0f, -1.0f)), FBits(3.14159274f), "atan2f(0, -1) -> pi");
    check(FBits(msvc8_x87_intrinsic_ln(1.0f)), 0x00000000u, "ln(1) -> +0");
    check(FBits(msvc8_x87_intrinsic_exp(0.0f)), 0x3F800000u, "exp(0) -> 1");
    check(static_cast<std::uint32_t>(FBits(static_cast<float>(msvc8_x87_pow(2.0, 10.0)))), FBits(1024.0f), "pow(2, 10) -> 1024");
    Record("integration known answers (real kernels)", static_cast<std::uint64_t>(checks), static_cast<std::uint64_t>(failures));

    // The acos / asin fast paths share fpatan with the transcription; with the real kernel
    // they must still agree everywhere.
    for (int which = 0; which < 2; ++which) {
      const char* name = which == 0 ? "acos fast vs transcription (real kernels)" : "asin fast vs transcription (real kernels)";
      MismatchLog log(name);
      const std::uint64_t n = Iters(20'000'000);
      const std::uint64_t bad = ParallelChunks(n, [&](const std::uint64_t chunk, const std::uint64_t begin, const std::uint64_t end) {
        Rng r(Mix(0x5EED2007C0DE0009ull + static_cast<std::uint64_t>(which), chunk));
        std::uint64_t mismatches = 0;
        for (std::uint64_t i = begin; i < end; ++i) {
          double x = r.Chance(80) ? r.Range(-1.0, 1.0) : static_cast<double>(difffuzz::GenFloat(r));
          if (r.Chance(10)) {
            x = (r.Chance(50) ? 1.0 : -1.0) * (1.0 - std::ldexp(r.Unit(), -static_cast<int>(r.Below(54))));
          }
          const double a = which == 0 ? emu::fast::Acos(x) : emu::fast::Asin(x);
          const double b = which == 0 ? emu::ref::Acos(x) : emu::ref::Asin(x);
          if (DBits(a) != DBits(b)) {
            ++mismatches;
            log.Print("x %016" PRIX64 " fast %016" PRIX64 " ref %016" PRIX64, DBits(x), DBits(a), DBits(b));
          }
        }
        return mismatches;
      });
      Record(name, n, bad);
    }
#else
    std::printf("integration: skipped (this binary links the test stand-in kernels; run x87core_tests_kernels)\n");
#endif
  }
} // namespace x87t
