// POSIX build: DirectSound for the dsound.h stand-in (include/dsound.h), on SDL3 audio.
//
// The engine uses DirectSound in two places, both unchanged from Windows:
// - func_InitSound (moho/audio/AudioEngine.cpp) creates a device, reads the speaker
//   configuration and releases it;
// - CMovieManager (moho/misc/StartupHelpers.cpp) creates the device the SFD movie player
//   streams embedded movie audio to: moho/movie/sfd/DirectSoundMovieSink.cpp (compiled
//   unchanged) makes a looping 48 kHz 16-bit stereo secondary buffer per movie, writes it
//   as a ring through Lock/Unlock and takes its playback clock from GetCurrentPosition.
//
// A device holds a reference on SDL's audio subsystem. A secondary buffer is a block of
// memory plus an SDL audio stream bound to its own logical device on the default
// playback device (SDL mixes logical devices with everything else that plays, FAudio's
// output included). While the buffer plays, SDL's get callback copies bytes from the
// play cursor on, wrapping when looping and stopping at the end otherwise, exactly what
// DirectSound's mixer reads; GetCurrentPosition reports the cursor less what SDL has
// queued but not yet consumed. Stop pauses the logical device and keeps the position;
// Play resumes it. Volume (hundredths of a decibel) is the stream's gain, frequency its
// frequency ratio. Return values are DirectSound's: DS_OK, DSERR_INVALIDPARAM,
// DSERR_CONTROLUNAVAIL without the matching DSBCAPS_CTRL* flag, DSERR_NODRIVER when SDL
// has no audio device.
//
// Locking: SDL holds a stream's lock while it runs that stream's callback, and the play
// state is guarded by that lock (SDL_LockAudioStream). SDL's device thread locks a
// device before its streams, so pausing or resuming a logical device happens after the
// stream lock is released.
//
// Test hook: with FAF_DSOUND_DUMP=<folder> in the environment, every secondary buffer
// also writes what SDL takes from it to <folder>/dsound-<n>-<rate>hz-<channels>ch-<bits>.raw
// (macos/tests/movies: the movie audio the game played, sample for sample).
//
// Not reproduced (the engine does not use them): pan (stored, not applied),
// DuplicateSoundBuffer, 3D buffers, effects and position notifications. A primary buffer
// (DSBCAPS_PRIMARYBUFFER) exists for its format calls only; SDL mixes the output.

#include "platform/posix/stubs/audio-movie/include/dsound.h"

#include "gpg/core/utils/Logging.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace
{
  // The subsystem reference each device and each buffer holds. Shell::Shutdown's
  // SDL_Quit may tear the subsystem down before the engine releases its last DirectSound
  // object; SDL then has destroyed every stream already.
  bool AcquireSdlAudio()
  {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      return true;
    }
    gpg::Warnf("DirectSound: SDL audio is unavailable: %s", SDL_GetError());
    return false;
  }

  bool SdlAudioRunning()
  {
    return SDL_WasInit(SDL_INIT_AUDIO) != 0;
  }

  void ReleaseSdlAudio()
  {
    if (SdlAudioRunning()) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
  }

  // DirectSound attenuation (hundredths of a decibel, DSBVOLUME_MIN..DSBVOLUME_MAX) as a
  // linear gain. DSBVOLUME_MIN is -100 dB, which DirectSound treats as silence.
  float GainOf(const LONG volume)
  {
    if (volume <= DSBVOLUME_MIN) {
      return 0.0f;
    }
    return static_cast<float>(std::pow(10.0, static_cast<double>(volume) / 2000.0));
  }

  bool SpecOf(const WAVEFORMATEX& format, SDL_AudioSpec& spec)
  {
    spec.channels = format.nChannels;
    spec.freq = static_cast<int>(format.nSamplesPerSec);
    if (format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample == 8) {
      spec.format = SDL_AUDIO_U8;
    } else if (format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample == 16) {
      spec.format = SDL_AUDIO_S16LE;
    } else if (format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample == 32) {
      spec.format = SDL_AUDIO_S32LE;
    } else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32) {
      spec.format = SDL_AUDIO_F32LE;
    } else {
      return false;
    }
    const DWORD block = static_cast<DWORD>(format.nChannels) * (format.wBitsPerSample / 8u);
    return format.nChannels >= 1 && format.nChannels <= 8 && format.nSamplesPerSec >= DSBFREQUENCY_MIN &&
      format.nSamplesPerSec <= DSBFREQUENCY_MAX && format.nBlockAlign == block;
  }

  class PosixSoundBuffer final : public IDirectSoundBuffer
  {
  public:
    static HRESULT Create(const DSBUFFERDESC& desc, LPDIRECTSOUNDBUFFER* const out)
    {
      const bool primary = (desc.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0;
      WAVEFORMATEX format{};
      SDL_AudioSpec spec{};
      if (primary) {
        if (desc.dwBufferBytes != 0 || desc.lpwfxFormat != nullptr) {
          return DSERR_INVALIDPARAM;
        }
        // DirectSound's default primary format.
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 22050;
        format.wBitsPerSample = 8;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = 44100;
      } else {
        if (desc.lpwfxFormat == nullptr || desc.dwBufferBytes < DSBSIZE_MIN || desc.dwBufferBytes > DSBSIZE_MAX) {
          return DSERR_INVALIDPARAM;
        }
        format = *desc.lpwfxFormat;
        format.cbSize = 0;
        if (!SpecOf(format, spec)) {
          return DSERR_BADFORMAT;
        }
      }

      auto* const buffer = new (std::nothrow) PosixSoundBuffer(desc, format, primary);
      if (buffer == nullptr) {
        return DSERR_OUTOFMEMORY;
      }
      if (!primary) {
        buffer->mData.assign(desc.dwBufferBytes - desc.dwBufferBytes % format.nBlockAlign, 0u);
        buffer->mFrequency = format.nSamplesPerSec;
        if (!AcquireSdlAudio()) {
          delete buffer;
          return DSERR_NODRIVER;
        }
        buffer->mHoldsAudio = true;
        if (const char* const dump = std::getenv("FAF_DSOUND_DUMP"); dump != nullptr && *dump != '\0') {
          static std::atomic<int> count{0};
          const std::string path = std::string(dump) + "/dsound-" + std::to_string(++count) + "-" +
            std::to_string(format.nSamplesPerSec) + "hz-" + std::to_string(format.nChannels) + "ch-" +
            std::to_string(format.wBitsPerSample) + ".raw";
          buffer->mDump = std::fopen(path.c_str(), "wb");
        }
        // A logical device of its own, opened paused; Play resumes it.
        buffer->mStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Feed, buffer);
        if (buffer->mStream == nullptr) {
          gpg::Warnf("DirectSound: cannot open an SDL audio stream: %s", SDL_GetError());
          buffer->Release();
          return DSERR_NODRIVER;
        }
      }
      *out = buffer;
      return DS_OK;
    }

    // --- IUnknown ----------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** const ppvObject) override
    {
      (void)riid;
      if (ppvObject == nullptr) {
        return E_POINTER;
      }
      *ppvObject = nullptr;
      return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
      return ++mRefs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
      const ULONG left = --mRefs;
      if (left == 0) {
        delete this;
      }
      return left;
    }

    // --- IDirectSoundBuffer ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetCaps(const LPDSBCAPS caps) override
    {
      if (caps == nullptr || caps->dwSize < sizeof(DSBCAPS)) {
        return DSERR_INVALIDPARAM;
      }
      caps->dwFlags = mFlags | DSBCAPS_LOCSOFTWARE;
      caps->dwBufferBytes = static_cast<DWORD>(mData.size());
      caps->dwUnlockTransferRate = 0;
      caps->dwPlayCpuOverhead = 0;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentPosition(const LPDWORD play, const LPDWORD write) override
    {
      DWORD position = 0;
      if (mStream != nullptr) {
        SDL_LockAudioStream(mStream);
        // What SDL has taken but not yet mixed is still ahead of the play cursor.
        const int queued = std::max(0, SDL_GetAudioStreamQueued(mStream));
        const DWORD size = Size();
        const DWORD pending = std::min<DWORD>(static_cast<DWORD>(queued), size);
        position = (mPlay + size - pending) % size;
        position -= position % mFormat.nBlockAlign;
        SDL_UnlockAudioStream(mStream);
      }
      if (play != nullptr) {
        *play = position;
      }
      if (write != nullptr) {
        // DirectSound keeps the write cursor a little ahead of the play cursor (the part
        // the mixer has committed to); 10 ms, whole blocks.
        DWORD lead = mFormat.nAvgBytesPerSec / 100u;
        lead -= lead % std::max<DWORD>(1u, mFormat.nBlockAlign);
        *write = mData.empty() ? 0u : (position + lead) % Size();
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFormat(const LPWAVEFORMATEX format, const DWORD allocated, const LPDWORD written) override
    {
      if (format == nullptr && written == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      if (format != nullptr) {
        const DWORD bytes = std::min<DWORD>(allocated, sizeof(WAVEFORMATEX));
        std::memcpy(format, &mFormat, bytes);
        if (written != nullptr) {
          *written = bytes;
        }
      } else {
        *written = sizeof(WAVEFORMATEX);
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetVolume(const LPLONG volume) override
    {
      if (volume == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      if ((mFlags & DSBCAPS_CTRLVOLUME) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      *volume = mVolume;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPan(const LPLONG pan) override
    {
      if (pan == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      if ((mFlags & DSBCAPS_CTRLPAN) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      *pan = mPan;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFrequency(const LPDWORD frequency) override
    {
      if (frequency == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      if ((mFlags & DSBCAPS_CTRLFREQUENCY) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      *frequency = mFrequency;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStatus(const LPDWORD status) override
    {
      if (status == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      DWORD result = 0;
      if (mStream != nullptr) {
        SDL_LockAudioStream(mStream);
        if (mPlaying) {
          result = DSBSTATUS_PLAYING | (mLooping ? DSBSTATUS_LOOPING : 0u);
        }
        SDL_UnlockAudioStream(mStream);
      }
      *status = result;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE Initialize(const LPDIRECTSOUND device, const LPCDSBUFFERDESC desc) override
    {
      (void)device;
      (void)desc;
      return DSERR_ALREADYINITIALIZED;
    }

    HRESULT STDMETHODCALLTYPE Lock(
      DWORD offset,
      DWORD bytes,
      LPVOID* const first,
      const LPDWORD firstBytes,
      LPVOID* const second,
      const LPDWORD secondBytes,
      const DWORD flags
    ) override
    {
      if (first == nullptr || firstBytes == nullptr || mData.empty()) {
        return mData.empty() ? DSERR_INVALIDCALL : DSERR_INVALIDPARAM;
      }
      const DWORD size = Size();
      if ((flags & DSBLOCK_FROMWRITECURSOR) != 0) {
        DWORD play = 0;
        GetCurrentPosition(&play, &offset);
      }
      if ((flags & DSBLOCK_ENTIREBUFFER) != 0) {
        bytes = size;
      }
      if (offset >= size || bytes == 0 || bytes > size) {
        return DSERR_INVALIDPARAM;
      }
      std::uint8_t* const base = mData.data();
      const DWORD head = std::min(bytes, size - offset);
      *first = base + offset;
      *firstBytes = head;
      if (second != nullptr) {
        *second = (bytes > head) ? base : nullptr;
      }
      if (secondBytes != nullptr) {
        *secondBytes = bytes - head;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE Play(const DWORD reserved, const DWORD priority, const DWORD flags) override
    {
      (void)reserved;
      (void)priority;
      if (mStream == nullptr) {
        return DS_OK; // the primary buffer: SDL's device is always mixing
      }
      SDL_LockAudioStream(mStream);
      mLooping = (flags & DSBPLAY_LOOPING) != 0;
      mPlaying = true;
      SDL_UnlockAudioStream(mStream);
      if (!SDL_ResumeAudioStreamDevice(mStream)) {
        gpg::Warnf("DirectSound: cannot start an SDL audio stream: %s", SDL_GetError());
        return DSERR_GENERIC;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override
    {
      if (mStream == nullptr) {
        return DSERR_INVALIDCALL;
      }
      if (position >= Size()) {
        return DSERR_INVALIDPARAM;
      }
      position -= position % mFormat.nBlockAlign;
      SDL_LockAudioStream(mStream);
      mPlay = position;
      // What SDL had taken from the old position must not play.
      SDL_ClearAudioStream(mStream);
      SDL_UnlockAudioStream(mStream);
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFormat(const LPCWAVEFORMATEX format) override
    {
      if (mStream != nullptr) {
        return DSERR_INVALIDCALL; // secondary buffers have a fixed format
      }
      if (format == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      SDL_AudioSpec spec{};
      if (!SpecOf(*format, spec)) {
        return DSERR_BADFORMAT;
      }
      mFormat = *format;
      mFormat.cbSize = 0;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetVolume(const LONG volume) override
    {
      if ((mFlags & DSBCAPS_CTRLVOLUME) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX) {
        return DSERR_INVALIDPARAM;
      }
      mVolume = volume;
      if (mStream != nullptr && !SDL_SetAudioStreamGain(mStream, GainOf(volume))) {
        return DSERR_GENERIC;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPan(const LONG pan) override
    {
      if ((mFlags & DSBCAPS_CTRLPAN) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      if (pan < DSBPAN_LEFT || pan > DSBPAN_RIGHT) {
        return DSERR_INVALIDPARAM;
      }
      if (pan != DSBPAN_CENTER) {
        FAF_POSIX_STUB("IDirectSoundBuffer::SetPan: pan is stored, not applied");
      }
      mPan = pan;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) override
    {
      if ((mFlags & DSBCAPS_CTRLFREQUENCY) == 0) {
        return DSERR_CONTROLUNAVAIL;
      }
      if (frequency == DSBFREQUENCY_ORIGINAL) {
        frequency = mFormat.nSamplesPerSec;
      }
      if (frequency < DSBFREQUENCY_MIN || frequency > DSBFREQUENCY_MAX) {
        return DSERR_INVALIDPARAM;
      }
      mFrequency = frequency;
      const float ratio = static_cast<float>(frequency) / static_cast<float>(mFormat.nSamplesPerSec);
      if (mStream != nullptr && !SDL_SetAudioStreamFrequencyRatio(mStream, ratio)) {
        return DSERR_GENERIC;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE Stop() override
    {
      if (mStream == nullptr) {
        return DS_OK;
      }
      SDL_LockAudioStream(mStream);
      mPlaying = false;
      SDL_UnlockAudioStream(mStream);
      // The position stays; what SDL already took plays first on the next Play.
      SDL_PauseAudioStreamDevice(mStream);
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE
    Unlock(const LPVOID first, const DWORD firstBytes, const LPVOID second, const DWORD secondBytes) override
    {
      // The memory is the buffer itself; nothing to copy back.
      const auto inside = [this](const void* const p, const DWORD n) {
        const auto* const b = static_cast<const std::uint8_t*>(p);
        return n == 0 || (b >= mData.data() && b + n <= mData.data() + mData.size());
      };
      if ((first != nullptr && !inside(first, firstBytes)) || (second != nullptr && !inside(second, secondBytes))) {
        return DSERR_INVALIDPARAM;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE Restore() override
    {
      return DS_OK; // SDL buffers are never lost
    }

  private:
    PosixSoundBuffer(const DSBUFFERDESC& desc, const WAVEFORMATEX& format, const bool primary)
      : mFlags(desc.dwFlags & ~DSBCAPS_LOCHARDWARE)
      , mFormat(format)
    {
      (void)primary;
    }

    ~PosixSoundBuffer()
    {
      if (mStream != nullptr && SdlAudioRunning()) {
        // Closes the logical device; SDL waits for a running callback first.
        SDL_DestroyAudioStream(mStream);
      }
      mStream = nullptr;
      if (mDump != nullptr) {
        std::fclose(mDump);
      }
      if (mHoldsAudio) {
        ReleaseSdlAudio();
      }
    }

    DWORD Size() const
    {
      return static_cast<DWORD>(mData.size());
    }

    // SDL's get callback, under the stream lock: hand SDL what DirectSound's mixer
    // would read from the play cursor on.
    static void SDLCALL Feed(void* const user, SDL_AudioStream* const stream, const int additional, const int total)
    {
      (void)total;
      auto& self = *static_cast<PosixSoundBuffer*>(user);
      if (!self.mPlaying || additional <= 0) {
        return;
      }
      const DWORD block = self.mFormat.nBlockAlign;
      DWORD wanted = static_cast<DWORD>(additional);
      wanted += (block - wanted % block) % block;
      const DWORD size = self.Size();
      while (wanted != 0 && self.mPlaying) {
        const DWORD count = std::min(wanted, size - self.mPlay);
        if (!SDL_PutAudioStreamData(stream, self.mData.data() + self.mPlay, static_cast<int>(count))) {
          return;
        }
        if (self.mDump != nullptr) {
          std::fwrite(self.mData.data() + self.mPlay, 1, count, self.mDump);
        }
        self.mPlay += count;
        wanted -= count;
        if (self.mPlay == size) {
          self.mPlay = 0;
          if (!self.mLooping) {
            self.mPlaying = false; // a one-shot buffer stops at its end
          }
        }
      }
    }

    std::atomic<ULONG> mRefs{1};
    std::FILE* mDump{}; // FAF_DSOUND_DUMP
    DWORD mFlags;
    WAVEFORMATEX mFormat;
    std::vector<std::uint8_t> mData;
    SDL_AudioStream* mStream{};
    bool mHoldsAudio{};
    LONG mVolume{DSBVOLUME_MAX};
    LONG mPan{DSBPAN_CENTER};
    DWORD mFrequency{};
    // Under the stream lock.
    DWORD mPlay{};
    bool mPlaying{};
    bool mLooping{};
  };

  class PosixDirectSound final : public IDirectSound
  {
  public:
    // Keeps the default output open for the device's lifetime, as DirectSound's primary
    // buffer keeps the endpoint: a logical device with no stream, which SDL mixes as
    // silence. Without it the physical device would close and reopen with every movie's
    // buffer.
    PosixDirectSound()
      : mHold(SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr))
    {
      if (mHold == 0) {
        gpg::Warnf("DirectSound: cannot open the default SDL audio output: %s", SDL_GetError());
      }
    }

    bool Opened() const
    {
      return mHold != 0;
    }

    SDL_AudioDeviceID Hold() const
    {
      return mHold;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** const ppvObject) override
    {
      (void)riid;
      if (ppvObject == nullptr) {
        return E_POINTER;
      }
      *ppvObject = nullptr;
      return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
      return ++mRefs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
      const ULONG left = --mRefs;
      if (left == 0) {
        delete this;
      }
      return left;
    }

    HRESULT STDMETHODCALLTYPE
    CreateSoundBuffer(const LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* const buffer, void* const outer) override
    {
      if (desc == nullptr || buffer == nullptr || outer != nullptr || desc->dwSize < sizeof(DSBUFFERDESC)) {
        return DSERR_INVALIDPARAM;
      }
      *buffer = nullptr;
      if ((desc->dwFlags & (DSBCAPS_CTRL3D | DSBCAPS_CTRLFX | DSBCAPS_CTRLPOSITIONNOTIFY)) != 0) {
        FAF_POSIX_STUB("IDirectSound::CreateSoundBuffer: 3D, effects and notifications are not supported");
        return DSERR_CONTROLUNAVAIL;
      }
      return PosixSoundBuffer::Create(*desc, buffer);
    }

    HRESULT STDMETHODCALLTYPE GetCaps(const LPDSCAPS caps) override
    {
      if (caps == nullptr || caps->dwSize < sizeof(DSCAPS)) {
        return DSERR_INVALIDPARAM;
      }
      const DWORD size = caps->dwSize;
      std::memset(caps, 0, sizeof(DSCAPS));
      caps->dwSize = size;
      caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
        DSCAPS_CONTINUOUSRATE | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT |
        DSCAPS_SECONDARY16BIT;
      caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
      caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
      caps->dwPrimaryBuffers = 1;
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE
    DuplicateSoundBuffer(const LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER* const duplicate) override
    {
      (void)original;
      FAF_POSIX_STUB("IDirectSound::DuplicateSoundBuffer");
      if (duplicate != nullptr) {
        *duplicate = nullptr;
      }
      return DSERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(const HWND hwnd, const DWORD level) override
    {
      (void)hwnd;
      if (level < DSSCL_NORMAL || level > DSSCL_WRITEPRIMARY) {
        return DSERR_INVALIDPARAM;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE Compact() override
    {
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(const LPDWORD config) override
    {
      if (config == nullptr) {
        return DSERR_INVALIDPARAM;
      }
      // The default output's channel count, as the speaker setup DirectSound reports.
      SDL_AudioSpec spec{};
      int channels = 2;
      if (SDL_GetAudioDeviceFormat(mHold, &spec, nullptr)) {
        channels = spec.channels;
      }
      switch (channels) {
        case 1:
          *config = DSSPEAKER_MONO;
          break;
        case 4:
          *config = DSSPEAKER_QUAD;
          break;
        case 6:
          *config = DSSPEAKER_5POINT1;
          break;
        case 8:
          *config = DSSPEAKER_7POINT1;
          break;
        default:
          *config = DSSPEAKER_STEREO;
          break;
      }
      return DS_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(const DWORD config) override
    {
      (void)config;
      return DS_OK; // a system setting; DirectSound 8 ignores it too
    }

    HRESULT STDMETHODCALLTYPE Initialize(const GUID* const device) override
    {
      (void)device;
      return DSERR_ALREADYINITIALIZED;
    }

  private:
    ~PosixDirectSound()
    {
      if (mHold != 0 && SdlAudioRunning()) {
        SDL_CloseAudioDevice(mHold);
      }
      ReleaseSdlAudio();
    }

    std::atomic<ULONG> mRefs{1};
    SDL_AudioDeviceID mHold;
  };
} // namespace

extern "C" HRESULT WINAPI DirectSoundCreate(const GUID* const pcGuidDevice, LPDIRECTSOUND* const ppDS, void* const pUnkOuter)
{
  if (ppDS == nullptr || pUnkOuter != nullptr) {
    return DSERR_INVALIDPARAM;
  }
  *ppDS = nullptr;
  if (pcGuidDevice != nullptr) {
    // Only the default device (null, DSDEVID_DefaultPlayback) is enumerated here.
    FAF_POSIX_STUB("DirectSoundCreate: a device GUID was given; using the default output");
  }
  if (!AcquireSdlAudio()) {
    return DSERR_NODRIVER;
  }
  auto* const device = new (std::nothrow) PosixDirectSound();
  if (device == nullptr) {
    ReleaseSdlAudio();
    return DSERR_OUTOFMEMORY;
  }
  if (!device->Opened()) {
    device->Release();
    return DSERR_NODRIVER; // no output device: what DirectSound reports without a driver
  }
  static std::atomic<bool> reported{false};
  if (!reported.exchange(true)) {
    SDL_AudioSpec spec{};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(device->Hold(), &spec, &frames)) {
      gpg::Logf(
        "DirectSound: SDL audio driver '%s', output %d Hz, %d channels, %d-frame buffer",
        SDL_GetCurrentAudioDriver(), spec.freq, spec.channels, frames
      );
    }
  }
  *ppDS = device;
  return DS_OK;
}
