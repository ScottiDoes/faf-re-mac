// POSIX replacement for moho/app/WinApp.cpp (excluded from the macOS build, see
// macos/exclusions.d/wx.cmake).
//
// WinApp.cpp is the Windows application shell: the low-level keyboard hook, the SEH filter
// and crash dialog, DbgHelp symbol lookups, the registry, the clipboard and the wx splash
// screen. This file defines every symbol it exports (moho/app/WinApp.h, the log window's
// target, the wx application factory) with the same behaviour where a headless replay run
// depends on it, and the POSIX equivalent or a logged stub where only UI or diagnostics do:
//
//   - real: the application loop (WIN_AppExecute, same order of calls as the Windows loop:
//     before-events stage, one window message or wx idle, IWinApp::Main, before-wait stage,
//     wake-up timer, wait-handle set), the task stages, the wake-up timer, the current app /
//     main window, the log target, call stacks (backtrace/dladdr), crash-report attachments
//     and the game-log dump, WINX_Exit / WINX_Printf / WINX_PrecreateLogWindow, the clipboard
//     (the Window area's, i.e. the system clipboard while the window system is up).
//     With the window system up (faf_posix::shell, the game) the loop's messages are the
//     Window area's queue - SDL's input as Win32 messages - and its wait wakes on input
//     (MsgWaitForMultipleObjectsEx with QS_ALLINPUT); headless there is no queue and the
//     loop is the wx idle / Main / wait cycle alone.
//   - stubs (FAF_POSIX_STUB): the registry (reads as "key not found", as on a machine
//     without the key), the crash dialog (written to the log and stderr), the splash screen
//     (the wx stand-in cannot load images).

#include "moho/app/WinApp.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <vector>
#include <cwchar>

#include "boost/mutex.h"
#include "gpg/core/containers/String.h"
#include "gpg/core/time/Timer.h"
#include "gpg/core/utils/Logging.h"
#include "legacy/containers/Vector.h"
#include "moho/app/CWaitHandleSet.h"
#include "moho/app/IWinApp.h"
#include "moho/app/WxRuntimeTypes.h"
#include "moho/core/Thread.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/resource/ResourceManager.h"
#include "platform/X87Precision.h"

// The application object: wxEntry builds a moho::MohoApp through this factory, and
// wxGetApp() (DECLARE_APP in WxRuntimeTypes.h) returns it.
IMPLEMENT_APP_NO_MAIN(moho::MohoApp)

namespace moho
{
  // The log window's target (WinApp.cpp 0x010A9BA0): a gpg log target that buffers lines
  // for the "Moho Log" dialog.
  CWinLogTarget sLogWindowTarget{};
} // namespace moho

namespace
{
  constexpr float kMaxFiniteTimeoutMs = 4294967300.0f;
  constexpr float kInfiniteWakeupMs = std::numeric_limits<float>::infinity();

  moho::IWinApp* sSupComApp = nullptr;
  gpg::time::Timer wakeupTimer;
  float wakeupTimerDur = kInfiniteWakeupMs;

  [[maybe_unused]] const bool gWinAppBootstrap = []() {
    moho::register_startTime();
    moho::register_wakeupTimer();
    return true;
  }();

  float ProbeWakeTimerMs()
  {
    const LONGLONG elapsedCycles = wakeupTimer.ElapsedCyclesAndReset();
    return gpg::time::CyclesToMilliseconds(elapsedCycles);
  }

  // func_round (0x0040D820): frndint, then one down when that rounded up.
  [[nodiscard]] int FloorFrndintAdjustDown(const float value) noexcept
  {
    const float rounded = std::nearbyintf(value);
    return static_cast<int>(rounded) + ((value < rounded) ? -1 : 0);
  }

  // WinApp.cpp's DispatchPendingMessage (engine addition): wxApp::Dispatch without its
  // blocking GetMessage - take the message Pending() saw, if it is still there, and hand it
  // to wxApp::DoMessage.
  void DispatchPendingMessage()
  {
    MSG message{};
    if (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) == FALSE) {
      return;
    }
    if (message.message == WM_QUIT) {
      wxTheApp->ExitMainLoop(); // what wxApp::DoMessage does on WM_QUIT
      return;
    }
    wxTheApp->DoMessage(reinterpret_cast<WXMSG*>(&message));
  }

  DWORD ComputeWaitTimeoutMs()
  {
    const float remainingMs = wakeupTimerDur - ProbeWakeTimerMs();
    if (remainingMs < 0.0f) {
      return 0;
    }
    if (remainingMs > kMaxFiniteTimeoutMs) {
      return INFINITE;
    }
    return static_cast<DWORD>(FloorFrndintAdjustDown(remainingMs));
  }

  // Crash-report attachments (WinApp.cpp's CrashReportAttachmentRegistry): the report
  // directory and the files registered for it.
  class CrashReportAttachmentRegistry
  {
  public:
    void SetOutputDir(const wchar_t* const outputDir)
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mOutputDir = outputDir != nullptr ? outputDir : L"";
      if (!mOutputDir.empty() && mOutputDir.back() != L'\\' && mOutputDir.back() != L'/') {
        mOutputDir.push_back(L'/');
      }
    }

    [[nodiscard]] std::wstring GetOutputDirSnapshot() const
    {
      std::lock_guard<std::mutex> lock(mMutex);
      return mOutputDir;
    }

    void RegisterFile(const wchar_t* const file)
    {
      if (file == nullptr || file[0] == L'\0') {
        return;
      }
      std::lock_guard<std::mutex> lock(mMutex);
      for (const std::wstring& existing : mFiles) {
        if (existing == file) {
          return;
        }
      }
      mFiles.emplace_back(file);
    }

  private:
    mutable std::mutex mMutex;
    std::wstring mOutputDir;
    std::vector<std::wstring> mFiles;
  };

  CrashReportAttachmentRegistry sCrashReportAttachments;

  std::mutex& SymbolMutex()
  {
    static std::mutex sMutex;
    return sMutex;
  }

  std::string Demangle(const char* const name)
  {
    if (name == nullptr) {
      return std::string();
    }
    int status = 0;
    char* const demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (status != 0 || demangled == nullptr) {
      std::free(demangled);
      return name;
    }
    std::string result(demangled);
    std::free(demangled);
    return result;
  }

  // The splash screen WINX_InitSplash shows (WinApp.cpp sSplashScreen).
  std::unique_ptr<wxSplashScreen> sSplashScreen;
} // namespace

// WinApp.cpp's body: a movable global block under an open/empty/set/close cycle. Headless,
// OpenClipboard fails (no clipboard without the window system).
bool moho::WIN_CopyToClipboard(const wchar_t* const text)
{
  if (text == nullptr) {
    return false;
  }

  const std::size_t characterCount = std::wcslen(text) + 1;
  const std::size_t payloadBytes = characterCount * sizeof(wchar_t);

  if (::OpenClipboard(nullptr) == FALSE) {
    return false;
  }

  (void)::EmptyClipboard();
  HGLOBAL globalBlock = ::GlobalAlloc(GMEM_MOVEABLE, payloadBytes);
  if (globalBlock == nullptr) {
    ::CloseClipboard();
    return false;
  }

  void* const targetBuffer = ::GlobalLock(globalBlock);
  if (targetBuffer == nullptr) {
    ::GlobalFree(globalBlock);
    ::CloseClipboard();
    return false;
  }

  std::copy_n(text, characterCount, static_cast<wchar_t*>(targetBuffer));
  ::GlobalUnlock(globalBlock);

  if (::SetClipboardData(CF_UNICODETEXT, globalBlock) == nullptr) {
    ::GlobalFree(globalBlock);
    ::CloseClipboard();
    return false;
  }

  ::CloseClipboard();
  return true;
}

void moho::register_startTime()
{
  new (&wakeupTimer) gpg::time::Timer();
}

void moho::register_wakeupTimer()
{
  wakeupTimerDur = kInfiniteWakeupMs;
}

moho::CTaskStage& moho::WIN_GetBeforeEventsStage()
{
  static CTaskStage sBeforeEventsStage;
  return sBeforeEventsStage;
}

moho::CTaskStage& moho::WIN_GetBeforeWaitStage()
{
  static CTaskStage sBeforeWaitStage;
  return sBeforeWaitStage;
}

moho::CWaitHandleSet* moho::WIN_GetWaitHandleSet()
{
  static CWaitHandleSet sWaitHandleSet{};
  return &sWaitHandleSet;
}

msvc8::string moho::SPlatSymbolInfo::FormatResolvedLine() const
{
  return gpg::STR_Printf(
    "%s + %zu bytes (%s(%u) + %u bytes)", symbol.c_str(), symDis, filename.c_str(), lineNum, lineDis
  );
}

void moho::WIN_SetWakeupTimer(float milliseconds)
{
  float resolvedWakeup = 0.0f;
  if (milliseconds >= 0.0f) {
    const float absoluteWakeupMs = gpg::time::CyclesToMilliseconds(wakeupTimer.ElapsedCycles()) + milliseconds;
    if (wakeupTimerDur <= absoluteWakeupMs) {
      return;
    }
    milliseconds = absoluteWakeupMs;
    resolvedWakeup = milliseconds;
  }
  wakeupTimerDur = resolvedWakeup;
}

void moho::WIN_AppRequestExit()
{
  wxTheApp->ExitMainLoop();
}

moho::IWinApp* moho::WIN_GetCurrentApp()
{
  return sSupComApp;
}

wxWindow* moho::WIN_GetMainWindow()
{
  return sMainWindow;
}

void moho::WIN_SetMainWindow(wxWindow* const mainWindow)
{
  sMainWindow = mainWindow;
}

// The Windows loop (WinApp.cpp 0x004F20B0) without what has no POSIX counterpart: the
// low-level keyboard hook (Windows-key suppression), the Win9x check, InitCommonControls and
// CoInitialize. The wx stand-in has no native messages, so Pending() is false and each turn
// runs the idle processing once, then IWinApp::Main and the wait.
void moho::WIN_AppExecute(IWinApp* const app)
{
  if (app == nullptr) {
    return;
  }

  sSupComApp = app;
  // enterLoop = false: wx builds the MohoApp, and the loop below pumps it.
  wxEntry(0, 0, nullptr, 0, false);

  THREAD_SetAffinity(true);
  PLAT_Init();
  PLAT_CatchStructuredExceptions();
  wakeupTimer.Reset();
  wakeupTimerDur = kInfiniteWakeupMs;

  if (!app->Init()) {
    // ::TerminateProcess(::GetCurrentProcess(), 1u) in the Windows build.
    std::fflush(nullptr);
    std::_Exit(1);
  }
  wxTheApp->SetExitOnFrameDelete(true);
  wxGetApp().SetKeepGoing();

#if defined(_PC_24)
  platform::SetX87PrecisionControl(_PC_24);
#else
  platform::SetX87PrecisionControl(0x00020000u); // _PC_24; a no-op off x86
#endif

  bool success = true;
  bool acceptNewEvent = true;
  for (;;) {
    while (acceptNewEvent) {
      ::SleepEx(0, TRUE);
      WIN_GetBeforeEventsStage().UserFrame();
      acceptNewEvent = false;
    }

    if (wxTheApp->Pending()) {
      DispatchPendingMessage(); // WinApp.cpp: wxTheApp->Dispatch() could block, see there
      success = true;
      continue;
    }

    if (success) {
      success = wxTheApp->ProcessIdle();
      continue;
    }

    if (!wxGetApp().KeepGoing()) {
      break;
    }

    app->Main();
    success = true;
    acceptNewEvent = true;

    WIN_GetBeforeWaitStage().UserFrame();

    const DWORD timeoutMs = ComputeWaitTimeoutMs();
    wakeupTimerDur = kInfiniteWakeupMs;
    WIN_GetWaitHandleSet()->MsgWaitEx(timeoutMs);
  }

  app->Destroy();
  WINX_Exit();
  PLAT_Exit();

  if (wxTheApp != nullptr) {
    bool moreIdle = true;
    for (;;) {
      if (wxTheApp->Pending()) {
        wxTheApp->Dispatch();
        moreIdle = true;
        continue;
      }
      if (!moreIdle) {
        break;
      }
      moreIdle = wxTheApp->ProcessIdle();
    }
    wxTheApp->OnExit();
    wxApp::CleanUp();
  }

  RES_Exit();
  sSupComApp = nullptr;
}

// No symbol handler to initialise: backtrace/dladdr need no setup.
void moho::PLAT_Init() {}

void moho::PLAT_CatchStructuredExceptions()
{
  FAF_POSIX_STUB("PLAT_CatchStructuredExceptions: no crash handler on POSIX yet");
}

void moho::PLAT_Exit() {}

void moho::PLAT_InitErrorReportOutputDir(const wchar_t* const outputDir)
{
  sCrashReportAttachments.SetOutputDir(outputDir);
}

void moho::PLAT_RegisterFileForErrorReport(const wchar_t* const file)
{
  sCrashReportAttachments.RegisterFile(file);
}

msvc8::string moho::LOG_GetRecentLines(const std::uint32_t maxLines)
{
  (void)maxLines;
  return gpg::GetRecentLogLines();
}

void moho::PLAT_CreateGameLogForReport()
{
  const msvc8::string recentLogLines = LOG_GetRecentLines(0);
  const char* const appShortName =
    (sSupComApp != nullptr && !sSupComApp->shortName.empty()) ? sSupComApp->shortName.c_str() : "SupCom";
  const std::wstring logFilePath =
    sCrashReportAttachments.GetOutputDirSnapshot() + gpg::STR_Utf8ToWide(appShortName) + L".sclog";
  const msvc8::string utf8Path = gpg::STR_WideToUtf8(logFilePath.c_str());

  std::FILE* const file = std::fopen(utf8Path.c_str(), "wb");
  if (file == nullptr) {
    gpg::Warnf(
      "PLAT_CreateGameLogForReport(\"%s\") log file creation failed: %s", utf8Path.c_str(), std::strerror(errno)
    );
    return;
  }
  if (std::fwrite(recentLogLines.c_str(), 1, recentLogLines.size(), file) != recentLogLines.size()) {
    gpg::Warnf(
      "PLAT_CreateGameLogForReport(\"%s\") log file writing failed: %s", utf8Path.c_str(), std::strerror(errno)
    );
    std::fclose(file);
    return;
  }
  std::fclose(file);
  PLAT_RegisterFileForErrorReport(logFilePath.c_str());
}

// The calling thread's stack (a non-null CPU context, which only the crash handler passes,
// is not walked). As in the Windows build, frames after the first are moved back into their
// call instruction.
std::uint32_t moho::PLAT_GetCallStack(
  void* const contextRecord, const std::uint32_t maxFrames, std::uintptr_t* const outFrames
)
{
  if (outFrames == nullptr || maxFrames == 0) {
    return 0;
  }
  if (contextRecord != nullptr) {
    FAF_POSIX_STUB("PLAT_GetCallStack: walking a saved CPU context is not implemented on POSIX");
    return 0;
  }

  std::vector<void*> frames(static_cast<std::size_t>(maxFrames) + 1);
  const int captured = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  // Frame 0 is this function; the Windows walk starts at its caller too.
  std::uint32_t frameCount = 0;
  for (int index = 1; index < captured && frameCount < maxFrames; ++index) {
    std::uintptr_t address = reinterpret_cast<std::uintptr_t>(frames[static_cast<std::size_t>(index)]);
    if (address == 0) {
      break;
    }
    if (frameCount != 0) {
      address -= 1;
    }
    outFrames[frameCount++] = address;
  }
  return frameCount;
}

bool moho::PLAT_GetSymbolInfo(const std::uintptr_t address, SPlatSymbolInfo* const outInfo)
{
  if (outInfo == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(SymbolMutex());
  Dl_info info{};
  if (::dladdr(reinterpret_cast<const void*>(address), &info) == 0 || info.dli_sname == nullptr) {
    return false;
  }
  outInfo->addr = address;
  outInfo->symbol.assign_owned(Demangle(info.dli_sname).c_str());
  outInfo->symDis = address - reinterpret_cast<std::uintptr_t>(info.dli_saddr);
  // No line tables without a debug-info reader: the image stands in for the file name.
  outInfo->filename.assign_owned(info.dli_fname != nullptr ? info.dli_fname : "(Unknown)");
  outInfo->lineNum = 0;
  outInfo->lineDis = 0;
  return true;
}

msvc8::string moho::PLAT_FormatCallstack(
  std::int32_t firstFrame, const std::int32_t endFrame, const std::uintptr_t* const frames
)
{
  msvc8::string formatted;
  formatted.assign_owned("");
  if (frames == nullptr || firstFrame >= endFrame) {
    return formatted;
  }
  if (firstFrame < 0) {
    firstFrame = 0;
  }

  std::string assembled;
  for (std::int32_t frameIndex = firstFrame; frameIndex < endFrame; ++frameIndex) {
    SPlatSymbolInfo symbolInfo{};
    if (PLAT_GetSymbolInfo(frames[frameIndex], &symbolInfo)) {
      assembled.append("\t");
      assembled.append(symbolInfo.FormatResolvedLine().c_str());
      assembled.append("\r\n");
    } else {
      const msvc8::string line =
        gpg::STR_Printf("\tUnknown symbol (address %p)\r\n", reinterpret_cast<const void*>(frames[frameIndex]));
      assembled.append(line.c_str());
    }
  }
  formatted.assign_owned(assembled);
  return formatted;
}

// Itanium names demangle; anything else (an MSVC-decorated name) comes back unchanged, which
// is what UnDecorateSymbolName does with a name it cannot parse.
msvc8::string moho::PLAT_UnDecorateSymbolName(const char* const name, const bool stripLeadingUnderscore)
{
  (void)stripLeadingUnderscore;
  msvc8::string result;
  result.assign_owned(name != nullptr ? Demangle(name).c_str() : "");
  return result;
}

// There is no registry: every key reads as absent and cannot be created, with the Windows
// build's log lines for those failures.
bool moho::PLAT_SetRegistryValue(
  const char* const keyPath, const std::uint8_t* const data, const std::uint32_t dataSize, const std::uint32_t valueType
)
{
  (void)data;
  (void)dataSize;
  (void)valueType;
  if (keyPath == nullptr) {
    return false;
  }
  FAF_POSIX_STUB("PLAT_SetRegistryValue: no registry on POSIX");
  gpg::Logf("PLAT_SetRegistryValue: Unable to create registry key \"%s\"", keyPath);
  return false;
}

bool moho::PLAT_SetRegistryValueDword(const char* const keyPath, const std::uint32_t value)
{
  return PLAT_SetRegistryValue(keyPath, reinterpret_cast<const std::uint8_t*>(&value), sizeof(value), 4u /* REG_DWORD */);
}

bool moho::PLAT_SetRegistryValueString(const char* const value, const char* const keyPath)
{
  const char* const safeValue = (value != nullptr) ? value : "";
  return PLAT_SetRegistryValue(
    keyPath,
    reinterpret_cast<const std::uint8_t*>(safeValue),
    static_cast<std::uint32_t>(std::strlen(safeValue) + 1U),
    1u /* REG_SZ */
  );
}

std::uint32_t moho::PLAT_GetRegistryValue(const char* const keyPath, void* const outData, const std::uint32_t maxDataBytes)
{
  (void)maxDataBytes;
  if (keyPath == nullptr || outData == nullptr) {
    return 0;
  }
  FAF_POSIX_STUB("PLAT_GetRegistryValue: no registry on POSIX");
  gpg::Logf("PLAT_GetRegistryValue: Unable to open registry key \"%s\"", keyPath);
  return 0;
}

// WinApp.cpp's body: CF_UNICODETEXT from the clipboard (owned by the main window when there
// is one), as UTF-8; empty when there is none (always, headless).
msvc8::string moho::WIN_GetClipboardText()
{
  const HWND ownerWindow = sMainWindow != nullptr
    ? reinterpret_cast<HWND>(static_cast<std::uintptr_t>(sMainWindow->GetHandle()))
    : nullptr;

  if (::IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE || ::OpenClipboard(ownerWindow) == FALSE) {
    return {};
  }

  msvc8::string clipboardText{};
  const HANDLE clipboardData = ::GetClipboardData(CF_UNICODETEXT);
  if (clipboardData != nullptr) {
    const wchar_t* const lockedText = static_cast<const wchar_t*>(::GlobalLock(clipboardData));
    if (lockedText != nullptr) {
      clipboardText = gpg::STR_WideToUtf8(lockedText);
      (void)::GlobalUnlock(clipboardData);
    }
  }

  (void)::CloseClipboard();
  return clipboardText;
}

msvc8::string moho::WIN_GetLastError()
{
  const DWORD errorCode = ::GetLastError();
  return gpg::STR_Printf("Unknown error 0x%08x", errorCode);
}

// No dialog: the summary and call stack go to the log (as the Windows build also does) and
// to stderr.
void moho::WIN_ShowCrashDialog(
  std::int32_t skipCallstackFrames,
  _EXCEPTION_POINTERS* const exceptionInfo,
  const gpg::StrArg caption,
  const gpg::StrArg summaryText
)
{
  std::ostringstream details;
  details << (summaryText != nullptr ? summaryText : "") << "\n\n";
  const msvc8::string args = CFG_GetArgs();
  details << "Cmd line arguments : " << args.c_str() << "\n\n";
  details << "Callstack:\n";

  if (exceptionInfo == nullptr) {
    skipCallstackFrames += 2;
  }
  std::uintptr_t stackFrames[64]{};
  const std::uint32_t frameCount = exceptionInfo == nullptr ? PLAT_GetCallStack(nullptr, 64, stackFrames) : 0u;
  const std::uint32_t firstFrame =
    skipCallstackFrames > 0 ? static_cast<std::uint32_t>(skipCallstackFrames) : static_cast<std::uint32_t>(0);
  gpg::Logf("CRASH: %s", summaryText != nullptr ? static_cast<const char*>(summaryText) : "<no summary>");
  if (frameCount <= firstFrame) {
    details << "    unavailable.\n";
    gpg::Logf("CRASH callstack: unavailable.");
  } else {
    const msvc8::string callstackText =
      PLAT_FormatCallstack(static_cast<std::int32_t>(firstFrame), static_cast<std::int32_t>(frameCount), stackFrames);
    details << callstackText.c_str();
    gpg::Logf("CRASH callstack:\n%s", callstackText.c_str());
  }
  details << "\nLast 100 lines of log...\n\n" << LOG_GetRecentLines(100u).c_str();

  FAF_POSIX_STUB("WIN_ShowCrashDialog: no crash dialog on POSIX, report written to stderr");
  std::fprintf(stderr, "[%s]\n%s\n", caption != nullptr ? static_cast<const char*>(caption) : "Crash", details.str().c_str());
  std::fflush(stderr);
}

void moho::WIN_OkBox(const gpg::StrArg caption, const gpg::StrArg text)
{
  const std::wstring wideCaption = gpg::STR_Utf8ToWide(caption);
  const std::wstring wideText = gpg::STR_Utf8ToWide(text);
  (void)::MessageBoxW(nullptr, wideText.c_str(), wideCaption.c_str(), 0x40000u);
}

bool moho::WIN_YesNoBox(const gpg::StrArg caption, const gpg::StrArg text)
{
  const std::wstring wideCaption = gpg::STR_Utf8ToWide(caption);
  const std::wstring wideText = gpg::STR_Utf8ToWide(text);
  return ::MessageBoxW(nullptr, wideText.c_str(), wideCaption.c_str(), 0x40004u) == IDYES;
}

void moho::WINX_Exit()
{
  for (std::size_t index = 0; index < managedWindows.size(); ++index) {
    if (WWinManagedDialog* const dialog = managedWindows[index].GetObjectPtr(); dialog != nullptr) {
      (void)dialog->Destroy();
      managedWindows[index].UnlinkFromOwnerChain();
    }
  }
  for (std::size_t index = 0; index < managedFrames.size(); ++index) {
    if (WWinManagedFrame* const frame = managedFrames[index].GetObjectPtr(); frame != nullptr) {
      (void)frame->Destroy();
      managedFrames[index].UnlinkFromOwnerChain();
    }
  }
}

wxString moho::WINX_Printf(const char* const format, ...)
{
  va_list args;
  va_start(args, format);
  const char* formatCursor = format;
  const msvc8::string formatted = gpg::STR_Va(formatCursor, args);
  va_end(args);
  return wxString(gpg::STR_Utf8ToWide(formatted.c_str()).c_str());
}

// The splash image cannot be loaded by the wx stand-in (wxBitmap::LoadFile fails), so, as
// in the Windows build when the file is missing, no splash screen is shown.
void moho::WINX_InitSplash(const gpg::StrArg filename)
{
  sSplashScreen.reset();
  wxBitmap bitmap;
  if (!bitmap.LoadFile(wxString(filename, wxConvUTF8), wxBITMAP_TYPE_PNG)) {
    return;
  }
  sSplashScreen.reset(new wxSplashScreen(
    bitmap, wxSPLASH_CENTRE_ON_SCREEN, 0, nullptr, -1, wxDefaultPosition, wxSize(1024, 768),
    wxSIMPLE_BORDER | wxSTAY_ON_TOP
  ));
}

void moho::WINX_PrecreateLogWindow()
{
  if (sLogWindowTarget.dialog != nullptr) {
    return;
  }
  WWinLogWindow* const createdLogWindow = new WWinLogWindow();
  boost::mutex::scoped_lock lock(sLogWindowTarget.lock);
  sLogWindowTarget.dialog = createdLogWindow;
}

void moho::WINX_ExitSplash()
{
  sSplashScreen.reset();
}
