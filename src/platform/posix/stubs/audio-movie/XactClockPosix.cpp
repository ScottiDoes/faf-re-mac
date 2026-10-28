// POSIX build: replaces moho/audio/XactClock.cpp (excluded in
// macos/exclusions.d/audio-movie.cmake).
//
// XactClock.cpp rewrites the KERNEL32 import table of xactengine2_9.dll (PE image
// walking, VirtualProtect, GetModuleHandleExW) so XACT 2.9 times itself from process
// start instead of Windows uptime. There is no XACT DLL on POSIX, and the only caller
// (func_RetreiveXACTCOMInterface) runs only with a live XACT instance, which the POSIX
// build never has. Milestone 3 (FAudio's XACT3) needs no patch: FAudio's clock is its
// own and does not wrap at 2^31 ms.

#include "moho/audio/XactClock.h"

namespace moho
{
  void SND_InstallXactClock(const IXACTEngine& engine)
  {
    (void)engine;
    FAF_POSIX_STUB("SND_InstallXactClock: no XACT DLL to patch on this platform");
  }
} // namespace moho
