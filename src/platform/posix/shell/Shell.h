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
} // namespace faf_posix::shell
