// Tests of the DirectSound stand-in on SDL3 (src/platform/posix/stubs/audio-movie/
// DirectSoundPosix.cpp), in the ways the engine uses DirectSound (package movies; notes:
// macos/porting/movies.md). Run under SDL's disk driver to capture what plays:
//
//   SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw dsound_test
//
// Checks the return values, Lock/Unlock across the wrap, that the play cursor advances at
// the buffer's rate while playing and holds while stopped, SetCurrentPosition, volume and
// frequency controls (DSERR_CONTROLUNAVAIL without their flags) and the speaker config.
// It plays 1 s of a 1 kHz tone at full volume, then 1 s at -20 dB (0.1 gain): the capture
// shows both.

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
  int failures = 0;

  void Check(const bool ok, const char* what)
  {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
      ++failures;
  }

  void Sleep(int ms)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }

  DWORD Position(IDirectSoundBuffer* b)
  {
    DWORD play = 0, write = 0;
    b->GetCurrentPosition(&play, &write);
    return play;
  }
} // namespace

int main()
{
  IDirectSound* device = nullptr;
  Check(DirectSoundCreate(nullptr, nullptr, nullptr) == DSERR_INVALIDPARAM, "DirectSoundCreate(null out) is DSERR_INVALIDPARAM");
  const HRESULT created = DirectSoundCreate(nullptr, &device, nullptr);
  Check(SUCCEEDED(created) && device != nullptr, "DirectSoundCreate gives a device");
  if (!device)
    return 1;
  Check(device->SetCooperativeLevel(nullptr, DSSCL_PRIORITY) == DS_OK, "SetCooperativeLevel(DSSCL_PRIORITY)");
  DWORD speakers = 0xFFFFFFFF;
  Check(device->GetSpeakerConfig(&speakers) == DS_OK && speakers <= DSSPEAKER_7POINT1, "GetSpeakerConfig");
  std::printf("speaker config %u\n", static_cast<unsigned>(speakers));

  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 48000;
  format.nAvgBytesPerSec = 192000;
  format.nBlockAlign = 4;
  format.wBitsPerSample = 16;
  DSBUFFERDESC desc{};
  desc.dwSize = sizeof(desc);
  desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
  desc.dwBufferBytes = 192000 * 2; // the movie sink's 2 s ring
  desc.lpwfxFormat = &format;
  IDirectSoundBuffer* buffer = nullptr;
  Check(device->CreateSoundBuffer(&desc, &buffer, nullptr) == DS_OK && buffer, "CreateSoundBuffer 48 kHz S16 stereo");
  if (!buffer)
    return 1;

  DSBCAPS caps{};
  caps.dwSize = sizeof(caps);
  Check(buffer->GetCaps(&caps) == DS_OK && caps.dwBufferBytes == desc.dwBufferBytes, "GetCaps buffer size");
  Check(buffer->SetFrequency(44100) == DSERR_CONTROLUNAVAIL, "SetFrequency without DSBCAPS_CTRLFREQUENCY");
  Check(buffer->SetVolume(1) == DSERR_INVALIDPARAM, "SetVolume above DSBVOLUME_MAX");
  Check(buffer->SetVolume(0) == DS_OK, "SetVolume(0)");

  // Fill the ring with a 1 kHz tone through a Lock that wraps.
  const DWORD size = desc.dwBufferBytes;
  std::vector<std::int16_t> tone(size / 2);
  for (std::size_t i = 0; i < tone.size() / 2; ++i) {
    const auto s = static_cast<std::int16_t>(std::lround(12000.0 * std::sin(2 * M_PI * 1000.0 * i / 48000.0)));
    tone[i * 2] = s;
    tone[i * 2 + 1] = s;
  }
  void *p1 = nullptr, *p2 = nullptr;
  DWORD n1 = 0, n2 = 0;
  Check(buffer->Lock(size - 4000, 8000, &p1, &n1, &p2, &n2, 0) == DS_OK && n1 == 4000 && n2 == 4000 && p2 != nullptr,
        "Lock across the end wraps to the start");
  Check(buffer->Unlock(p1, n1, p2, n2) == DS_OK, "Unlock");
  Check(buffer->Lock(0, size, &p1, &n1, &p2, &n2, 0) == DS_OK && n1 == size && n2 == 0, "Lock whole buffer");
  std::memcpy(p1, tone.data(), n1);
  Check(buffer->Unlock(p1, n1, p2, n2) == DS_OK, "Unlock whole buffer");
  Check(buffer->Lock(size, 4, &p1, &n1, &p2, &n2, 0) == DSERR_INVALIDPARAM, "Lock at the end is DSERR_INVALIDPARAM");

  DWORD status = 0;
  Check(buffer->GetStatus(&status) == DS_OK && status == 0, "not playing before Play");
  Check(Position(buffer) == 0, "position 0 before Play");

  const auto t0 = std::chrono::steady_clock::now();
  Check(buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK, "Play looping");
  Check(buffer->GetStatus(&status) == DS_OK && status == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING), "status playing+looping");
  Sleep(500);
  DWORD pos = Position(buffer);
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  double played = pos / 192000.0;
  std::printf("after %.3f s the play cursor is at %.3f s\n", elapsed, played);
  Check(std::fabs(played - elapsed) < 0.1, "play cursor follows real time within 100 ms");
  Sleep(500);
  Check(buffer->SetVolume(-2000) == DS_OK, "SetVolume(-2000): -20 dB");
  Sleep(1000);
  Check(buffer->Stop() == DS_OK, "Stop");
  Check(buffer->GetStatus(&status) == DS_OK && status == 0, "status stopped");
  const DWORD held = Position(buffer);
  Sleep(200);
  Check(Position(buffer) == held, "Stop holds the position");
  Check(buffer->SetCurrentPosition(4800) == DS_OK && Position(buffer) == 4800, "SetCurrentPosition");
  Check(buffer->SetCurrentPosition(size) == DSERR_INVALIDPARAM, "SetCurrentPosition past the end");

  // A one-shot (non-looping) 0.1 s buffer stops at its end.
  desc.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_GETCURRENTPOSITION2;
  desc.dwBufferBytes = 19200;
  IDirectSoundBuffer* once = nullptr;
  Check(device->CreateSoundBuffer(&desc, &once, nullptr) == DS_OK && once, "CreateSoundBuffer one-shot");
  if (once) {
    Check(once->SetVolume(0) == DSERR_CONTROLUNAVAIL, "SetVolume without DSBCAPS_CTRLVOLUME");
    Check(once->SetFrequency(96000) == DS_OK, "SetFrequency(96000) with DSBCAPS_CTRLFREQUENCY");
    Check(once->Play(0, 0, 0) == DS_OK, "Play one-shot");
    Sleep(300);
    Check(once->GetStatus(&status) == DS_OK && status == 0, "one-shot stops at its end");
    Check(once->Release() == 0, "Release one-shot");
  }

  // Primary buffer: format calls only.
  DSBUFFERDESC primary{};
  primary.dwSize = sizeof(primary);
  primary.dwFlags = DSBCAPS_PRIMARYBUFFER;
  IDirectSoundBuffer* prim = nullptr;
  Check(device->CreateSoundBuffer(&primary, &prim, nullptr) == DS_OK && prim, "CreateSoundBuffer primary");
  if (prim) {
    Check(prim->SetFormat(&format) == DS_OK, "primary SetFormat");
    prim->Release();
  }
  WAVEFORMATEX bad = format;
  bad.nBlockAlign = 3;
  desc.lpwfxFormat = &bad;
  desc.dwFlags = 0;
  IDirectSoundBuffer* none = nullptr;
  Check(device->CreateSoundBuffer(&desc, &none, nullptr) == DSERR_BADFORMAT && none == nullptr, "bad block align is DSERR_BADFORMAT");

  Check(buffer->Release() == 0, "Release buffer");
  Check(device->Release() == 0, "Release device");
  std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
  return failures ? 1 : 0;
}
