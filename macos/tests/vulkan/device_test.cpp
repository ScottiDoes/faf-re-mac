// The Vulkan gal device on a real window (macos/tests/vulkan, see vulkan.md):
// gpg::gal::Device::Create(DeviceApi::Vulkan) for a head whose window is a test HWND
// of the SDL3 shell double, then frames of Clear + Present: a background colour per
// phase and a box moved with D3D9 viewport clears, one engine-style resize (window
// resized, DeviceContext head resized, Device::Reset), optionally a minimize/restore
// and the out-of-date and no-image paths (FAF_VULKAN_TEST_* diagnostics).
//
//   vk_device_test [--frames N] [--resize-at N] [--reset 0|1] [--minimize-at N] [--vsync 0|1] [--head-divisor N]
//
// Driver: FAF_VULKAN_DRIVER=moltenvk|kosmickrisp. Validation: FAF_VULKAN_VALIDATION=1
// (its messages are logged as "Vulkan validation: ..."). Exit 0 when every frame
// presented without a gal error.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ShellDouble.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/backends/common/RuntimeInfo.h"
#include "platform/posix/shell/Shell.h"

namespace
{
  class StderrLog final : public gpg::LogTarget
  {
  public:
    StderrLog()
      : gpg::LogTarget(true)
    {}

    void OnMessage(
      const gpg::LogSeverity level, const msvc8::string& message, const msvc8::vector<msvc8::string>& context, const int previousDepth
    ) override
    {
      static_cast<void>(context);
      static_cast<void>(previousDepth);
      std::fprintf(stderr, "%s: %s\n", (level == gpg::LogSeverity::Warn) ? "warning" : "info", message.c_str());
    }
  };

  int Option(int argc, char** argv, const char* name, const int fallback)
  {
    for (int index = 1; index + 1 < argc; ++index) {
      if (std::strcmp(argv[index], name) == 0) {
        return std::atoi(argv[index + 1]);
      }
    }
    return fallback;
  }

  std::uint32_t Argb(const std::uint32_t r, const std::uint32_t g, const std::uint32_t b)
  {
    return 0xFF000000U | (r << 16U) | (g << 8U) | b;
  }
} // namespace

int main(int argc, char** argv)
{
  // Never destroyed: the engine's statics are not built for an orderly exit (the
  // runner leaves with _exit too), so the test leaves the same way.
  static StderrLog* const log = new StderrLog();
  static_cast<void>(log);
  const int frames = Option(argc, argv, "--frames", 120);
  const int resizeAt = Option(argc, argv, "--resize-at", 60);
  const int minimizeAt = Option(argc, argv, "--minimize-at", -1);
  const bool vsync = Option(argc, argv, "--vsync", 1) != 0;
  // --reset 0: the window is resized but the device not reset (the engine missed a
  // WM_SIZE): the swap chain stays suboptimal and the layer scales it.
  const bool resetOnResize = Option(argc, argv, "--reset", 1) != 0;

  if (!faf_posix::shell::Init()) {
    return 3;
  }
  const HWND window = faf_vk_test::CreateTestWindow("faf-re Vulkan device test", 320, 200);
  if (window == nullptr) {
    return 3;
  }
  int width = 0;
  int height = 0;
  faf_vk_test::PixelSize(window, width, height);
  // --head-divisor N: the head asks for 1/N of the window (a back buffer the layer must fit).
  std::fprintf(stderr, "test: window 320x200 points = %dx%d pixels (scale %.2f)\n", width, height, faf_vk_test::DisplayScale(window));
  const int divisor = std::max(Option(argc, argv, "--head-divisor", 1), 1);
  width /= divisor;
  height /= divisor;

  gpg::gal::DeviceContext context(gpg::gal::DeviceApi::Vulkan);
  context.mVSync = vsync;
  gpg::gal::Head head{};
  head.mHandle = window;
  head.mWindow = window;
  head.mWindowed = true;
  head.mWidth = static_cast<std::uint32_t>(width);
  head.mHeight = static_cast<std::uint32_t>(height);
  head.framesPerSecond = 60;
  context.AddHead(head);

  int presented = 0;
  int status = 0;
  try {
    gpg::gal::Device::Create(&context);
    gpg::gal::Device* const device = gpg::gal::Device::GetInstance();
    for (int frame = 0; frame < frames; ++frame) {
      faf_posix::shell::PumpEvents();
      if (frame == resizeAt) {
        // What the game's frame does on WM_SIZE (SyncSupComFrameClientSizeAndViewport):
        // a copy of the context with the head at the new client size, then Reset.
        faf_vk_test::ResizeTestWindow(window, 400, 260);
        int newWidth = 0;
        int newHeight = 0;
        faf_vk_test::PixelSize(window, newWidth, newHeight);
        std::fprintf(stderr, "test: frame %d: resized to %dx%d pixels%s\n", frame, newWidth, newHeight, resetOnResize ? "; Reset" : "; no Reset");
        if (resetOnResize) {
          width = newWidth;
          height = newHeight;
          gpg::gal::DeviceContext resized(*device->GetDeviceContext());
          resized.GetHead(0).mWidth = static_cast<std::uint32_t>(width);
          resized.GetHead(0).mHeight = static_cast<std::uint32_t>(height);
          device->Reset(&resized);
        }
      }
      if (frame == minimizeAt) {
        std::fprintf(stderr, "test: frame %d: minimize\n", frame);
        faf_vk_test::MinimizeTestWindow(window);
      }
      if (minimizeAt >= 0 && frame == minimizeAt + 10) {
        std::fprintf(stderr, "test: frame %d: restore\n", frame);
        faf_vk_test::RestoreTestWindow(window);
      }

      const gpg::gal::OutputContext* const output = device->GetHeadOutputContext(0);
      const std::uint32_t targetWidth = static_cast<std::uint32_t>(width);
      const std::uint32_t targetHeight = static_cast<std::uint32_t>(height);
      device->BeginScene();
      device->ClearTarget(output);
      // Background: red-brown before the resize, blue after.
      const bool resized = frame >= resizeAt;
      device->Clear(true, true, true, resized ? Argb(0x20, 0x40, 0xC0) : Argb(0xC0, 0x30, 0x20), 1.0f, 0);
      // A box that moves across, cleared through a viewport (D3D9: Clear covers the viewport).
      D3DVIEWPORT9 viewport{};
      viewport.Width = targetWidth / 4U;
      viewport.Height = targetHeight / 4U;
      viewport.X = (static_cast<std::uint32_t>(frame) * 8U) % (targetWidth - viewport.Width);
      viewport.Y = targetHeight / 2U - viewport.Height / 2U;
      viewport.MaxZ = 1.0f;
      device->SetViewport(&viewport);
      device->Clear(true, false, false, resized ? Argb(0xF0, 0xE0, 0x30) : Argb(0x30, 0xD0, 0x40), 1.0f, 0);
      // A white corner marker: top-left 16x16 pixels.
      viewport = D3DVIEWPORT9{};
      viewport.Width = 16U;
      viewport.Height = 16U;
      viewport.MaxZ = 1.0f;
      device->SetViewport(&viewport);
      device->Clear(true, false, false, Argb(0xFF, 0xFF, 0xFF), 1.0f, 0);
      viewport.Width = targetWidth;
      viewport.Height = targetHeight;
      device->SetViewport(&viewport);
      device->EndScene();
      device->Present();
      ++presented;
    }
    gpg::gal::DeviceRuntimeInfo info;
    if (device->GetRuntimeInfo(info)) {
      std::fprintf(stderr, "test: %s, %s, %s, driver %s, swap chain %s\n", info.api.c_str(), info.adapter.c_str(), info.featureLevel.c_str(),
                   info.driverVersion.c_str(), info.swapChain.c_str());
    }
  } catch (const gpg::gal::Error& error) {
    std::fprintf(stderr, "test: gal error %s(%d): %s\n", error.GetFile(), error.GetLine(), error.what());
    status = 1;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "test: exception: %s\n", error.what());
    status = 1;
  }
  gpg::gal::Device::DestroyInstance();
  faf_vk_test::DestroyTestWindow(window);
  faf_posix::shell::Shutdown();
  std::fprintf(stderr, "test: %d of %d frames presented, %s\n", presented, frames, (status == 0) ? "PASS" : "FAIL");
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(status);
}
