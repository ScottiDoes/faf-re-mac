// Win32 on POSIX: Window (declarations in compat/Window.h).
//
// While the window system is active (faf_posix::shell::Init, the game) every call goes to
// the shell's implementation (shell/*.cpp: real windows, the message queue, input, displays,
// the clipboard). Otherwise - headless runs - the process has no native window, message
// queue or clipboard, and every call answers for that state with Windows' return values;
// see compat/Window.h for the conventions. The headless answers are unchanged from before
// the shell existed.

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "platform/posix/shell/ShellInternal.h"


namespace
{
  bool ShellActive() noexcept
  {
    return faf_posix::shell::IsActive();
  }

  constexpr LONG kScreenWidth = 1920;
  constexpr LONG kScreenHeight = 1080;

  // Stand-ins for the handles Windows always has: the desktop window and the one monitor.
  HWND DesktopWindow() noexcept
  {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x00010010u));
  }
  HMONITOR PrimaryMonitor() noexcept
  {
    return reinterpret_cast<HMONITOR>(static_cast<std::uintptr_t>(0x00010001u));
  }

  RECT ScreenRect() noexcept
  {
    return RECT{0, 0, kScreenWidth, kScreenHeight};
  }

  std::string Utf8FromWide(LPCWSTR text)
  {
    std::string out;
    if (text == nullptr) {
      return out;
    }
    for (; *text != L'\0'; ++text) {
      const auto cp = static_cast<std::uint32_t>(*text);
      if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }
    return out;
  }

  int AnswerMessageBox(const char* caption, const char* text, UINT type)
  {
    FAF_POSIX_STUB("MessageBox: no message boxes on POSIX yet, text goes to stderr");
    std::fprintf(stderr, "[MessageBox] %s: %s\n", caption != nullptr ? caption : "", text != nullptr ? text : "");
    std::fflush(stderr);
    switch (type & MB_TYPEMASK) {
      case MB_OKCANCEL:
      case MB_RETRYCANCEL:
        return IDCANCEL;
      case MB_ABORTRETRYIGNORE:
        return IDABORT;
      case MB_YESNO:
      case MB_YESNOCANCEL:
        return IDNO;
      default:
        return IDOK;
    }
  }

  struct CursorState
  {
    std::atomic<HCURSOR> cursor{nullptr};
    std::atomic<int> displayCount{0};
    std::atomic<LONG> x{0};
    std::atomic<LONG> y{0};
    std::atomic<bool> clipped{false};
    RECT clip{0, 0, kScreenWidth, kScreenHeight};
  };

  CursorState& Cursor()
  {
    static CursorState state;
    return state;
  }

  std::uint64_t MonotonicMilliseconds()
  {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  }
} // namespace

// --- Message boxes -----------------------------------------------------------------------------
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) noexcept
{
  (void)hWnd;
  return AnswerMessageBox(Utf8FromWide(lpCaption).c_str(), Utf8FromWide(lpText).c_str(), uType);
}

int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) noexcept
{
  (void)hWnd;
  return AnswerMessageBox(lpCaption, lpText, uType);
}

BOOL MessageBeep(UINT uType) noexcept
{
  (void)uType;
  return TRUE;
}

// --- Messages ----------------------------------------------------------------------------------
BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  }
  (void)lpMsg;
  (void)hWnd;
  (void)wMsgFilterMin;
  (void)wMsgFilterMax;
  (void)wRemoveMsg;
  return FALSE;
}

BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept
{
  return PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

// No message will ever arrive, so a GetMessage loop is told to quit instead of blocking.
BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  }
  (void)hWnd;
  (void)wMsgFilterMin;
  (void)wMsgFilterMax;
  FAF_POSIX_STUB("GetMessageW: no message queue on POSIX yet, returning WM_QUIT");
  if (lpMsg != nullptr) {
    std::memset(lpMsg, 0, sizeof(*lpMsg));
    lpMsg->message = WM_QUIT;
  }
  return FALSE;
}

BOOL TranslateMessage(const MSG* lpMsg) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::TranslateMessage(lpMsg);
  }
  (void)lpMsg;
  return FALSE;
}

LRESULT DispatchMessageW(const MSG* lpMsg) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::DispatchMessageW(lpMsg);
  }
  (void)lpMsg;
  return 0;
}

LRESULT DispatchMessageA(const MSG* lpMsg) noexcept
{
  return DispatchMessageW(lpMsg);
}

void PostQuitMessage(int nExitCode) noexcept
{
  if (ShellActive()) {
    faf_posix::shell::win32::PostQuitMessage(nExitCode);
    return;
  }
  (void)nExitCode;
}

BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::PostMessageW(hWnd, Msg, wParam, lParam);
  }
  (void)hWnd;
  (void)Msg;
  (void)wParam;
  (void)lParam;
  return FALSE;
}

BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  return PostMessageW(hWnd, Msg, wParam, lParam);
}

BOOL PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::PostThreadMessageW(idThread, Msg, wParam, lParam);
  }
  (void)idThread;
  (void)Msg;
  (void)wParam;
  (void)lParam;
  return FALSE;
}

LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SendMessageW(hWnd, Msg, wParam, lParam);
  }
  (void)hWnd;
  (void)Msg;
  (void)wParam;
  (void)lParam;
  return 0;
}

LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  return SendMessageW(hWnd, Msg, wParam, lParam);
}

LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::DefWindowProcW(hWnd, Msg, wParam, lParam);
  }
  (void)hWnd;
  (void)Msg;
  (void)wParam;
  (void)lParam;
  return 0;
}

LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept
{
  return lpPrevWndFunc != nullptr ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

DWORD GetQueueStatus(UINT flags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetQueueStatus(flags);
  }
  (void)flags;
  return 0;
}

DWORD GetMessageTime() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetMessageTime();
  }
  return static_cast<DWORD>(MonotonicMilliseconds());
}

DWORD GetMessagePos() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetMessagePos();
  }
  return static_cast<DWORD>(MAKELONG(static_cast<WORD>(Cursor().x.load()), static_cast<WORD>(Cursor().y.load())));
}

// Nothing can arrive without a window system: return at once rather than block forever.
BOOL WaitMessage() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::WaitMessage();
  }
  FAF_POSIX_STUB("WaitMessage: no message queue without the window system");
  return FALSE;
}

// --- Window classes ----------------------------------------------------------------------------
// Kept with or without the window system: a class is only data until a window uses it.
namespace
{
  ATOM RegisterWindowClass(LPCWSTR name, UINT style, WNDPROC proc, HCURSOR cursor, HINSTANCE instance) noexcept
  {
    if (name == nullptr || reinterpret_cast<std::uintptr_t>(name) <= 0xFFFFu || name[0] == L'\0') {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    return faf_posix::shell::RegisterWindowClass(name, style, proc, cursor, instance);
  }
} // namespace

ATOM RegisterClassW(const WNDCLASSW* lpWndClass) noexcept
{
  if (lpWndClass == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return RegisterWindowClass(lpWndClass->lpszClassName, lpWndClass->style, lpWndClass->lpfnWndProc, lpWndClass->hCursor,
                             lpWndClass->hInstance);
}

ATOM RegisterClassExW(const WNDCLASSEXW* lpWndClass) noexcept
{
  if (lpWndClass == nullptr || lpWndClass->cbSize < sizeof(WNDCLASSEXW)) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return RegisterWindowClass(lpWndClass->lpszClassName, lpWndClass->style, lpWndClass->lpfnWndProc, lpWndClass->hCursor,
                             lpWndClass->hInstance);
}

BOOL UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) noexcept
{
  (void)hInstance;
  return faf_posix::shell::UnregisterWindowClass(lpClassName) ? TRUE : FALSE;
}

// --- Windows -----------------------------------------------------------------------------------
HWND CreateWindowExW(
  DWORD dwExStyle,
  LPCWSTR lpClassName,
  LPCWSTR lpWindowName,
  DWORD dwStyle,
  int X,
  int Y,
  int nWidth,
  int nHeight,
  HWND hWndParent,
  HMENU hMenu,
  HINSTANCE hInstance,
  LPVOID lpParam
) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
  }
  (void)dwExStyle;
  (void)lpClassName;
  (void)lpWindowName;
  (void)dwStyle;
  (void)X;
  (void)Y;
  (void)nWidth;
  (void)nHeight;
  (void)hWndParent;
  (void)hMenu;
  (void)hInstance;
  (void)lpParam;
  FAF_POSIX_STUB("CreateWindowExW: no native windows on POSIX yet");
  return nullptr;
}

BOOL DestroyWindow(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::DestroyWindow(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

BOOL IsWindow(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsWindow(hWnd);
  }
  return hWnd != nullptr && hWnd == DesktopWindow() ? TRUE : FALSE;
}

BOOL IsWindowVisible(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsWindowVisible(hWnd);
  }
  return IsWindow(hWnd);
}

BOOL IsWindowEnabled(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsWindowEnabled(hWnd);
  }
  return IsWindow(hWnd);
}

BOOL IsIconic(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsIconic(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

BOOL IsZoomed(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsZoomed(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

BOOL ShowWindow(HWND hWnd, int nCmdShow) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ShowWindow(hWnd, nCmdShow);
  }
  (void)hWnd;
  (void)nCmdShow;
  return FALSE;
}

BOOL UpdateWindow(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::UpdateWindow(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

BOOL EnableWindow(HWND hWnd, BOOL bEnable) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::EnableWindow(hWnd, bEnable);
  }
  (void)hWnd;
  (void)bEnable;
  return FALSE;
}

BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::MoveWindow(hWnd, X, Y, nWidth, nHeight, bRepaint);
  }
  (void)hWnd;
  (void)X;
  (void)Y;
  (void)nWidth;
  (void)nHeight;
  (void)bRepaint;
  return FALSE;
}

BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
  }
  (void)hWnd;
  (void)hWndInsertAfter;
  (void)X;
  (void)Y;
  (void)cx;
  (void)cy;
  (void)uFlags;
  return FALSE;
}

BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetWindowRect(hWnd, lpRect);
  }
  if (lpRect == nullptr || hWnd != DesktopWindow()) {
    return FALSE;
  }
  *lpRect = ScreenRect();
  return TRUE;
}

BOOL GetClientRect(HWND hWnd, LPRECT lpRect) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetClientRect(hWnd, lpRect);
  }
  return GetWindowRect(hWnd, lpRect);
}

// The frame adds nothing: there is no non-client area without a native window.
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::AdjustWindowRectEx(lpRect, dwStyle, bMenu, 0);
  }
  (void)dwStyle;
  (void)bMenu;
  return lpRect != nullptr ? TRUE : FALSE;
}

BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
  }
  (void)dwExStyle;
  return AdjustWindowRect(lpRect, dwStyle, bMenu);
}

BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ScreenToClient(hWnd, lpPoint);
  }
  return hWnd == DesktopWindow() && lpPoint != nullptr ? TRUE : FALSE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ClientToScreen(hWnd, lpPoint);
  }
  return hWnd == DesktopWindow() && lpPoint != nullptr ? TRUE : FALSE;
}

BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::InvalidateRect(hWnd, lpRect, bErase);
  }
  (void)lpRect;
  (void)bErase;
  return hWnd == nullptr ? TRUE : FALSE;
}

BOOL ValidateRect(HWND hWnd, const RECT* lpRect) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ValidateRect(hWnd, lpRect);
  }
  (void)lpRect;
  return hWnd == nullptr ? TRUE : FALSE;
}

BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetWindowTextW(hWnd, lpString);
  }
  (void)hWnd;
  (void)lpString;
  return FALSE;
}

BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString) noexcept
{
  if (ShellActive()) {
    // ANSI text is taken as UTF-8 (the engine's narrow strings are).
    std::wstring wide;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(lpString != nullptr ? lpString : ""); *p != 0;) {
      std::uint32_t cp = *p++;
      int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
      cp &= extra == 3 ? 0x07u : extra == 2 ? 0x0Fu : extra == 1 ? 0x1Fu : 0xFFu;
      for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) {
        cp = (cp << 6) | (*p++ & 0x3Fu);
      }
      wide.push_back(static_cast<wchar_t>(cp));
    }
    return faf_posix::shell::win32::SetWindowTextW(hWnd, wide.c_str());
  }
  (void)hWnd;
  (void)lpString;
  return FALSE;
}

int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetWindowTextW(hWnd, lpString, nMaxCount);
  }
  (void)hWnd;
  if (lpString != nullptr && nMaxCount > 0) {
    lpString[0] = L'\0';
  }
  return 0;
}

LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetWindowLongPtrW(hWnd, nIndex);
  }
  (void)hWnd;
  (void)nIndex;
  return 0;
}

LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetWindowLongPtrW(hWnd, nIndex, dwNewLong);
  }
  (void)hWnd;
  (void)nIndex;
  (void)dwNewLong;
  return 0;
}

LONG GetWindowLongW(HWND hWnd, int nIndex) noexcept
{
  return static_cast<LONG>(GetWindowLongPtrW(hWnd, nIndex));
}

LONG SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) noexcept
{
  return static_cast<LONG>(SetWindowLongPtrW(hWnd, nIndex, dwNewLong));
}

HWND GetParent(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetParent(hWnd);
  }
  (void)hWnd;
  return nullptr;
}

HWND SetParent(HWND hWndChild, HWND hWndNewParent) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetParent(hWndChild, hWndNewParent);
  }
  (void)hWndChild;
  (void)hWndNewParent;
  return nullptr;
}

HWND GetAncestor(HWND hwnd, UINT gaFlags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetAncestor(hwnd, gaFlags);
  }
  (void)hwnd;
  (void)gaFlags;
  return nullptr;
}

HWND GetWindow(HWND hWnd, UINT uCmd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetWindow(hWnd, uCmd);
  }
  (void)hWnd;
  (void)uCmd;
  return nullptr;
}

BOOL IsChild(HWND hWndParent, HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsChild(hWndParent, hWnd);
  }
  (void)hWndParent;
  (void)hWnd;
  return FALSE;
}

BOOL BringWindowToTop(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::BringWindowToTop(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

HWND ChildWindowFromPoint(HWND hWndParent, POINT Point) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ChildWindowFromPoint(hWndParent, Point);
  }
  (void)Point;
  return hWndParent == DesktopWindow() ? hWndParent : nullptr;
}

HWND GetDesktopWindow() noexcept
{
  return DesktopWindow();
}

HWND GetForegroundWindow() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetForegroundWindow();
  }
  return nullptr;
}

BOOL SetForegroundWindow(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetForegroundWindow(hWnd);
  }
  (void)hWnd;
  return FALSE;
}

HWND GetActiveWindow() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetActiveWindow();
  }
  return nullptr;
}

HWND SetActiveWindow(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetActiveWindow(hWnd);
  }
  (void)hWnd;
  return nullptr;
}

HWND GetFocus() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetFocus();
  }
  return nullptr;
}

HWND SetFocus(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetFocus(hWnd);
  }
  (void)hWnd;
  return nullptr;
}

HWND GetCapture() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetCapture();
  }
  return nullptr;
}

HWND SetCapture(HWND hWnd) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetCapture(hWnd);
  }
  (void)hWnd;
  return nullptr;
}

BOOL ReleaseCapture() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ReleaseCapture();
  }
  return TRUE;
}

HWND WindowFromPoint(POINT Point) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::WindowFromPoint(Point);
  }
  (void)Point;
  return nullptr;
}

BOOL FlashWindow(HWND hWnd, BOOL bInvert) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::FlashWindow(hWnd, bInvert);
  }
  (void)hWnd;
  (void)bInvert;
  return FALSE;
}

DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) noexcept
{
  (void)hWnd;
  if (lpdwProcessId != nullptr) {
    *lpdwProcessId = 0;
  }
  return 0;
}

// --- Menus and dialogs -------------------------------------------------------------------------
HMENU GetSystemMenu(HWND hWnd, BOOL bRevert) noexcept
{
  (void)hWnd;
  (void)bRevert;
  return nullptr;
}

BOOL AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem) noexcept
{
  (void)hMenu;
  (void)uFlags;
  (void)uIDNewItem;
  (void)lpNewItem;
  return FALSE;
}

BOOL DrawMenuBar(HWND hWnd) noexcept
{
  (void)hWnd;
  return FALSE;
}

INT_PTR DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) noexcept
{
  (void)hInstance;
  (void)lpTemplateName;
  (void)hWndParent;
  (void)lpDialogFunc;
  (void)dwInitParam;
  FAF_POSIX_STUB("DialogBoxParamW: no dialogs on POSIX yet");
  return -1;
}

BOOL EndDialog(HWND hDlg, INT_PTR nResult) noexcept
{
  (void)hDlg;
  (void)nResult;
  return FALSE;
}

HWND GetDlgItem(HWND hDlg, int nIDDlgItem) noexcept
{
  (void)hDlg;
  (void)nIDDlgItem;
  return nullptr;
}

BOOL SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString) noexcept
{
  (void)hDlg;
  (void)nIDDlgItem;
  (void)lpString;
  return FALSE;
}

void InitCommonControls() noexcept {}

// --- Input -------------------------------------------------------------------------------------
SHORT GetKeyState(int nVirtKey) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetKeyState(nVirtKey);
  }
  (void)nVirtKey;
  return 0;
}

SHORT GetAsyncKeyState(int vKey) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetAsyncKeyState(vKey);
  }
  (void)vKey;
  return 0;
}

BOOL GetKeyboardState(PBYTE lpKeyState) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetKeyboardState(lpKeyState);
  }
  if (lpKeyState == nullptr) {
    return FALSE;
  }
  std::memset(lpKeyState, 0, 256);
  return TRUE;
}

// MAPVK_VK_TO_CHAR for the keys whose virtual-key code is their character; the scan-code
// mappings need a keyboard layout, which arrives with the SDL3 shell.
UINT MapVirtualKeyW(UINT uCode, UINT uMapType) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::MapVirtualKeyW(uCode, uMapType);
  }
  if (uMapType == MAPVK_VK_TO_CHAR) {
    if ((uCode >= '0' && uCode <= '9') || (uCode >= 'A' && uCode <= 'Z') || uCode == VK_SPACE) {
      return uCode;
    }
    if (uCode >= VK_NUMPAD0 && uCode <= VK_NUMPAD9) {
      return '0' + (uCode - VK_NUMPAD0);
    }
  }
  return 0;
}

UINT MapVirtualKeyA(UINT uCode, UINT uMapType) noexcept
{
  return MapVirtualKeyW(uCode, uMapType);
}

HCURSOR SetCursor(HCURSOR hCursor) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetCursor(hCursor);
  }
  return Cursor().cursor.exchange(hCursor);
}

HCURSOR GetCursor() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetCursor();
  }
  return Cursor().cursor.load();
}

int ShowCursor(BOOL bShow) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ShowCursor(bShow);
  }
  return Cursor().displayCount.fetch_add(bShow ? 1 : -1) + (bShow ? 1 : -1);
}

BOOL GetCursorPos(LPPOINT lpPoint) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetCursorPos(lpPoint);
  }
  if (lpPoint == nullptr) {
    return FALSE;
  }
  lpPoint->x = Cursor().x.load();
  lpPoint->y = Cursor().y.load();
  return TRUE;
}

BOOL SetCursorPos(int X, int Y) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetCursorPos(X, Y);
  }
  Cursor().x.store(X);
  Cursor().y.store(Y);
  return TRUE;
}

BOOL GetCursorInfo(PCURSORINFO pci) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetCursorInfo(pci);
  }
  if (pci == nullptr) {
    return FALSE;
  }
  pci->flags = Cursor().displayCount.load() >= 0 ? CURSOR_SHOWING : 0;
  pci->hCursor = Cursor().cursor.load();
  pci->ptScreenPos.x = Cursor().x.load();
  pci->ptScreenPos.y = Cursor().y.load();
  return TRUE;
}

BOOL ClipCursor(const RECT* lpRect) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::ClipCursor(lpRect);
  }
  CursorState& state = Cursor();
  state.clipped.store(lpRect != nullptr);
  state.clip = lpRect != nullptr ? *lpRect : ScreenRect();
  return TRUE;
}

BOOL GetClipCursor(LPRECT lpRect) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetClipCursor(lpRect);
  }
  if (lpRect == nullptr) {
    return FALSE;
  }
  *lpRect = Cursor().clipped.load() ? Cursor().clip : ScreenRect();
  return TRUE;
}

// System cursors and icons come back as distinct non-null handles (their resource ids), so
// callers can compare and pass them around; nothing ever draws them.
HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) noexcept
{
  if (hInstance != nullptr) {
    return nullptr;
  }
  return reinterpret_cast<HCURSOR>(const_cast<WCHAR*>(lpCursorName));
}

HICON LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) noexcept
{
  if (hInstance != nullptr) {
    return nullptr;
  }
  return reinterpret_cast<HICON>(const_cast<WCHAR*>(lpIconName));
}

HICON CreateIconIndirect(PICONINFO piconinfo) noexcept
{
  (void)piconinfo;
  FAF_POSIX_STUB("CreateIconIndirect: no native cursors or icons on POSIX yet");
  return nullptr;
}

BOOL DestroyCursor(HCURSOR hCursor) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::DestroyCursor(hCursor);
  }
  (void)hCursor;
  return TRUE;
}

BOOL DestroyIcon(HICON hIcon) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::DestroyCursor(hIcon);
  }
  (void)hIcon;
  return TRUE;
}

UINT GetDoubleClickTime() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetDoubleClickTime();
  }
  return 500;
}

// --- Hooks -------------------------------------------------------------------------------------
HHOOK SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) noexcept
{
  (void)hmod;
  (void)dwThreadId;
  if (lpfn == nullptr) {
    return nullptr;
  }
  // Windows' one caller (WinApp.cpp, WH_KEYBOARD_LL) suppresses the Windows key in full
  // screen; macOS has no counterpart (the Command key opens no menu over a game).
  FAF_POSIX_STUB("SetWindowsHookExW: hooks are accepted and never called");
  // A distinct non-null handle per hook type; the hook is never called.
  return reinterpret_cast<HHOOK>(static_cast<std::uintptr_t>(0x00020000u + static_cast<unsigned>(idHook)));
}

BOOL UnhookWindowsHookEx(HHOOK hhk) noexcept
{
  return hhk != nullptr ? TRUE : FALSE;
}

LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam) noexcept
{
  (void)hhk;
  (void)nCode;
  (void)wParam;
  (void)lParam;
  return 0;
}

// --- System settings ---------------------------------------------------------------------------
BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) noexcept
{
  if (ShellActive()) {
    BOOL handled = FALSE;
    const BOOL result = faf_posix::shell::win32::SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni, &handled);
    if (handled) {
      return result;
    }
  }
  (void)fWinIni;
  switch (uiAction) {
    case SPI_GETSCREENSAVEACTIVE:
    case SPI_GETBEEP:
    case SPI_GETSCREENREADER:
      if (pvParam == nullptr) {
        return FALSE;
      }
      *static_cast<BOOL*>(pvParam) = FALSE;
      return TRUE;
    case SPI_GETWORKAREA:
      if (pvParam == nullptr) {
        return FALSE;
      }
      *static_cast<RECT*>(pvParam) = ScreenRect();
      return TRUE;
    case SPI_GETWHEELSCROLLLINES:
      if (pvParam == nullptr) {
        return FALSE;
      }
      *static_cast<UINT*>(pvParam) = 3;
      return TRUE;
    case SPI_GETMOUSESPEED:
      if (pvParam == nullptr) {
        return FALSE;
      }
      *static_cast<int*>(pvParam) = 10;
      return TRUE;
    case SPI_GETMOUSE:
      if (pvParam == nullptr) {
        return FALSE;
      }
      static_cast<int*>(pvParam)[0] = 6;
      static_cast<int*>(pvParam)[1] = 10;
      static_cast<int*>(pvParam)[2] = 1;
      return TRUE;
    case SPI_GETFOREGROUNDLOCKTIMEOUT:
      if (pvParam == nullptr) {
        return FALSE;
      }
      *static_cast<DWORD*>(pvParam) = 200000;
      return TRUE;
    case SPI_GETSTICKYKEYS:
    case SPI_GETTOGGLEKEYS:
    case SPI_GETFILTERKEYS:
      // { cbSize, dwFlags, ... }: no accessibility feature is on.
      if (pvParam == nullptr || uiParam < 2 * sizeof(DWORD)) {
        return FALSE;
      }
      std::memset(static_cast<char*>(pvParam) + sizeof(DWORD), 0, uiParam - sizeof(DWORD));
      return TRUE;
    case SPI_SETSCREENSAVEACTIVE:
    case SPI_SETSTICKYKEYS:
    case SPI_SETTOGGLEKEYS:
    case SPI_SETFILTERKEYS:
    case SPI_SETMOUSE:
    case SPI_SETMOUSESPEED:
    case SPI_SETFOREGROUNDLOCKTIMEOUT:
      return TRUE;
    default:
      return FALSE;
  }
}

BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) noexcept
{
  return SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
}

int GetSystemMetrics(int nIndex) noexcept
{
  if (ShellActive()) {
    BOOL handled = FALSE;
    const int value = faf_posix::shell::win32::GetSystemMetrics(nIndex, &handled);
    if (handled) {
      return value;
    }
  }
  switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXVIRTUALSCREEN:
    case SM_CXFULLSCREEN:
    case SM_CXMAXIMIZED:
      return kScreenWidth;
    case SM_CYSCREEN:
    case SM_CYVIRTUALSCREEN:
    case SM_CYMAXIMIZED:
      return kScreenHeight;
    case SM_CYFULLSCREEN:
      return kScreenHeight - 23;
    case SM_CMONITORS:
    case SM_MOUSEPRESENT:
      return 1;
    case SM_CMOUSEBUTTONS:
      return 3;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:
    case SM_CXDRAG:
    case SM_CYDRAG:
    case SM_CXFRAME:
    case SM_CYFRAME:
      return 4;
    case SM_CXBORDER:
    case SM_CYBORDER:
      return 1;
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME:
      return 3;
    case SM_CXCURSOR:
    case SM_CYCURSOR:
    case SM_CXICON:
    case SM_CYICON:
      return 32;
    case SM_CXSMICON:
    case SM_CYSMICON:
      return 16;
    case SM_CYCAPTION:
      return 23;
    case SM_CYMENU:
      return 20;
    case SM_CXVSCROLL:
    case SM_CYHSCROLL:
      return 17;
    case SM_CXMIN:
      return 112;
    case SM_CYMIN:
      return 27;
    default:
      return 0;
  }
}

HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::MonitorFromPoint(pt, dwFlags);
  }
  const bool inside = pt.x >= 0 && pt.y >= 0 && pt.x < kScreenWidth && pt.y < kScreenHeight;
  if (!inside && dwFlags == MONITOR_DEFAULTTONULL) {
    return nullptr;
  }
  return PrimaryMonitor();
}

HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::MonitorFromRect(lprc, dwFlags);
  }
  if (lprc == nullptr) {
    return dwFlags == MONITOR_DEFAULTTONULL ? nullptr : PrimaryMonitor();
  }
  const bool overlaps = lprc->right > 0 && lprc->bottom > 0 && lprc->left < kScreenWidth && lprc->top < kScreenHeight;
  if (!overlaps && dwFlags == MONITOR_DEFAULTTONULL) {
    return nullptr;
  }
  return PrimaryMonitor();
}

HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::MonitorFromWindow(hwnd, dwFlags);
  }
  if (hwnd != DesktopWindow() && dwFlags == MONITOR_DEFAULTTONULL) {
    return nullptr;
  }
  return PrimaryMonitor();
}

BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetMonitorInfoW(hMonitor, lpmi);
  }
  if (hMonitor != PrimaryMonitor() || lpmi == nullptr || lpmi->cbSize < sizeof(MONITORINFO)) {
    return FALSE;
  }
  lpmi->rcMonitor = ScreenRect();
  lpmi->rcWork = ScreenRect();
  lpmi->dwFlags = MONITORINFOF_PRIMARY;
  if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
    auto* const extended = reinterpret_cast<LPMONITORINFOEXW>(lpmi);
    const wchar_t kDevice[] = L"\\\\.\\DISPLAY1";
    std::memcpy(extended->szDevice, kDevice, sizeof(kDevice));
  }
  return TRUE;
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept
{
  if (lpmi != nullptr && lpmi->cbSize > sizeof(MONITORINFO)) {
    // MONITORINFOEXA's narrow device name: fill only the common part.
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMonitor, &info)) {
      return FALSE;
    }
    lpmi->rcMonitor = info.rcMonitor;
    lpmi->rcWork = info.rcWork;
    lpmi->dwFlags = info.dwFlags;
    std::strncpy(reinterpret_cast<char*>(lpmi) + sizeof(MONITORINFO), "\\\\.\\DISPLAY1", CCHDEVICENAME);
    return TRUE;
  }
  return GetMonitorInfoW(hMonitor, lpmi);
}

// --- Clipboard ---------------------------------------------------------------------------------
BOOL OpenClipboard(HWND hWndNewOwner) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::OpenClipboard(hWndNewOwner);
  }
  (void)hWndNewOwner;
  FAF_POSIX_STUB("OpenClipboard: no clipboard on POSIX yet");
  return FALSE;
}

BOOL CloseClipboard() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::CloseClipboard();
  }
  return FALSE;
}

BOOL EmptyClipboard() noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::EmptyClipboard();
  }
  return FALSE;
}

HANDLE SetClipboardData(UINT uFormat, HANDLE hMem) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::SetClipboardData(uFormat, hMem);
  }
  (void)uFormat;
  (void)hMem;
  return nullptr;
}

HANDLE GetClipboardData(UINT uFormat) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::GetClipboardData(uFormat);
  }
  (void)uFormat;
  return nullptr;
}

BOOL IsClipboardFormatAvailable(UINT format) noexcept
{
  if (ShellActive()) {
    return faf_posix::shell::win32::IsClipboardFormatAvailable(format);
  }
  (void)format;
  return FALSE;
}
