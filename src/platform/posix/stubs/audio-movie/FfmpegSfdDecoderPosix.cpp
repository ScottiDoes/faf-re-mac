// POSIX build: replaces moho/movie/sfd/FfmpegSfdDecoder.cpp (excluded in
// macos/exclusions.d/audio-movie.cmake) until FFmpeg is built for macOS.
//
// The real decoder needs FFmpeg 7.1.3's libavformat/libavcodec/libswscale/libswresample
// (dependencies/ffmpeg, built on Windows by dependencies/ffmpeg/faf/CMakeLists.txt from
// the configure output in faf/x64 and faf/x86; there is no arm64-darwin configuration
// yet). Without it every movie is unavailable: Open throws, which sfd::Player::Open
// already turns into its failed status (4) and an error report, and CMovie::OpenMovie
// into "initial mwPlyGetStat failed for movie ..." and a false return. The game handles
// that as it handles a missing or unplayable movie; benchmark and replay runs pass
// /nomovie and never open one.

#include "moho/movie/sfd/FfmpegSfdDecoder.h"

#include <stdexcept>

namespace moho::sfd
{
  struct FfmpegDecoder::Impl
  {
  };

  FfmpegDecoder::FfmpegDecoder() = default;
  FfmpegDecoder::~FfmpegDecoder() = default;

  void FfmpegDecoder::Open(const char* const filename, const bool decodeAudio)
  {
    (void)filename;
    (void)decodeAudio;
    FAF_POSIX_STUB("sfd::FfmpegDecoder::Open: FFmpeg is not built for this platform; movies are unavailable");
    impl.reset();
    throw std::runtime_error("FFmpeg is not built for this platform; movie playback is unavailable");
  }

  bool FfmpegDecoder::Read(DecodeBatch& batch)
  {
    batch = {};
    return false;
  }

  bool FfmpegDecoder::HasAudio() const
  {
    return false;
  }

  int FfmpegDecoder::Width() const
  {
    return 0;
  }

  int FfmpegDecoder::Height() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioSampleRate() const
  {
    return 0;
  }

  int FfmpegDecoder::AudioChannels() const
  {
    return 0;
  }
} // namespace moho::sfd
