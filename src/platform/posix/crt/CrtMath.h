#pragma once

// faf_crtmath: the C math entry points the engine, Wild Magic and Lua reference, defined in
// the executable (macos/docs/DETERMINISM.md §4.4). ld64 binds every reference from the
// engine's objects and static archives to a definition in an object file before it looks at
// libSystem, so `sin`, `pow`, `atan2f`, ... below are the ones the engine calls. Frameworks
// and libc++.dylib keep their own two-level bindings to libSystem.
//
// What the Windows x64 reference build links instead is the static UCRT 10.0.22000 (FMA3
// paths). The bodies here are, for now:
//   - Apple libm, reached through a pointer resolved once with dlsym(RTLD_NEXT, name), which
//     never resolves to these definitions (checked at resolution and by the self-test);
//   - plus the UCRT's NaN results where Apple's differ: a domain error (NaN from non-NaN
//     operands, e.g. asin(2), log(-1), sin(inf), pow(-2, 0.5), fmod(1, 0), sqrt(-1)) gives
//     the x86 default NaN 0xFFF8000000000000 / 0xFFC00000 ("-nan(ind)"), where Apple gives
//     0x7FF8000000000000 / 0x7FC00000; a NaN operand comes back quieted with its sign and
//     payload (the first NaN operand of two);
//   - the nearest float for Annex F's exact +-pi and +-pi/2 results of atan2f/atanf/asinf/
//     acosf, which Apple rounds toward zero (CrtMath.cpp, NearestPiMultiple).
//   Every other Annex F special value (pow(x, 0) = 1, log(0) = -inf, atan2 of signed zeros
//   and infinities, ...) is Apple's, which follows Annex F as the UCRT does.
// FAF_POSIX_TODO(determinism): finite results are Apple's, which differ from UCRT 22000's in
// the last bit on some inputs. The functions the trace (below) shows the sim reaching get
// exact UCRT 22000 clones (DETERMINISM.md §4.3 A/A').
//
// Trace mode: FAF_CRTMATH_TRACE=1 in the environment (read once, on the first call of any
// entry point) counts calls per (function, return address, caller's return address) and
// samples distinct operands. The table goes to FAF_CRTMATH_TRACE_FILE, default
// ./faf_crtmath_trace.txt (current directory), at exit and on faf_crtmathTraceDump().
// With the variable unset each entry point pays one relaxed load and a predicted branch.

#include <cstddef>
#include <cstdint>

namespace faf_posix::crt
{
  // The x86 default NaN ("real indefinite", -nan(ind)): what SSE produces for an invalid
  // operation and what the UCRT returns for a domain error.
  inline constexpr std::uint64_t kX86DefaultNan64 = 0xFFF8000000000000ull;
  inline constexpr std::uint32_t kX86DefaultNan32 = 0xFFC00000u;
} // namespace faf_posix::crt

extern "C" {
  // True when FAF_CRTMATH_TRACE=1 (reads the environment on first use, then never again).
  bool faf_crtmathTraceEnabled() noexcept;

  // Writes the trace file now (again at exit). Returns false when tracing is off or the file
  // cannot be written (the reason goes to stderr).
  bool faf_crtmathTraceDump() noexcept;

  // Resolves every entry point's Apple libm implementation and checks that none of them is
  // the entry point itself (no recursion). Returns the number of bad bindings; when non-zero
  // and `firstBad` is not null, copies the first bad function's name there.
  int faf_crtmathCheckBindings(char* firstBad, std::size_t firstBadSize) noexcept;

  // Number of entry points and their names (for tests and the symbol check).
  std::size_t faf_crtmathFunctionCount() noexcept;
  const char* faf_crtmathFunctionName(std::size_t index) noexcept;
}
