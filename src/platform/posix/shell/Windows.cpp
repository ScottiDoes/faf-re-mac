// The shell's windows: the table behind the Window area's HWNDs while the shell is active.
//
// A top-level HWND is an SDL window (created hidden, SDL_WINDOW_METAL, high pixel density);
// its client area is the SDL window's drawable. A child HWND (WS_CHILD) is a rectangle in its
// parent's client area, with its own procedure, visibility, focus and, on request, its own
// CAMetalLayer over exactly that rectangle. Every coordinate is in drawable pixels; screen
// coordinates are screen points times the primary display's pixel density.
//
// Messages are sent the way Windows sends them: geometry changes send WM_MOVE / WM_SIZE (a
// resize by the user runs the sizing sequence WM_SYSCOMMAND(SC_SIZE), WM_ENTERSIZEMOVE,
// WM_SIZE, WM_EXITSIZEMOVE), activation sends WM_ACTIVATEAPP / WM_NCACTIVATE / WM_ACTIVATE,
// focus WM_KILLFOCUS / WM_SETFOCUS. The table lock is never held while a window procedure
// runs.

#include "platform/posix/shell/ShellState.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace faf_posix::shell
{
  namespace detail
  {
    namespace
    {
      constexpr float kTitleBarPoints = 28.0f; // AppKit's standard title bar
      constexpr int kDefaultClientPoints[2] = {800, 600};

      HWND DesktopHandle() noexcept
      {
        return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x00010010u));
      }

      LONG Round(double value) noexcept
      {
        return static_cast<LONG>(std::lround(value));
      }

      // Screen pixels of the window's non-client area (AppKit puts only a title bar around
      // the content).
      void Borders(const WindowRecord* top, LONG* left, LONG* topBorder, LONG* right, LONG* bottom) noexcept
      {
        *left = *topBorder = *right = *bottom = 0;
        if (top == nullptr || top->sdl == nullptr) {
          return;
        }
        int t = 0;
        int l = 0;
        int b = 0;
        int r = 0;
        if (!SDL_GetWindowBordersSize(top->sdl, &t, &l, &b, &r)) {
          if ((top->style & WS_CAPTION) == WS_CAPTION && (SDL_GetWindowFlags(top->sdl) & SDL_WINDOW_FULLSCREEN) == 0) {
            t = static_cast<int>(kTitleBarPoints);
          }
        }
        const float scale = S().screenScale;
        *left = Round(l * scale);
        *topBorder = Round(t * scale);
        *right = Round(r * scale);
        *bottom = Round(b * scale);
      }

      // A child's rectangle in its top-level window's client pixels.
      bool RectInTopLevel(WindowRecord* record, RECT* rect, WindowRecord** topOut) noexcept
      {
        RECT r = record->childRect;
        WindowRecord* current = record;
        while (current->IsChild()) {
          WindowRecord* const parent = Find(current->parent);
          if (parent == nullptr) {
            return false;
          }
          if (parent->IsChild()) {
            r.left += parent->childRect.left;
            r.top += parent->childRect.top;
            r.right += parent->childRect.left;
            r.bottom += parent->childRect.top;
          }
          current = parent;
        }
        if (current == record) {
          r = RECT{0, 0, record->clientWidth, record->clientHeight};
        }
        *rect = r;
        *topOut = current;
        return true;
      }

      bool ShownChain(const WindowRecord* record) noexcept
      {
        while (record != nullptr) {
          if (!record->visible) {
            return false;
          }
          if (!record->IsChild()) {
            return true;
          }
          record = Find(record->parent);
        }
        return false;
      }

      void InvalidateTree(WindowRecord* record) noexcept
      {
        record->invalid = true;
        for (HWND child : record->children) {
          if (WindowRecord* const c = Find(child)) {
            InvalidateTree(c);
          }
        }
      }

      void PlaceMetalViews(WindowRecord* record) noexcept
      {
        PlaceMetalView(record);
        for (HWND child : record->children) {
          if (WindowRecord* const c = Find(child)) {
            PlaceMetalViews(c);
          }
        }
      }

      SDL_DisplayID DisplayAtScreenPixels(LONG x, LONG y) noexcept
      {
        float px = 0.0f;
        float py = 0.0f;
        ScreenPoints(POINT{x, y}, &px, &py);
        const SDL_Point point{static_cast<int>(px), static_cast<int>(py)};
        const SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
        return display != 0 ? display : SDL_GetPrimaryDisplay();
      }

      float DisplayDensity(SDL_DisplayID display) noexcept
      {
        if (!S().highDensity) {
          return 1.0f;
        }
        const SDL_DisplayMode* const mode = SDL_GetDesktopDisplayMode(display);
        return mode != nullptr && mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
      }

      void ApplyTopLevelStyle(WindowRecord* top) noexcept
      {
        if (top->sdl == nullptr) {
          return;
        }
        SDL_SetWindowBordered(top->sdl, (top->style & WS_CAPTION) == WS_CAPTION);
        SDL_SetWindowResizable(top->sdl, (top->style & WS_THICKFRAME) != 0);
        SDL_SetWindowAlwaysOnTop(top->sdl, (top->exStyle & WS_EX_TOPMOST) != 0);
      }

      // WM_GETMINMAXINFO, as Windows asks for it before showing or sizing a window; the
      // answer becomes SDL's minimum / maximum client size.
      void ApplyMinMax(HWND window) noexcept
      {
        MINMAXINFO info{};
        SDL_Window* sdl = nullptr;
        LONG l = 0;
        LONG t = 0;
        LONG r = 0;
        LONG b = 0;
        float density = 1.0f;
        {
          std::lock_guard<std::recursive_mutex> guard(S().lock);
          WindowRecord* const top = Find(window);
          if (top == nullptr || top->sdl == nullptr) {
            return;
          }
          sdl = top->sdl;
          Borders(top, &l, &t, &r, &b);
          density = DensityOf(top);
          info.ptMaxSize = POINT{65535, 65535};
          info.ptMinTrackSize = POINT{0, 0};
          info.ptMaxTrackSize = POINT{65535, 65535};
        }
        (void)Send(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info));
        const float toPoints = 1.0f / density;
        const float screenToClient = density / S().screenScale;
        const int minW = std::max<LONG>(0, Round((info.ptMinTrackSize.x - l - r) * screenToClient * toPoints));
        const int minH = std::max<LONG>(0, Round((info.ptMinTrackSize.y - t - b) * screenToClient * toPoints));
        SDL_SetWindowMinimumSize(sdl, minW, minH);
        if (info.ptMaxTrackSize.x < 65535 || info.ptMaxTrackSize.y < 65535) {
          const int maxW = std::max<LONG>(1, Round((info.ptMaxTrackSize.x - l - r) * screenToClient * toPoints));
          const int maxH = std::max<LONG>(1, Round((info.ptMaxTrackSize.y - t - b) * screenToClient * toPoints));
          SDL_SetWindowMaximumSize(sdl, maxW, maxH);
        } else {
          SDL_SetWindowMaximumSize(sdl, 0, 0);
        }
      }

      // The game's "fullscreen" frame is a borderless window the size of the display mode
      // (CScApp: WS_POPUP at the head's resolution). A borderless top-level window that covers
      // its whole monitor is put in desktop full screen, and taken out when it no longer
      // covers it; windows with a caption are left alone.
      void ApplyFullScreen(HWND window) noexcept
      {
        SDL_Window* sdl = nullptr;
        bool borderless = false;
        bool ours = false;
        {
          std::lock_guard<std::recursive_mutex> guard(S().lock);
          WindowRecord* const top = Find(window);
          if (top == nullptr || top->sdl == nullptr || !top->visible) {
            return;
          }
          sdl = top->sdl;
          borderless = (top->style & WS_CAPTION) != WS_CAPTION;
          ours = top->shellFullScreen;
        }
        bool covers = false;
        if (borderless) {
          RECT frame{};
          MONITORINFO monitor{};
          monitor.cbSize = sizeof(monitor);
          if (win32::GetWindowRect(window, &frame) == FALSE ||
              win32::GetMonitorInfoW(win32::MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST), &monitor) == FALSE) {
            return;
          }
          covers = (frame.right - frame.left) >= (monitor.rcMonitor.right - monitor.rcMonitor.left) &&
                   (frame.bottom - frame.top) >= (monitor.rcMonitor.bottom - monitor.rcMonitor.top);
        }
        const bool fullScreen = (SDL_GetWindowFlags(sdl) & SDL_WINDOW_FULLSCREEN) != 0;
        if (covers && !fullScreen) {
          SDL_SetWindowFullscreen(sdl, true);
          ours = true;
        } else if (!covers && fullScreen && ours) {
          SDL_SetWindowFullscreen(sdl, false); // a window the shell put there, and only that
          ours = false;
        }
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        if (WindowRecord* const top = Find(window)) {
          top->shellFullScreen = ours;
        }
      }

      void RemoveQueuedFor(HWND window) noexcept
      {
        State& s = S();
        std::lock_guard<std::mutex> guard(s.queueLock);
        s.queue.erase(std::remove_if(s.queue.begin(), s.queue.end(), [window](const MSG& m) { return m.hwnd == window; }),
                      s.queue.end());
      }

      bool Contains(const RECT& rect, POINT point) noexcept
      {
        return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
      }
    } // namespace

    WindowRecord* Find(HWND window) noexcept
    {
      State& s = S();
      const auto it = s.windows.find(window);
      return it != s.windows.end() ? it->second.get() : nullptr;
    }

    WindowRecord* FindBySdl(SDL_WindowID id) noexcept
    {
      State& s = S();
      const auto it = s.bySdl.find(id);
      return it != s.bySdl.end() ? Find(it->second) : nullptr;
    }

    WindowRecord* TopLevelOf(WindowRecord* record) noexcept
    {
      while (record != nullptr && record->IsChild()) {
        record = Find(record->parent);
      }
      return record;
    }

    HWND TopLevelHandle(HWND window) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const top = TopLevelOf(Find(window));
      return top != nullptr ? top->handle : nullptr;
    }

    float DensityOf(const WindowRecord* top) noexcept
    {
      if (top != nullptr && top->sdl != nullptr) {
        const float density = SDL_GetWindowPixelDensity(top->sdl);
        if (density > 0.0f) {
          return density;
        }
      }
      return S().highDensity ? S().screenScale : 1.0f;
    }

    LRESULT Send(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
      WNDPROC proc = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const record = Find(window);
        if (record == nullptr) {
          return 0;
        }
        proc = record->proc;
      }
      if (proc == nullptr) {
        return win32::DefWindowProcW(window, message, wParam, lParam);
      }
      return proc(window, message, wParam, lParam);
    }

    bool ClientOriginOnScreen(HWND window, POINT* origin) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(window);
      if (record == nullptr) {
        return false;
      }
      RECT inTop{};
      WindowRecord* top = nullptr;
      if (!RectInTopLevel(record, &inTop, &top)) {
        return false;
      }
      const float scale = S().screenScale;
      const float density = DensityOf(top);
      origin->x = Round((top->screenX + inTop.left / density) * scale);
      origin->y = Round((top->screenY + inTop.top / density) * scale);
      return true;
    }

    void PlaceMetalView(WindowRecord* record) noexcept
    {
      if (record == nullptr || record->metalView == nullptr) {
        return;
      }
      RECT rect{};
      WindowRecord* top = nullptr;
      if (!RectInTopLevel(record, &rect, &top)) {
        return;
      }
      const bool fills = rect.left == 0 && rect.top == 0 && rect.right == top->clientWidth && rect.bottom == top->clientHeight;
      const double density = DensityOf(top);
      SetMetalViewFrame(record->metalView, rect.left / density, rect.top / density, (rect.right - rect.left) / density,
                        (rect.bottom - rect.top) / density, fills || record == top);
      SetMetalViewHidden(record->metalView, !ShownChain(record));
    }

    HWND HitTest(HWND topLevel, POINT clientPoint, POINT* windowPoint) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* current = Find(topLevel);
      if (current == nullptr) {
        return nullptr;
      }
      POINT local = clientPoint;
      for (;;) {
        WindowRecord* next = nullptr;
        for (HWND childHandle : current->children) {
          WindowRecord* const child = Find(childHandle);
          if (child == nullptr || !child->visible) {
            continue;
          }
          if (Contains(child->childRect, local)) {
            next = child;
            break;
          }
        }
        if (next == nullptr || !next->enabled) {
          // A disabled child takes no mouse input; Windows gives it to nobody (the parent
          // keeps it here, which is what a click on a disabled control looks like).
          break;
        }
        local.x -= next->childRect.left;
        local.y -= next->childRect.top;
        current = next;
      }
      if (windowPoint != nullptr) {
        *windowPoint = local;
      }
      return current->handle;
    }

    void SyncTopLevel(HWND window, bool userAction) noexcept
    {
      bool moved = false;
      bool resized = false;
      bool stateChanged = false;
      bool minimized = false;
      bool maximized = false;
      int width = 0;
      int height = 0;
      POINT origin{};
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const top = Find(window);
        if (top == nullptr || top->sdl == nullptr || top->destroying) {
          return;
        }
        const SDL_WindowFlags flags = SDL_GetWindowFlags(top->sdl);
        minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
        // A captioned window in a full-screen space is "maximized"; a borderless one there is
        // the game's full-screen frame, which Windows never maximizes.
        const bool captioned = (top->style & WS_CAPTION) == WS_CAPTION;
        maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0 || (captioned && (flags & SDL_WINDOW_FULLSCREEN) != 0);
        int x = 0;
        int y = 0;
        SDL_GetWindowSizeInPixels(top->sdl, &width, &height);
        SDL_GetWindowPosition(top->sdl, &x, &y);
        stateChanged = minimized != top->minimized || maximized != top->maximized;
        resized = !minimized && (width != top->clientWidth || height != top->clientHeight);
        moved = !minimized && (x != top->screenX || y != top->screenY);
        top->minimized = minimized;
        top->maximized = maximized;
        if (!minimized) {
          top->clientWidth = width;
          top->clientHeight = height;
          top->screenX = x;
          top->screenY = y;
        }
        if (resized || stateChanged) {
          InvalidateTree(top);
          PlaceMetalViews(top);
        }
      }
      if (moved) {
        (void)ClientOriginOnScreen(window, &origin);
        (void)Send(window, WM_MOVE, 0, MAKELPARAM(static_cast<WORD>(origin.x), static_cast<WORD>(origin.y)));
      }
      if (minimized) {
        if (stateChanged) {
          (void)Send(window, WM_SIZE, SIZE_MINIMIZED, 0);
        }
        return;
      }
      if (!stateChanged && !resized) {
        return;
      }
      const WPARAM kind = maximized ? SIZE_MAXIMIZED : SIZE_RESTORED;
      const LPARAM size = MAKELPARAM(static_cast<WORD>(width), static_cast<WORD>(height));
      if (userAction && resized && !stateChanged && !maximized) {
        // A drag on the frame: what DefWindowProc's sizing loop sends on Windows.
        (void)Send(window, WM_SYSCOMMAND, SC_SIZE, 0);
        (void)Send(window, WM_ENTERSIZEMOVE, 0, 0);
        (void)Send(window, WM_SIZE, kind, size);
        (void)Send(window, WM_EXITSIZEMOVE, 0, 0);
      } else {
        (void)Send(window, WM_SIZE, kind, size);
      }
      // The children may have been moved by the WM_SIZE handlers.
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      if (WindowRecord* const top = Find(window)) {
        PlaceMetalViews(top);
      }
    }

    void OnFocusGained(HWND topLevel) noexcept
    {
      State& s = S();
      HWND previous = nullptr;
      bool appWasActive = false;
      std::vector<HWND> tops;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        if (Find(topLevel) == nullptr || s.activeWindow == topLevel) {
          return;
        }
        previous = s.activeWindow;
        if (WindowRecord* const old = Find(previous); old != nullptr && s.focus != nullptr) {
          if (TopLevelOf(Find(s.focus)) == old) {
            old->lastFocus = s.focus;
          }
        }
        appWasActive = s.appActive;
        s.activeWindow = topLevel;
        s.appActive = true;
        tops = s.topLevels;
      }
      if (previous != nullptr) {
        (void)Send(previous, WM_NCACTIVATE, FALSE, 0);
        (void)Send(previous, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(topLevel));
      }
      if (!appWasActive) {
        for (HWND top : tops) {
          (void)Send(top, WM_ACTIVATEAPP, TRUE, 0);
        }
      }
      (void)Send(topLevel, WM_NCACTIVATE, TRUE, 0);
      (void)Send(topLevel, WM_ACTIVATE, WA_ACTIVE, reinterpret_cast<LPARAM>(previous));
      ApplyClip();
    }

    void OnFocusLost(HWND topLevel) noexcept
    {
      State& s = S();
      // Focus moving to another of our windows: its FOCUS_GAINED does the switch.
      if (SDL_Window* const focused = SDL_GetKeyboardFocus()) {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        if (WindowRecord* const other = FindBySdl(SDL_GetWindowID(focused)); other != nullptr && other->handle != topLevel) {
          return;
        }
      }
      HWND previousFocus = nullptr;
      HWND previousCapture = nullptr;
      std::vector<HWND> tops;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        if (s.activeWindow != topLevel) {
          return;
        }
        WindowRecord* const top = Find(topLevel);
        if (top != nullptr && s.focus != nullptr && TopLevelOf(Find(s.focus)) == top) {
          top->lastFocus = s.focus;
        }
        previousFocus = s.focus;
        previousCapture = s.capture;
        s.focus = nullptr;
        s.capture = nullptr;
        s.activeWindow = nullptr;
        s.appActive = false;
        tops = s.topLevels;
      }
      (void)Send(topLevel, WM_NCACTIVATE, FALSE, 0);
      (void)Send(topLevel, WM_ACTIVATE, WA_INACTIVE, 0);
      for (HWND top : tops) {
        (void)Send(top, WM_ACTIVATEAPP, FALSE, 0);
      }
      if (previousFocus != nullptr) {
        (void)Send(previousFocus, WM_KILLFOCUS, 0, 0);
      }
      if (previousCapture != nullptr) {
        SDL_CaptureMouse(false);
        (void)Send(previousCapture, WM_CAPTURECHANGED, 0, 0);
      }
      ApplyClip();
    }

    void DestroyAll() noexcept
    {
      std::vector<HWND> tops;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        tops = S().topLevels;
      }
      for (auto it = tops.rbegin(); it != tops.rend(); ++it) {
        (void)win32::DestroyWindow(*it);
      }
    }

    void ApplyDisplayScale() noexcept
    {
      std::vector<HWND> tops;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        tops = S().topLevels;
      }
      for (HWND top : tops) {
        SyncTopLevel(top, false);
      }
    }
  } // namespace detail

  ATOM RegisterWindowClass(LPCWSTR name, const UINT style, const WNDPROC proc, const HCURSOR cursor,
                           const HINSTANCE instance) noexcept
  {
    static ATOM sNextAtom = 0xC000;
    detail::State& s = detail::S();
    std::lock_guard<std::recursive_mutex> guard(s.lock);
    const std::wstring key(name);
    if (s.classes.count(key) != 0) {
      ::SetLastError(1410L /* ERROR_CLASS_ALREADY_EXISTS */);
      return 0;
    }
    s.classes[key] = detail::WindowClass{style, proc, cursor, instance};
    return sNextAtom++;
  }

  bool UnregisterWindowClass(LPCWSTR name) noexcept
  {
    if (name == nullptr) {
      return false;
    }
    detail::State& s = detail::S();
    std::lock_guard<std::recursive_mutex> guard(s.lock);
    return s.classes.erase(std::wstring(name)) != 0;
  }

  namespace win32
  {
    using namespace detail;

    HWND CreateWindowExW(const DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, const DWORD dwStyle,
                         const int X, const int Y, const int nWidth, const int nHeight, const HWND hWndParent,
                         const HMENU hMenu, const HINSTANCE hInstance, const LPVOID lpParam) noexcept
    {
      (void)lpParam;
      if (!IsMainThread()) {
        FAF_POSIX_STUB("CreateWindowExW: windows are created on the main thread only");
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
      }
      State& s = S();
      HWND handle = nullptr;
      bool showTopLevel = false;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        auto record = std::make_unique<WindowRecord>();
        handle = reinterpret_cast<HWND>(s.nextHandle);
        s.nextHandle += 4;
        record->handle = handle;
        record->className = lpClassName != nullptr ? lpClassName : L"";
        record->text = lpWindowName != nullptr ? lpWindowName : L"";
        record->style = dwStyle & ~static_cast<DWORD>(WS_VISIBLE);
        record->exStyle = dwExStyle;
        record->instance = hInstance;
        const auto cls = s.classes.find(record->className);
        if (cls != s.classes.end()) {
          record->proc = cls->second.proc;
          record->classStyle = cls->second.style;
          record->classCursor = cls->second.cursor;
        } else {
          // A system class the shell does not draw (EDIT, BUTTON, ...): DefWindowProc.
          record->proc = nullptr;
          record->classCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        }

        WindowRecord* const parent = hWndParent != nullptr ? Find(hWndParent) : nullptr;
        if ((dwStyle & WS_CHILD) != 0) {
          if (parent == nullptr) {
            ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return nullptr;
          }
          record->parent = hWndParent;
          record->id = reinterpret_cast<LONG_PTR>(hMenu);
          const LONG x = X == CW_USEDEFAULT ? 0 : X;
          const LONG y = X == CW_USEDEFAULT ? 0 : Y;
          const LONG w = nWidth == CW_USEDEFAULT ? 0 : std::max(0, nWidth);
          const LONG h = nWidth == CW_USEDEFAULT ? 0 : std::max(0, nHeight);
          record->childRect = RECT{x, y, x + w, y + h};
          record->visible = (dwStyle & WS_VISIBLE) != 0;
          record->invalid = true;
          parent->children.insert(parent->children.begin(), handle);
        } else {
          record->owner = parent != nullptr ? parent->handle : nullptr;
          // Client size in pixels: Windows' sizes include the frame.
          RECT frame{0, 0, 0, 0};
          (void)win32::AdjustWindowRectEx(&frame, dwStyle, FALSE, dwExStyle);
          const SDL_DisplayID display =
            X == CW_USEDEFAULT ? SDL_GetPrimaryDisplay() : DisplayAtScreenPixels(X + 1, Y + 1);
          const float density = DisplayDensity(display);
          const float scale = s.screenScale;
          int pointsW = kDefaultClientPoints[0];
          int pointsH = kDefaultClientPoints[1];
          if (nWidth != CW_USEDEFAULT) {
            const LONG clientW = std::max<LONG>(1, nWidth - (frame.right - frame.left));
            const LONG clientH = std::max<LONG>(1, (nHeight == CW_USEDEFAULT ? nWidth : nHeight) - (frame.bottom - frame.top));
            pointsW = std::max<LONG>(1, Round(clientW / density));
            pointsH = std::max<LONG>(1, Round(clientH / density));
          }
          SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL;
          if (s.highDensity) {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
          }
          if ((dwStyle & WS_THICKFRAME) != 0) {
            flags |= SDL_WINDOW_RESIZABLE;
          }
          if ((dwStyle & WS_CAPTION) != WS_CAPTION) {
            flags |= SDL_WINDOW_BORDERLESS;
          }
          if ((dwExStyle & WS_EX_TOPMOST) != 0) {
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
          }
          if ((dwExStyle & WS_EX_TOOLWINDOW) != 0) {
            flags |= SDL_WINDOW_UTILITY;
          }
          const std::string title = Utf8FromWide(record->text.c_str());
          SDL_Window* const sdl = SDL_CreateWindow(title.c_str(), pointsW, pointsH, flags);
          if (sdl == nullptr) {
            std::fprintf(stderr, "[shell] SDL_CreateWindow failed: %s\n", SDL_GetError());
            ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
          }
          if (X == CW_USEDEFAULT) {
            SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
          } else {
            // The client origin is inside the frame.
            const float x = (X - frame.left) / scale;
            const float y = (Y - frame.top) / scale;
            SDL_SetWindowPosition(sdl, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
          }
          SDL_StartTextInput(sdl);
          record->sdl = sdl;
          record->sdlId = SDL_GetWindowID(sdl);
          s.bySdl[record->sdlId] = handle;
          s.topLevels.push_back(handle);
          SDL_GetWindowSizeInPixels(sdl, &record->clientWidth, &record->clientHeight);
          SDL_GetWindowPosition(sdl, &record->screenX, &record->screenY);
          // The density may differ from the display's guess (a window SDL placed elsewhere).
          if (nWidth != CW_USEDEFAULT) {
            const LONG wantW = std::max<LONG>(1, nWidth - (frame.right - frame.left));
            const LONG wantH = std::max<LONG>(1, (nHeight == CW_USEDEFAULT ? nWidth : nHeight) - (frame.bottom - frame.top));
            const float actual = DensityOf(record.get());
            if (std::abs(actual - density) > 0.01f) {
              SDL_SetWindowSize(sdl, std::max<LONG>(1, Round(wantW / actual)), std::max<LONG>(1, Round(wantH / actual)));
              SDL_GetWindowSizeInPixels(sdl, &record->clientWidth, &record->clientHeight);
            }
          }
          showTopLevel = (dwStyle & WS_VISIBLE) != 0;
          if ((dwStyle & WS_MAXIMIZE) != 0) {
            SDL_MaximizeWindow(sdl);
          }
        }
        s.windows.emplace(handle, std::move(record));
      }
      if (showTopLevel) {
        (void)win32::ShowWindow(handle, SW_SHOWNORMAL);
      }
      return handle;
    }

    BOOL DestroyWindow(const HWND hWnd) noexcept
    {
      State& s = S();
      std::vector<HWND> children;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
          return FALSE;
        }
        if (record->destroying) {
          return TRUE;
        }
        record->destroying = true;
        children = record->children;
        if (record->sdl != nullptr && record->visible) {
          SDL_HideWindow(record->sdl);
        }
        if (record->metalView != nullptr) {
          SetMetalViewHidden(record->metalView, true);
        }
      }
      (void)Send(hWnd, WM_DESTROY, 0, 0);
      for (HWND child : children) {
        (void)win32::DestroyWindow(child);
      }
      (void)Send(hWnd, WM_NCDESTROY, 0, 0);

      HWND lostCapture = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          return TRUE;
        }
        if (s.focus == hWnd) {
          s.focus = nullptr;
        }
        if (s.capture == hWnd) {
          s.capture = nullptr;
          lostCapture = hWnd;
        }
        if (s.activeWindow == hWnd) {
          s.activeWindow = nullptr;
        }
        if (s.mouseTopLevel == hWnd) {
          s.mouseTopLevel = nullptr;
        }
        for (HWND top : s.topLevels) {
          if (WindowRecord* const t = Find(top); t != nullptr && t->lastFocus == hWnd) {
            t->lastFocus = nullptr;
          }
        }
        if (WindowRecord* const parent = Find(record->parent)) {
          auto& siblings = parent->children;
          siblings.erase(std::remove(siblings.begin(), siblings.end(), hWnd), siblings.end());
        }
        if (record->metalView != nullptr) {
          SDL_Metal_DestroyView(record->metalView);
          record->metalView = nullptr;
        }
        if (record->sdl != nullptr) {
          SDL_StopTextInput(record->sdl);
          s.bySdl.erase(record->sdlId);
          SDL_DestroyWindow(record->sdl);
          record->sdl = nullptr;
          s.topLevels.erase(std::remove(s.topLevels.begin(), s.topLevels.end(), hWnd), s.topLevels.end());
        }
        s.windows.erase(hWnd);
      }
      if (lostCapture != nullptr) {
        SDL_CaptureMouse(false);
      }
      RemoveQueuedFor(hWnd);
      return TRUE;
    }

    BOOL IsWindow(const HWND hWnd) noexcept
    {
      if (hWnd == DesktopHandle()) {
        return TRUE;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      return record != nullptr && !record->destroying ? TRUE : FALSE;
    }

    BOOL IsWindowVisible(const HWND hWnd) noexcept
    {
      if (hWnd == DesktopHandle()) {
        return TRUE;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      return ShownChain(Find(hWnd)) ? TRUE : FALSE;
    }

    BOOL IsWindowEnabled(const HWND hWnd) noexcept
    {
      if (hWnd == DesktopHandle()) {
        return TRUE;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      return record != nullptr && record->enabled ? TRUE : FALSE;
    }

    BOOL IsIconic(const HWND hWnd) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      return record != nullptr && !record->IsChild() && record->minimized ? TRUE : FALSE;
    }

    BOOL IsZoomed(const HWND hWnd) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      return record != nullptr && !record->IsChild() && record->maximized ? TRUE : FALSE;
    }

    BOOL ShowWindow(const HWND hWnd, const int nCmdShow) noexcept
    {
      State& s = S();
      bool wasVisible = false;
      bool isChild = false;
      SDL_Window* sdl = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
          return FALSE;
        }
        wasVisible = record->visible;
        isChild = record->IsChild();
        sdl = record->sdl;
      }
      if (!IsMainThread() && !isChild) {
        FAF_POSIX_STUB("ShowWindow: top-level windows change on the main thread only");
        return wasVisible ? TRUE : FALSE;
      }

      if (nCmdShow == SW_HIDE) {
        if (!wasVisible) {
          return FALSE;
        }
        HWND focusToParent = nullptr;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          WindowRecord* const record = Find(hWnd);
          if (record == nullptr) {
            return FALSE;
          }
          record->visible = false;
          if (sdl != nullptr) {
            SDL_HideWindow(sdl);
          } else {
            PlaceMetalViews(record);
            if (WindowRecord* const parent = Find(record->parent)) {
              parent->invalid = true;
            }
            for (HWND f = s.focus; f != nullptr;) {
              if (f == hWnd) {
                focusToParent = record->parent;
                break;
              }
              WindowRecord* const fr = Find(f);
              f = fr != nullptr && fr->IsChild() ? fr->parent : nullptr;
            }
          }
        }
        (void)Send(hWnd, WM_SHOWWINDOW, FALSE, 0);
        if (focusToParent != nullptr) {
          (void)win32::SetFocus(focusToParent);
        }
        return TRUE;
      }

      if (!wasVisible) {
        (void)Send(hWnd, WM_SHOWWINDOW, TRUE, 0);
      }
      if (sdl != nullptr) {
        if (!wasVisible) {
          ApplyMinMax(hWnd);
        }
        switch (nCmdShow) {
          case SW_MINIMIZE:
          case SW_SHOWMINIMIZED:
          case SW_SHOWMINNOACTIVE:
          case SW_FORCEMINIMIZE:
            SDL_ShowWindow(sdl);
            SDL_MinimizeWindow(sdl);
            break;
          case SW_MAXIMIZE:
            SDL_ShowWindow(sdl);
            SDL_MaximizeWindow(sdl);
            break;
          case SW_RESTORE:
            SDL_ShowWindow(sdl);
            SDL_RestoreWindow(sdl);
            break;
          case SW_SHOWNORMAL: {
            const SDL_WindowFlags flags = SDL_GetWindowFlags(sdl);
            SDL_ShowWindow(sdl);
            if ((flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0) {
              SDL_RestoreWindow(sdl);
            }
            break;
          }
          default:
            SDL_ShowWindow(sdl);
            break;
        }
      }
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        if (WindowRecord* const record = Find(hWnd)) {
          record->visible = true;
          InvalidateTree(record);
          PlaceMetalViews(record);
        }
      }
      if (sdl != nullptr) {
        SyncTopLevel(hWnd, false);
        ApplyFullScreen(hWnd);
      }
      return wasVisible ? TRUE : FALSE;
    }

    BOOL UpdateWindow(const HWND hWnd) noexcept
    {
      bool paint = false;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          return FALSE;
        }
        paint = record->invalid && ShownChain(record);
        if (paint) {
          record->invalid = false;
        }
      }
      if (paint) {
        (void)Send(hWnd, WM_PAINT, 0, 0);
      }
      return TRUE;
    }

    BOOL EnableWindow(const HWND hWnd, const BOOL bEnable) noexcept
    {
      bool wasDisabled = false;
      bool changed = false;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          return FALSE;
        }
        wasDisabled = !record->enabled;
        changed = record->enabled != (bEnable != FALSE);
        record->enabled = bEnable != FALSE;
      }
      if (changed) {
        (void)Send(hWnd, WM_ENABLE, bEnable, 0);
      }
      return wasDisabled ? TRUE : FALSE;
    }

    BOOL MoveWindow(const HWND hWnd, const int X, const int Y, const int nWidth, const int nHeight, const BOOL bRepaint) noexcept
    {
      return win32::SetWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, SWP_NOZORDER | SWP_NOACTIVATE | (bRepaint ? 0 : SWP_NOREDRAW));
    }

    BOOL SetWindowPos(const HWND hWnd, const HWND hWndInsertAfter, const int X, const int Y, const int cx, const int cy,
                      const UINT uFlags) noexcept
    {
      State& s = S();
      bool childMoved = false;
      bool childSized = false;
      SDL_Window* sdl = nullptr;
      RECT childRect{};
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        WindowRecord* const record = Find(hWnd);
        if (record == nullptr) {
          ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
          return FALSE;
        }
        if (record->IsChild()) {
          RECT r = record->childRect;
          if ((uFlags & SWP_NOMOVE) == 0) {
            const LONG w = r.right - r.left;
            const LONG h = r.bottom - r.top;
            r.left = X;
            r.top = Y;
            r.right = X + w;
            r.bottom = Y + h;
          }
          if ((uFlags & SWP_NOSIZE) == 0) {
            r.right = r.left + std::max(0, cx);
            r.bottom = r.top + std::max(0, cy);
          }
          childMoved = r.left != record->childRect.left || r.top != record->childRect.top;
          childSized = (r.right - r.left) != (record->childRect.right - record->childRect.left) ||
                       (r.bottom - r.top) != (record->childRect.bottom - record->childRect.top);
          record->childRect = r;
          childRect = r;
          if ((uFlags & SWP_NOZORDER) == 0) {
            WindowRecord* const parent = Find(record->parent);
            if (parent != nullptr) {
              auto& siblings = parent->children;
              siblings.erase(std::remove(siblings.begin(), siblings.end(), hWnd), siblings.end());
              if (hWndInsertAfter == HWND_BOTTOM) {
                siblings.push_back(hWnd);
              } else if (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == HWND_NOTOPMOST) {
                siblings.insert(siblings.begin(), hWnd);
              } else {
                auto it = std::find(siblings.begin(), siblings.end(), hWndInsertAfter);
                siblings.insert(it == siblings.end() ? siblings.begin() : it + 1, hWnd);
              }
            }
          }
          if (childMoved || childSized) {
            InvalidateTree(record);
            if (WindowRecord* const parent = Find(record->parent)) {
              parent->invalid = true;
            }
            PlaceMetalViews(record);
          }
        } else {
          sdl = record->sdl;
          if ((uFlags & SWP_FRAMECHANGED) != 0) {
            ApplyTopLevelStyle(record);
          }
        }
      }

      if (sdl != nullptr) {
        if (!IsMainThread()) {
          FAF_POSIX_STUB("SetWindowPos: top-level windows change on the main thread only");
          return FALSE;
        }
        LONG l = 0;
        LONG t = 0;
        LONG r = 0;
        LONG b = 0;
        float density = 1.0f;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          WindowRecord* const top = Find(hWnd);
          Borders(top, &l, &t, &r, &b);
          density = DensityOf(top);
        }
        const float scale = s.screenScale;
        if ((uFlags & SWP_NOSIZE) == 0) {
          // Screen pixels of the frame -> client pixels -> points.
          const double clientW = std::max<LONG>(1, cx - l - r) * (density / scale);
          const double clientH = std::max<LONG>(1, cy - t - b) * (density / scale);
          const int wantW = std::max<LONG>(1, Round(clientW / density));
          const int wantH = std::max<LONG>(1, Round(clientH / density));
          int haveW = 0;
          int haveH = 0;
          SDL_GetWindowSize(sdl, &haveW, &haveH);
          if (wantW != haveW || wantH != haveH) {
            SDL_SetWindowSize(sdl, wantW, wantH);
          }
        }
        if ((uFlags & SWP_NOMOVE) == 0) {
          const int x = static_cast<int>(std::lround((X + l) / scale));
          const int y = static_cast<int>(std::lround((Y + t) / scale));
          int haveX = 0;
          int haveY = 0;
          SDL_GetWindowPosition(sdl, &haveX, &haveY);
          if (x != haveX || y != haveY) {
            SDL_SetWindowPosition(sdl, x, y);
          }
        }
        if ((uFlags & SWP_NOZORDER) == 0) {
          if (hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == HWND_NOTOPMOST) {
            SDL_SetWindowAlwaysOnTop(sdl, hWndInsertAfter == HWND_TOPMOST);
            std::lock_guard<std::recursive_mutex> guard(s.lock);
            if (WindowRecord* const top = Find(hWnd)) {
              top->exStyle = hWndInsertAfter == HWND_TOPMOST ? (top->exStyle | WS_EX_TOPMOST) : (top->exStyle & ~WS_EX_TOPMOST);
            }
          } else if (hWndInsertAfter == HWND_TOP && (uFlags & SWP_NOACTIVATE) == 0 &&
                     SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true)) {
            // Windows activates the window it brings to the top. A process that asked SDL
            // not to activate windows it shows (the tests) keeps the focus where it is.
            SDL_RaiseWindow(sdl);
          }
        }
        if ((uFlags & SWP_FRAMECHANGED) != 0) {
          ApplyMinMax(hWnd); // the frame and the size limits are asked for again
        }
        SyncTopLevel(hWnd, false);
        if ((uFlags & (SWP_NOSIZE | SWP_FRAMECHANGED)) != SWP_NOSIZE) {
          ApplyFullScreen(hWnd);
        }
      } else {
        if (childMoved) {
          (void)Send(hWnd, WM_MOVE, 0, MAKELPARAM(static_cast<WORD>(childRect.left), static_cast<WORD>(childRect.top)));
        }
        if (childSized) {
          (void)Send(hWnd, WM_SIZE, SIZE_RESTORED,
                     MAKELPARAM(static_cast<WORD>(childRect.right - childRect.left), static_cast<WORD>(childRect.bottom - childRect.top)));
        }
      }

      if ((uFlags & SWP_SHOWWINDOW) != 0) {
        (void)win32::ShowWindow(hWnd, (uFlags & SWP_NOACTIVATE) != 0 ? SW_SHOWNA : SW_SHOW);
      } else if ((uFlags & SWP_HIDEWINDOW) != 0) {
        (void)win32::ShowWindow(hWnd, SW_HIDE);
      }
      return TRUE;
    }

    BOOL GetClientRect(const HWND hWnd, const LPRECT lpRect) noexcept
    {
      if (lpRect == nullptr) {
        return FALSE;
      }
      if (hWnd == DesktopHandle()) {
        return win32::GetWindowRect(hWnd, lpRect);
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
      }
      if (record->IsChild()) {
        *lpRect = RECT{0, 0, record->childRect.right - record->childRect.left, record->childRect.bottom - record->childRect.top};
      } else if (record->minimized) {
        *lpRect = RECT{0, 0, 0, 0};
      } else {
        *lpRect = RECT{0, 0, record->clientWidth, record->clientHeight};
      }
      return TRUE;
    }

    BOOL GetWindowRect(const HWND hWnd, const LPRECT lpRect) noexcept
    {
      if (lpRect == nullptr) {
        return FALSE;
      }
      State& s = S();
      if (hWnd == DesktopHandle()) {
        // The primary monitor, as the desktop window's rectangle.
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (win32::GetMonitorInfoW(win32::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info) == FALSE) {
          return FALSE;
        }
        *lpRect = info.rcMonitor;
        return TRUE;
      }
      POINT origin{};
      if (!ClientOriginOnScreen(hWnd, &origin)) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
      }
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return FALSE;
      }
      WindowRecord* const top = TopLevelOf(record);
      const float toScreen = s.screenScale / DensityOf(top);
      if (record->IsChild()) {
        lpRect->left = origin.x;
        lpRect->top = origin.y;
        lpRect->right = origin.x + Round((record->childRect.right - record->childRect.left) * toScreen);
        lpRect->bottom = origin.y + Round((record->childRect.bottom - record->childRect.top) * toScreen);
        return TRUE;
      }
      LONG l = 0;
      LONG t = 0;
      LONG r = 0;
      LONG b = 0;
      Borders(record, &l, &t, &r, &b);
      lpRect->left = origin.x - l;
      lpRect->top = origin.y - t;
      lpRect->right = origin.x + Round(record->clientWidth * toScreen) + r;
      lpRect->bottom = origin.y + Round(record->clientHeight * toScreen) + b;
      return TRUE;
    }

    BOOL AdjustWindowRectEx(const LPRECT lpRect, const DWORD dwStyle, const BOOL bMenu, const DWORD dwExStyle) noexcept
    {
      (void)bMenu;
      (void)dwExStyle;
      if (lpRect == nullptr) {
        return FALSE;
      }
      if ((dwStyle & WS_CHILD) == 0 && (dwStyle & WS_CAPTION) == WS_CAPTION) {
        lpRect->top -= Round(kTitleBarPoints * S().screenScale);
      }
      return TRUE;
    }

    BOOL ClientToScreen(const HWND hWnd, const LPPOINT lpPoint) noexcept
    {
      if (lpPoint == nullptr) {
        return FALSE;
      }
      if (hWnd == DesktopHandle()) {
        return TRUE;
      }
      POINT origin{};
      if (!ClientOriginOnScreen(hWnd, &origin)) {
        return FALSE;
      }
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      const float toScreen = s.screenScale / DensityOf(TopLevelOf(Find(hWnd)));
      lpPoint->x = origin.x + Round(lpPoint->x * toScreen);
      lpPoint->y = origin.y + Round(lpPoint->y * toScreen);
      return TRUE;
    }

    BOOL ScreenToClient(const HWND hWnd, const LPPOINT lpPoint) noexcept
    {
      if (lpPoint == nullptr) {
        return FALSE;
      }
      if (hWnd == DesktopHandle()) {
        return TRUE;
      }
      POINT origin{};
      if (!ClientOriginOnScreen(hWnd, &origin)) {
        return FALSE;
      }
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      const float toClient = DensityOf(TopLevelOf(Find(hWnd))) / s.screenScale;
      lpPoint->x = Round((lpPoint->x - origin.x) * toClient);
      lpPoint->y = Round((lpPoint->y - origin.y) * toClient);
      return TRUE;
    }

    BOOL InvalidateRect(const HWND hWnd, const RECT* lpRect, const BOOL bErase) noexcept
    {
      (void)lpRect;
      (void)bErase;
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      if (hWnd == nullptr) {
        for (HWND top : s.topLevels) {
          if (WindowRecord* const record = Find(top)) {
            InvalidateTree(record);
          }
        }
        return TRUE;
      }
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return FALSE;
      }
      record->invalid = true;
      return TRUE;
    }

    BOOL ValidateRect(const HWND hWnd, const RECT* lpRect) noexcept
    {
      (void)lpRect;
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return hWnd == nullptr ? TRUE : FALSE;
      }
      record->invalid = false;
      return TRUE;
    }

    BOOL SetWindowTextW(const HWND hWnd, LPCWSTR lpString) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return FALSE;
      }
      record->text = lpString != nullptr ? lpString : L"";
      if (record->sdl != nullptr && IsMainThread()) {
        SDL_SetWindowTitle(record->sdl, Utf8FromWide(record->text.c_str()).c_str());
      }
      return TRUE;
    }

    int GetWindowTextW(const HWND hWnd, const LPWSTR lpString, const int nMaxCount) noexcept
    {
      if (lpString == nullptr || nMaxCount <= 0) {
        return 0;
      }
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        lpString[0] = L'\0';
        return 0;
      }
      const int count = std::min<int>(nMaxCount - 1, static_cast<int>(record->text.size()));
      std::copy_n(record->text.c_str(), count, lpString);
      lpString[count] = L'\0';
      return count;
    }

    LONG_PTR GetWindowLongPtrW(const HWND hWnd, const int nIndex) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
      }
      switch (nIndex) {
        case GWLP_WNDPROC:
          return reinterpret_cast<LONG_PTR>(record->proc);
        case GWLP_USERDATA:
          return record->userData;
        case GWLP_ID:
          return record->id;
        case GWLP_HINSTANCE:
          return reinterpret_cast<LONG_PTR>(record->instance);
        case GWLP_HWNDPARENT:
          return reinterpret_cast<LONG_PTR>(record->IsChild() ? record->parent : record->owner);
        case GWL_STYLE: {
          DWORD style = record->style;
          if (record->visible) {
            style |= WS_VISIBLE;
          }
          if (!record->enabled) {
            style |= WS_DISABLED;
          }
          if (record->minimized) {
            style |= WS_MINIMIZE;
          }
          if (record->maximized) {
            style |= WS_MAXIMIZE;
          }
          return static_cast<LONG_PTR>(static_cast<LONG>(style));
        }
        case GWL_EXSTYLE:
          return static_cast<LONG_PTR>(static_cast<LONG>(record->exStyle));
        default:
          return 0;
      }
    }

    LONG_PTR SetWindowLongPtrW(const HWND hWnd, const int nIndex, const LONG_PTR dwNewLong) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
      }
      LONG_PTR previous = 0;
      switch (nIndex) {
        case GWLP_WNDPROC:
          previous = reinterpret_cast<LONG_PTR>(record->proc);
          record->proc = reinterpret_cast<WNDPROC>(dwNewLong);
          break;
        case GWLP_USERDATA:
          previous = record->userData;
          record->userData = dwNewLong;
          break;
        case GWLP_ID:
          previous = record->id;
          record->id = dwNewLong;
          break;
        case GWLP_HWNDPARENT:
          previous = reinterpret_cast<LONG_PTR>(record->owner);
          if (!record->IsChild()) {
            record->owner = reinterpret_cast<HWND>(dwNewLong);
          }
          break;
        case GWL_STYLE:
          previous = static_cast<LONG_PTR>(static_cast<LONG>(record->style));
          // Visibility, minimize and maximize keep their own state (Windows changes them only
          // through ShowWindow); the frame follows the style at once.
          record->style = static_cast<DWORD>(dwNewLong) & ~static_cast<DWORD>(WS_VISIBLE | WS_DISABLED | WS_MINIMIZE | WS_MAXIMIZE);
          if (record->sdl != nullptr && IsMainThread()) {
            ApplyTopLevelStyle(record);
          }
          break;
        case GWL_EXSTYLE:
          previous = static_cast<LONG_PTR>(static_cast<LONG>(record->exStyle));
          record->exStyle = static_cast<DWORD>(dwNewLong);
          if (record->sdl != nullptr && IsMainThread()) {
            ApplyTopLevelStyle(record);
          }
          break;
        default:
          break;
      }
      return previous;
    }

    HWND GetParent(const HWND hWnd) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return nullptr;
      }
      return record->IsChild() ? record->parent : record->owner;
    }

    HWND SetParent(const HWND hWndChild, const HWND hWndNewParent) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const record = Find(hWndChild);
      WindowRecord* const parent = Find(hWndNewParent);
      if (record == nullptr || !record->IsChild() || parent == nullptr) {
        FAF_POSIX_STUB("SetParent: only child windows move between parents on POSIX");
        return nullptr;
      }
      const HWND previous = record->parent;
      if (WindowRecord* const old = Find(previous)) {
        old->children.erase(std::remove(old->children.begin(), old->children.end(), hWndChild), old->children.end());
      }
      if (record->metalView != nullptr) {
        SDL_Metal_DestroyView(record->metalView); // made again over the new parent's window
        record->metalView = nullptr;
      }
      record->parent = hWndNewParent;
      parent->children.insert(parent->children.begin(), hWndChild);
      InvalidateTree(record);
      return previous;
    }

    HWND GetAncestor(const HWND hwnd, const UINT gaFlags) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* record = Find(hwnd);
      if (record == nullptr) {
        return nullptr;
      }
      switch (gaFlags) {
        case GA_PARENT:
          return record->IsChild() ? record->parent : DesktopHandle();
        case GA_ROOT:
          return TopLevelOf(record)->handle;
        case GA_ROOTOWNER: {
          record = TopLevelOf(record);
          while (record != nullptr && Find(record->owner) != nullptr) {
            record = Find(record->owner);
          }
          return record != nullptr ? record->handle : nullptr;
        }
        default:
          return nullptr;
      }
    }

    HWND GetWindow(const HWND hWnd, const UINT uCmd) noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      WindowRecord* const record = Find(hWnd);
      if (record == nullptr) {
        return nullptr;
      }
      if (uCmd == GW_CHILD) {
        return record->children.empty() ? nullptr : record->children.front();
      }
      if (uCmd == GW_OWNER) {
        return record->IsChild() ? nullptr : record->owner;
      }
      const std::vector<HWND>* siblings = &s.topLevels;
      if (record->IsChild()) {
        WindowRecord* const parent = Find(record->parent);
        if (parent == nullptr) {
          return nullptr;
        }
        siblings = &parent->children;
      }
      if (siblings->empty()) {
        return nullptr;
      }
      const auto it = std::find(siblings->begin(), siblings->end(), hWnd);
      switch (uCmd) {
        case GW_HWNDFIRST:
          return siblings->front();
        case GW_HWNDLAST:
          return siblings->back();
        case GW_HWNDNEXT:
          return it != siblings->end() && it + 1 != siblings->end() ? *(it + 1) : nullptr;
        case GW_HWNDPREV:
          return it != siblings->end() && it != siblings->begin() ? *(it - 1) : nullptr;
        default:
          return nullptr;
      }
    }

    BOOL IsChild(const HWND hWndParent, const HWND hWnd) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* record = Find(hWnd);
      while (record != nullptr && record->IsChild()) {
        if (record->parent == hWndParent) {
          return TRUE;
        }
        record = Find(record->parent);
      }
      return FALSE;
    }

    BOOL BringWindowToTop(const HWND hWnd) noexcept
    {
      return win32::SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    HWND ChildWindowFromPoint(const HWND hWndParent, const POINT Point) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(S().lock);
      WindowRecord* const parent = Find(hWndParent);
      if (parent == nullptr) {
        return nullptr;
      }
      RECT client{};
      if (parent->IsChild()) {
        client = RECT{0, 0, parent->childRect.right - parent->childRect.left, parent->childRect.bottom - parent->childRect.top};
      } else {
        client = RECT{0, 0, parent->clientWidth, parent->clientHeight};
      }
      if (!Contains(client, Point)) {
        return nullptr;
      }
      for (HWND childHandle : parent->children) {
        WindowRecord* const child = Find(childHandle);
        if (child != nullptr && Contains(child->childRect, Point)) {
          return childHandle;
        }
      }
      return hWndParent;
    }

    HWND GetForegroundWindow() noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      return s.appActive ? s.activeWindow : nullptr;
    }

    BOOL SetForegroundWindow(const HWND hWnd) noexcept
    {
      SDL_Window* sdl = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const top = TopLevelOf(Find(hWnd));
        if (top == nullptr) {
          return FALSE;
        }
        sdl = top->sdl;
      }
      if (sdl != nullptr && IsMainThread()) {
        SDL_RaiseWindow(sdl);
      }
      return TRUE;
    }

    HWND GetActiveWindow() noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      return s.activeWindow;
    }

    HWND SetActiveWindow(const HWND hWnd) noexcept
    {
      const HWND previous = win32::GetActiveWindow();
      (void)win32::SetForegroundWindow(hWnd);
      return previous;
    }

    HWND GetFocus() noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      return s.focus;
    }

    HWND SetFocus(const HWND hWnd) noexcept
    {
      State& s = S();
      HWND previous = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        previous = s.focus;
        if (hWnd != nullptr) {
          WindowRecord* const record = Find(hWnd);
          if (record == nullptr || record->destroying) {
            ::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return nullptr;
          }
          if (WindowRecord* const top = TopLevelOf(record)) {
            top->lastFocus = hWnd;
          }
        }
        if (hWnd == previous) {
          return previous;
        }
        s.focus = hWnd;
      }
      if (previous != nullptr) {
        (void)Send(previous, WM_KILLFOCUS, reinterpret_cast<WPARAM>(hWnd), 0);
      }
      if (hWnd != nullptr) {
        (void)Send(hWnd, WM_SETFOCUS, reinterpret_cast<WPARAM>(previous), 0);
      }
      return previous;
    }

    HWND GetCapture() noexcept
    {
      State& s = S();
      std::lock_guard<std::recursive_mutex> guard(s.lock);
      return s.capture;
    }

    HWND SetCapture(const HWND hWnd) noexcept
    {
      State& s = S();
      HWND previous = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        if (Find(hWnd) == nullptr) {
          return nullptr;
        }
        previous = s.capture;
        s.capture = hWnd;
      }
      if (IsMainThread()) {
        SDL_CaptureMouse(true);
      }
      if (previous != nullptr && previous != hWnd) {
        (void)Send(previous, WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(hWnd));
      }
      return previous;
    }

    BOOL ReleaseCapture() noexcept
    {
      State& s = S();
      HWND previous = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        previous = s.capture;
        s.capture = nullptr;
      }
      if (previous != nullptr) {
        if (IsMainThread()) {
          SDL_CaptureMouse(false);
        }
        (void)Send(previous, WM_CAPTURECHANGED, 0, 0);
      }
      return TRUE;
    }

    HWND WindowFromPoint(const POINT Point) noexcept
    {
      State& s = S();
      std::vector<HWND> order;
      {
        std::lock_guard<std::recursive_mutex> guard(s.lock);
        // The window the mouse is in first (SDL knows it is on top there), then the others.
        if (s.mouseTopLevel != nullptr) {
          order.push_back(s.mouseTopLevel);
        }
        if (s.activeWindow != nullptr && s.activeWindow != s.mouseTopLevel) {
          order.push_back(s.activeWindow);
        }
        for (HWND top : s.topLevels) {
          if (top != s.mouseTopLevel && top != s.activeWindow) {
            order.push_back(top);
          }
        }
      }
      for (HWND top : order) {
        RECT frame{};
        bool shown = false;
        {
          std::lock_guard<std::recursive_mutex> guard(s.lock);
          WindowRecord* const record = Find(top);
          shown = record != nullptr && record->visible && !record->minimized;
        }
        if (!shown || win32::GetWindowRect(top, &frame) == FALSE || !Contains(frame, Point)) {
          continue;
        }
        POINT client = Point;
        (void)win32::ScreenToClient(top, &client);
        RECT clientRect{};
        (void)win32::GetClientRect(top, &clientRect);
        if (!Contains(clientRect, client)) {
          return top; // the frame (title bar)
        }
        return HitTest(top, client, nullptr);
      }
      return nullptr;
    }

    BOOL FlashWindow(const HWND hWnd, const BOOL bInvert) noexcept
    {
      SDL_Window* sdl = nullptr;
      {
        std::lock_guard<std::recursive_mutex> guard(S().lock);
        WindowRecord* const top = TopLevelOf(Find(hWnd));
        if (top == nullptr) {
          return FALSE;
        }
        sdl = top->sdl;
      }
      if (sdl != nullptr && IsMainThread()) {
        SDL_FlashWindow(sdl, bInvert ? SDL_FLASH_BRIEFLY : SDL_FLASH_CANCEL);
      }
      return FALSE;
    }

    LRESULT DefWindowProcW(const HWND hWnd, const UINT Msg, const WPARAM wParam, const LPARAM lParam) noexcept
    {
      switch (Msg) {
        case WM_CLOSE:
          (void)win32::DestroyWindow(hWnd);
          return 0;
        case WM_SYSCOMMAND:
          switch (wParam & 0xFFF0u) {
            case SC_CLOSE:
              (void)Send(hWnd, WM_CLOSE, 0, 0);
              return 0;
            case SC_MINIMIZE:
              (void)win32::ShowWindow(hWnd, SW_MINIMIZE);
              return 0;
            case SC_MAXIMIZE:
              (void)win32::ShowWindow(hWnd, SW_MAXIMIZE);
              return 0;
            case SC_RESTORE:
              (void)win32::ShowWindow(hWnd, SW_RESTORE);
              return 0;
            default:
              // SC_SIZE / SC_MOVE: the system's sizing loop runs in AppKit, not here.
              return 0;
          }
        case WM_SETCURSOR: {
          HWND parent = nullptr;
          HCURSOR classCursor = nullptr;
          {
            std::lock_guard<std::recursive_mutex> guard(S().lock);
            WindowRecord* const record = Find(hWnd);
            if (record == nullptr) {
              return FALSE;
            }
            parent = record->IsChild() ? record->parent : nullptr;
            classCursor = record->classCursor;
          }
          if (parent != nullptr && Send(parent, WM_SETCURSOR, wParam, lParam) != 0) {
            return TRUE;
          }
          if (LOWORD(lParam) == HTCLIENT && classCursor != nullptr) {
            (void)::SetCursor(classCursor);
            return TRUE;
          }
          return FALSE;
        }
        case WM_ACTIVATE: {
          if (LOWORD(wParam) == WA_INACTIVE || HIWORD(wParam) != 0) {
            return 0;
          }
          HWND target = hWnd;
          {
            std::lock_guard<std::recursive_mutex> guard(S().lock);
            WindowRecord* const record = Find(hWnd);
            if (record == nullptr || record->minimized) {
              return 0;
            }
            // Windows' DefWindowProc focuses the window itself; the dialog manager and wx
            // put the focus back on the child that had it, which is what this does.
            WindowRecord* const last = Find(record->lastFocus);
            if (last != nullptr && TopLevelOf(last) == record && ShownChain(last)) {
              target = last->handle;
            }
            if (S().focus != nullptr && TopLevelOf(Find(S().focus)) == record) {
              return 0; // already inside this window
            }
          }
          (void)win32::SetFocus(target);
          return 0;
        }
        case WM_MOUSEWHEEL: {
          HWND parent = nullptr;
          {
            std::lock_guard<std::recursive_mutex> guard(S().lock);
            WindowRecord* const record = Find(hWnd);
            parent = record != nullptr && record->IsChild() ? record->parent : nullptr;
          }
          return parent != nullptr ? Send(parent, Msg, wParam, lParam) : 0;
        }
        case WM_SYSKEYDOWN:
          if (wParam == VK_F4 && (lParam & (1 << 29)) != 0) {
            (void)Send(TopLevelHandle(hWnd), WM_SYSCOMMAND, SC_CLOSE, 0);
          }
          return 0;
        case WM_PAINT:
          (void)win32::ValidateRect(hWnd, nullptr);
          return 0;
        case WM_ERASEBKGND:
        case WM_NCACTIVATE:
        case WM_QUERYENDSESSION:
          return TRUE;
        case WM_MOUSEACTIVATE:
          return 1; // MA_ACTIVATE
        default:
          return 0;
      }
    }
  } // namespace win32
} // namespace faf_posix::shell
