// Test double of the Shell.h window system on SDL3 (see ShellDouble.h). Compiled
// without the engine's WinCompat.h force-include.

#include "ShellDouble.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstdint>
#include <cstdio>
#include <map>

#include "platform/posix/shell/Shell.h"

namespace
{
  struct TestWindow
  {
    SDL_Window* window = nullptr;
    SDL_MetalView view = nullptr;
  };

  bool sActive = false;
  std::uintptr_t sNextToken = 0x7E570000U;
  std::map<HWND, TestWindow> sWindows;

  TestWindow* Find(const HWND window)
  {
    const auto found = sWindows.find(window);
    return (found != sWindows.end()) ? &found->second : nullptr;
  }

  /** Lets SDL apply a size or state change before the test goes on. */
  void Settle(SDL_Window* const window)
  {
    SDL_SyncWindow(window);
    SDL_PumpEvents();
  }
} // namespace

namespace faf_posix::shell
{
  bool Init()
  {
    if (sActive) {
      return true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
      std::fprintf(stderr, "shell double: SDL_Init failed: %s\n", SDL_GetError());
      return false;
    }
    sActive = true;
    return true;
  }

  void Shutdown()
  {
    for (auto& [token, window] : sWindows) {
      static_cast<void>(token);
      if (window.view != nullptr) {
        SDL_Metal_DestroyView(window.view);
      }
      SDL_DestroyWindow(window.window);
    }
    sWindows.clear();
    if (sActive) {
      SDL_Quit();
      sActive = false;
    }
  }

  bool IsActive() noexcept
  {
    return sActive;
  }

  void* MetalLayer(const HWND window)
  {
    TestWindow* const found = Find(window);
    return (found != nullptr && found->view != nullptr) ? SDL_Metal_GetLayer(found->view) : nullptr;
  }

  bool PumpEvents()
  {
    bool any = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      any = true;
    }
    return any;
  }

  bool WaitForEvent(const int milliseconds)
  {
    return SDL_WaitEventTimeout(nullptr, milliseconds);
  }
} // namespace faf_posix::shell

namespace faf_vk_test
{
  HWND CreateTestWindow(const char* const title, const int width, const int height)
  {
    if (!faf_posix::shell::Init()) {
      return nullptr;
    }
    TestWindow entry;
    entry.window = SDL_CreateWindow(
      title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_METAL
    );
    if (entry.window == nullptr) {
      std::fprintf(stderr, "shell double: SDL_CreateWindow failed: %s\n", SDL_GetError());
      return nullptr;
    }
    entry.view = SDL_Metal_CreateView(entry.window);
    if (entry.view == nullptr) {
      std::fprintf(stderr, "shell double: SDL_Metal_CreateView failed: %s\n", SDL_GetError());
      SDL_DestroyWindow(entry.window);
      return nullptr;
    }
    Settle(entry.window);
    const HWND token = reinterpret_cast<HWND>(sNextToken);
    sNextToken += 0x10U;
    sWindows[token] = entry;
    return token;
  }

  void DestroyTestWindow(const HWND window)
  {
    const auto found = sWindows.find(window);
    if (found == sWindows.end()) {
      return;
    }
    if (found->second.view != nullptr) {
      SDL_Metal_DestroyView(found->second.view);
    }
    SDL_DestroyWindow(found->second.window);
    sWindows.erase(found);
  }

  void ResizeTestWindow(const HWND window, const int width, const int height)
  {
    if (TestWindow* const found = Find(window)) {
      SDL_SetWindowSize(found->window, width, height);
      Settle(found->window);
    }
  }

  void MinimizeTestWindow(const HWND window)
  {
    if (TestWindow* const found = Find(window)) {
      SDL_MinimizeWindow(found->window);
      Settle(found->window);
    }
  }

  void RestoreTestWindow(const HWND window)
  {
    if (TestWindow* const found = Find(window)) {
      SDL_RestoreWindow(found->window);
      Settle(found->window);
    }
  }

  void PixelSize(const HWND window, int& width, int& height)
  {
    width = 0;
    height = 0;
    if (TestWindow* const found = Find(window)) {
      SDL_GetWindowSizeInPixels(found->window, &width, &height);
    }
  }

  float DisplayScale(const HWND window)
  {
    TestWindow* const found = Find(window);
    return (found != nullptr) ? SDL_GetWindowDisplayScale(found->window) : 0.0f;
  }
} // namespace faf_vk_test
