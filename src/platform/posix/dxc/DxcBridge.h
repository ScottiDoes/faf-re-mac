#pragma once

// The engine's only way into DXC (libdxcompiler.dylib) on POSIX.
//
// This header is plain C++: no Win32 stand-in and no DXC type crosses it, so engine
// translation units (which see platform/posix/WinCompat.h) and the bridge (which sees
// DXC's own dxcapi.h + WinAdapter.h, macos/dxc.cmake) can both include it. Users:
// platform/posix/stubs/render/gal/ShaderCompilerPosix.cpp (gpg::gal::effects::
// ShaderCompiler on POSIX) and platform/posix/gal/HlslPreprocess.cpp (D3DPreprocess's
// job). Notes: macos/porting/shaders.md.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace faf_posix::dxc
{
  struct LoadStatus
  {
    bool loaded = false;
    std::string error;   // why DXC could not be loaded (empty when it was)
    std::string version; // "dxc <major>.<minor> (<commits> <hash>)": part of shader cache keys
    std::string path;    // the library that was loaded
  };

  /**
   * Loads libdxcompiler.dylib on first use (thread-safe; later calls return the same
   * status). Looked for in $FAF_DXC_DIR, then the Vulkan SDK the build was configured
   * with (FAF_VULKAN_SDK_DIR "/lib"), then next to the executable, then by name on the
   * dynamic loader's search path. Checks that it provides IDxcUtils and IDxcCompiler3.
   */
  [[nodiscard]] const LoadStatus& Load();

  struct CompileOutput
  {
    bool succeeded = false;           // IDxcResult::GetStatus succeeded
    std::vector<std::uint8_t> object; // DXC_OUT_OBJECT (DXIL or SPIR-V)
    std::string text;                 // DXC_OUT_HLSL (-P: the preprocessed source)
    std::string messages;             // DXC_OUT_ERRORS (errors, or warnings on success)
  };

  /**
   * IDxcCompiler3::Compile of UTF-8 `source` with the DXC command line `arguments`
   * (wchar_t as DXC takes them on this platform), without an include handler. Each
   * thread has its own compiler object, so threads compile in parallel. Fails with a
   * message when DXC is not loaded.
   */
  [[nodiscard]] CompileOutput Compile(std::string_view source, const std::vector<std::wstring>& arguments);

  /** UTF-8 to the wchar_t text DXC takes on this platform (UTF-32); bad bytes become U+FFFD. */
  [[nodiscard]] std::wstring Widen(std::string_view utf8);
} // namespace faf_posix::dxc
