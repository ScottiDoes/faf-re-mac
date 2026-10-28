// The headless replay runner's application: CScApp (moho/app/CScApp.cpp) without the window,
// the render device and rendering. moho::WIN_AppExecute runs it exactly as it runs CScApp:
// Init once, then Main each turn of the loop until wxTheApp->ExitMainLoop(), then Destroy.
//
// What is left out, and why it does not change what the sim computes:
//  - CScApp::CreateDevice / CreateAppFrame's device half (gal::Device::Create, the renderer
//    attempts, graphics option groups, the movie manager set-up), REN_Init (fonts),
//    REN_Frame, CD3DDevice::Refresh (present), OPTIONS_Apply and the screensaver and cursor
//    clip. moho::REN_InitHeadless() (headless/HeadlessRender.h) brings the device up with
//    CPU-backed null resources instead, so the user-side session code that needs a device
//    (terrain finalisation, texture and buffer creation) runs unchanged.
//  - Nothing is shown: the frame and the game viewport the UI attaches to are created but
//    never shown, and the app always takes CScApp::Main's minimized branch (it sleeps until
//    the next beat is due or the sim publishes one) instead of presenting.
// The sim runs on its own threads (SimDriver) and is fed by the replay stream; the user side
// only consumes its sync packets. The UI's Lua still runs (the replay benchmark's
// /benchspeed hook lives in gamemain.lua's CreateUI).

#include "platform/posix/runner/Runner.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "gpg/core/containers/String.h"
#include "gpg/core/profiling/Profiler.h"
#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "moho/app/CWaitHandleSet.h"
#include "moho/app/WinApp.h"
#include "moho/app/WxRuntimeTypes.h"
#include "moho/audio/AudioEngine.h"
#include "moho/audio/IUserSoundManager.h"
#include "moho/audio/SofdecRuntime.h"
#include "moho/console/CConCommand.h"
#include "moho/lua/CScrLuaObjectFactory.h"
#include "moho/math/MathReflection.h"
#include "moho/misc/CDiskWatch.h"
#include "moho/misc/ScrDebugHooks.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/misc/TimeBar.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/net/CGpgNetInterface.h"
#include "moho/render/RCamManager.h"
#include "moho/render/WRenViewport.h"
#include "moho/render/d3d/CD3DDevice.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/CWldSessionLoaderImpl.h"
#include "moho/sim/ISTIDriver.h"
#include "moho/sim/SimDriver.h"
#include "moho/task/CTaskThread.h"
#include "moho/ui/IUIManager.h"
#include "moho/ui/UiRuntimeTypes.h"
#include "moho/ui/UiWorldShapes.h"
#include "platform/posix/headless/HeadlessRender.h"

namespace moho
{
  // moho/sim/CWldSession.cpp: sync beats the user side has applied (main thread only). The
  // first beat is beat 0, so the last applied beat is this count minus one.
  extern std::uint64_t gUserSyncBeats;
} // namespace moho

namespace
{
  // Longest the loop sleeps between two frames, so the run-end checks (stall, /timeout,
  // SIGINT) and the user stage's Lua threads keep running while no beat is due.
  constexpr float kMaxSleepMilliseconds = 100.0f;

  // CScApp's window title (CScApp::CreateDevice); the frame is never shown.
  constexpr const char* kFrameTitle = "Forged Alliance";

  [[nodiscard]] double SecondsBetween(
    const std::chrono::steady_clock::time_point from, const std::chrono::steady_clock::time_point to
  )
  {
    return std::chrono::duration<double>(to - from).count();
  }

  void ExitMainLoop(const char* const reason)
  {
    faf_runner::ReportSetExitReason(reason);
    if (wxTheApp != nullptr) {
      wxTheApp->ExitMainLoop();
    }
  }

  // The head the null device reports, else CScApp's default window size.
  [[nodiscard]] wxSize HeadlessHeadSize()
  {
    if (gpg::gal::Device::IsReady()) {
      if (gpg::gal::DeviceContext* const context = gpg::gal::Device::GetInstance()->GetDeviceContext();
          context != nullptr && context->GetHeadCount() > 0) {
        const gpg::gal::Head& head = context->GetHead(0);
        if (head.mWidth > 0 && head.mHeight > 0) {
          return wxSize(static_cast<int>(head.mWidth), static_cast<int>(head.mHeight));
        }
      }
    }
    return wxSize(moho::wnd_DefaultCreateWidth, moho::wnd_DefaultCreateHeight);
  }
} // namespace

faf_runner::HeadlessApp::HeadlessApp()
  : IWinApp("SupCom", "SupremeCommander")
{
  // CScApp's constructor: the frame timer, the empty ring, the app identity.
  mCurTime.Reset();
  mFrameRates.start = 0;
  mFrameRates.end = 0;
  moho::APP_InitializeIdentity();
}

faf_runner::HeadlessApp::~HeadlessApp()
{
  mFrameRates.Reset();
}

bool faf_runner::HeadlessApp::FailInit(const char* const what)
{
  // WIN_AppExecute ends the process straight after a failed Init (TerminateProcess(1) on
  // Windows), so the result is reported and the process ended here, with the runner's
  // "did not complete" status rather than 1, which means mismatches.
  gpg::Warnf("faf_replay: start-up failed: %s", what);
  std::fprintf(stderr, "faf_replay: start-up failed: %s\n", what);
  const int exitCode = ReportFinish(kExitInitFailed);
  std::fflush(nullptr);
  std::_Exit(exitCode);
}

// CScApp::Init (0x008CEDE0) in its order, minus what needs a window or draws.
bool faf_runner::HeadlessApp::Init()
{
  moho::APP_InitializeIdentity();
  mFrameRates.Reset();
  mCurTime.Reset();
  mFirstFramePending = true;
  mInitialized = false;
  mStartTime = Clock::now();
  mLastProgressTime = mStartTime;
  mLastSpeedCheck = mStartTime;

  // The SSE2 lane starts disabled unless /sse2 is given (global_mode_sse2 is 1 until this
  // runs; acos/asin/pow take the other lane otherwise).
  (void)SetSse2Mode(moho::CFG_GetArgOption("/sse2", 0, nullptr) ? 1 : 0);

  // /splash: no splash screen without a display.

  if (!AppInitCommonServices()) {
    return FailInit("AppInitCommonServices");
  }

  msvc8::vector<msvc8::string> preferenceArgs;
  if (moho::CFG_GetArgOption("/prefs", 1, &preferenceArgs) && !preferenceArgs.empty()) {
    moho::USER_LoadPreferences(preferenceArgs[0]);
  } else {
    moho::USER_LoadPreferences(msvc8::string("Game.prefs"));
  }

  // CScApp::Init runs it here and does not look at the result either.
  (void)moho::SCR_LuaDoScript(moho::USER_GetLuaState(), "/lua/userInit.lua", nullptr);

  moho::USER_EnsureDocumentDirectories();

  msvc8::vector<msvc8::string> profileArgs;
  if (moho::CFG_GetArgOption("/profile", 1, &profileArgs) && !profileArgs.empty()) {
    moho::USER_SetCurrentProfile(profileArgs[0]);
  }

  [[maybe_unused]] const bool debugFacilitiesEnabled = moho::USER_DebugFacilitiesEnabled();

  // REN_Init (font registration) is left out: nothing is drawn.
  moho::UI_Init();

  // In place of CScApp::CreateDevice: the frame, the game viewport, then the device.
  CreateHeadlessFrame();
  return true;
}

// CScApp::CreateAppFrame without the window system: the frame, the game viewport the UI's
// world views attach to (ren_Viewport), the null device bound to that viewport (CreateAppFrame
// creates the viewport before gal::Device::Create and binds it with SetRenViewport, which runs
// WRenViewport::D3DWindowOnDeviceInit: batchers, range/vision renderers, head targets), the
// log window, and the viewport as the UI's head 0.
void faf_runner::HeadlessApp::CreateHeadlessFrame()
{
  const wxSize size = HeadlessHeadSize();
  mFrame = new WSupComFrame(kFrameTitle, wxDefaultPosition, size, wxDEFAULT_FRAME_STYLE);
  mFrame->SetClientSize(size.x, size.y);
  moho::WIN_SetMainWindow(mFrame);

  moho::WD3DViewport* viewport = nullptr;
  int clientWidth = size.x;
  int clientHeight = size.y;
  if (!GetOptions().noUi) {
    moho::sMainWindow->GetClientSize(&clientWidth, &clientHeight);
    if (clientWidth <= 0 || clientHeight <= 0) {
      clientWidth = size.x;
      clientHeight = size.y;
    }
    const Wm3::Vector2i viewportSize{clientWidth, clientHeight};
    viewport = moho::REN_CreateGameViewport(moho::sMainWindow, kFrameTitle, viewportSize, false);
    // REN_InitHeadless binds moho::ren_Viewport when it is set (CreateAppFrame's
    // SetRenViewport(viewport)); CreateAppFrame publishes it right after the device.
    moho::ren_Viewport = static_cast<moho::WRenViewport*>(viewport);
  }

  if (!moho::REN_InitHeadless()) {
    (void)FailInit("REN_InitHeadless");
  }

  // The log window collects the log for its dialog; it also bounds the lines the log target
  // keeps (the dialog merges them, capped), as on Windows.
  moho::WINX_PrecreateLogWindow();

  if (viewport == nullptr) {
    gpg::Logf("faf_replay: /noui: no UI frame or game viewport");
    return;
  }

  if (moho::IUIManager* const uiManager = moho::UI_GetManager()) {
    (void)uiManager->AddFrame(viewport, mFrame);
  }
  gpg::Logf("faf_replay: headless head %dx%d", clientWidth, clientHeight);
}

// CScApp::Main (0x008D1470): the same frame, minus REN_Frame and the present.
void faf_runner::HeadlessApp::Main()
{
  FAF_PROFILE_ZONE_NAMED("AppFrame");
  moho::CTimeBarSection appFrameSection("AppFrame");
  moho::scapp::PublishFrameHeapStats();
  moho::WIN_SetMainWindow(mFrame);

  moho::CWaitHandleSet* const waitHandleSet = moho::WIN_GetWaitHandleSet();
  if (mAddedSyncEvent != nullptr) {
    if (waitHandleSet != nullptr) {
      waitHandleSet->RemoveHandle(mAddedSyncEvent);
    }
    mAddedSyncEvent = nullptr;
  }

  float frameSeconds = 0.0f;
  float fixedFrameSeconds = -1.0f;
  if (mFirstFramePending) {
    mFirstFramePending = false;
    mCurTime.Reset();
  } else {
    fixedFrameSeconds = moho::scapp::TryReadFixedFrameDeltaSeconds();
    if (fixedFrameSeconds > 0.0f) {
      frameSeconds = fixedFrameSeconds;
    } else {
      float elapsed = mCurTime.ElapsedMilliseconds();
      if (moho::sc_FrameTimeClamp > elapsed) {
        moho::WIN_SetWakeupTimer(moho::sc_FrameTimeClamp - elapsed);
        return;
      }
      frameSeconds = static_cast<float>(elapsed * 0.001);
    }
    mCurTime.Reset();
  }

  (void)mFrameRates.Push(frameSeconds);
  moho::UI_SetRenderWorldFrameSeconds(frameSeconds);
  moho::SND_Frame();
  moho::DISK_UpdateWatcher();

  float smoothedFrameSeconds = fixedFrameSeconds > 0.0f ? fixedFrameSeconds : mFrameRates.Median();
  const int frameClock = moho::scapp::GetFrameClockMode();
  if (fixedFrameSeconds <= 0.0f && frameClock != 0 &&
      moho::SIM_GetSessionPacing() != moho::ESimSessionPacing::Network) {
    smoothedFrameSeconds = frameClock == 2 ? frameSeconds : mFrameRates.Mean();
  }
  if (!moho::WLD_Frame(smoothedFrameSeconds)) {
    ExitMainLoop(kExitSessionEnded);
  }

  moho::CWldSession* const activeSession = moho::WLD_GetActiveSession();
  const float simDeltaSeconds = activeSession != nullptr ? activeSession->mTimeSinceLastTick : 0.0f;
  moho::CAM_GetManager()->Frame(simDeltaSeconds, frameSeconds);
  if (moho::sUserStage != nullptr) {
    FAF_PROFILE_ZONE_NAMED("UserStage");
    moho::sUserStage->UserFrame();
  }
  ::ADXM_ExecMain();
  // REN_Frame: nothing is drawn.
  if (moho::IUserSoundManager* const userSound = moho::USER_GetSound(); userSound != nullptr) {
    userSound->Frame(simDeltaSeconds, frameSeconds);
  }
  if (moho::IUIManager* const uiManager = moho::UI_GetManager()) {
    uiManager->UpdateFrameRate(frameSeconds);
  }

  ScheduleNextFrame();

  if (!mInitialized) {
    moho::scapp::InitializeSessionFromCommandLine();
    mInitialized = true;
    if (!moho::WLD_IsSessionActive()) {
      std::fprintf(stderr, "faf_replay: the replay session did not start (see the log)\n");
      ExitMainLoop(kExitLoadFailed);
    }
  }

  NoteProgress();
  ApplyRunnerGameSpeed();
  CheckRunEnd();
}

// CScApp::Main's minimized branch, always: sleep until the next beat is due, or until the
// sim publishes sync data when one is already due; never longer than kMaxSleepMilliseconds.
// Outside a playing session (loading, front end) frames run at the sc_FrameTimeClamp rate.
void faf_runner::HeadlessApp::ScheduleNextFrame()
{
  float sleepMilliseconds = moho::sc_FrameTimeClamp;
  moho::CWldSession* const activeSession = moho::WLD_GetActiveSession();
  moho::ISTIDriver* const simDriver = moho::WLD_GetDriver();
  if (activeSession != nullptr && moho::WLD_IsSessionPlaying()) {
    const float delayToNextBeatSeconds = activeSession->GetDelayToNextBeat();
    if (delayToNextBeatSeconds > 0.0f) {
      sleepMilliseconds = delayToNextBeatSeconds * 1000.0f;
    } else if (simDriver != nullptr) {
      if (moho::CWaitHandleSet* const waitHandleSet = moho::WIN_GetWaitHandleSet()) {
        mAddedSyncEvent = simDriver->GetSyncDataAvailableEvent();
        waitHandleSet->AddHandle(mAddedSyncEvent);
      }
      sleepMilliseconds = kMaxSleepMilliseconds;
    }
  }
  moho::WIN_SetWakeupTimer(std::min(sleepMilliseconds, kMaxSleepMilliseconds));
}

void faf_runner::HeadlessApp::NoteProgress()
{
  const Clock::time_point now = Clock::now();
  if (moho::WLD_IsSessionActive()) {
    mSessionSeen = true;
  }
  if (moho::CWldSession* const activeSession = moho::WLD_GetActiveSession(); activeSession != nullptr) {
    const std::int32_t tick = activeSession->mGameTick;
    if (tick > 0 && mFirstTick < 0) {
      mFirstTick = tick;
      mFirstTickTime = now;
    }
    if (tick != mLastTick) {
      mLastTickChangeTime = now;
    }
    mLastTick = tick;
    if (activeSession->IsGameOver != 0u) {
      mGameOverSeen = true;
    }
  }
  const std::uint64_t beats = moho::gUserSyncBeats;
  if (beats != mLastBeats) {
    mLastBeats = beats;
    mLastProgressTime = now;
  }
  ReportNoteProgress(mLastBeats, mLastTick);
}

// /noui has no UI Lua, so no benchmark hook: the runner requests the /benchspeed game speed
// itself, as the hook does (once the game ticks, whenever the requested speed differs).
void faf_runner::HeadlessApp::ApplyRunnerGameSpeed()
{
  const RunnerOptions& options = GetOptions();
  if (!options.noUi || !options.haveBenchSpeed || mLastTick <= 0) {
    return;
  }
  const Clock::time_point now = Clock::now();
  if (SecondsBetween(mLastSpeedCheck, now) < 1.0) {
    return;
  }
  mLastSpeedCheck = now;
  moho::ISTIDriver* const simDriver = moho::WLD_GetDriver();
  moho::CClientManagerImpl* const clientManager = simDriver != nullptr ? simDriver->GetClientManager() : nullptr;
  if (clientManager != nullptr && clientManager->GetSimRateRequested() != options.benchSpeed) {
    // The hook's own call (ConExecute("WLD_GameSpeed " .. target)): the console command
    // clamps to [-10, 50]; WLD_SetGameSpeed, the UI slider's entry, stops at +10.
    moho::CON_Executef("WLD_GameSpeed %d", options.benchSpeed);
  }
}

// The run ends when the session goes away, when no beat has arrived for /stalltimeout seconds
// after the first one (a replay's stream ends with an end-game command, so a finished replay
// has reached game over by then; a stall without it is a hang), at /timeout, or on SIGINT.
// /exitongameover is the engine's own (CWldSession::DoBeat).
void faf_runner::HeadlessApp::CheckRunEnd()
{
  const RunnerOptions& options = GetOptions();
  const Clock::time_point now = Clock::now();

  if (InterruptRequested()) {
    ExitMainLoop(kExitInterrupted);
    return;
  }
  if (mSessionSeen && !moho::WLD_IsSessionActive()) {
    ExitMainLoop(kExitSessionEnded);
    return;
  }
  if (options.timeoutSeconds > 0.0 && SecondsBetween(mStartTime, now) > options.timeoutSeconds) {
    ExitMainLoop(kExitTimeout);
    return;
  }
  if (options.stallSeconds > 0.0 && mLastBeats > 0 && SecondsBetween(mLastProgressTime, now) > options.stallSeconds) {
    gpg::Logf(
      "faf_replay: no new beat for %.1f s (last beat %llu, tick %d, game over %s)",
      SecondsBetween(mLastProgressTime, now),
      static_cast<unsigned long long>(mLastBeats - 1u),
      static_cast<int>(mLastTick),
      mGameOverSeen ? "yes" : "no"
    );
    ExitMainLoop(mGameOverSeen ? kExitEndOfReplay : kExitStalled);
  }
}

// The sim's throughput between the first frame that saw the game tick move and the last one
// that saw it change (load time and the end-of-replay wait are left out). A tick is 0.1 s of
// game time, so ticks/s / 10 is the multiple of real time.
void faf_runner::HeadlessApp::LogSimRate()
{
  if (mFirstTick < 0 || mLastTick <= mFirstTick) {
    return;
  }
  const double seconds = SecondsBetween(mFirstTickTime, mLastTickChangeTime);
  if (seconds <= 0.0) {
    return;
  }
  const double ticksPerSecond = static_cast<double>(mLastTick - mFirstTick) / seconds;
  gpg::Logf(
    "faf_replay: sim rate: ticks %d..%d in %.1f s = %.1f ticks/s = %.2fx real time",
    static_cast<int>(mFirstTick), static_cast<int>(mLastTick), seconds, ticksPerSecond, ticksPerSecond / 10.0
  );
}

// CScApp::Destroy (0x008D0F20) in its order, with REN_ShutdownHeadless for D3D_Exit.
void faf_runner::HeadlessApp::Destroy()
{
  NoteProgress();
  LogSimRate();
  if (ReportGetExitReason() == nullptr) {
    // The loop ended without the runner asking: /exitongameover (CWldSession::DoBeat) or
    // the game (ExitApplication, the escape menu's quit).
    ReportSetExitReason(mGameOverSeen ? kExitGameOver : kExitRequested);
  }

  if (mAddedSyncEvent != nullptr) {
    if (moho::CWaitHandleSet* const waitHandleSet = moho::WIN_GetWaitHandleSet()) {
      waitHandleSet->RemoveHandle(mAddedSyncEvent);
    }
    mAddedSyncEvent = nullptr;
  }

  moho::WLD_Teardown();
  // The teardown applies the sync packets still queued; count them.
  mLastBeats = moho::gUserSyncBeats;
  ReportNoteProgress(mLastBeats, mLastTick);

  moho::GPGNET_Shutdown();
  moho::UI_Exit();
  moho::GetWldSessionLoader()->Finalize();
  moho::DestroyMovieManagerSingleton();
  moho::REN_ShutdownHeadless();
  moho::WIN_SetMainWindow(nullptr);
  moho::ren_Viewport = nullptr;
  moho::SND_Shutdown();
  moho::SCR_DestroyDebugWindow();
  moho::USER_SavePreferences();

  if (mFrame != nullptr) {
    (void)mFrame->Destroy();
    mFrame = nullptr;
  }
}
