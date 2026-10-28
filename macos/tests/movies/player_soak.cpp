// Soak test of the SFD player (sfd::Player in IndependentSfdBackend.cpp) the way the game
// drives a menu background movie (package movies; notes: macos/porting/movies.md):
//
//   player_soak [--sound] [--seconds S] [--loop] [--max-drift S] FILE.sfd
//
// Opens FILE through the legacy entry points CMovie uses (mwPlyCreateSofdec,
// mwPlyStartFname, mwPlyPause), then for S seconds (default 40) takes the current frame
// 60 times a second as CMovie::UpdatePlaybackFrame does, restarting at the end with
// --loop (Lua Movie:Loop). With --sound a DirectSound device is handed to the player
// (SDL3 audio; set SDL_AUDIO_DRIVER=dummy or disk). Fails when the player reports status
// 4, or when the frame shown drifts more than --max-drift seconds (default 0.25) from the
// wall-clock time played. With --sound the player's clock is the audio device's: SDL's
// disk and dummy drivers run a few percent slower than real time, so pass a larger
// --max-drift there (or use the real output device). Prints the frames seen and the
// largest drift.

#include "moho/audio/SofdecRuntime.h"
#include "moho/movie/sfd/FfmpegSfdDecoder.h"
#include "moho/movie/sfd/IndependentSfdBackend.h"

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace moho::sfd;

int main(int argc, char** argv)
{
  bool sound = false, loop = false;
  double seconds = 40, maxDriftAllowed = 0.25;
  const char* file = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--sound"))
      sound = true;
    else if (!std::strcmp(argv[i], "--loop"))
      loop = true;
    else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
      seconds = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--max-drift") && i + 1 < argc)
      maxDriftAllowed = std::atof(argv[++i]);
    else
      file = argv[i];
  }
  if (!file) {
    std::fprintf(stderr, "usage: player_soak [--sound] [--seconds S] [--loop] [--max-drift S] FILE.sfd\n");
    return 2;
  }
  SetErrorCallback([](std::uint32_t, const char* message) -> int {
    std::fprintf(stderr, "player error: %s\n", message);
    return 0;
  }, 0);
  IDirectSound* device = nullptr;
  if (sound) {
    if (FAILED(DirectSoundCreate(nullptr, &device, nullptr))) {
      std::fprintf(stderr, "no DirectSound device\n");
      return 1;
    }
    SetSoundDevice(device);
  }
  int rc = 0;
  {
    moho::MwsfcreCreateParams parameters{};
    auto* p = mwPlyCreateSofdec(&parameters);
    mwPlyStartFname(p, file);
    if (mwPlyGetStat(p) != 2) {
      std::fprintf(stderr, "open failed (status %d)\n", mwPlyGetStat(p));
      rc = 1;
    } else {
      mwPlyPause(p, 0);
      using Clock = std::chrono::steady_clock;
      const auto start = Clock::now();
      auto lap = start;
      double lastPts = -1, maxDrift = 0;
      long frames = 0, restarts = 0;
      while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
        const int status = mwPlyGetStat(p);
        if (status == 4) {
          std::fprintf(stderr, "FAIL: player failed after %.2f s\n",
                       std::chrono::duration<double>(Clock::now() - start).count());
          rc = 1;
          break;
        }
        if (status == 3) {
          if (!loop)
            break;
          mwPlyStartFname(p, file);
          mwPlyPause(p, 0);
          lap = Clock::now();
          lastPts = -1;
          ++restarts;
          continue;
        }
        moho::MwsfdFrameInfo info{};
        mwPlyGetCurFrm(p, &info);
        if (info.bufferAddress) {
          const auto* frame = static_cast<const DecodedVideoFrame*>(info.bufferAddress);
          if (frame->pts != lastPts) {
            ++frames;
            lastPts = frame->pts;
          }
          const double played = std::chrono::duration<double>(Clock::now() - lap).count();
          // Ignore the first second (start-up) for the drift measure.
          if (played > 1.0)
            maxDrift = std::max(maxDrift, std::fabs(frame->pts - played));
        }
        mwPlyRelCurFrm(p);
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
      }
      std::printf("%s: %ld distinct frames shown, %ld restart(s), largest drift %.3f s\n", file, frames, restarts,
                  maxDrift);
      if (maxDrift > maxDriftAllowed) {
        std::printf("FAIL: drift above %.3f s\n", maxDriftAllowed);
        rc = 1;
      }
    }
    mwPlyDestroy(p);
  }
  Shutdown();
  if (device)
    device->Release();
  std::printf("%s\n", rc ? "FAILED" : "PASS");
  return rc;
}
