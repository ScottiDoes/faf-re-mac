# DXC bridge: the code that talks to libdxcompiler.dylib (the Vulkan SDK's DXC).
#
# DXC's portable headers (dxcapi.h + WinAdapter.h) declare their own IUnknown, HRESULT,
# GUID, BSTR, ... which collide with the engine's Win32 stand-ins (WinCompat.h is
# force-included into every engine translation unit). So the bridge is a separate
# OBJECT library without those force-includes: src/platform/posix/dxc/*.cpp, exposing a
# plain C++ interface (no Win32 or DXC types) that the engine's ShaderCompilerPosix.cpp
# calls. Owned by the shaders package (macos/porting/shaders.md).
file(GLOB _faf_dxc_bridge_sources CONFIGURE_DEPENDS "${FAF_SRC}/platform/posix/dxc/*.cpp")
if(_faf_dxc_bridge_sources)
  add_library(faf_dxc_bridge OBJECT ${_faf_dxc_bridge_sources})
  target_include_directories(faf_dxc_bridge PRIVATE
    "${FAF_SRC}/platform/posix/dxc"
    "${FAF_VULKAN_SDK}/include/dxc")
  target_compile_definitions(faf_dxc_bridge PRIVATE "FAF_VULKAN_SDK_DIR=\"${FAF_VULKAN_SDK}\"")
  # No faf_codegen: nothing here touches simulation math.
endif()
