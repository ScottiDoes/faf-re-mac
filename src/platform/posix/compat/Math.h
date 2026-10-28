#pragma once

// Win32 on POSIX: Math. Included only by ../WinCompat.h (see its rules), last.
//
// The lrint family with Windows' 32-bit `long` (macos/docs/LP64_AUDIT.md LP64-01). The sim
// rounds positions to cells and times to ticks with std::lrintf. On Windows (msvcr80 and the
// x64 reference build alike) a NaN or a value outside int32 gives the integer indefinite
// 0x80000000; macOS's 64-bit lrintf returns the exact value, or 0 for NaN. In range, both
// round in the current mode (nearest) and agree.
//
// The engine spells these std::lrintf / std::lrint, so the shim renames them: <cmath> is
// included here first, before the macros exist, and its include guards keep later
// includes from redeclaring anything under the new names. lround is UI-only and untouched.

#include <cmath>
#include <cstdint>

inline long faf_posix_lrint(double x) noexcept
{
  if (!(x >= -2147483648.0 && x < 2147483648.0)) {
    return -2147483647L - 1;  // integer indefinite, NaN included
  }
  return static_cast<long>(static_cast<std::int32_t>(::lrint(x)));
}

inline long faf_posix_lrintf(float x) noexcept
{
  if (!(x >= -2147483648.0f && x < 2147483648.0f)) {
    return -2147483647L - 1;
  }
  return static_cast<long>(static_cast<std::int32_t>(::lrintf(x)));
}

// std::lrint(float) is the float overload, as in <cmath>.
inline long faf_posix_lrint(float x) noexcept { return faf_posix_lrintf(x); }

namespace std {
  using ::faf_posix_lrint;
  using ::faf_posix_lrintf;
}

#define lrint faf_posix_lrint
#define lrintf faf_posix_lrintf

// RAND_MAX as on Windows. The executable defines MSVC's rand (crt/MsvcRand.cpp), whose
// results are 0..0x7FFF; macOS's <stdlib.h> says 0x7FFFFFFF. No engine source uses RAND_MAX
// today (Lua's math.random hard-codes % 0x7FFF), so this changes no result; it keeps any
// future `rand() / RAND_MAX` in the engine on the Windows scale. <cstdlib> first, so its
// include guard keeps later includes from redefining it.
#include <cstdlib>
#undef RAND_MAX
#define RAND_MAX 0x7FFF
