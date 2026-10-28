#pragma once

// Private state of the shell package (shell/*.cpp only).

#include "platform/posix/shell/ShellInternal.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace faf_posix::shell::detail
{
  // One window of the table: a top-level window (an SDL window) or a child window (a
  // rectangle in its parent's client area). Every coordinate is in drawable pixels.
  struct WindowRecord
  {
    HWND handle = nullptr;
    std::wstring className;
    std::wstring text;
    DWORD style = 0;
    DWORD exStyle = 0;
    WNDPROC proc = nullptr;
    UINT classStyle = 0;
    HCURSOR classCursor = nullptr;
    LONG_PTR userData = 0;
    LONG_PTR id = 0;
    HINSTANCE instance = nullptr;
    HWND parent = nullptr; // WS_CHILD windows
    HWND owner = nullptr;  // top-level windows (CreateWindowEx's parent argument)
    std::vector<HWND> children; // z-order, front first
    bool visible = false;
    bool enabled = true;
    bool invalid = false; // needs WM_PAINT
    bool destroying = false;

    // Child windows: the window rect relative to the parent's client area (a child has no
    // non-client area, so its client area is the same size).
    RECT childRect{};

    // Top-level windows.
    SDL_Window* sdl = nullptr;
    SDL_WindowID sdlId = 0;
    int clientWidth = 0;  // cached drawable size
    int clientHeight = 0;
    int screenX = 0;      // cached client-area origin, screen points (SDL's position)
    int screenY = 0;
    bool minimized = false;
    bool maximized = false; // maximized, or a captioned window in a full-screen space
    bool shellFullScreen = false; // a borderless window covering its monitor, put in full screen
    HWND lastFocus = nullptr; // the descendant that had the focus when it was deactivated
    float wheelRemainder = 0.0f;

    // The CAMetalLayer's view (SDL_Metal_CreateView), made on the first MetalLayer().
    SDL_MetalView metalView = nullptr;

    [[nodiscard]] bool IsChild() const noexcept { return (style & WS_CHILD) != 0 && parent != nullptr; }
  };

  struct WindowClass
  {
    UINT style = 0;
    WNDPROC proc = nullptr;
    HCURSOR cursor = nullptr;
    HINSTANCE instance = nullptr;
  };

  struct State
  {
    std::recursive_mutex lock; // the window table, the classes, focus/capture/activation
    std::atomic<bool> active{false};
    bool sdlStarted = false;

    std::unordered_map<HWND, std::unique_ptr<WindowRecord>> windows;
    std::unordered_map<SDL_WindowID, HWND> bySdl;
    std::vector<HWND> topLevels; // creation order
    std::uintptr_t nextHandle = 0x00100000u;
    std::map<std::wstring, WindowClass> classes;

    HWND focus = nullptr;
    HWND activeWindow = nullptr;
    HWND capture = nullptr;
    bool appActive = false;

    // The main thread's message queue.
    std::mutex queueLock;
    std::deque<MSG> queue;
    bool quitPosted = false;
    int quitCode = 0;
    DWORD lastMessageTime = 0;
    POINT lastMessagePos{};
    HANDLE queueEvent = nullptr;
    Uint32 wakeEventType = 0;

    // Keyboard: the state of the messages taken from the queue (GetKeyState) and of the
    // device (GetAsyncKeyState). 0x80 down, 0x01 toggled.
    std::array<std::atomic<std::uint8_t>, 256> syncKeys{};
    std::array<std::atomic<std::uint8_t>, 256> asyncKeys{};
    std::uint32_t lastScanInfo = 0; // lParam bits 16..24 of the last key-down (for WM_CHAR)

    // Mouse.
    HWND mouseTopLevel = nullptr; // the top-level window SDL reports the mouse in
    std::uint32_t mouseButtons = 0; // MK_LBUTTON | MK_RBUTTON | ...

    // Cursor.
    HCURSOR cursor = nullptr;
    int cursorShowCount = 0;
    bool cursorHidden = false;
    bool clipped = false;
    RECT clip{};

    // The screen's coordinate scale: screen pixels per screen point (the primary display's
    // pixel density), and whether windows are created high-density at all.
    float screenScale = 1.0f;
    bool highDensity = true;
  };

  State& S() noexcept;

  // --- Window table (Windows.cpp) ----------------------------------------------------------
  WindowRecord* Find(HWND window) noexcept; // caller holds S().lock
  WindowRecord* FindBySdl(SDL_WindowID id) noexcept;
  WindowRecord* TopLevelOf(WindowRecord* record) noexcept;
  HWND TopLevelHandle(HWND window) noexcept;
  // Calls the window's procedure (lock not held). 0 for an unknown window.
  LRESULT Send(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
  // The client area's origin on screen, in screen pixels; and the window's density.
  bool ClientOriginOnScreen(HWND window, POINT* origin) noexcept;
  float DensityOf(const WindowRecord* topLevel) noexcept;
  // Re-reads a top-level window's size, position and state from SDL and sends the
  // messages Windows sends for the change (WM_SIZE, WM_MOVE, the sizing loop).
  void SyncTopLevel(HWND window, bool userAction) noexcept;
  // Activation (SDL focus) changes.
  void OnFocusGained(HWND topLevel) noexcept;
  void OnFocusLost(HWND topLevel) noexcept;
  // Places a child window's Metal view over its client area (or the whole content view).
  void PlaceMetalView(WindowRecord* record) noexcept;
  // The deepest visible, enabled window under a point of a top-level's client area.
  HWND HitTest(HWND topLevel, POINT clientPoint, POINT* windowPoint) noexcept;
  void DestroyAll() noexcept;
  void ApplyDisplayScale() noexcept;

  // --- Message queue (Queue.cpp) -----------------------------------------------------------
  void Post(const MSG& message) noexcept;      // back of the queue
  void PostFront(const MSG& message) noexcept; // next out (TranslateMessage's characters)
  DWORD Now() noexcept;                        // message time (milliseconds)
  // Moves pending SDL events into the queue (one at a time while it is empty, so sent
  // messages keep their place among the queued ones).
  void TranslatePending(bool untilQueued) noexcept;
  bool SdlHasInput() noexcept;
  // A visible window waits for WM_PAINT (Windows' QS_PAINT: wakes MsgWaitForMultipleObjects
  // and WaitMessage; the game's frame loop runs on it, CD3DDevice::Refresh invalidating the
  // viewport every frame).
  bool PaintPending() noexcept;

  // --- Synthetic test input (TestInput.cpp; FAF_SHELL_TEST_INPUT, off by default) --------
  void TestInputInit() noexcept;                   // Init(): reads the script
  void TestInputFrame() noexcept;                  // a WM_PAINT was taken: runs due steps
  bool TestInputActive() noexcept;
  bool TestInputDrops(const SDL_Event& event) noexcept; // real input while a script runs
  void GlobalMouse(float* x, float* y) noexcept;   // the pointer in global points (virtual in tests)
  bool TestInputWarp(float x, float y) noexcept;   // SetCursorPos in tests: moves the virtual pointer

  // --- Input (Input.cpp) -------------------------------------------------------------------
  void TranslateEvent(const SDL_Event& event) noexcept;
  void NoteMessageRemoved(const MSG& message) noexcept; // GetKeyState follows the queue
  void ResetKeys() noexcept;
  void ApplyCursor() noexcept;
  void ApplyClip() noexcept;
  // Screen pixels of a point in SDL's global (screen point) space and back.
  POINT ScreenPixels(float x, float y) noexcept;
  void ScreenPoints(POINT pixels, float* x, float* y) noexcept;

  // --- Displays (System.cpp) ---------------------------------------------------------------
  void RefreshDisplays() noexcept;
  void ReleaseClipboard() noexcept;

  // --- Metal views (MetalView.cpp) ---------------------------------------------------------
  // Positions an SDL Metal view (an NSView) in its superview: frame in points measured from
  // the content view's top-left, or filling it and following its size.
  void SetMetalViewFrame(SDL_MetalView view, double x, double y, double width, double height, bool fill) noexcept;
  void SetMetalViewHidden(SDL_MetalView view, bool hidden) noexcept;

  std::string Utf8FromWide(const wchar_t* text);
  std::wstring WideFromUtf8(const char* text);
} // namespace faf_posix::shell::detail
