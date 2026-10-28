#pragma once

// Test double of src/platform/posix/shell/Shell.h for the Vulkan backend's tests: the
// faf_posix::shell functions on SDL3 directly, plus a few calls the tests use to make
// and move windows. Each test window is an SDL window with a Metal view; its "HWND" is
// a token only this double knows (no Window-area window behind it), so a test sets the
// head's size itself instead of letting DeviceVulkan call GetClientRect.
//
// Built without WinCompat.h (SDL's headers and the Win32 stand-ins do not mix): HWND
// is declared here exactly as WinCompat.h declares it.

struct HWND__;
typedef struct HWND__* HWND; // as WinCompat.h's FAF_DECLARE_HANDLE(HWND); repeating the typedef is valid C++

namespace faf_vk_test
{
  // A window of `width` x `height` points (Retina: twice as many pixels), unfocusable so
  // it does not take the keyboard from the user; null on failure. Starts the double's
  // SDL video if faf_posix::shell::Init has not.
  HWND CreateTestWindow(const char* title, int width, int height);
  void DestroyTestWindow(HWND window);
  void ResizeTestWindow(HWND window, int width, int height); // points
  void MinimizeTestWindow(HWND window);
  void RestoreTestWindow(HWND window);
  // The client area in drawable pixels (what the shell contract makes every Win32 size).
  void PixelSize(HWND window, int& width, int& height);
  float DisplayScale(HWND window);
} // namespace faf_vk_test
