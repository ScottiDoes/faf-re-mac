# FFmpeg 7.1.3's avutil, swresample, swscale, avcodec and avformat as static libraries for
# the macOS build (package movies; macos/ffmpeg.cmake adds this directory). The macOS
# counterpart of dependencies/ffmpeg/faf/CMakeLists.txt, which accepts only clang-cl and
# its x64/x86 configurations: the same component set (docs/FAF_SFD_COMPATIBILITY.md,
# LGPL v2.1+, no --enable-gpl or --enable-nonfree), configured for arm64 macOS with
# Apple clang.
#
# The sources and flags come from arm64-darwin/sources.cmake and the configuration
# headers from arm64-darwin/, both written by generate.sh (FFmpeg's configure plus the
# submodule's faf/gen_sources.py). Nothing here or there changes FFmpeg's sources, and
# nothing is written into dependencies/ffmpeg.
#
# Targets: ffmpeg_avutil, ffmpeg_swresample, ffmpeg_swscale, ffmpeg_avcodec,
# ffmpeg_avformat (STATIC, libavutil.a etc.). FFMPEG_FAF_PUBLIC_INCLUDE_DIR (set in the
# parent scope) holds only the public headers (lib*/*.h and the generated
# libavutil/avconfig.h): FFmpeg's source root cannot go on a C++ include path, because
# its VERSION file shadows <version> on a case-insensitive file system.

set(FFMPEG_FAF_SOURCE_DIR "${FAF_DEPENDENCIES_DIR}/ffmpeg")
set(FFMPEG_FAF_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/arm64-darwin")
if(NOT EXISTS "${FFMPEG_FAF_SOURCE_DIR}/libavcodec/avcodec.h")
  message(FATAL_ERROR "No FFmpeg source at ${FFMPEG_FAF_SOURCE_DIR} (git submodule dependencies/ffmpeg).")
endif()
include("${FFMPEG_FAF_CONFIG_DIR}/sources.cmake")

# FFmpeg's own optimisation and flags in every configuration (FFMPEG_FAF_COMPILE_OPTIONS
# carries configure's -O3): FFmpeg relies on dead-code elimination for disabled
# components. No NDEBUG either, as in FFmpeg's build. This directory only.
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_C_FLAGS_${_config} "")
  set(CMAKE_ASM_FLAGS_${_config} "")
endforeach()

# The public headers, copied at configure time (the submodule is never modified).
set(_public "${CMAKE_CURRENT_BINARY_DIR}/include")
foreach(_lib avutil swresample swscale avcodec avformat)
  file(GLOB _headers "${FFMPEG_FAF_SOURCE_DIR}/lib${_lib}/*.h")
  file(COPY ${_headers} DESTINATION "${_public}/lib${_lib}")
endforeach()
configure_file("${FFMPEG_FAF_CONFIG_DIR}/libavutil/avconfig.h" "${_public}/libavutil/avconfig.h" COPYONLY)
set(FFMPEG_FAF_PUBLIC_INCLUDE_DIR "${_public}" PARENT_SCOPE)

# Dependencies between the libraries (configure's <lib>_deps).
set(_ffmpeg_deps_avutil "")
set(_ffmpeg_deps_swresample avutil)
set(_ffmpeg_deps_swscale avutil)
set(_ffmpeg_deps_avcodec avutil)
set(_ffmpeg_deps_avformat avcodec avutil)

# The assembler command of FFmpeg's build: $(AS) $(CPPFLAGS) $(ASFLAGS) (ffbuild/config.mak).
set(_ffmpeg_asm_options -mdynamic-no-pic -fPIC -Qunused-arguments)

foreach(_lib avutil swresample swscale avcodec avformat)
  string(TOUPPER "${_lib}" _LIB)
  set(_sources "${FFMPEG_FAF_${_LIB}_SOURCES}")
  list(TRANSFORM _sources PREPEND "${FFMPEG_FAF_SOURCE_DIR}/")
  add_library(ffmpeg_${_lib} STATIC ${_sources})
  set_target_properties(ffmpeg_${_lib} PROPERTIES OUTPUT_NAME ${_lib})
  # FFmpeg's compile command: -I. -I$(SRC_PATH) $(CPPFLAGS) $(CFLAGS), plus what
  # ffbuild/library.mak adds for library objects.
  target_include_directories(ffmpeg_${_lib} PRIVATE
    "${FFMPEG_FAF_CONFIG_DIR}" "${FFMPEG_FAF_SOURCE_DIR}" ${FFMPEG_FAF_INCLUDE_DIRECTORIES})
  target_compile_definitions(ffmpeg_${_lib} PRIVATE
    ${FFMPEG_FAF_DEFINITIONS} HAVE_AV_CONFIG_H BUILDING_${_lib})
  target_compile_options(ffmpeg_${_lib} PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${FFMPEG_FAF_COMPILE_OPTIONS}>"
    "$<$<COMPILE_LANGUAGE:ASM>:${_ffmpeg_asm_options}>")
  foreach(_dep IN LISTS _ffmpeg_deps_${_lib})
    target_link_libraries(ffmpeg_${_lib} PUBLIC ffmpeg_${_dep})
  endforeach()
  # configure's EXTRALIBS-<lib>: -pthread and -lm are part of libSystem. avutil's list
  # also names CoreFoundation, CoreVideo and CoreMedia (configure probes them for the
  # hardware-frame code, which this configuration leaves out); nothing references them,
  # so they are not linked.
  set(_system "${FFMPEG_FAF_${_LIB}_SYSTEM_LIBS}")
  list(REMOVE_ITEM _system -pthread m -framework CoreFoundation CoreVideo CoreMedia)
  if(_system)
    message(FATAL_ERROR "FFmpeg lib${_lib}: unexpected system libraries ${_system}; see macos/ffmpeg/CMakeLists.txt")
  endif()
endforeach()
