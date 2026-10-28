# Translation units of src/sources.cmake the macOS build leaves out (regexes, matched
# against the list entries). Each group says what replaces it.

set(FAF_MACOS_EXCLUDED_SOURCES
  # Direct3D 9/10/11/12 and DXGI renderers: Vulkan (MoltenVK) is the macOS renderer.
  # Kept: the renderer-neutral tables and text the Vulkan backend shares,
  # dxgi/DxgiFormats.cpp, d3d11/D3D11VertexLayouts.cpp and d3d11/EffectSourceD3D11.cpp.
  "^gpg/gal/backends/(d3d9|d3d10|d3d12)/"
  "^gpg/gal/backends/d3d11/(D3D11Effects|D3D11Resources|DeviceD3D11|GpuDrivenD3D11)\\.cpp$"
  "^gpg/gal/backends/dxgi/(DxgiFactory|DxgiSwapChain)\\.cpp$"
  # Effects 11, only for the Direct3D 11 renderer.
  "/FX11/"
  # boost.thread's Win32 TLS-cleanup hooks; the pthread implementation needs none.
  "/libs/thread/src/tss_(dll|pe|hooks)\\.cpp$"
)

# A TU that a POSIX stand-in replaces goes in FAF_MACOS_REPLACED_SOURCES instead, as
# "<regex>=><path under src/>": the stand-in then compiles at the original's position, so
# static initializers keep their order (CMakeLists.txt). Use plain exclusion only when
# nothing replaces the TU, or when the stand-in has no static initializers.
set(FAF_MACOS_REPLACED_SOURCES "")

# Porting packages keep their own lists in exclusions.d/<package>.cmake, each appending
# to FAF_MACOS_EXCLUDED_SOURCES or FAF_MACOS_REPLACED_SOURCES with a reason per entry.
file(GLOB _faf_exclusion_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/exclusions.d/*.cmake")
foreach(_f IN LISTS _faf_exclusion_files)
  include("${_f}")
endforeach()
