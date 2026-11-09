// POSIX stand-in for moho/render/font/MsdfFontCook.cpp (replaced in macos/exclusions.d/render.cmake),
// which needs FreeType and msdfgen: no cook succeeds and no atlas loads, so text stays on the
// glyph bitmaps (moho/render/d3d/CD3DFont.cpp).

#include "moho/render/font/MsdfFontCook.h"

namespace moho
{
  namespace
  {
    const char kNotBuilt[] = "this build has no MSDF font cook";
  }

  struct MsdfCookPlan::Impl
  {
    std::vector<SMsdfGpuEdge> edges;
    std::vector<SMsdfGpuGlyph> glyphs;
  };

  MsdfCookPlan::MsdfCookPlan() : mImpl(std::make_unique<Impl>()) {}
  MsdfCookPlan::~MsdfCookPlan() = default;

  bool MsdfCookPlan::Prepare(const void*, std::size_t, const SMsdfCookOptions&, std::string& error)
  {
    error = kNotBuilt;
    return false;
  }

  bool MsdfCookPlan::GenerateOnCpu(std::string& error)
  {
    error = kNotBuilt;
    return false;
  }

  bool MsdfCookPlan::TimedOut() const { return false; }
  void MsdfCookPlan::BuildGpuData() {}
  const std::vector<SMsdfGpuEdge>& MsdfCookPlan::GpuEdges() const { return mImpl->edges; }
  const std::vector<SMsdfGpuGlyph>& MsdfCookPlan::GpuGlyphs() const { return mImpl->glyphs; }
  float MsdfCookPlan::RangeTexels() const { return 0.0f; }
  void MsdfCookPlan::StoreGlyphField(std::size_t, std::vector<float>&) {}
  void MsdfCookPlan::Finish(SMsdfFontAtlas&) {}

  std::vector<std::uint32_t> MSDF_DefaultCodepoints() { return {}; }
  SMsdfCookOptions MSDF_MakeCookOptions(std::int32_t) { return {}; }
  SMsdfCookOptions MSDF_MakeModCookOptions(std::int32_t) { return {}; }

  bool MSDF_CookFont(const void*, std::size_t, const SMsdfCookOptions&, SMsdfFontAtlas&, std::string& error)
  {
    error = kNotBuilt;
    return false;
  }

  std::vector<std::string> MSDF_ReadFontNames(const void*, std::size_t) { return {}; }
  std::vector<std::uint8_t> MSDF_SaveAtlas(const SMsdfFontAtlas&) { return {}; }
  bool MSDF_LoadAtlas(const void*, std::size_t, SMsdfFontAtlas&) { return false; }
} // namespace moho
