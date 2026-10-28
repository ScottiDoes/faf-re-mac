// The window system of the POSIX build (Shell.h): SDL3 video and events.
//
// Init() starts SDL's video subsystem on the main thread; from then on the Window area's
// HWNDs are real (Windows.cpp), input arrives as Win32 messages (Input.cpp, Queue.cpp), and
// each window can present through a CAMetalLayer (MetalLayer, MetalView.cpp). Headless runs
// never call Init(), and every function here then answers "no window system".
//
// What a pixel is (Panel.h): by default a pixel of the display panel, so the game's
// resolution lines up with the panel; FAF_SHELL_HIGHDPI=1 the backing store's pixels,
// FAF_SHELL_HIGHDPI=0 one pixel per point.

#include "platform/posix/shell/ShellState.h"
#include "platform/posix/shell/Panel.h"

#include <pthread.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace faf_posix::shell
{
  namespace detail
  {
    State& S() noexcept
    {
      // Never destroyed: windows and cursors may be torn down by static destructors.
      static State* const sState = new State();
      return *sState;
    }

    std::string Utf8FromWide(const wchar_t* text)
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

    std::wstring WideFromUtf8(const char* text)
    {
      std::wstring out;
      if (text == nullptr) {
        return out;
      }
      const auto* p = reinterpret_cast<const unsigned char*>(text);
      while (*p != 0) {
        std::uint32_t cp = *p;
        int extra = 0;
        if (cp >= 0xF0) {
          cp &= 0x07;
          extra = 3;
        } else if (cp >= 0xE0) {
          cp &= 0x0F;
          extra = 2;
        } else if (cp >= 0xC0) {
          cp &= 0x1F;
          extra = 1;
        } else if (cp >= 0x80) {
          cp = 0xFFFD; // stray continuation byte
        }
        ++p;
        for (int i = 0; i < extra; ++i) {
          if ((*p & 0xC0) != 0x80) {
            cp = 0xFFFD;
            break;
          }
          cp = (cp << 6) | (*p & 0x3F);
          ++p;
        }
        out.push_back(static_cast<wchar_t>(cp));
      }
      return out;
    }
  } // namespace detail

  using detail::S;

  bool IsMainThread() noexcept
  {
    return pthread_main_np() != 0;
  }

  bool Init()
  {
    detail::State& s = S();
    if (s.active.load()) {
      return true;
    }
    if (!IsMainThread()) {
      std::fprintf(stderr, "[shell] Init: must run on the main thread (SDL video and AppKit)\n");
      return false;
    }

    // Windows behaviour where SDL's default differs: closing the last window is the
    // application's business (WM_CLOSE), and the click that activates a window reaches it.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "1");

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
      std::fprintf(stderr, "[shell] Init: SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
      return false;
    }
    s.sdlStarted = true;

    // Retina backing stores unless pixels are points (Panel.h, FAF_SHELL_HIGHDPI=0).
    s.highDensity = detail::PixelModeSetting() != detail::PixelMode::Points;
    s.wakeEventType = SDL_RegisterEvents(1);
    detail::RefreshDisplays();

    if (s.queueEvent == nullptr) {
      s.queueEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    faf_posixSetThreadMessageEvent(s.queueEvent);

    s.cursor = ::LoadCursorW(nullptr, IDC_ARROW);
    s.cursorShowCount = 0;
    s.cursorHidden = false;
    detail::ResetKeys();

    s.active.store(true);
    detail::ApplyCursor();
    detail::TestInputInit();
    std::fprintf(stderr, "[shell] SDL %d.%d.%d video '%s', screen scale %.2f%s\n", SDL_MAJOR_VERSION,
                 SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver(), s.screenScale,
                 detail::PixelModeSetting() == detail::PixelMode::Panel     ? " (panel pixels)"
                 : detail::PixelModeSetting() == detail::PixelMode::Backing ? " (FAF_SHELL_HIGHDPI=1: backing pixels)"
                                                                            : " (FAF_SHELL_HIGHDPI=0: one pixel per point)");
    return true;
  }

  void Shutdown()
  {
    detail::State& s = S();
    if (!s.active.load()) {
      return;
    }
    detail::DestroyAll();
    detail::ReleaseClipboard();
    faf_posixSetThreadMessageEvent(nullptr);
    {
      std::lock_guard<std::mutex> guard(s.queueLock);
      s.queue.clear();
      s.quitPosted = false;
    }
    s.active.store(false);
    if (s.sdlStarted) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
      SDL_Quit();
      s.sdlStarted = false;
    }
  }

  bool IsActive() noexcept
  {
    return S().active.load(std::memory_order_acquire);
  }

  void* MetalLayer(HWND window)
  {
    if (!IsActive() || !IsMainThread()) {
      return nullptr;
    }
    detail::State& s = S();
    std::lock_guard<std::recursive_mutex> guard(s.lock);
    detail::WindowRecord* const record = detail::Find(window);
    if (record == nullptr) {
      return nullptr;
    }
    detail::WindowRecord* const top = detail::TopLevelOf(record);
    if (top == nullptr || top->sdl == nullptr) {
      return nullptr;
    }
    if (record->metalView == nullptr) {
      record->metalView = SDL_Metal_CreateView(top->sdl);
      if (record->metalView == nullptr) {
        std::fprintf(stderr, "[shell] SDL_Metal_CreateView failed: %s\n", SDL_GetError());
        return nullptr;
      }
    }
    detail::PlaceMetalView(record);
    return SDL_Metal_GetLayer(record->metalView);
  }

  bool PumpEvents()
  {
    if (!IsActive() || !IsMainThread()) {
      return false;
    }
    bool delivered = false;
    MSG message{};
    while (win32::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
      if (message.message == WM_QUIT) {
        // Left for the application's own loop to see.
        win32::PostQuitMessage(static_cast<int>(message.wParam));
        return true;
      }
      win32::TranslateMessage(&message);
      win32::DispatchMessageW(&message);
      delivered = true;
    }
    return delivered;
  }

  bool WaitForEvent(int milliseconds)
  {
    if (!IsActive() || !IsMainThread()) {
      return false;
    }
    detail::State& s = S();
    {
      std::lock_guard<std::mutex> guard(s.queueLock);
      if (!s.queue.empty() || s.quitPosted) {
        return true;
      }
    }
    SDL_PumpEvents();
    if (detail::SdlHasInput() || detail::PaintPending()) {
      return true;
    }
    return SDL_WaitEventTimeout(nullptr, milliseconds < 0 ? -1 : milliseconds);
  }

  bool InputAvailable()
  {
    if (!IsActive() || !IsMainThread()) {
      return false;
    }
    detail::State& s = S();
    {
      std::lock_guard<std::mutex> guard(s.queueLock);
      if (!s.queue.empty() || s.quitPosted) {
        return true;
      }
    }
    SDL_PumpEvents();
    // QS_ALLINPUT includes QS_PAINT: a window waiting for WM_PAINT ends the wait too.
    return detail::SdlHasInput() || detail::PaintPending();
  }

  void WakeMainThread() noexcept
  {
    detail::State& s = S();
    if (!s.active.load() || s.wakeEventType == 0) {
      return;
    }
    SDL_Event event{};
    event.type = s.wakeEventType;
    (void)SDL_PushEvent(&event);
  }

  HANDLE QueueEvent() noexcept
  {
    return S().queueEvent;
  }

  float PixelDensity(HWND window) noexcept
  {
    detail::State& s = S();
    if (!s.active.load()) {
      return 1.0f;
    }
    std::lock_guard<std::recursive_mutex> guard(s.lock);
    detail::WindowRecord* const record = detail::Find(window);
    if (record == nullptr) {
      return s.screenScale;
    }
    return detail::DensityOf(detail::TopLevelOf(record));
  }
} // namespace faf_posix::shell
