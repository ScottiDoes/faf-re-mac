# Package movies (milestone 3): translation units of src/sources.cmake replaced in the
# macOS build for the SFD movie player. Split out of audio-movie.cmake by the lead so the
# movies package owns its own list. Notes: macos/porting/movies.md (and audio-movie.md).
#
# Nothing is replaced since milestone 3. moho/movie/sfd/FfmpegSfdDecoder.cpp compiles
# unchanged against FFmpeg 7.1.3 built from dependencies/ffmpeg (macos/ffmpeg.cmake), and
# moho/movie/sfd/DirectSoundMovieSink.cpp compiles unchanged against the DirectSound
# stand-in on SDL3 audio (src/platform/posix/stubs/audio-movie/DirectSoundPosix.cpp).
