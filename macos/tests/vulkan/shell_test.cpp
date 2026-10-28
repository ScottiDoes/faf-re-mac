// The Vulkan gal device on windows of the real shell (src/platform/posix/shell), the way
// the game makes them (CScApp::CreateAppFrame): a frame HWND and a child "viewport" HWND
// filling its client area; head.mHandle = the frame, head.mWindow = the viewport, the
// head's size = GetClientRect(viewport) in drawable pixels. So VulkanSurfacePosix gets
// its CAMetalLayer from shell::MetalLayer for a child window, as in the game. Frames of
// Clear + Present, then the engine's WM_SIZE path: the frame resized (SetWindowPos), the
// viewport moved to the new client size, Device::Reset at that size.
//
//   vk_shell_test [--frames N] [--resize-at N]
//
// Engine services are the engine's own (no test doubles). Driver and validation as
// vk_device_test.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/OutputContext.hpp"
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

  void Pump()
  {
    faf_posix::shell::PumpEvents();
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
  }

  /** Resizes the frame so its client area is `width` x `height` pixels and lays the viewport over it. */
  void SizeFrame(HWND frame, HWND viewport, int width, int height)
  {
    RECT outer{0, 0, width, height};
    ::AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0U);
    ::SetWindowPos(frame, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump();
    RECT client{};
    ::GetClientRect(frame, &client);
    ::MoveWindow(viewport, 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
    Pump();
  }
} // namespace

int main(int argc, char** argv)
{
  static StderrLog* const log = new StderrLog();
  static_cast<void>(log);
  const int frames = Option(argc, argv, "--frames", 120);
  const int resizeAt = Option(argc, argv, "--resize-at", 60);

  if (!faf_posix::shell::Init()) {
    std::fprintf(stderr, "test: shell::Init failed\n");
    std::_Exit(3);
  }
  RECT outer{0, 0, 640, 400};
  ::AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0U);
  const HWND frame = ::CreateWindowExW(
    0U, L"FafVulkanShellTestFrame", L"faf-re Vulkan shell test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, nullptr, nullptr
  );
  RECT client{};
  ::GetClientRect(frame, &client);
  const HWND viewport = ::CreateWindowExW(
    0U, L"FafVulkanShellTestViewport", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, client.right - client.left, client.bottom - client.top,
    frame, nullptr, nullptr, nullptr
  );
  if (frame == nullptr || viewport == nullptr) {
    std::fprintf(stderr, "test: CreateWindowExW failed (%u)\n", static_cast<unsigned>(::GetLastError()));
    std::_Exit(3);
  }
  ::ShowWindow(frame, SW_SHOWNA);
  Pump();
  ::GetClientRect(viewport, &client);
  std::fprintf(stderr, "test: frame %p, viewport %p, viewport client %ldx%ld pixels, layer %p\n", static_cast<void*>(frame),
               static_cast<void*>(viewport), static_cast<long>(client.right - client.left), static_cast<long>(client.bottom - client.top),
               faf_posix::shell::MetalLayer(viewport));

  gpg::gal::DeviceContext context(gpg::gal::DeviceApi::Vulkan);
  context.mVSync = true;
  gpg::gal::Head head{};
  head.mHandle = frame;
  head.mWindow = viewport;
  head.mWindowed = true;
  head.mWidth = static_cast<std::uint32_t>(client.right - client.left);
  head.mHeight = static_cast<std::uint32_t>(client.bottom - client.top);
  head.framesPerSecond = 60;
  context.AddHead(head);

  int status = 0;
  int presented = 0;
  try {
    gpg::gal::Device::Create(&context);
    gpg::gal::Device* const device = gpg::gal::Device::GetInstance();
    std::uint32_t width = head.mWidth;
    std::uint32_t height = head.mHeight;
    for (int frameIndex = 0; frameIndex < frames; ++frameIndex) {
      Pump();
      if (frameIndex == resizeAt) {
        SizeFrame(frame, viewport, 800, 520);
        ::GetClientRect(viewport, &client);
        width = static_cast<std::uint32_t>(client.right - client.left);
        height = static_cast<std::uint32_t>(client.bottom - client.top);
        std::fprintf(stderr, "test: frame %d: viewport now %ux%u pixels; Reset\n", frameIndex, width, height);
        gpg::gal::DeviceContext resized(*device->GetDeviceContext());
        resized.GetHead(0).mWidth = width;
        resized.GetHead(0).mHeight = height;
        device->Reset(&resized);
      }
      const bool after = frameIndex >= resizeAt;
      device->BeginScene();
      device->ClearTarget(device->GetHeadOutputContext(0));
      device->Clear(true, true, true, after ? 0xFF2040C0U : 0xFFC03020U, 1.0f, 0);
      D3DVIEWPORT9 viewportRect{};
      viewportRect.Width = width / 4U;
      viewportRect.Height = height / 4U;
      viewportRect.X = (static_cast<std::uint32_t>(frameIndex) * 8U) % (width - viewportRect.Width);
      viewportRect.Y = height / 2U - viewportRect.Height / 2U;
      viewportRect.MaxZ = 1.0f;
      device->SetViewport(&viewportRect);
      device->Clear(true, false, false, after ? 0xFFF0E030U : 0xFF30D040U, 1.0f, 0);
      viewportRect = D3DVIEWPORT9{};
      viewportRect.Width = 16U;
      viewportRect.Height = 16U;
      viewportRect.MaxZ = 1.0f;
      device->SetViewport(&viewportRect);
      device->Clear(true, false, false, 0xFFFFFFFFU, 1.0f, 0);
      viewportRect.Width = width;
      viewportRect.Height = height;
      device->SetViewport(&viewportRect);
      device->EndScene();
      device->Present();
      ++presented;
    }
  } catch (const gpg::gal::Error& error) {
    std::fprintf(stderr, "test: gal error %s(%d): %s\n", error.GetFile(), error.GetLine(), error.what());
    status = 1;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "test: exception: %s\n", error.what());
    status = 1;
  }
  gpg::gal::Device::DestroyInstance();
  ::DestroyWindow(frame);
  Pump();
  faf_posix::shell::Shutdown();
  std::fprintf(stderr, "test: %d of %d frames presented, %s\n", presented, frames, (status == 0) ? "PASS" : "FAIL");
  std::fflush(stderr);
  std::_Exit(status);
}
