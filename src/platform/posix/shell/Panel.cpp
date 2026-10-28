// The display panel (Panel.h, Shell.h): what a pixel is for the game on macOS, the
// display's native panel resolution, and the full-screen modes the game offers.
//
// A window's pixels are the backing store's: on a scaled desktop (a 4K panel that "looks
// like" 3008x1692 is composited at 6016x3384 and shrunk to 3840x2160 by the display) no
// app reaches the panel's pixels, and drawing at the backing resolution leaves one
// downscale, the sharpest result. Full screen switches the display to a real mode at one
// pixel per point - the panel's native one by default - so every game pixel is a panel
// pixel. The player sets FAF's UI Scale.

#include "platform/posix/shell/Panel.h"

#include "platform/posix/shell/Shell.h"
#include "platform/posix/shell/ShellState.h"

#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace faf_posix::shell
{
  namespace
  {
    // UiScaleCorrection's last result (LastUiScaleCorrection).
    std::atomic<float> gLastUiScaleCorrection{1.0f};

    struct FullscreenEntry
    {
      int width = 0;
      int height = 0;
      int refreshRate = 0;
    };

    struct PanelState
    {
      std::mutex lock;
      std::map<SDL_DisplayID, std::pair<int, int>> native; // known native resolutions (0x0: none)
      std::vector<FullscreenEntry> modes;                  // the main display's list, built once
      bool modesBuilt = false;
      FullscreenEntry pending;                              // SetFullscreenMode
    };

    PanelState& P() noexcept
    {
      static PanelState state;
      return state;
    }

    // macOS's IOKit display mode flag for the panel's native timing (IOGraphicsTypes.h).
    constexpr std::uint32_t kDisplayModeNativeFlag = 0x02000000U;

    CGDirectDisplayID CoreGraphicsDisplay(const SDL_DisplayID display) noexcept
    {
      SDL_Rect bounds{};
      if (!SDL_GetDisplayBounds(display, &bounds)) {
        return CGMainDisplayID();
      }
      // SDL's display bounds are in the same global points CoreGraphics uses (origin at the
      // main display's top left).
      const CGPoint centre = CGPointMake(bounds.x + bounds.w / 2.0, bounds.y + bounds.h / 2.0);
      CGDirectDisplayID id = 0;
      std::uint32_t count = 0;
      if (CGGetDisplaysWithPoint(centre, 1, &id, &count) != kCGErrorSuccess || count == 0) {
        return CGMainDisplayID();
      }
      return id;
    }

    std::pair<int, int> FindNative(const CGDirectDisplayID id) noexcept
    {
      const void* keys[] = {kCGDisplayShowDuplicateLowResolutionModes};
      const void* values[] = {kCFBooleanTrue};
      CFDictionaryRef options = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
      CFArrayRef all = CGDisplayCopyAllDisplayModes(id, options);
      if (options != nullptr) {
        CFRelease(options);
      }
      if (all == nullptr) {
        return {0, 0};
      }
      std::pair<int, int> native{0, 0};
      std::pair<int, int> largestUnscaled{0, 0};
      for (CFIndex i = 0; i < CFArrayGetCount(all); ++i) {
        CGDisplayModeRef mode = static_cast<CGDisplayModeRef>(const_cast<void*>(CFArrayGetValueAtIndex(all, i)));
        const int pixelWidth = static_cast<int>(CGDisplayModeGetPixelWidth(mode));
        const int pixelHeight = static_cast<int>(CGDisplayModeGetPixelHeight(mode));
        if ((CGDisplayModeGetIOFlags(mode) & kDisplayModeNativeFlag) != 0 && pixelWidth > native.first) {
          native = {pixelWidth, pixelHeight};
        }
        if (static_cast<int>(CGDisplayModeGetWidth(mode)) == pixelWidth && pixelWidth > largestUnscaled.first) {
          largestUnscaled = {pixelWidth, pixelHeight};
        }
      }
      CFRelease(all);
      return native.first > 0 ? native : largestUnscaled;
    }

    void BuildModes(PanelState& p) noexcept
    {
      p.modes.clear();
      p.modesBuilt = true;
      const SDL_DisplayID display = SDL_GetPrimaryDisplay();
      int nativeWidth = 0;
      int nativeHeight = 0;
      const bool haveNative = detail::NativePixels(display, nativeWidth, nativeHeight);
      int count = 0;
      SDL_DisplayMode** const modes = SDL_GetFullscreenDisplayModes(display, &count);
      for (int i = 0; modes != nullptr && i < count; ++i) {
        const SDL_DisplayMode* const mode = modes[i];
        // Real resolutions only: one pixel per point, so the size is the pixels drawn.
        if (mode->pixel_density != 1.0f) {
          continue;
        }
        // The panel's own shape (a 4:3 mode on a 16:9 panel would be stretched).
        if (haveNative && std::abs(mode->w * nativeHeight - mode->h * nativeWidth) > nativeWidth * nativeHeight / 100) {
          continue;
        }
        const int refresh = mode->refresh_rate > 0.0f ? static_cast<int>(std::lround(mode->refresh_rate)) : 60;
        auto same = std::find_if(p.modes.begin(), p.modes.end(), [mode](const FullscreenEntry& e) {
          return e.width == mode->w && e.height == mode->h;
        });
        if (same == p.modes.end()) {
          p.modes.push_back(FullscreenEntry{mode->w, mode->h, refresh});
        } else if (refresh > same->refreshRate) {
          same->refreshRate = refresh; // one entry per size, at its fastest refresh rate
        }
      }
      SDL_free(modes);
      // The native resolution first (the default), then the rest largest first.
      std::stable_sort(p.modes.begin(), p.modes.end(), [&](const FullscreenEntry& a, const FullscreenEntry& b) {
        const bool aNative = a.width == nativeWidth && a.height == nativeHeight;
        const bool bNative = b.width == nativeWidth && b.height == nativeHeight;
        if (aNative != bNative) {
          return aNative;
        }
        return a.width * a.height > b.width * b.height;
      });
    }
  } // namespace

  namespace detail
  {
    PixelMode PixelModeSetting() noexcept
    {
      static const PixelMode mode = [] {
        const char* const value = std::getenv("FAF_SHELL_HIGHDPI");
        return value != nullptr && std::strcmp(value, "0") == 0 ? PixelMode::Points : PixelMode::Backing;
      }();
      return mode;
    }

    bool NativePixels(const SDL_DisplayID display, int& width, int& height) noexcept
    {
      PanelState& p = P();
      std::lock_guard<std::mutex> guard(p.lock);
      auto it = p.native.find(display);
      if (it == p.native.end()) {
        it = p.native.emplace(display, FindNative(CoreGraphicsDisplay(display))).first;
      }
      width = it->second.first;
      height = it->second.second;
      return width > 0 && height > 0;
    }

    float DisplayScale(const SDL_DisplayID display) noexcept
    {
      if (PixelModeSetting() == PixelMode::Points) {
        return 1.0f;
      }
      // The current mode's own pixels: the desktop's backing store, or a full-screen mode.
      const SDL_DisplayMode* const current = SDL_GetCurrentDisplayMode(display);
      return current != nullptr && current->pixel_density > 0.0f ? current->pixel_density : 1.0f;
    }

    void ForgetDisplays() noexcept
    {
      PanelState& p = P();
      std::lock_guard<std::mutex> guard(p.lock);
      p.native.clear();
      p.modes.clear();
      p.modesBuilt = false;
    }

    const SDL_DisplayMode* ExclusiveMode(const SDL_DisplayID display, const int width, const int height, const int refreshRate) noexcept
    {
      static SDL_DisplayMode chosen{};
      int count = 0;
      SDL_DisplayMode** const modes = SDL_GetFullscreenDisplayModes(display, &count);
      const SDL_DisplayMode* best = nullptr;
      for (int i = 0; modes != nullptr && i < count; ++i) {
        const SDL_DisplayMode* const mode = modes[i];
        if (mode->pixel_density != 1.0f || mode->w != width || mode->h != height) {
          continue;
        }
        if (best == nullptr || std::fabs(mode->refresh_rate - refreshRate) < std::fabs(best->refresh_rate - refreshRate)) {
          best = mode;
        }
      }
      if (best != nullptr) {
        chosen = *best;
      }
      SDL_free(modes);
      return best != nullptr ? &chosen : nullptr;
    }

    bool PendingFullscreenMode(int& width, int& height, int& refreshRate) noexcept
    {
      PanelState& p = P();
      std::lock_guard<std::mutex> guard(p.lock);
      width = p.pending.width;
      height = p.pending.height;
      refreshRate = p.pending.refreshRate;
      return width > 0 && height > 0;
    }
  } // namespace detail

  bool FullscreenMode(const int index, int& width, int& height, int& refreshRate) noexcept
  {
    if (!IsActive() || index < 0) {
      return false;
    }
    PanelState& p = P();
    {
      std::lock_guard<std::mutex> guard(p.lock);
      if (p.modesBuilt) {
        if (index >= static_cast<int>(p.modes.size())) {
          return false;
        }
        width = p.modes[index].width;
        height = p.modes[index].height;
        refreshRate = p.modes[index].refreshRate;
        return true;
      }
    }
    // Built outside the lock: BuildModes asks NativePixels, which takes it.
    PanelState scratch;
    BuildModes(scratch);
    {
      std::lock_guard<std::mutex> guard(p.lock);
      p.modes = std::move(scratch.modes);
      p.modesBuilt = true;
    }
    return FullscreenMode(index, width, height, refreshRate);
  }

  bool ResolveFullscreenMode(int& width, int& height, int& refreshRate) noexcept
  {
    int modeWidth = 0;
    int modeHeight = 0;
    int modeRefresh = 0;
    for (int index = 0; FullscreenMode(index, modeWidth, modeHeight, modeRefresh); ++index) {
      if (modeWidth == width && modeHeight == height) {
        if (refreshRate <= 0) {
          refreshRate = modeRefresh;
        }
        return true;
      }
    }
    if (!FullscreenMode(0, modeWidth, modeHeight, modeRefresh)) {
      return false;
    }
    width = modeWidth;
    height = modeHeight;
    refreshRate = modeRefresh;
    return true;
  }

  namespace
  {
    float ComputeUiScaleCorrection() noexcept
    {
      if (!IsActive()) {
        return 1.0f;
      }
      // The display of the game's window (the first visible top-level one), else the main one.
      SDL_DisplayID display = 0;
      {
        std::lock_guard<std::recursive_mutex> guard(detail::S().lock);
        for (const HWND handle : detail::S().topLevels) {
          const detail::WindowRecord* const top = detail::Find(handle);
          if (top != nullptr && top->sdl != nullptr && top->visible) {
            display = SDL_GetDisplayForWindow(top->sdl);
            break;
          }
        }
      }
      if (display == 0) {
        display = SDL_GetPrimaryDisplay();
      }
      const SDL_DisplayMode* const current = SDL_GetCurrentDisplayMode(display);
      int nativeWidth = 0;
      int nativeHeight = 0;
      if (current == nullptr || current->w <= 0 || !detail::NativePixels(display, nativeWidth, nativeHeight)) {
        return 1.0f;
      }
      const float backingWidth = current->w * (current->pixel_density > 0.0f ? current->pixel_density : 1.0f);
      return std::max(1.0f, backingWidth / static_cast<float>(nativeWidth));
    }
  } // namespace

  float UiScaleCorrection() noexcept
  {
    const float correction = ComputeUiScaleCorrection();
    gLastUiScaleCorrection.store(correction, std::memory_order_relaxed);
    return correction;
  }

  float LastUiScaleCorrection() noexcept
  {
    return gLastUiScaleCorrection.load(std::memory_order_relaxed);
  }

  void SetFullscreenMode(const int width, const int height, const int refreshRate) noexcept
  {
    PanelState& p = P();
    std::lock_guard<std::mutex> guard(p.lock);
    p.pending = FullscreenEntry{std::max(width, 0), std::max(height, 0), std::max(refreshRate, 0)};
  }
} // namespace faf_posix::shell
