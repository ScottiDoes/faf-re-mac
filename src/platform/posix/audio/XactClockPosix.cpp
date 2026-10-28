// POSIX build: replaces moho/audio/XactClock.cpp (macos/exclusions.d/audio-movie.cmake).
//
// On Windows, SND_InstallXactClock rewrites xactengine2_9.dll's KERNEL32 imports
// (QueryPerformanceCounter, GetTickCount) so XACT times itself from process start: XACT
// 2.9 compares its millisecond clock as a signed 32-bit value and stops updating cues,
// streams and notifications between 2^31 and 2^32 ms of Windows uptime (24.86 to 49.7
// days), the "sound partly disappears after 24.8 days" bug (XactClock.h).
//
// The POSIX engine is FAudio's FACT (platform/posix/audio/FactXact.cpp). FACT's clock is
// FAudio_timems(), SDL_GetTicks(): milliseconds since SDL started in this process, not
// since the machine booted. So FACT already has what the patch gives XACT, a clock that
// starts with the game, and there is no DLL import table to patch. `/realtickcount` and
// FAF_TEST_TICK_OFFSET_DAYS (the patch's test switches) have nothing to act on.

#include <cstdlib>

#include "gpg/core/utils/Logging.h"
#include "moho/audio/XactClock.h"
#include "moho/misc/StartupHelpers.h"

namespace moho
{
  void SND_InstallXactClock(const IXACTEngine& engine)
  {
    (void)engine;
    static bool sReported = false;
    if (sReported) {
      return;
    }
    sReported = true;
    const char* const testDays = std::getenv("FAF_TEST_TICK_OFFSET_DAYS");
    if (CFG_GetArgOption("/realtickcount", 0, nullptr) || (testDays != nullptr && *testDays != '\0')) {
      gpg::Logf("SND: /realtickcount and FAF_TEST_TICK_OFFSET_DAYS do nothing here: FACT's clock counts from process start");
    }
  }
} // namespace moho
