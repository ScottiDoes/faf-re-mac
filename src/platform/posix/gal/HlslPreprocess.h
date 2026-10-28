#pragma once

// D3DPreprocess's job on POSIX. Lead-owned contract: the shaders package implements it
// (src/platform/posix/gal/HlslPreprocess.cpp); DeviceVulkan::CreateEffect's FAF_POSIX
// branch calls it where Windows calls d3dcompiler_47's D3DPreprocess, and hands the text
// to d3d11::TranslateEffectSource unchanged. The text must therefore be what that
// translator accepts from D3DPreprocess: every macro expanded, conditionals resolved,
// comments removed.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace faf_posix::gal
{
  struct PreprocessedEffect
  {
    bool succeeded = false;
    std::string text;   // the preprocessed source (no terminating NUL)
    std::string errors; // the diagnostics when it failed, warnings otherwise
  };

  // Preprocesses an effect (.fx) source. `defines` are the effect's macros in order
  // (D3D_SHADER_MACRO pairs without the terminator). As on Windows, where the engine
  // passes no ID3DInclude, an #include fails.
  [[nodiscard]] PreprocessedEffect PreprocessEffect(
    std::string_view source, const char* sourceName, const std::vector<std::pair<std::string, std::string>>& defines
  );
} // namespace faf_posix::gal
