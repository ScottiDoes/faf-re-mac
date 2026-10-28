// POSIX build: replaces moho/movie/sfd/DirectSoundMovieSink.cpp (excluded in
// macos/exclusions.d/audio-movie.cmake).
//
// The real sink plays movie audio through a DirectSound secondary buffer. sfd::Player
// creates one only when CMovieManager handed it a DirectSound device, and on POSIX
// DirectSoundCreate never yields one (DirectSoundPosix.cpp), so players run without a
// sink on their steady clock, as under /nosound on Windows. The constructor fails the
// way the real one does without a device; the other members are never reached and
// behave as an empty, drained sink. Milestone 3: a CoreAudio (or FAudio/SDL) sink with
// the same interface, whose Clock() is the device's played-sample count.

#include "moho/movie/sfd/DirectSoundMovieSink.h"

#include <stdexcept>

namespace moho::sfd
{
  struct AudioSink::Impl
  {
  };

  AudioSink::AudioSink(IDirectSound* const device)
    : impl(std::make_unique<Impl>())
  {
    (void)device;
    FAF_POSIX_STUB("sfd::AudioSink: no movie audio output on this platform");
    throw std::runtime_error("No movie audio device");
  }

  AudioSink::~AudioSink() = default;

  void AudioSink::Pump() {}

  std::size_t AudioSink::Write(const std::span<const std::int16_t> stereo)
  {
    (void)stereo;
    return 0;
  }

  void AudioSink::Pause(const bool paused)
  {
    (void)paused;
  }

  void AudioSink::Stop() {}

  void AudioSink::SetVolume(const long attenuation)
  {
    (void)attenuation;
  }

  double AudioSink::Clock() const
  {
    return 0.0;
  }

  double AudioSink::Submitted() const
  {
    return 0.0;
  }

  bool AudioSink::Empty() const
  {
    return true;
  }
} // namespace moho::sfd
