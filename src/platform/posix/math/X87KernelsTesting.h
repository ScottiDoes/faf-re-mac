#pragma once

// Test hooks of X87Kernels.cpp (macos/tests/x87-kernels). They exist only when the file is
// compiled with FAF_X87_KERNELS_TESTING; the engine build never defines it, and the hooks
// never change a result in Mode::Normal.

#if defined(FAF_X87_KERNELS_TESTING)

#include <cstdint>

namespace faf_posix::x87::kernels_testing
{
  enum class Mode : int
  {
    Normal = 0,    // production behaviour
    ForceSlow = 1, // every general case takes the 256-bit slow path
    FastOnly = 2,  // never take the slow path; an undecided fast result is rounded anyway
  };

  // The fast path's last 128-bit approximation (before the final rounding): value
  // (-1)^neg * (hi:lo) * 2^(e - 127), |true - (hi:lo)| <= err units of the last bit.
  struct FastApprox
  {
    bool valid;
    bool neg;
    int e;
    std::uint64_t hi;
    std::uint64_t lo;
    std::uint64_t err;
  };

  void SetMode(Mode mode) noexcept;
  FastApprox LastFast() noexcept;     // thread-local; valid only after a general-case call
  void ClearLastFast() noexcept;
  std::uint64_t FastUndecided() noexcept; // fast path could not decide (slow path taken)
  std::uint64_t SlowUndecided() noexcept; // slow path could not decide either (near-tie)
  void ResetCounters() noexcept;
} // namespace faf_posix::x87::kernels_testing

#endif
