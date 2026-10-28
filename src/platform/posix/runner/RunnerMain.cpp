// faf_replay: the headless native replay runner (milestone 1).
//
// main() takes the place of WinMain (moho/app/WinMain.cpp, not in the macOS build): it names
// the thread, installs the die handler, runs the application through moho::WIN_AppExecute and
// returns its result. Before that it runs the platform self-tests and refuses to start a
// sim that would not be bit-identical to Windows. The command line was published to the
// engine (__argc/__argv) by startup/StaticInitPreRegister.cpp before any initializer ran, so
// CFG_GetArgOption works here as it does in WinMain.
//
// Output: one line on stdout,
//   FAF_REPLAY_RESULT replay=<id> beats=<last beat> mismatches=<count> exit=<reason>
// and exit status 0 (replay played to its end, no mismatch), 1 (mismatches) or 2 (the run did
// not complete). Switches and an example: macos/porting/runner.md.

#include "platform/posix/runner/Runner.h"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"
#include "moho/app/WinApp.h"
#include "moho/misc/StartupHelpers.h"

#if __has_include("platform/posix/crt/SelfTest.h")
#include "platform/posix/crt/SelfTest.h"
#define FAF_RUNNER_HAVE_SELFTEST 1
#else
#define FAF_RUNNER_HAVE_SELFTEST 0
#endif

namespace
{
  faf_runner::RunnerOptions gOptions;
  std::atomic<bool> gInterruptRequested{false};

  // The crash handlers' own stack, so a stack overflow on the main thread still reports.
  alignas(16) char gAltStack[64 * 1024];

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

  [[nodiscard]] bool HasSwitch(const char* const option)
  {
    return moho::CFG_GetArgOption(option, 0, nullptr);
  }

  [[nodiscard]] double ParseSeconds(const msvc8::string& text, const double fallback)
  {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return (end != text.c_str() && value >= 0.0) ? value : fallback;
  }

  // /replayid <id>, else the replay file's name without its extension.
  [[nodiscard]] std::string ResolveReplayId(const msvc8::string& replayPath)
  {
    msvc8::string replayId;
    if (GetArg("/replayid", &replayId) && !replayId.empty()) {
      return replayId.c_str();
    }
    std::string path = replayPath.c_str();
    std::replace(path.begin(), path.end(), '\\', '/');
    const std::string stem = std::filesystem::path(path).stem().string();
    return stem.empty() ? std::string("unknown") : stem;
  }

  void ReadOptions()
  {
    msvc8::string value;
    gOptions.noUi = HasSwitch("/noui");
    if (GetArg("/stalltimeout", &value)) {
      gOptions.stallSeconds = ParseSeconds(value, gOptions.stallSeconds);
    }
    if (GetArg("/timeout", &value)) {
      gOptions.timeoutSeconds = ParseSeconds(value, gOptions.timeoutSeconds);
    }
    if (GetArg("/benchspeed", &value)) {
      gOptions.benchSpeed = std::atoi(value.c_str());
      gOptions.haveBenchSpeed = true;
    }
  }

  // gpg::Die (WinMain installs FatalErrorDieHandler, which writes the crash report and shows
  // the dialog): report the message and the result, then end the process with status 3.
  void RunnerDieHandler(const char* const message)
  {
    std::fprintf(stderr, "faf_replay: fatal: %s\n", message != nullptr ? message : "engine Die()");
    std::fflush(stderr);
    faf_runner::ReportSetExitReason(faf_runner::kExitDie);
    (void)faf_runner::ReportFinish(faf_runner::kExitDie, false);
    std::_Exit(3);
  }

  void OnInterruptSignal(const int signalNumber)
  {
    (void)signalNumber;
    gInterruptRequested.store(true);
  }

  void OnCrashSignal(const int signalNumber)
  {
    // SA_RESETHAND restored the default action: report, then let the signal end the process.
    faf_runner::ReportFinishFromSignal(signalNumber);
    (void)::raise(signalNumber);
  }

  void InstallSignalHandlers()
  {
    stack_t altStack{};
    altStack.ss_sp = gAltStack;
    altStack.ss_size = sizeof(gAltStack);
    altStack.ss_flags = 0;
    (void)::sigaltstack(&altStack, nullptr);

    struct sigaction crash{};
    crash.sa_handler = &OnCrashSignal;
    sigemptyset(&crash.sa_mask);
    crash.sa_flags = SA_RESETHAND | SA_ONSTACK | SA_NODEFER;
    for (const int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) {
      (void)::sigaction(signalNumber, &crash, nullptr);
    }

    // The first Ctrl-C ends the run cleanly at the next frame; a second one kills it.
    struct sigaction interrupt{};
    interrupt.sa_handler = &OnInterruptSignal;
    sigemptyset(&interrupt.sa_mask);
    interrupt.sa_flags = SA_RESETHAND;
    (void)::sigaction(SIGINT, &interrupt, nullptr);
    (void)::sigaction(SIGTERM, &interrupt, nullptr);
  }

  [[nodiscard]] bool RunSelfTests()
  {
#if FAF_RUNNER_HAVE_SELFTEST
    return faf_posixRunSelfTests(true);
#else
    std::fprintf(
      stderr, "faf_replay: built without platform/posix/crt/SelfTest.h; the platform self-tests cannot run\n"
    );
    return false;
#endif
  }

  // Every exit leaves without running the engine's static destructors: the log is flushed per
  // line and the session, preferences and sim logs were closed by HeadlessApp::Destroy, so
  // nothing is lost, and a teardown-order problem in ~950 translation units' statics cannot
  // turn a finished run into a crash exit status. (Measured: a plain exit() aborts in
  // DISK_ResetWatch, an atexit handler registered during static init before CDiskWatch.cpp's
  // own mutex, which libc++ has destroyed by then: "mutex lock failed".)
  [[noreturn]] void LeaveProcess(const int exitCode)
  {
    std::fflush(nullptr);
    std::_Exit(exitCode);
  }

  void PrintUsage()
  {
    std::fprintf(
      stderr,
      "usage: faf_replay /init <init.lua> /replay <file.scfareplay> [/replayid <id>] [/log <file>]\n"
      "                  [/benchspeed <n>] [/exitongameover] [/stalltimeout <s>] [/timeout <s>]\n"
      "                  [/nosound] [/nomovie] [/nobugreport] [/framerate <n>] [/sse2] [/noui]\n"
      "                  [/echolog] [/skipselftest] [engine switches ...]\n"
      "See macos/porting/runner.md.\n"
    );
  }
} // namespace

const faf_runner::RunnerOptions& faf_runner::GetOptions()
{
  return gOptions;
}

bool faf_runner::InterruptRequested()
{
  return gInterruptRequested.load();
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  gpg::SetThreadName(0xFFFFFFFFu, "Main");
  InstallSignalHandlers();

  msvc8::string replayPath;
  const bool haveReplay = GetArg("/replay", &replayPath);
  faf_runner::ReportInit(haveReplay ? ResolveReplayId(replayPath).c_str() : "none");
  if (HasSwitch("/echolog")) {
    faf_runner::ReportEchoLogToStderr();
  }

  if (!haveReplay) {
    PrintUsage();
    LeaveProcess(faf_runner::ReportFinish(faf_runner::kExitUsage));
  }
  ReadOptions();

  if (!RunSelfTests()) {
    if (!HasSwitch("/skipselftest")) {
      std::fprintf(stderr, "faf_replay: platform self-tests failed; not starting the sim (/skipselftest overrides)\n");
      LeaveProcess(faf_runner::ReportFinish(faf_runner::kExitSelfTestFailed));
    }
    std::fprintf(stderr, "faf_replay: platform self-tests failed; continuing because of /skipselftest\n");
  }

  (void)gpg::SetDieHandler(&RunnerDieHandler);

  gpg::time::Timer runTimer{};
  try {
    faf_runner::HeadlessApp app;
    moho::WIN_AppExecute(&app);
  } catch (const std::exception& exception) {
    // WinMain's handler (gpg::gal::Error derives from std::exception): Die, which reports.
    gpg::Die("Unhandled exception:\n\n%s", exception.what());
  }

  const int totalSeconds = static_cast<int>(runTimer.ElapsedSeconds());
  gpg::Logf("Run time: %dh%02dm%02ds", totalSeconds / 3600, (totalSeconds % 3600) / 60, totalSeconds % 60);
  const int exitCode = faf_runner::ReportFinish(faf_runner::kExitRequested);

  LeaveProcess(exitCode);
}
