// faf_game: the game on macOS (milestone 2).
//
// main() takes the place of WinMain (moho/app/WinMain.cpp, replaced in the macOS build by
// stubs/wx/WinMainPosix.cpp, which keeps its console commands). It does what WinMain does,
// in WinMain's order: names the thread, installs the CRT diagnostic hooks and the probe/watch
// handler, handles the start-up switches, installs the die handler, runs CScApp through
// moho::WIN_AppExecute and returns the application's exit value. The command line was
// published to the engine (__argc/__argv) by startup/StaticInitPreRegister.cpp before any
// initializer ran, so CFG_GetArgOption works here as it does in WinMain.
//
// POSIX differences:
//  - The window system (faf_posix::shell, SDL3) starts first, on the main thread, before
//    wxEntry and CScApp::Init create any window, and is shut down after the app.
//  - The platform self-tests run before anything else, as in the headless runner: a failed
//    check means this build's sim would not be bit-identical to Windows (multiplayer and
//    replays would desync), so the game does not start (/skipselftest overrides).
//  - Windows-only start-up pieces have no counterpart and are logged when asked for:
//    /crashtest (SEH crash paths), /alloclog (DbgHelp symbols), /waitfordebugger (message
//    box), /mediacenter (Windows Media Center). The accessibility-key changes (sticky,
//    toggle and filter keys' Windows hotkeys) are not made: macOS has no such hotkeys
//    for a game to suppress, and this program does not change system settings.
//  - The process ends with std::_Exit after the log is flushed and the windows are closed,
//    as the headless runner does: the engine's own state (preferences, logs, session) is
//    closed by CScApp::Destroy and WIN_AppExecute, and ~950 translation units' static
//    destructors have teardown-order problems MSVC's CRT tolerates (CDiskWatch's mutex is
//    fixed; FAF_GAME_STATIC_EXIT=1 runs exit() instead, to find the others).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>

#include "gpg/core/diagnostics/CrashHooks.h"
#include "gpg/core/diagnostics/CrashReport.h"
#include "gpg/core/diagnostics/ProbeWatch.h"
#include "gpg/core/time/Timer.h"
#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Error.hpp"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"
#include "moho/app/CScApp.h"
#include "moho/app/WinApp.h"
#include "moho/misc/StartupHelpers.h"
#include "platform/posix/crt/SelfTest.h"
#include "platform/posix/shell/Shell.h"

namespace
{
  [[nodiscard]] bool HasSwitch(const char* const option)
  {
    return moho::CFG_GetArgOption(option, 0, nullptr);
  }

  [[nodiscard]] bool GetArg(const char* const option, msvc8::string* const outValue)
  {
    msvc8::vector<msvc8::string> args;
    if (!moho::CFG_GetArgOption(option, 1, &args) || args.empty()) {
      return false;
    }
    if (outValue != nullptr) {
      *outValue = args[0];
    }
    return true;
  }

  // Ends the process: windows closed, every stream flushed, no static destructors (see the
  // file comment).
  [[noreturn]] void LeaveProcess(const int exitCode)
  {
    faf_posix::shell::Shutdown();
    std::fflush(nullptr);
    if (const char* const staticExit = std::getenv("FAF_GAME_STATIC_EXIT");
        staticExit != nullptr && staticExit[0] == '1') {
      std::exit(exitCode);
    }
    std::_Exit(exitCode);
  }

  // WinMain's FatalErrorDieHandler (0x004F1500): the crash report, then the dialog. On POSIX
  // HandleFatalPath prints the report's path to stderr in place of the dialog; the message
  // goes to stderr and the log too. Windows ends the process at gpg::Die's __debugbreak
  // (the SEH filter); here it ends straight after the report.
  void GameDieHandler(const char* const message)
  {
    const char* const text = (message != nullptr) ? message : "engine Die()";
    std::fprintf(stderr, "faf_game: fatal: %s\n", text);
    gpg::Logf("faf_game: fatal: %s", text);
    gpg::diag::CrashReportContext reportContext{};
    reportContext.reason = text;
    gpg::diag::HandleFatalPath(reportContext);
    LeaveProcess(1);
  }

  // WinMain's /w and /p arming (FADeepProbe-compatible); the handler stays dormant until
  // something is armed.
  void ArmProbesFromCommandLine()
  {
    gpg::diag::InstallProbeWatchHandler();

    msvc8::string watchSpec;
    if (GetArg("/w", &watchSpec)) {
      (void)gpg::diag::ParseWatchSpec(watchSpec.c_str());
    }

    msvc8::string probeSpec;
    if (GetArg("/p", &probeSpec)) {
      const char* p = probeSpec.c_str();
      while (*p != '\0') {
        char one[128] = {};
        const char* const comma = std::strchr(p, ',');
        const std::size_t length = (comma != nullptr) ? static_cast<std::size_t>(comma - p) : std::strlen(p);
        if (length == 0 || length >= sizeof(one)) {
          break;
        }
        std::memcpy(one, p, length);
        (void)gpg::diag::ParseProbeSpec(one);
        if (comma == nullptr) {
          break;
        }
        p = comma + 1;
      }
    }
  }
} // namespace

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  gpg::SetThreadName(0xFFFFFFFFu, "Main");

  // The window system first: SDL's video must start on the main thread before wxEntry and
  // CScApp::Init create windows.
  if (!faf_posix::shell::Init()) {
    std::fprintf(stderr, "faf_game: the window system did not start (see the messages above)\n");
    LeaveProcess(1);
  }

  if (!faf_posixRunSelfTests(false)) {
    if (!HasSwitch("/skipselftest")) {
      std::fprintf(
        stderr, "faf_game: platform self-tests failed; the sim would not match Windows (/skipselftest overrides)\n"
      );
      LeaveProcess(1);
    }
    std::fprintf(stderr, "faf_game: platform self-tests failed; continuing because of /skipselftest\n");
  }

  gpg::time::Timer runTimer{};

  gpg::diag::CrtDiagOptions diagOptions{};
  diagOptions.enableHeapCheck = HasSwitch("/heapcheck");
  gpg::diag::InstallCrtDiagnosticHooks(diagOptions);

  if (HasSwitch("/crashtest")) {
    FAF_POSIX_STUB("/crashtest: the crash-report tests are Windows-only (SEH crash paths)");
  }

  ArmProbesFromCommandLine();

  if (HasSwitch("/waitfordebugger")) {
    FAF_POSIX_STUB("/waitfordebugger: no message box on POSIX; attach before the window opens");
  }

  if (HasSwitch("/aqtime")) {
    moho::APP_SetAqtimeInstrumentationMode(0);
  }

  if (HasSwitch("/alloclog")) {
    FAF_POSIX_STUB("/alloclog: the allocation log is not available on POSIX");
  }

  (void)gpg::SetDieHandler(&GameDieHandler);

  if (HasSwitch("/singleproc")) {
    DWORD_PTR processAffinityMask = 0;
    DWORD_PTR systemAffinityMask = 0;
    (void)::GetProcessAffinityMask(::GetCurrentProcess(), &processAffinityMask, &systemAffinityMask);
    DWORD_PTR selectedMask = 1;
    if (processAffinityMask != 0) {
      unsigned bitIndex = 0;
      while (bitIndex + 1 < sizeof(DWORD_PTR) * 8U && ((processAffinityMask >> bitIndex) & 1U) == 0U) {
        ++bitIndex;
      }
      selectedMask = static_cast<DWORD_PTR>(1ULL << bitIndex);
    }
    (void)::SetProcessAffinityMask(::GetCurrentProcess(), selectedMask);
  }

  if (HasSwitch("/purgecache")) {
    moho::USER_PurgeAppCacheDir();
  }

  int exitCode = 0;
  try {
    {
      CScApp app;
      moho::WIN_AppExecute(&app);
      exitCode = app.GetExitValue();
      app.framerates.Reset();
    }

    const int totalSeconds = static_cast<int>(runTimer.ElapsedSeconds());
    gpg::Logf("Run time: %dh%02dm%02ds", totalSeconds / 3600, (totalSeconds % 3600) / 60, totalSeconds % 60);
  } catch (const gpg::gal::Error& galError) {
    std::ostringstream formatted;
    formatted << "file : " << galError.GetFile() << "(" << galError.GetLine() << ")\n";
    formatted << "error: " << galError.what();
    gpg::Die("GAL Exception: %s", formatted.str().c_str());
  } catch (const std::exception& ex) {
    gpg::Die("Unhandled exception:\n\n%s", ex.what());
  }

  if (HasSwitch("/mediacenter")) {
    FAF_POSIX_STUB("/mediacenter: Windows Media Center does not exist on macOS");
  }
  LeaveProcess(exitCode);
}
