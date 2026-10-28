# x87-core tests: the software x87 (src/platform/posix/math/X87Core.h, X87Emu.cpp) against
# Berkeley SoftFloat 3e, Layer 2 against Layer 1, known answers and timings.
# Standalone project; never part of build/macos:
#
#   cmake -S macos/tests/x87-core -B build/x87-core-tests -G Ninja
#   ninja -C build/x87-core-tests && build/x87-core-tests/x87core_tests [--quick] [suite...]

cmake_minimum_required(VERSION 3.20)
project(x87_core_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(FAF_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(FAF_SRC "${FAF_ROOT}/src")

find_package(Threads REQUIRED)

# --- Berkeley SoftFloat 3e, built exactly as macos/deps.cmake builds faf_softfloat --------
# (8086 specialization, LITTLEENDIAN, SOFTFLOAT_FAST_INT64, SOFTFLOAT_ROUND_ODD,
# THREAD_LOCAL=_Thread_local; macos/softfloat/platform.h). Test oracle only.
set(_sf_root "${FAF_ROOT}/dependencies/berkeley-softfloat-3")
if(NOT EXISTS "${_sf_root}/source/include/softfloat.h")
  message(FATAL_ERROR "SoftFloat not found at ${_sf_root}")
endif()
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
add_library(x87t_softfloat STATIC ${_sf_sources})
target_include_directories(x87t_softfloat
  PRIVATE "${FAF_ROOT}/macos/softfloat" "${_sf_root}/source/8086"
  PUBLIC "${_sf_root}/source/include")
target_compile_definitions(x87t_softfloat
  PRIVATE SOFTFLOAT_ROUND_ODD INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32
  PUBLIC LITTLEENDIAN SOFTFLOAT_FAST_INT64 THREAD_LOCAL=_Thread_local)
target_compile_options(x87t_softfloat PRIVATE -w)

# --- The engine's float code generation (macos/CMakeLists.txt faf_codegen) ---------------
set(_libm_calls
  acos asin atan atan2 cos cosh sin sinh tan tanh
  exp exp2 expm1 log log10 log1p log2 logb pow cbrt hypot erf erfc tgamma lgamma)
set(_codegen -ffp-contract=off -fno-fast-math -fmath-errno -fno-strict-aliasing -fwrapv
  -fno-delete-null-pointer-checks -fno-strict-float-cast-overflow -fno-finite-loops)
foreach(_fn IN LISTS _libm_calls)
  list(APPEND _codegen -fno-builtin-${_fn} -fno-builtin-${_fn}f -fno-builtin-${_fn}l)
endforeach()

set(_test_sources main.cpp TestLayer1.cpp TestRoutines.cpp TestKat.cpp TestIntegration.cpp Bench.cpp
  SoftFloatShim.c "${FAF_SRC}/platform/posix/math/X87Emu.cpp")

# x87core_tests: the transcendental instructions come from KernelStandIn.cpp (TEST-ONLY,
# host libm), so these tests depend on nothing but X87Core.h / X87Emu.cpp.
add_executable(x87core_tests ${_test_sources} KernelStandIn.cpp)

# x87core_tests_kernels: the same tests on the real kernels (src/.../X87Kernels.cpp, package
# x87-kernels), plus the known answers that need them (suite "integration").
set(_kernels "${FAF_SRC}/platform/posix/math/X87Kernels.cpp")
set(_targets x87core_tests)
if(EXISTS "${_kernels}")
  add_executable(x87core_tests_kernels ${_test_sources} "${_kernels}")
  target_compile_definitions(x87core_tests_kernels PRIVATE X87T_REAL_KERNELS)
  list(APPEND _targets x87core_tests_kernels)
endif()

foreach(_t IN LISTS _targets)
  target_include_directories(${_t} PRIVATE "${FAF_SRC}" "${CMAKE_CURRENT_LIST_DIR}")
  target_compile_options(${_t} PRIVATE ${_codegen} -O3 -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(${_t} PRIVATE x87t_softfloat Threads::Threads)
endforeach()
