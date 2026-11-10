# Third-party libraries for the macOS build: Wild Magic 3.8 (headers) and zlib 1.2.3,
# from the same submodules as the Windows build.

# --- Wild Magic 3.8 (headers only) -----------------------------------------------------
# As in cmake/deps/wildmagic: the engine uses Wild Magic's header code only; the compiled
# code it called is engine code (src/moho/math/Wm3Queries.cpp, Wm3FoundationStatics.cpp).
# The subdirectory list is read from there instead of keeping a second copy. Wild Magic
# templates that use members of dependent bases unqualified (accepted by cl without
# /permissive-) are qualified by wildmagic3p8_faf_macos.patch.
set(WM3_ROOT "${FAF_DEPENDENCIES_DIR}/WildMagic3p8")
set(WM3_FOUNDATION "${WM3_ROOT}/Foundation")
file(READ "${FAF_ROOT}/cmake/deps/wildmagic/CMakeLists.txt" _wm3_cmake)
string(REGEX MATCH "set\\(WM3_FOUNDATION_SUBDIRS([^)]*)\\)" _ "${_wm3_cmake}")
string(REGEX REPLACE "[ \t\r\n]+" ";" WM3_FOUNDATION_SUBDIRS "${CMAKE_MATCH_1}")
list(FILTER WM3_FOUNDATION_SUBDIRS EXCLUDE REGEX "^$")
set(WM3_FOUNDATION_INCLUDES "${WM3_FOUNDATION}")
foreach(_dir IN LISTS WM3_FOUNDATION_SUBDIRS)
  list(APPEND WM3_FOUNDATION_INCLUDES "${WM3_FOUNDATION}/${_dir}")
endforeach()
list(LENGTH WM3_FOUNDATION_SUBDIRS _n)
message(STATUS "Wild Magic 3.8: headers from ${_n} Foundation folders")

add_library(Wm3Foundation INTERFACE)
target_include_directories(Wm3Foundation SYSTEM INTERFACE ${WM3_FOUNDATION_INCLUDES})

# --- zlib 1.2.3 ------------------------------------------------------------------------
set(_zlib_src "${FAF_DEPENDENCIES_DIR}/zlib-1.2.3")
add_library(faf_zlib STATIC
  "${_zlib_src}/adler32.c" "${_zlib_src}/compress.c" "${_zlib_src}/crc32.c"
  "${_zlib_src}/deflate.c" "${_zlib_src}/gzio.c" "${_zlib_src}/infback.c"
  "${_zlib_src}/inffast.c" "${_zlib_src}/inflate.c" "${_zlib_src}/inftrees.c"
  "${_zlib_src}/trees.c" "${_zlib_src}/uncompr.c" "${_zlib_src}/zutil.c")
target_include_directories(faf_zlib PUBLIC "${_zlib_src}")
target_compile_options(faf_zlib PRIVATE -w)
target_link_libraries(faf_zlib PRIVATE faf_codegen)

# --- Zstandard and Monocypher ------------------------------------------------------------
# As in cmake/deps/zstd and cmake/deps/monocypher; build_app.sh unpacks the pinned archives.
set(_zstd_src "${FAF_DEPENDENCIES_DIR}/zstd/lib")
file(GLOB _zstd_sources CONFIGURE_DEPENDS
  "${_zstd_src}/common/*.c" "${_zstd_src}/compress/*.c" "${_zstd_src}/decompress/*.c")
add_library(faf_zstd STATIC ${_zstd_sources})
target_include_directories(faf_zstd SYSTEM PUBLIC "${_zstd_src}")
target_compile_definitions(faf_zstd PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0)
target_compile_options(faf_zstd PRIVATE -w)
target_link_libraries(faf_zstd PRIVATE faf_codegen)

set(_monocypher_src "${FAF_DEPENDENCIES_DIR}/monocypher/src")
add_library(faf_monocypher STATIC "${_monocypher_src}/monocypher.c")
target_include_directories(faf_monocypher SYSTEM PUBLIC "${_monocypher_src}")
target_compile_options(faf_monocypher PRIVATE -w)
target_link_libraries(faf_monocypher PRIVATE faf_codegen)

# --- FreeType and msdfgen -----------------------------------------------------------------
# Upstream's own target files; FAF_LINUX only picks clang's warning flags there.
set(FAF_LINUX ON)
add_subdirectory("${FAF_ROOT}/cmake/deps/freetype" "${CMAKE_BINARY_DIR}/deps/freetype")
add_subdirectory("${FAF_ROOT}/cmake/deps/msdfgen" "${CMAKE_BINARY_DIR}/deps/msdfgen")
unset(FAF_LINUX)

# --- Berkeley SoftFloat 3e (test oracle only) ------------------------------------------
# Reference for the x87 emulation's exact core (macos/docs/DETERMINISM.md §3.5, §6.1):
# extF80 with extF80_roundingPrecision 32/64/80 and the "8086" specialization, which
# follows x87's NaN rules. Never linked into the engine: only test executables use it,
# so it is EXCLUDE_FROM_ALL. dependencies/berkeley-softfloat-3 is a clone of
# github.com/ucb-bar/berkeley-softfloat-3 at 0a5ad290c6b287f6605bb99a35ad0842c1e61aa5
# (BSD-3, COPYING.txt). The source lists come from its Linux-x86_64-GCC Makefile (the
# 64-bit FAST_INT64 configuration); the BF16 routines are left out, since the 8086
# specialization does not define their NaN helpers.
set(_sf_root "${FAF_DEPENDENCIES_DIR}/berkeley-softfloat-3")
if(EXISTS "${_sf_root}/source/include/softfloat.h")
  file(READ "${_sf_root}/build/Linux-x86_64-GCC/Makefile" _sf_makefile)
  set(_sf_sources "")
  foreach(_group PRIMITIVES SPECIALIZE OTHERS)
    string(REGEX MATCH "OBJS_${_group} =([^\n]*\\\\\n)*[^\n]*" _block "${_sf_makefile}")
    string(REGEX MATCHALL "[A-Za-z0-9_]+\\$\\(OBJ\\)" _objs "${_block}")
    foreach(_obj IN LISTS _objs)
      string(REPLACE "$(OBJ)" "" _name "${_obj}")
      if(_name MATCHES "[Bb][Ff]16")
        continue()
      endif()
      if(_group STREQUAL "SPECIALIZE")
        list(APPEND _sf_sources "${_sf_root}/source/8086/${_name}.c")
      else()
        list(APPEND _sf_sources "${_sf_root}/source/${_name}.c")
      endif()
    endforeach()
  endforeach()
  add_library(faf_softfloat STATIC EXCLUDE_FROM_ALL ${_sf_sources})
  target_include_directories(faf_softfloat
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/softfloat" "${_sf_root}/source/8086"
    PUBLIC "${_sf_root}/source/include")
  target_compile_definitions(faf_softfloat
    PRIVATE SOFTFLOAT_ROUND_ODD INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32
    # LITTLEENDIAN selects the extFloat80_t field order (softfloat_types.h): users need it too.
    PUBLIC LITTLEENDIAN SOFTFLOAT_FAST_INT64 THREAD_LOCAL=_Thread_local)
  target_compile_options(faf_softfloat PRIVATE -w)
endif()
