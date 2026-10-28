#pragma once

// The display panel: the scale between SDL's points and the pixels the shell gives Win32
// code (Shell.h), the panel's native resolution, and the game's exclusive full-screen modes.
// Panel.cpp.

#include <SDL3/SDL.h>

namespace faf_posix::shell::detail
{
  // How many pixels a point is (FAF_SHELL_HIGHDPI):
  // - Backing (default): the current display mode's own pixels - the backing store on the
  //   desktop (2 on a Retina desktop, so macOS only ever shrinks the game's image once, as
  //   it does every app's), and 1 in a full-screen mode the game set (every game pixel a
  //   panel pixel at the native mode).
  // - Points ("0"): one pixel per point.
  enum class PixelMode
  {
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
