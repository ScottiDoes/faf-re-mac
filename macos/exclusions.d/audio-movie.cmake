# Package audio-movie: translation units of src/sources.cmake left out of the macOS build.
# Each entry says why and which stand-in under src/platform/posix/stubs/audio-movie/
# defines its symbols instead. Notes: macos/porting/audio-movie.md.

# PE import-table patch of xactengine2_9.dll (VirtualProtect, GetModuleHandleExW,
# IMAGE_* walking): no XACT DLL exists on POSIX.
# Replaced by stubs/audio-movie/XactClockPosix.cpp (SND_InstallXactClock, logged no-op).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^moho/audio/XactClock\\.cpp$=>platform/posix/stubs/audio-movie/XactClockPosix.cpp")

# FFmpeg-backed SFD decoder: FFmpeg is not built for macOS yet (milestone 3).
# Replaced by stubs/audio-movie/FfmpegSfdDecoderPosix.cpp (sfd::FfmpegDecoder whose
# Open fails, so every movie reports unavailable).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^moho/movie/sfd/FfmpegSfdDecoder\\.cpp$=>platform/posix/stubs/audio-movie/FfmpegSfdDecoderPosix.cpp")

# DirectSound movie audio sink: DirectSound is Windows-only.
# Replaced by stubs/audio-movie/DirectSoundMovieSinkPosix.cpp (sfd::AudioSink; never
# constructed, since DirectSoundCreate yields no device on POSIX).
list(APPEND FAF_MACOS_REPLACED_SOURCES "^moho/movie/sfd/DirectSoundMovieSink\\.cpp$=>platform/posix/stubs/audio-movie/DirectSoundMovieSinkPosix.cpp")
