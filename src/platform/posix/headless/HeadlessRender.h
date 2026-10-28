#pragma once

// Contract between the headless replay runner (src/platform/posix/runner/) and the render
// package's null device (gpg::gal::DeviceNull). Lead-owned; implemented by the render
// package, called by the runner. See macos/porting/render.md, "Headless replay".
//
// The runner does not call CScApp::CreateDevice (wx frames), REN_Init, REN_Frame or
// CD3DDevice::Refresh. Instead it calls REN_InitHeadless() where CScApp::AppInit would
// create the device. Afterwards the user-side session code that needs a device
// (CWldSession::DoBeat -> CWldTerrainRes::NotifyMapChange -> Finalize, texture and buffer
// creation, effect loading) runs unchanged against CPU-backed null resources: draws,
// clears and presents do nothing, locks return real memory.

namespace moho
{
  // Creates the null gal device and brings moho::D3D_GetDevice() to the state the
  // windowed start-up leaves it in (device context, resources, effects loaded as no-op
  // effects), with no window. Returns false (and logs why) on failure. Call once, on the
  // main thread, after the engine's common services and preferences are up.
  bool REN_InitHeadless();

  // Releases what REN_InitHeadless created. Safe to call when it failed or never ran.
  void REN_ShutdownHeadless();
} // namespace moho
