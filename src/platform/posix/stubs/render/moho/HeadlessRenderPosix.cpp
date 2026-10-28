// POSIX build: moho::REN_InitHeadless / moho::REN_ShutdownHeadless, the render side of the
// headless replay runner (contract: platform/posix/headless/HeadlessRender.h).
//
// REN_InitHeadless does what CScApp::CreateAppFrame does to the render device, with the null
// gal device (gpg::gal::DeviceNull) in place of a windowed one and no frame:
//
//   CD3DDevice::Clear2(true); gal::Device::Create(context); CD3DDevice::SetRenViewport(viewport);
//   CD3DDevice::Clear2(false);
//
// SetRenViewport is what leaves moho::D3D_GetDevice() in the windowed start-up state: head
// writer locks over the null back buffers, the graphics/shadow fidelity support levels,
// CD3DDeviceResources::InitResources(true) (every /effects/*.fx loaded as a null effect;
// a missing effect dies there exactly as on Windows) and the device-init broadcast. The
// viewport bound is moho::ren_Viewport when the caller has already created the game viewport
// (CreateAppFrame creates it before the device), else none: CD3DDevice::Paint then never
// renders, and nothing presents.
//
// REN_ShutdownHeadless is CScApp::Destroy's D3D_Exit: the index-sheet and particle singletons,
// then CD3DDevice::Destroy (device-exit broadcast, resources, gal::Device::DestroyInstance).

#include "platform/posix/headless/HeadlessRender.h"

#include <cstdint>
#include <exception>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "moho/misc/StartupHelpers.h"
#include "moho/render/WRenViewport.h"
#include "moho/render/d3d/CD3DDevice.h"

namespace
{
  bool sHeadlessUp = false;

  // CD3DDevice::Destroy without the app-level singletons: what a failed start leaves.
  void DestroyFailedDevice() noexcept
  {
    try {
      if (gpg::gal::Device::IsReady()) {
        moho::D3D_GetDevice()->Destroy();
      }
    } catch (...) {
    }
  }
} // namespace

bool moho::REN_InitHeadless()
{
  if (sHeadlessUp) {
    return true;
  }
  if (gpg::gal::Device::IsReady()) {
    gpg::Warnf("REN_InitHeadless: a gal device already exists; not replacing it");
    return false;
  }

  try {
    // One head, as CScApp::CreateDevice's default window: the default size, windowed
    // (mWindowed false: a frame window, not the borderless "fullscreen" style).
    gpg::gal::DeviceContext context(gpg::gal::DeviceApi::Null);
    context.mVSync = false;
    context.mValidate = false;
    gpg::gal::Head head{};
    head.mWindowed = false;
    head.mWidth = static_cast<std::uint32_t>(moho::wnd_DefaultCreateWidth);
    head.mHeight = static_cast<std::uint32_t>(moho::wnd_DefaultCreateHeight);
    head.framesPerSecond = 60;
    context.AddHead(head);

    moho::CD3DDevice* const device = moho::D3D_GetDevice();
    device->Clear2(true);
    gpg::gal::Device::Create(&context);
    device->SetRenViewport(moho::ren_Viewport);
    device->Clear2(false);
  } catch (const gpg::gal::Error& error) {
    gpg::Warnf("REN_InitHeadless: %s(%d) %s", error.GetFile(), error.GetLine(), error.what());
    DestroyFailedDevice();
    return false;
  } catch (const std::exception& exception) {
    gpg::Warnf("REN_InitHeadless: %s", exception.what());
    DestroyFailedDevice();
    return false;
  }

  sHeadlessUp = true;
  gpg::Logf(
    "REN_InitHeadless: null renderer up (%dx%d, %s)", moho::wnd_DefaultCreateWidth, moho::wnd_DefaultCreateHeight,
    (moho::ren_Viewport != nullptr) ? "game viewport bound" : "no viewport"
  );
  return true;
}

void moho::REN_ShutdownHeadless()
{
  if (!sHeadlessUp) {
    return;
  }
  sHeadlessUp = false;
  moho::D3D_Exit();
}
