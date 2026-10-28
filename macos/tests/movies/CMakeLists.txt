# Tests of package movies (milestone 3; notes: macos/porting/movies.md).
#
#   cmake -S macos/tests/movies -B build/m4-movies-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
#   ninja -C build/m4-movies-tests
#
# FFmpeg is built here from macos/ffmpeg (as macos/ffmpeg.cmake builds it for the engine).
# The SFD player's TUs (src/moho/movie/sfd/*.cpp, src/moho/audio/SofdecRuntime.cpp) and
# the DirectSound stand-in compile as the engine compiles them (WinCompat.h
# force-included); TestShims.cpp supplies the logging and the two Win32 calls they use.
#
# Executables:
#   movie_census     decode every movie in the folders given (movie_census.cpp)
#   dsound_test      the DirectSound stand-in on SDL3 (dsound_test.cpp)
#   sfd-inspect      upstream tools/sfd-inspect/main.cpp, unchanged: --decode, --play,
#                    --play-sound (player + DirectSound sink), --reject
#   sfd-parser-tests upstream tools/sfd-inspect/parser-tests.cpp, unchanged

cmake_minimum_required(VERSION 3.25)
project(faf_movies_tests LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64)

get_filename_component(FAF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(FAF_SRC "${FAF_ROOT}/src")
set(FAF_DEPENDENCIES_DIR "${FAF_ROOT}/dependencies")
find_package(SDL3 CONFIG REQUIRED HINTS /opt/homebrew)

add_subdirectory("${FAF_ROOT}/macos/ffmpeg" ffmpeg)

set(SFD "${FAF_SRC}/moho/movie/sfd")
add_library(faf_sfd STATIC
  "${SFD}/SfdHeaderParser.cpp" "${SFD}/SfdMetadata.cpp" "${SFD}/FfmpegSfdDecoder.cpp"
  "${SFD}/DirectSoundMovieSink.cpp" "${SFD}/IndependentSfdBackend.cpp"
  "${FAF_SRC}/moho/audio/SofdecRuntime.cpp"
  "${FAF_SRC}/platform/posix/stubs/audio-movie/DirectSoundPosix.cpp"
  TestShims.cpp)
target_compile_definitions(faf_sfd PUBLIC FAF_POSIX=1 FAF_MACOS=1 NOMINMAX UNICODE _UNICODE
  BOOST_ALL_NO_LIB BOOST_NO_AUTO_PTR _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)
target_include_directories(faf_sfd BEFORE PUBLIC
  "${FAF_SRC}/platform/posix/include"
  "${FAF_SRC}/platform/posix/wx"
  "${FAF_SRC}/platform/posix/stubs/audio-movie/include")
target_include_directories(faf_sfd PUBLIC "${FAF_SRC}" "${FAF_DEPENDENCIES_DIR}/boost_1_34_1")
target_compile_options(faf_sfd PUBLIC
  "SHELL:-include \"${FAF_SRC}/platform/posix/WinCompat.h\""
  "SHELL:-include \"${FAF_SRC}/platform/posix/LayoutAsserts.h\""
  -fms-extensions -fdeclspec -Wno-microsoft -Wno-unknown-pragmas -Wno-pragma-pack)
set_property(SOURCE "${SFD}/FfmpegSfdDecoder.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES "${FFMPEG_FAF_PUBLIC_INCLUDE_DIR}")
target_link_libraries(faf_sfd PUBLIC
  ffmpeg_avformat ffmpeg_avcodec ffmpeg_swscale ffmpeg_swresample ffmpeg_avutil SDL3::SDL3)

add_executable(movie_census movie_census.cpp)
target_link_libraries(movie_census PRIVATE faf_sfd)
add_executable(dsound_test dsound_test.cpp)
target_link_libraries(dsound_test PRIVATE faf_sfd)
add_executable(sfd-inspect "${FAF_ROOT}/tools/sfd-inspect/main.cpp")
target_link_libraries(sfd-inspect PRIVATE faf_sfd)
add_executable(sfd-parser-tests "${FAF_ROOT}/tools/sfd-inspect/parser-tests.cpp" "${SFD}/SfdHeaderParser.cpp")
target_include_directories(sfd-parser-tests PRIVATE "${FAF_SRC}")

add_executable(player_soak player_soak.cpp)
target_link_libraries(player_soak PRIVATE faf_sfd)

# For a before/after comparison: player_soak against another IndependentSfdBackend.cpp,
# e.g. upstream's (git show cb1b9fd7:src/moho/movie/sfd/IndependentSfdBackend.cpp > file).
set(FAF_MOVIES_OTHER_BACKEND "" CACHE FILEPATH "IndependentSfdBackend.cpp to build player_soak_other with")
if(FAF_MOVIES_OTHER_BACKEND)
  get_target_property(_sfd_sources faf_sfd SOURCES)
  list(REMOVE_ITEM _sfd_sources "${SFD}/IndependentSfdBackend.cpp")
  add_executable(player_soak_other player_soak.cpp ${_sfd_sources} "${FAF_MOVIES_OTHER_BACKEND}")
  get_target_property(_defs faf_sfd INTERFACE_COMPILE_DEFINITIONS)
  get_target_property(_incs faf_sfd INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(_opts faf_sfd INTERFACE_COMPILE_OPTIONS)
  target_compile_definitions(player_soak_other PRIVATE ${_defs})
  target_include_directories(player_soak_other PRIVATE ${_incs} "${SFD}")
  target_compile_options(player_soak_other PRIVATE ${_opts})
  target_link_libraries(player_soak_other PRIVATE
    ffmpeg_avformat ffmpeg_avcodec ffmpeg_swscale ffmpeg_swresample ffmpeg_avutil SDL3::SDL3)
endif()
