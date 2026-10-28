# Native macOS (Apple Silicon) build of the engine.
#
# A separate project from the Windows build (../CMakeLists.txt, clang-cl only):
# Apple clang, arm64, POSIX. It compiles the same translation units in the same
# order (src/sources.cmake: the static-init order is load-bearing), minus the
# Windows-only ones listed below, against src/platform/posix: Win32 types at
# their Windows sizes and the Win32 calls the engine makes, on POSIX.
#
#   cmake -S macos -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/macos
#
# Dependencies are not fetched here: dependencies/boost_1_34_1 must exist
# (boost_1_34_1_faf_required.patch applied), and the WildMagic3p8 and zlib
# submodules must carry their faf patches plus wildmagic3p8_faf_macos.patch.

cmake_minimum_required(VERSION 3.25)
project(faf_engine_macos LANGUAGES C CXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  message(FATAL_ERROR "This build targets macOS on Apple Silicon.")
endif()

set(FAF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FAF_SRC "${FAF_ROOT}/src")
set(FAF_DEPENDENCIES_DIR "${FAF_ROOT}/dependencies" CACHE PATH "Third-party tree")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OSX_ARCHITECTURES arm64)

# --- Code generation (same rules as cmake/FafCodegen.cmake, GNU spelling) -------------
# The simulation is lockstep: every result must match the Windows builds bit for
# bit. No contraction into fused multiply-add, no libm builtins (the engine's own
# x87-faithful math decides those results), no optimisations that assume UB away.
add_library(faf_codegen INTERFACE)
set(_faf_libm_calls
  acos asin atan atan2 cos cosh sin sinh tan tanh
  exp exp2 expm1 log log10 log1p log2 logb pow cbrt hypot erf erfc tgamma lgamma)
set(_faf_codegen_options
  -ffp-contract=off -fno-fast-math
  # Darwin defaults to -fno-math-errno, under which libc++'s std::sin(float),
  # std::pow(float, float), ... become LLVM intrinsics that ignore -fno-builtin-*:
  # constant-folded with the compiler's libm, pow(x, 2) rewritten to x*x. With errno
  # semantics they stay calls (macos/docs/DETERMINISM.md §5 rule 3).
  -fmath-errno
  -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
  -fno-strict-float-cast-overflow -fno-finite-loops
  -fms-extensions -fdeclspec)
foreach(_fn IN LISTS _faf_libm_calls)
  list(APPEND _faf_codegen_options -fno-builtin-${_fn} -fno-builtin-${_fn}f -fno-builtin-${_fn}l)
endforeach()
target_compile_options(faf_codegen INTERFACE
  "$<$<COMPILE_LANGUAGE:C,CXX>:${_faf_codegen_options}>"
  $<$<COMPILE_LANGUAGE:CXX>:-fno-strict-return>)

# --- Third-party ----------------------------------------------------------------------
include(deps.cmake)

# --- Window system and GPU (milestone 2) ---------------------------------------------
# SDL3 (Homebrew) for windows, input and cursors; the LunarG Vulkan SDK for the Vulkan
# loader, the Metal drivers (MoltenVK, KosmicKrisp), DXC (libdxcompiler.dylib) and the
# validation layers. The engine compiles against SDL3's headers; only the executables
# link SDL3, and the Vulkan loader and DXC are loaded at run time.
find_package(SDL3 CONFIG REQUIRED HINTS /opt/homebrew)
# Through the ~/Gaming link, so the path has no space (it is baked into the binaries as
# the development fallback for the loader, the drivers and DXC).
set(FAF_VULKAN_SDK "$ENV{HOME}/Gaming/VulkanSDK/1.4.363.0/macOS" CACHE PATH
  "LunarG Vulkan SDK, its macOS folder (lib/, include/, share/vulkan/)")
if(NOT EXISTS "${FAF_VULKAN_SDK}/lib/libvulkan.1.dylib" OR NOT EXISTS "${FAF_VULKAN_SDK}/include/dxc/dxcapi.h")
  message(FATAL_ERROR "No Vulkan SDK at FAF_VULKAN_SDK=${FAF_VULKAN_SDK} (libvulkan.1.dylib, include/dxc).")
endif()
# Frameworks the macOS stand-ins call (Core Text fonts, ImageIO texture files, the
# CAMetalLayer behind a window), linked into every executable that holds the engine.
set(FAF_MACOS_FRAMEWORKS
  "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText"
  "-framework ImageIO" "-framework QuartzCore" "-framework Metal")

# --- Engine ---------------------------------------------------------------------------
include("${FAF_SRC}/sources.cmake")
include(exclusions.cmake)

# A replaced TU's stand-in compiles at the original's position in the list, so its
# static initializers keep their place in the (load-bearing) init order.
set(_faf_engine_sources "")
set(_faf_replacement_sources "")
foreach(_src IN LISTS FAF_ENGINE_SOURCES)
  set(_replacement "")
  foreach(_entry IN LISTS FAF_MACOS_REPLACED_SOURCES)
    string(REPLACE "=>" ";" _pair "${_entry}")
    list(GET _pair 0 _pattern)
    list(GET _pair 1 _with)
    if(_src MATCHES "${_pattern}")
      set(_replacement "${FAF_SRC}/${_with}")
      break()
    endif()
  endforeach()
  if(_replacement)
    if(NOT EXISTS "${_replacement}")
      message(FATAL_ERROR "Replacement for ${_src} does not exist: ${_replacement}")
    endif()
    list(APPEND _faf_engine_sources "${_replacement}")
    list(APPEND _faf_replacement_sources "${_replacement}")
    continue()
  endif()
  set(_excluded FALSE)
  foreach(_pattern IN LISTS FAF_MACOS_EXCLUDED_SOURCES)
    if(_src MATCHES "${_pattern}")
      set(_excluded TRUE)
      break()
    endif()
  endforeach()
  if(NOT _excluded)
    if(IS_ABSOLUTE "${_src}")
      list(APPEND _faf_engine_sources "${_src}")
    else()
      list(APPEND _faf_engine_sources "${FAF_SRC}/${_src}")
    endif()
  endif()
endforeach()
list(LENGTH FAF_ENGINE_SOURCES _all)
list(LENGTH _faf_engine_sources _kept)
list(LENGTH _faf_replacement_sources _replaced)
message(STATUS "Engine: ${_kept} of ${_all} translation units, ${_replaced} of them POSIX stand-ins "
  "(rest Windows-only, see exclusions.cmake)")

# The POSIX platform layer: Win32 calls the engine makes, and stand-ins for the
# Windows-only subsystems (renderer, audio, app shell) until their ports exist.
file(GLOB_RECURSE _faf_posix_sources CONFIGURE_DEPENDS "${FAF_SRC}/platform/posix/*.cpp")
# The headless runner's and the game's own files build into faf_replay and faf_game,
# below; the DXC bridge builds without the Win32 stand-ins (macos/dxc.cmake).
list(FILTER _faf_posix_sources EXCLUDE REGEX "/platform/posix/(runner|game|dxc)/")
# Runs the GPG_PREREGISTER_INIT providers; must be the first object linked (see the file).
set(_faf_first_source "${FAF_SRC}/platform/posix/startup/StaticInitPreRegister.cpp")
list(REMOVE_ITEM _faf_posix_sources "${_faf_first_source}")
if(_faf_replacement_sources)
  list(REMOVE_ITEM _faf_posix_sources ${_faf_replacement_sources})
endif()

add_library(faf_engine OBJECT ${_faf_first_source} ${_faf_engine_sources} ${_faf_posix_sources})
target_compile_definitions(faf_engine PUBLIC
  FAF_POSIX=1 FAF_MACOS=1 NOMINMAX
  LUAPLUS_LIB LUA_NUMBER=float
  BOOST_ALL_NO_LIB BOOST_NO_AUTO_PTR
  # boost 1.34 uses std::unary_function/binary_function, which libc++ removed in C++17.
  _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
  UNICODE _UNICODE)
target_include_directories(faf_engine BEFORE PUBLIC
  # Stand-in <windows.h>, <winsock2.h>, ... ahead of everything else.
  "${FAF_SRC}/platform/posix/include"
  # Header-only wxWidgets 2.4.2 stand-in (<wx/defs.h>, ...; see its wx/defs.h).
  "${FAF_SRC}/platform/posix/wx"
  # Stand-in <dsound.h>, <xact3.h> (null audio until milestone 3).
  "${FAF_SRC}/platform/posix/stubs/audio-movie/include")
target_include_directories(faf_engine PUBLIC
  "${FAF_SRC}"
  "${FAF_DEPENDENCIES_DIR}/boost_1_34_1"
  "${FAF_DEPENDENCIES_DIR}/zlib-1.2.3"
  # Vulkan headers (submodule): the Vulkan backend is the macOS renderer (MoltenVK).
  "${FAF_DEPENDENCIES_DIR}/vulkan/include"
  # SDL3 headers for the window system (src/platform/posix/shell).
  "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
# Development fallback for finding the Vulkan loader, the ICD/layer manifests and DXC
# when nothing else names them (FAF_VULKAN_LIBRARY, VK_DRIVER_FILES, FAF_DXC_DIR).
target_compile_definitions(faf_engine PUBLIC "FAF_VULKAN_SDK_DIR=\"${FAF_VULKAN_SDK}\"")
# The native build's own engine Lua (macos/fafengine-macos), when the run folder has no copy.
target_compile_definitions(faf_engine PUBLIC "FAF_MACOS_LUA_DIR=\"${CMAKE_CURRENT_LIST_DIR}/fafengine-macos\"")
target_link_libraries(faf_engine PUBLIC faf_codegen Wm3Foundation faf_zlib)
target_compile_options(faf_engine PUBLIC
  # Every translation unit sees the Win32 stand-ins, as every Windows TU sees
  # <windows.h> through platform/Platform.h.
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${FAF_SRC}/platform/posix/WinCompat.h\">"
  # The x86 layout asserts, off as in the x64 build (platform/posix/LayoutAsserts.h).
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${FAF_SRC}/platform/posix/LayoutAsserts.h\">"
  # The recovered tree is written against MSVC; keep clang's portability chatter
  # out of the log so real problems stand out (same list as src/CMakeLists.txt).
  -Wno-microsoft -Wno-unused-parameter -Wno-unused-variable -Wno-unused-private-field
  -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-but-set-variable
  -Wno-unused-lambda-capture -Wno-unused-value -Wno-unused-command-line-argument
  -Wno-missing-field-initializers -Wno-sign-compare -Wno-switch
  -Wno-invalid-offsetof -Wno-keyword-macro -Wno-unknown-pragmas
  -Wno-ignored-pragma-intrinsic -Wno-pragma-pack -Wno-ignored-attributes
  -Wno-deprecated-declarations -Wno-deprecated-enum-enum-conversion
  -Wno-nonportable-include-path -Wno-reorder-ctor
  -Wno-delete-non-abstract-non-virtual-dtor -Wno-overloaded-virtual
  -Wno-inconsistent-missing-override -Wno-missing-braces -Wno-braced-scalar-init
  -Wno-char-subscripts -Wno-parentheses -Wno-dangling-else
  -Wno-tautological-constant-out-of-range-compare
  -Wno-tautological-undefined-compare -Wno-undefined-var-template
  -Wno-writable-strings -Wno-comment -Wno-extra-tokens -Wno-nontrivial-memcall
  -Wno-mismatched-tags -Wno-extra-qualification)

# --- DXC bridge (milestone 2) ------------------------------------------------------------
# Defines faf_dxc_bridge (OBJECT) when src/platform/posix/dxc has sources; empty otherwise.
include(dxc.cmake)
set(_faf_bridge_objects "")
if(TARGET faf_dxc_bridge)
  set(_faf_bridge_objects $<TARGET_OBJECTS:faf_dxc_bridge>)
endif()

# --- Headless replay runner (milestone 1) ---------------------------------------------
# faf_replay: the engine plus src/platform/posix/runner (main(), headless app, null
# renderer start-up). The executable is built from object lists only, engine objects
# first, so StaticInitPreRegister.cpp stays the first object linked and the engine's
# initializers keep src/sources.cmake order; the runner's objects come after them.
# faf_replay.map (ld map) shows the final order.
file(GLOB_RECURSE _faf_runner_sources CONFIGURE_DEPENDS "${FAF_SRC}/platform/posix/runner/*.cpp")
if(_faf_runner_sources)
  add_library(faf_runner OBJECT ${_faf_runner_sources})
  # Usage requirements only (includes, defines, force-includes): an OBJECT library
  # linked to another OBJECT library does not take its objects.
  target_link_libraries(faf_runner PUBLIC faf_engine)
  add_executable(faf_replay $<TARGET_OBJECTS:faf_engine> ${_faf_bridge_objects} $<TARGET_OBJECTS:faf_runner>)
  set_target_properties(faf_replay PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(faf_replay PRIVATE
    Wm3Foundation faf_zlib SDL3::SDL3 ${FAF_MACOS_FRAMEWORKS}
    # Wm3System.cpp (bundle paths): FSRef / ProcessSerialNumber APIs.
    "-framework CoreServices" "-framework ApplicationServices")
  # The build path may contain a space (/Volumes/SSD Programs/...): target_link_options
  # quotes the argument, a raw -Wl item in target_link_libraries does not.
  target_link_options(faf_replay PRIVATE "LINKER:-map,${CMAKE_CURRENT_BINARY_DIR}/faf_replay.map")
endif()

# --- The game (milestone 2) -----------------------------------------------------------
# faf_game: the engine plus src/platform/posix/game (main() in place of WinMain.cpp's
# WinMain, running CScApp through WIN_AppExecute with real windows). Same object order
# rule as faf_replay.
file(GLOB_RECURSE _faf_game_sources CONFIGURE_DEPENDS "${FAF_SRC}/platform/posix/game/*.cpp")
if(_faf_game_sources)
  add_library(faf_game_main OBJECT ${_faf_game_sources})
  target_link_libraries(faf_game_main PUBLIC faf_engine)
  add_executable(faf_game $<TARGET_OBJECTS:faf_engine> ${_faf_bridge_objects} $<TARGET_OBJECTS:faf_game_main>)
  set_target_properties(faf_game PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(faf_game PRIVATE
    Wm3Foundation faf_zlib SDL3::SDL3 ${FAF_MACOS_FRAMEWORKS}
    "-framework CoreServices" "-framework ApplicationServices")
  target_link_options(faf_game PRIVATE "LINKER:-map,${CMAKE_CURRENT_BINARY_DIR}/faf_game.map")
endif()
