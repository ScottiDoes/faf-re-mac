#pragma once

// The shell package's internal interface (not a contract; Shell.h is the contract).
//
// While the shell is active (faf_posix::shell::Init succeeded), the Window area
// (WinCompatWindow.cpp) forwards its user32 functions to the `win32` namespace below; the
// Sync area's MsgWaitForMultipleObjects(Ex) wakes on input through InputAvailable(); the gal
// cursor stand-in (WindowsCursorPosix.cpp) makes its colour cursors with CreateColorCursor().
// Headless, none of this is reached: the Window area keeps its window-less answers.
//
// Threads: windows, the message queue's reader and the SDL calls belong to the main thread.
// PostMessageW / PostThreadMessageW, the key state, GetCursorPos and the window queries are
// safe from any thread.

#include "platform/posix/shell/Shell.h"

#include <cstdint>

namespace faf_posix::shell
{
  // True on the thread that called Init() (the process's main thread).
  [[nodiscard]] bool IsMainThread() noexcept;

  // True when the main thread's queue holds a message, the window system has an event
  // waiting, or a visible window needs WM_PAINT (QS_ALLINPUT, QS_PAINT included; pumps SDL;
  // main thread only).
  [[nodiscard]] bool InputAvailable();

  // Wakes a WaitForEvent() in progress (any thread).
  void WakeMainThread() noexcept;

  // Synthetic test input (FAF_SHELL_TEST_INPUT, TestInput.cpp): the game registers how a
  // "shot" step captures a frame (faf_game: the engine's dump_Frame console command, which
  // writes the next rendered frame as a BMP into `directory`; the path has no spaces).
  using TestShotHandler = void (*)(const char* directory);
  void SetTestShotHandler(TestShotHandler handler) noexcept;

  // The Win32 event that PostMessageW signals, given to the Sync area as the main thread's
  // "message queue" event (faf_posixSetThreadMessageEvent).
  [[nodiscard]] HANDLE QueueEvent() noexcept;

  // A colour cursor from `size` x `size` 32-bit pixels, rows top-down, each pixel
  // 0xAARRGGBB (B,G,R,A in memory, a DIB's order), hotspot in those pixels. The cursor is
  // shown at one display point per `pixelsPerPoint` pixels (2 on Retina), so it keeps its
  // size relative to the pixel-sized UI. Null on failure. Free with DestroyCursor.
  [[nodiscard]] HCURSOR CreateColorCursor(const std::uint32_t* pixels, int size, int hotX, int hotY,
                                          float pixelsPerPoint);

  // The pixel density of the window's display (2 on Retina; 1 without a window).
  [[nodiscard]] float PixelDensity(HWND window) noexcept;

  // RegisterClass / UnregisterClass (with or without Init: a class is only data). Returns the
  // class atom, 0 with ERROR_CLASS_ALREADY_EXISTS when the name is taken.
  ATOM RegisterWindowClass(LPCWSTR name, UINT style, WNDPROC proc, HCURSOR cursor, HINSTANCE instance) noexcept;
  bool UnregisterWindowClass(LPCWSTR name) noexcept;
} // namespace faf_posix::shell

// The Window area's functions while the shell is active: same signatures and Windows
// semantics, see compat/Window.h.
namespace faf_posix::shell::win32
{
  // Messages.
  BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept;
  BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept;
  BOOL TranslateMessage(const MSG* lpMsg) noexcept;
  LRESULT DispatchMessageW(const MSG* lpMsg) noexcept;
  void PostQuitMessage(int nExitCode) noexcept;
  BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
  BOOL PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
  LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
  LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
  DWORD GetQueueStatus(UINT flags) noexcept;
  DWORD GetMessageTime() noexcept;
  DWORD GetMessagePos() noexcept;
  BOOL WaitMessage() noexcept;

  // Windows.
  HWND CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y,
                       int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                       LPVOID lpParam) noexcept;
  BOOL DestroyWindow(HWND hWnd) noexcept;
  BOOL IsWindow(HWND hWnd) noexcept;
  BOOL IsWindowVisible(HWND hWnd) noexcept;
  BOOL IsWindowEnabled(HWND hWnd) noexcept;
  BOOL IsIconic(HWND hWnd) noexcept;
  BOOL IsZoomed(HWND hWnd) noexcept;
  BOOL ShowWindow(HWND hWnd, int nCmdShow) noexcept;
  BOOL UpdateWindow(HWND hWnd) noexcept;
  BOOL EnableWindow(HWND hWnd, BOOL bEnable) noexcept;
  BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) noexcept;
  BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) noexcept;
  BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) noexcept;
  BOOL GetClientRect(HWND hWnd, LPRECT lpRect) noexcept;
  BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle) noexcept;
  BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint) noexcept;
  BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) noexcept;
  BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) noexcept;
  BOOL ValidateRect(HWND hWnd, const RECT* lpRect) noexcept;
  BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString) noexcept;
  int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) noexcept;
  LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex) noexcept;
  LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) noexcept;
  HWND GetParent(HWND hWnd) noexcept;
  HWND SetParent(HWND hWndChild, HWND hWndNewParent) noexcept;
  HWND GetAncestor(HWND hwnd, UINT gaFlags) noexcept;
  HWND GetWindow(HWND hWnd, UINT uCmd) noexcept;
  BOOL IsChild(HWND hWndParent, HWND hWnd) noexcept;
  BOOL BringWindowToTop(HWND hWnd) noexcept;
  HWND ChildWindowFromPoint(HWND hWndParent, POINT Point) noexcept;
  HWND GetForegroundWindow() noexcept;
  BOOL SetForegroundWindow(HWND hWnd) noexcept;
  HWND GetActiveWindow() noexcept;
  HWND SetActiveWindow(HWND hWnd) noexcept;
  HWND GetFocus() noexcept;
  HWND SetFocus(HWND hWnd) noexcept;
  HWND GetCapture() noexcept;
  HWND SetCapture(HWND hWnd) noexcept;
  BOOL ReleaseCapture() noexcept;
  HWND WindowFromPoint(POINT Point) noexcept;
  BOOL FlashWindow(HWND hWnd, BOOL bInvert) noexcept;

  // Input.
  SHORT GetKeyState(int nVirtKey) noexcept;
  SHORT GetAsyncKeyState(int vKey) noexcept;
  BOOL GetKeyboardState(PBYTE lpKeyState) noexcept;
  UINT MapVirtualKeyW(UINT uCode, UINT uMapType) noexcept;
  HCURSOR SetCursor(HCURSOR hCursor) noexcept;
  HCURSOR GetCursor() noexcept;
  int ShowCursor(BOOL bShow) noexcept;
  BOOL GetCursorPos(LPPOINT lpPoint) noexcept;
  BOOL SetCursorPos(int X, int Y) noexcept;
  BOOL GetCursorInfo(PCURSORINFO pci) noexcept;
  BOOL ClipCursor(const RECT* lpRect) noexcept;
  BOOL GetClipCursor(LPRECT lpRect) noexcept;
  BOOL DestroyCursor(HCURSOR hCursor) noexcept;
  UINT GetDoubleClickTime() noexcept;

  // Displays and settings. `handled` (may be null) tells the caller whether the shell
  // answered the index; the others keep the Window area's defaults.
  BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, BOOL* handled) noexcept;
  int GetSystemMetrics(int nIndex, BOOL* handled) noexcept;
  HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags) noexcept;
  HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags) noexcept;
  HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags) noexcept;
  BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept;

  // Clipboard.
  BOOL OpenClipboard(HWND hWndNewOwner) noexcept;
  BOOL CloseClipboard() noexcept;
  BOOL EmptyClipboard() noexcept;
  HANDLE SetClipboardData(UINT uFormat, HANDLE hMem) noexcept;
  HANDLE GetClipboardData(UINT uFormat) noexcept;
  BOOL IsClipboardFormatAvailable(UINT format) noexcept;
} // namespace faf_posix::shell::win32
