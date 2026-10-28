#pragma once

// Panel pixels: the scale between SDL's points and the pixels the shell gives Win32 code
// (Shell.h), and the game's exclusive full-screen modes. Panel.cpp.

#include <SDL3/SDL.h>

namespace faf_posix::shell::detail
{
  // How many pixels a point is (FAF_SHELL_HIGHDPI):
  // - Panel (default): the display panel's own pixels. On the desktop that is the panel's
  //   native width over the desktop's width in points (1.28 for a 4K panel that "looks like"
  //   3008x1692, 2 for a MacBook at its default setting); in a full-screen mode the game set,
  //   the mode's own pixels (the panel scales a lower mode up itself).
  // - Backing ("1"): the window's backing store (2 on any Retina desktop).
  // - Points ("0"): one pixel per point.
  enum class PixelMode
  {
    Panel,
    Backing,
    Points,
  };
  [[nodiscard]] PixelMode PixelModeSetting() noexcept;

  // Pixels per point on `display`, by the setting above.
  [[nodiscard]] float DisplayScale(SDL_DisplayID display) noexcept;

  // The panel's native resolution (the modes macOS flags native), false when unknown.
  [[nodiscard]] bool NativePixels(SDL_DisplayID display, int& width, int& height) noexcept;

  // Forgets what was learned about the displays (they changed).
  void ForgetDisplays() noexcept;

  // The display mode for an exclusive full-screen request of this size, in the display's
  // pixels at one pixel per point, with the refresh rate nearest `refreshRate`; null when
  // the display has no such mode.
  [[nodiscard]] const SDL_DisplayMode* ExclusiveMode(SDL_DisplayID display, int width, int height, int refreshRate) noexcept;

  // The pending SetFullscreenMode request (Shell.h); false when there is none.
  [[nodiscard]] bool PendingFullscreenMode(int& width, int& height, int& refreshRate) noexcept;
} // namespace faf_posix::shell::detail
