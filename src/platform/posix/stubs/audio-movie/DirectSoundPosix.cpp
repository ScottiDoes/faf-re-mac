// POSIX build: DirectSoundCreate for the dsound.h stand-in (include/dsound.h).
//
// DirectSound is Windows-only. Reports what DirectSoundCreate reports on a Windows
// machine without a sound driver, DSERR_NODRIVER with a null device, so both callers
// take their existing no-device paths:
// - func_InitSound (moho/audio/AudioEngine.cpp) logs "Failed to create DirectSound."
//   and keeps the stereo speaker layout;
// - CMovieManager::CreateDirectSound (moho/misc/StartupHelpers.cpp) logs the same and
//   the movie player runs without audio, on its steady clock.

#include "platform/posix/stubs/audio-movie/include/dsound.h"

extern "C" HRESULT WINAPI DirectSoundCreate(const GUID* const pcGuidDevice, LPDIRECTSOUND* const ppDS, void* const pUnkOuter)
{
  (void)pcGuidDevice;
  (void)pUnkOuter;
  FAF_POSIX_STUB("DirectSoundCreate: no DirectSound on this platform (DSERR_NODRIVER)");
  if (ppDS == nullptr) {
    return DSERR_INVALIDPARAM;
  }
  *ppDS = nullptr;
  return DSERR_NODRIVER;
}
