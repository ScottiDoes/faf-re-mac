# Tests of the Vulkan gal backend on the Apple drivers (package "vulkan", see
# macos/porting/vulkan.md). Builds the engine (macos/, as faf_replay links it) into this
# tree and links the tests against its objects:
#
#   cmake -S macos/tests/vulkan -B build/m2-vulkan-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
#   ninja -C build/m2-vulkan-tests vk_device_test vk_effect_test vk_shell_test probe_swapchain faf_replay_check
#   macos/tests/vulkan/run_device_test.sh <moltenvk|kosmickrisp> <plain|validate|screenshot>
#
# vk_device_test, vk_effect_test: gpg::gal::Device (Vulkan) on an SDL3 window through a
#   test double of Shell.h (ShellDouble.cpp); the shell's own Shell.h functions are
#   hidden in Shell.cpp.o (ld -r -unexported_symbol) so the double's win. With
#   FAF_VK_TEST_FAKE_SERVICES=ON, ShaderCompiler::Available and ImageCodec::Instance are
#   replaced the same way (GalOverrides.cpp; for when the shaders/textures packages are
#   not in).
# vk_shell_test: the device on the real shell's windows (frame + viewport child), no doubles.
# probe_swapchain: the drivers alone (no engine).
# faf_replay_check: the engine's headless runner from this tree (milestone 1).

cmake_minimum_required(VERSION 3.25)
project(faf_vulkan_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_ARCHITECTURES arm64)

option(FAF_VK_TEST_SHELL_DOUBLE "Use the test's SDL3 double of Shell.h" ON)
option(FAF_VK_TEST_FAKE_SERVICES "Stand in for ShaderCompiler::Available and ImageCodec::Instance" OFF)

set(FAF_VK_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_subdirectory("${FAF_VK_TEST_ROOT}/macos" "${CMAKE_BINARY_DIR}/engine" EXCLUDE_FROM_ALL)
# The engine directory's imported targets and plain variables stay in its scope.
find_package(SDL3 CONFIG REQUIRED HINTS /opt/homebrew)
set(_frameworks
  "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText"
  "-framework ImageIO" "-framework QuartzCore" "-framework Metal")

# --- The engine's objects, with the replaced symbols hidden ------------------------------
set(_hide_shell
  __ZN9faf_posix5shell4InitEv __ZN9faf_posix5shell8ShutdownEv __ZN9faf_posix5shell8IsActiveEv
  __ZN9faf_posix5shell10MetalLayerEP6HWND__ __ZN9faf_posix5shell10PumpEventsEv __ZN9faf_posix5shell12WaitForEventEi)
set(_hide_ShaderCompilerPosix __ZNK3gpg3gal7effects14ShaderCompiler9AvailableEv)
set(_hide_ImageCodecPosix __ZN3gpg3gal7imaging10ImageCodec8InstanceEv)

set(_replaced "")
set(_hidden_objects "")
macro(faf_vk_hide name regex)
  set(_object "$<FILTER:$<TARGET_OBJECTS:faf_engine>,INCLUDE,${regex}>")
  set(_out "${CMAKE_BINARY_DIR}/hidden/${name}.o")
  set(_args "")
  foreach(_symbol IN LISTS _hide_${name})
    list(APPEND _args -unexported_symbol ${_symbol})
  endforeach()
  add_custom_command(OUTPUT "${_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/hidden"
    COMMAND ld -r "${_object}" -o "${_out}" ${_args}
    DEPENDS faf_engine "${_object}"
    COMMENT "Hiding the engine's ${name} symbols the test replaces"
    VERBATIM COMMAND_EXPAND_LISTS)
  list(APPEND _hidden_objects "${_out}")
  list(APPEND _replaced "${regex}")
endmacro()
if(FAF_VK_TEST_SHELL_DOUBLE)
  # The double defines the Shell.h functions; the shell's own definitions of them are
  # hidden, the rest of the shell (the Window area's Win32 calls) stays.
  faf_vk_hide(shell "/shell/Shell[.]cpp[.]o$")
endif()
if(FAF_VK_TEST_FAKE_SERVICES)
  faf_vk_hide(ShaderCompilerPosix "/ShaderCompilerPosix[.]cpp[.]o$")
  faf_vk_hide(ImageCodecPosix "/ImageCodecPosix[.]cpp[.]o$")
endif()
set(_engine_objects "$<TARGET_OBJECTS:faf_engine>")
foreach(_regex IN LISTS _replaced)
  set(_engine_objects "$<FILTER:${_engine_objects},EXCLUDE,${_regex}>")
endforeach()
set_source_files_properties(${_hidden_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# --- vk_device_test --------------------------------------------------------------------
# Engine-flagged test code (WinCompat.h force-include, engine includes).
set(_test_sources device_test.cpp)
if(FAF_VK_TEST_FAKE_SERVICES)
  list(APPEND _test_sources GalOverrides.cpp)
endif()
add_library(vk_test_engine_side OBJECT ${_test_sources})
target_link_libraries(vk_test_engine_side PUBLIC faf_engine)

set(_double_objects "")
if(FAF_VK_TEST_SHELL_DOUBLE)
  # The shell double talks to SDL3 without the Win32 stand-ins.
  add_library(vk_test_shell_double OBJECT ShellDouble.cpp)
  target_include_directories(vk_test_shell_double PRIVATE "${FAF_VK_TEST_ROOT}/src")
  target_link_libraries(vk_test_shell_double PRIVATE SDL3::SDL3)
  set(_double_objects $<TARGET_OBJECTS:vk_test_shell_double>)
endif()

set(_bridge "")
if(TARGET faf_dxc_bridge)
  set(_bridge $<TARGET_OBJECTS:faf_dxc_bridge>)
endif()
# Engine objects first (StaticInitPreRegister.cpp stays the first object linked).
add_executable(vk_device_test ${_engine_objects} ${_hidden_objects} ${_bridge} $<TARGET_OBJECTS:vk_test_engine_side> ${_double_objects})
set_target_properties(vk_device_test PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(vk_device_test PRIVATE
  Wm3Foundation faf_zlib SDL3::SDL3 ${_frameworks}
  "-framework CoreServices" "-framework ApplicationServices")

# --- vk_effect_test: a game effect (DXC) drawn through the device --------------------------
add_library(vk_test_effect_side OBJECT effect_test.cpp $<$<BOOL:${FAF_VK_TEST_FAKE_SERVICES}>:GalOverrides.cpp>)
target_link_libraries(vk_test_effect_side PUBLIC faf_engine)
add_executable(vk_effect_test ${_engine_objects} ${_hidden_objects} ${_bridge} $<TARGET_OBJECTS:vk_test_effect_side> ${_double_objects})
set_target_properties(vk_effect_test PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(vk_effect_test PRIVATE
  Wm3Foundation faf_zlib SDL3::SDL3 ${_frameworks}
  "-framework CoreServices" "-framework ApplicationServices")

# --- probe_swapchain: the drivers alone (no engine) -------------------------------------
add_executable(probe_swapchain probe_swapchain.cpp)
target_include_directories(probe_swapchain PRIVATE "${FAF_VK_TEST_ROOT}/dependencies/vulkan/include")
target_compile_definitions(probe_swapchain PRIVATE "FAF_VULKAN_SDK_DIR=\"${FAF_VULKAN_SDK}\"")
target_link_libraries(probe_swapchain PRIVATE SDL3::SDL3)

# --- faf_replay_check: the headless runner of this tree (milestone 1 must keep passing) --
# FAF_REPLAY_EXE=build/m2-vulkan-tests/faf_replay_check OUT=build/m2-vulkan-tests/replay
# SPEED=50 macos/run_replay.sh
if(TARGET faf_runner)
  add_executable(faf_replay_check $<TARGET_OBJECTS:faf_engine> ${_bridge} $<TARGET_OBJECTS:faf_runner>)
  set_target_properties(faf_replay_check PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(faf_replay_check PRIVATE
    Wm3Foundation faf_zlib SDL3::SDL3 ${_frameworks}
    "-framework CoreServices" "-framework ApplicationServices")
endif()

# --- vk_shell_test: the device on the real shell's windows (frame + viewport child) -------
# No doubles: the engine's own shell, shader compiler and image codec.
add_library(vk_test_shell_side OBJECT shell_test.cpp)
target_link_libraries(vk_test_shell_side PUBLIC faf_engine)
add_executable(vk_shell_test $<TARGET_OBJECTS:faf_engine> ${_bridge} $<TARGET_OBJECTS:vk_test_shell_side>)
set_target_properties(vk_shell_test PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(vk_shell_test PRIVATE
  Wm3Foundation faf_zlib SDL3::SDL3 ${_frameworks}
  "-framework CoreServices" "-framework ApplicationServices")
