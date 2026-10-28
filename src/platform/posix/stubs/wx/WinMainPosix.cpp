// POSIX counterpart of the console commands moho/app/WinMain.cpp registers (WinMain.cpp is
// excluded from the macOS build, see macos/exclusions.d/wx.cmake).
//
// WinMain itself has no counterpart here: the headless runner provides main(). The three
// commands keep their names and descriptions so the console lists the same set; the
// allocation log (gpg::SetMemHook plus DbgHelp symbols) and the crash tests are Windows
// diagnostics and only log that they are unavailable.

#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"
#include "moho/console/CConFunc.h"

namespace
{
  void SC_StartMemoryLog(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_POSIX_STUB("SC_StartMemoryLog: the allocation log is not available on POSIX");
  }

  void SC_StopMemoryLog(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_POSIX_STUB("SC_StopMemoryLog: the allocation log is not available on POSIX");
  }

  void SC_FafCrashTest(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_POSIX_STUB("faf_crashtest: the crash-report tests are not available on POSIX");
  }

  // WinMain.cpp's descriptions (0x00E4F23C, 0x00E4F274) and its faf_crashtest addition.
  moho::CConFunc gCConFunc_SC_StartMemoryLog("SC_StartMemoryLog", "Start up memory logging to filename", &SC_StartMemoryLog);
  moho::CConFunc gCConFunc_SC_StopMemoryLog("SC_StopMemoryLog", "Stop memory logging", &SC_StopMemoryLog);
  moho::CConFunc gCConFunc_FafCrashTest(
    "faf_crashtest", "Crash the game on purpose to test the crash report", &SC_FafCrashTest
  );
} // namespace
