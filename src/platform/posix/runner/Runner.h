#pragma once

// The headless replay runner (faf_replay, milestone 1): declarations shared by its files.
//
//   RunnerMain.cpp    main(): start-up self-tests, die/signal handlers, WIN_AppExecute
//   HeadlessApp.cpp   the IWinApp that WIN_AppExecute runs: CScApp's start-up, session start
//                     and frame, minus the window, the device and rendering
//   ReplayReport.cpp  the run's result: checksum mismatches seen in the log, the last beat,
//                     the exit reason, and the FAF_REPLAY_RESULT line
//
// Usage and expected output: macos/porting/runner.md.

#include <chrono>
#include <cstdint>

#include "gpg/core/time/Timer.h"
#include "moho/app/CScApp.h"
#include "moho/app/IWinApp.h"

class wxWindow;
class WSupComFrame;

namespace faf_runner
{
  // --- Result (ReplayReport.cpp) -----------------------------------------------------------

  // Exit reasons (exit=<reason>). A run passes when it ends with kExitGameOver or
  // kExitEndOfReplay and no checksum mismatched.
  inline constexpr const char* kExitGameOver = "gameover";          // /exitongameover
  inline constexpr const char* kExitEndOfReplay = "end-of-replay";  // game over, then no more beats
  inline constexpr const char* kExitStalled = "stalled";            // no beats, no game over
  inline constexpr const char* kExitSessionEnded = "session-ended"; // the world session went away
  inline constexpr const char* kExitRequested = "exit-requested";   // ExitApplication, ...
  inline constexpr const char* kExitTimeout = "timeout";            // /timeout
  inline constexpr const char* kExitInterrupted = "interrupted";    // SIGINT / SIGTERM
  inline constexpr const char* kExitLoadFailed = "load-failed";     // the replay did not start
  inline constexpr const char* kExitInitFailed = "init-failed";     // start-up failed
  inline constexpr const char* kExitSelfTestFailed = "selftest-failed";
  inline constexpr const char* kExitUsage = "usage";
  inline constexpr const char* kExitDie = "die";                    // gpg::Die

  // Remembers the replay id for the result line and starts counting the sim's
  // "Checksum for beat N mismatched" warnings (a gpg log target). Call once, first.
  void ReportInit(const char* replayId);

  // Records why the run ends; the first reason given wins.
  void ReportSetExitReason(const char* reason);
  [[nodiscard]] const char* ReportGetExitReason();

  // Progress as the user side applied it: the count of sim beats applied (beats are
  // numbered from 0, so the last beat is count - 1) and the game tick.
  void ReportNoteProgress(std::uint64_t appliedBeats, std::int32_t gameTick);

  [[nodiscard]] int ReportMismatchCount();

  // Prints "FAF_REPLAY_RESULT replay=<id> beats=<last beat> mismatches=<n> exit=<reason>"
  // to stdout once (later calls do nothing) and logs it. `fallbackReason` is used when no
  // reason was recorded. Returns the process exit code: 0 pass, 1 mismatches, 2 the run
  // did not complete. `writeToLog` false keeps it off the gpg log (the die handler).
  int ReportFinish(const char* fallbackReason, bool writeToLog = true);

  // Async-signal-safe form for the crash handler: exit=crash-<signal name>.
  void ReportFinishFromSignal(int signalNumber);

  // Mirrors every log line to stderr (/echolog).
  void ReportEchoLogToStderr();

  // --- Switches (RunnerMain.cpp) ------------------------------------------------------------

  struct RunnerOptions
  {
    bool noUi = false;               // /noui: no UI frame or viewport (fallback, see runner.md)
    double stallSeconds = 10.0;      // /stalltimeout <s>: no new beat for this long ends the run
    double timeoutSeconds = 0.0;     // /timeout <s>: wall-clock limit (0: none)
    int benchSpeed = 0;              // /benchspeed <n>
    bool haveBenchSpeed = false;
  };

  [[nodiscard]] const RunnerOptions& GetOptions();

  // Set by SIGINT/SIGTERM; HeadlessApp::Main ends the run on the next frame.
  [[nodiscard]] bool InterruptRequested();

  // --- The application (HeadlessApp.cpp) ----------------------------------------------------

  // CScApp without a window, a device or rendering, run by moho::WIN_AppExecute:
  //  Init     CScApp::Init's order: identity, SetSse2Mode, common services (/log, data
  //           paths, /init), preferences, /lua/userInit.lua, document directories, UI_Init,
  //           then REN_InitHeadless() where CScApp creates the device, and a hidden frame and
  //           game viewport for the UI (CScApp::CreateAppFrame without Device::Create).
  //  Main     CScApp::Main's frame: WLD_Frame, cameras, the user stage, sound, UI frame rate;
  //           no REN_Frame or present. It waits like CScApp's minimized branch (until the next
  //           beat is due or the sim publishes one). The first frame starts the /replay
  //           session through CScApp's own InitializeSessionFromCommandLine.
  //  Destroy  CScApp::Destroy's order with REN_ShutdownHeadless in place of D3D_Exit.
  class HeadlessApp final : public moho::IWinApp
  {
  public:
    HeadlessApp();
    ~HeadlessApp() override;

    bool Init() override;
    void Main() override;
    void Destroy() override;

  private:
    [[noreturn]] bool FailInit(const char* what);
    void CreateHeadlessFrame();
    void ScheduleNextFrame();
    void CheckRunEnd();
    void ApplyRunnerGameSpeed();
    void NoteProgress();
    void LogSimRate();

    using Clock = std::chrono::steady_clock;

    WSupComFrame* mFrame = nullptr;
    bool mInitialized = false;
    bool mFirstFramePending = true;
    bool mSessionSeen = false;
    bool mGameOverSeen = false;
    gpg::time::Timer mCurTime{};
    CScApp::RollingFrameRates mFrameRates{};
    HANDLE mAddedSyncEvent = nullptr; // the sync-data event this app put in the wait set
    std::uint64_t mLastBeats = 0;
    std::int32_t mLastTick = 0;
    Clock::time_point mStartTime{};
    Clock::time_point mLastProgressTime{};
    Clock::time_point mLastSpeedCheck{};
    // Sim rate: the first frame that saw the game tick move past 0, and the tick then.
    Clock::time_point mFirstTickTime{};
    std::int32_t mFirstTick = -1;
    Clock::time_point mLastTickChangeTime{};
  };
} // namespace faf_runner
