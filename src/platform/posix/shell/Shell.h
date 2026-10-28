#pragma once

// The window system of the POSIX build (SDL3), as the rest of the port sees it.
// Lead-owned contract: the shell package implements it in src/platform/posix/shell/;
// the Window area and the wx stand-in create and drive windows through it, the Vulkan
// backend presents into a window's CAMetalLayer, the app loop pumps its events.
//
// Rules:
// - Main thread only (SDL video and AppKit require it).
// - Every coordinate Win32 code sees (client and window rects, mouse positions, monitor
//   sizes, swapchain extents) is in drawable pixels, so a Retina window is as many
//   pixels wide as its drawable and the engine lays out its UI in pixels, as on Windows.
// - Headless runs (faf_replay without a window) never call Init(); every function then
//   answers "no window system": IsActive() false, MetalLayer() null, no events.

namespace faf_posix::shell
{
  // Starts SDL's video and event subsystems once. Returns false and logs why on failure.
  bool Init();

  // Destroys every window the shell created and quits SDL. Safe without Init().
  void Shutdown();

  // True between a successful Init() and Shutdown().
  [[nodiscard]] bool IsActive() noexcept;

  // The CAMetalLayer* (as void*) that presents the client area of `window`, a top-level
  // or child HWND from the Window area, sized to the client area in drawable pixels
  // (contentsScale set for the display). The Vulkan backend passes it to
  // VK_EXT_metal_surface. Null when the HWND has no native window.
  [[nodiscard]] void* MetalLayer(HWND window);

  // Moves the pending native events into the Win32 message flow (window procedures, the
  // wx stand-in's event handlers, input state for GetKeyState/GetCursorPos). Returns true
  // when it delivered at least one. Never blocks.
  bool PumpEvents();

  // Blocks until a native event is pending or `milliseconds` pass (negative: no limit).
  // The app loop's MsgWaitForMultipleObjects uses it to wake on input.
  bool WaitForEvent(int milliseconds);

  // What a pixel is. Every coordinate Win32 code sees is in pixels (rule above), and by
  // default a pixel is a pixel of the display PANEL, whatever macOS's desktop scaling:
  // a window covering half of a 4K panel is 1920x1080 and the game draws 1920x1080 (macOS
  // resamples a scaled desktop), and full screen switches the display to a real mode at
  // one pixel per point - the panel's native resolution by default - so every game pixel
  // is a panel pixel. FAF_SHELL_HIGHDPI=1 uses the backing store's pixels instead (2 per
  // point on any Retina desktop), FAF_SHELL_HIGHDPI=0 one pixel per point (Panel.h).
  //
  // The game's full-screen resolutions on the main display: its modes at one pixel per
  // point with the panel's shape, one per size at its fastest refresh rate, the panel's
  // native resolution first (index 0, the default). False past the end or before Init().
  bool FullscreenMode(int index, int& width, int& height, int& refreshRate) noexcept;

  // The game's borderless frame of this client size (CScApp, SC_PrimaryAdapter: a
  // primary_adapter resolution) goes into exclusive full screen at the display mode of that
  // size and the nearest refresh rate; 0x0 cancels. A borderless frame that covers its
  // display without such a request goes into desktop full screen.
  void SetFullscreenMode(int width, int height, int refreshRate) noexcept;

  // The full-screen mode for a requested resolution: the request when it is one of
  // FullscreenMode's (its refresh rate when the request has none), else the panel's native
  // resolution - so FA's default 1024,768,60, or a size this display lacks, opens at native.
  // False (request unchanged) before Init() or without modes.
  bool ResolveFullscreenMode(int& width, int& height, int& refreshRate) noexcept;
} // namespace faf_posix::shell
