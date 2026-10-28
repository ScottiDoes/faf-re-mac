// MSVC's rand/srand, defined in the executable so ld64 binds the engine's references here
// instead of to libSystem's different generator (macos/docs/DETERMINISM.md §2.2).
//
// Lua's math.random/math.randomseed (lua/LuaObject.cpp math_random, math_randomseed) feed
// `std::rand() % 0x7FFF` into the simulation, so the sequence must be the Windows one: the
// msvcr80 / UCRT linear congruential generator, one state per thread starting at 1 (UCRT
// keeps it in the per-thread data, _rand_state; a thread that never calls srand starts from
// 1). First values from state 1: 41, 18467, 6334, 26500, 19169.
//
// RAND_MAX: on Windows it is 0x7FFF; macOS's <stdlib.h> says 0x7FFFFFFF. No engine source
// uses RAND_MAX (math_random hard-codes % 0x7FFF; checked 27/10/2026). Wild Magic's
// Math<Real>::UnitRandom/SymmetricRandom/IntervalRandom divide by it, but nothing the
// engine links reaches them (macos/porting/crt.md). compat/Math.h makes RAND_MAX 0x7FFF for
// engine translation units anyway, so any future user agrees with this rand.

#include <cstdint>
#include <cstdlib>

namespace
{
  // Constant-initialized: no TLS initializer, state 1 on every new thread.
  thread_local std::uint32_t t_holdrand = 1;
} // namespace

extern "C" void srand(unsigned int seed)
{
  t_holdrand = seed;
}

extern "C" int rand(void)
{
  t_holdrand = t_holdrand * 214013u + 2531011u;
  return static_cast<int>((t_holdrand >> 16) & 0x7FFFu);
}
