#pragma once

// The headless replay runner (faf_replay, milestone 1): declarations shared by its files.
//
//   RunnerMain.cpp    main(): start-up self-tests, die/signal handlers, WIN_AppExecute
//   HeadlessApp.cpp   the IWinApp that WIN_AppExecute runs: CScApp's start-up, session start
//                     and frame, minus the window, the device and rendering
//   ReplayReport.cpp  the run's result: checksum mismatches seen in the log, the last beat,
//                     the exit reason, and the FAF_REPLAY_RESULT line
//   ReplayChecks.cpp  the replay's recorded player checksums: how many the run compared
//   FpcrWatch.cpp     every thread's FPCR (sim workers included) must stay 0
//
// Usage and expected output: macos/porting/runner.md.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

  // Verdicts (verdict=<v>), with the exit status: pass 0; mismatch 1 (any checksum mismatched);
  // incomplete 2 (the run did not end with gameover or end-of-replay); fpcr 2 (a thread ran
  // with FPCR != 0); unverified 2 (the run compared no recorded checksum, or the replay's
  // checksums could not be counted).
  inline constexpr const char* kVerdictPass = "pass";
  inline constexpr const char* kVerdictMismatch = "mismatch";
  inline constexpr const char* kVerdictIncomplete = "incomplete";
  inline constexpr const char* kVerdictFpcr = "fpcr";
  inline constexpr const char* kVerdictUnverified = "unverified";

  // Records why the run ends; the first reason given wins.
  void ReportSetExitReason(const char* reason);
  [[nodiscard]] const char* ReportGetExitReason();

  // Progress as the user side applied it: the count of sim beats applied (beats are
  // numbered from 0, so the last beat is count - 1) and the game tick.
  void ReportNoteProgress(std::uint64_t appliedBeats, std::int32_t gameTick);

  [[nodiscard]] int ReportMismatchCount();

  // Prints
  //   FAF_REPLAY_RESULT replay=<id> beats=<last beat> tick=<game tick> recorded=<r>
  //     checked=<c> mismatches=<n> exit=<reason> verdict=<v>
  // (one line) to stdout once (later calls do nothing) and logs it. recorded and checked are
  // -1 when the replay's checksums could not be counted (ReplayChecks*). `fallbackReason` is
  // used when no reason was recorded. Returns the process exit code of the verdict (above).
  // `writeToLog` false keeps it off the gpg log (the die handler).
  int ReportFinish(const char* fallbackReason, bool writeToLog = true);

  // Async-signal-safe form for the crash handler: exit=crash-<signal name>, verdict mismatch
  // or incomplete.
  void ReportFinishFromSignal(int signalNumber);

  // Mirrors every log line to stderr (/echolog).
  void ReportEchoLogToStderr();

  // --- The replay's recorded checksums (ReplayChecks.cpp) ----------------------------------

  struct ReplayChecksSummary
  {
    std::int64_t recorded = 0;    // CMDST_VerifyChecksum records in the command stream
    std::int64_t streamBeats = 0; // CMDST_Advance records
    bool truncated = false;       // the stream ends in a message the engine cannot read
    // Sim::mCurBeat when the sim compares each record it compares, in stream order.
    std::vector<std::int64_t> comparedAt;
  };

  // Reads a .scfareplay image (header, then the command stream) the way the engine does.
  // False (with `error`) when the header is not a replay header.
  [[nodiscard]] bool ReplayChecksParse(
    const std::uint8_t* data, std::size_t size, ReplayChecksSummary* out, std::string* error
  );

  // Loads the /replay file's checksum records for the result line. Call once, before the run.
  [[nodiscard]] bool ReplayChecksLoad(const char* path, std::string* error);

  // -1 until ReplayChecksLoad succeeded.
  [[nodiscard]] std::int64_t ReplayChecksRecorded();
  [[nodiscard]] std::int64_t ReplayChecksComparable();  // compared when the whole stream plays
  [[nodiscard]] std::int64_t ReplayChecksStreamBeats();

  // Records the sim had compared once the user side applied `appliedSyncPackets` sync
  // packets (the opening one included). Async-signal-safe.
  [[nodiscard]] std::int64_t ReplayChecksCompared(std::uint64_t appliedSyncPackets);

  // --- Per-thread FPCR check (FpcrWatch.cpp) ------------------------------------------------

  // Reads the FPCR of every thread of the process; false when one is not 0 (logged, with the
  // thread). The run's verdict is fpcr when any sample found one.
  bool FpcrWatchSample();
  [[nodiscard]] int FpcrWatchSamples();
  [[nodiscard]] int FpcrWatchBadReads();
  void FpcrWatchLog();

  // --- Switches (RunnerMain.cpp) ------------------------------------------------------------

  struct RunnerOptions
  {
    bool noUi = false;               // /noui: no UI frame or viewport (fallback, see runner.md)
    double stallSeconds = 10.0;      // /stalltimeout <s>: no new beat for this long ends the run
    double timeoutSeconds = 0.0;     // /timeout <s>: wall-clock limit (0: none)
    int benchSpeed = 0;              // /benchspeed <n>
    bool haveBenchSpeed = false;
    bool noFpcrCheck = false;        // /nofpcrcheck: no per-thread FPCR sampling
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
    void SampleFpcr(bool force);

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
    Clock::time_point mLastFpcrSample{};
  };
} // namespace faf_runner
