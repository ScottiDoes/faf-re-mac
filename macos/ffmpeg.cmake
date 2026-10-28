# Package movies (milestone 3): FFmpeg 7.1.3 for the SFD movie decoder
# (src/moho/movie/sfd/FfmpegSfdDecoder.cpp). Included by macos/CMakeLists.txt after
# faf_engine is defined; sets FAF_FFMPEG_LIBRARIES, which faf_replay and faf_game link.
#
# The libraries build from the submodule's unmodified sources with the arm64 macOS
# configuration in macos/ffmpeg/arm64-darwin/ (macos/ffmpeg/CMakeLists.txt; regenerate it
# with macos/ffmpeg/generate.sh). Static, LGPL v2.1+, the component set of
# docs/FAF_SFD_COMPATIBILITY.md, NEON assembly on, no system frameworks.
# Notes: macos/porting/movies.md.

# FFmpeg's aarch64 NEON code is in .S files. enable_language must run in the directory
# common to every target that links them (this one, through the executables).
enable_language(ASM)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/ffmpeg" "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg")

set(FAF_FFMPEG_LIBRARIES
  ffmpeg_avformat ffmpeg_avcodec ffmpeg_swscale ffmpeg_swresample ffmpeg_avutil)

# Only the decoder TU sees FFmpeg's headers (the public ones; the engine's force-included
# WinCompat.h does not clash with them).
set_property(SOURCE "${FAF_SRC}/moho/movie/sfd/FfmpegSfdDecoder.cpp"
  APPEND PROPERTY INCLUDE_DIRECTORIES "${FFMPEG_FAF_PUBLIC_INCLUDE_DIR}")
