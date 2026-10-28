// POSIX build: the two X3DAudio entry points moho/audio/AudioEngine.cpp declares.
//
// X3DAudio (x3daudio1_7.dll) is Windows-only. Both entry points are reached only through
// an AudioEngine with a live XACT instance (func_AudioInitialize from the AudioEngine
// constructor after IXACTEngine::Initialize, func_X3DAudioCalculate from
// AudioEngine::Calculate3D, which returns first when mInstance is null). The POSIX build
// has no XACT instance (func_InitSound runs it as with /nosound), so these never run;
// they exist so the engine links. Milestone 3: FAudio ships F3DAudio, whose
// F3DAudioInitialize / F3DAudioCalculate take the same arguments and replace these.

#include <cstdint>
#include <cstring>

#include "moho/audio/AudioEngine.h"

namespace
{
  // X3DAUDIO_HANDLE is 20 bytes; AudioEngineImpl::mAudioHandle reserves exactly that.
  constexpr std::size_t kX3dAudioHandleBytes = sizeof(moho::AudioEngineImpl::mAudioHandle);
  static_assert(kX3dAudioHandleBytes == 20, "X3DAUDIO_HANDLE is 20 bytes");
} // namespace

extern "C" void X3DAudioInitialize(const std::uint32_t speakerChannelMask, const float speedOfSound, void* const instance)
{
  (void)speakerChannelMask;
  (void)speedOfSound;
  FAF_POSIX_STUB("X3DAudioInitialize: no X3DAudio on this platform");
  if (instance != nullptr) {
    std::memset(instance, 0, kX3dAudioHandleBytes);
  }
}

extern "C" void X3DAudioCalculate(
  const void* const instance,
  const moho::Audio3DListener* const listener,
  const moho::Audio3DEmitter* const emitter,
  const std::uint32_t flags,
  moho::Audio3DDspSettings* const dspSettings
)
{
  (void)instance;
  (void)listener;
  (void)emitter;
  (void)flags;
  (void)dspSettings;
  FAF_POSIX_STUB("X3DAudioCalculate: no X3DAudio on this platform; DSP settings left unchanged");
}
