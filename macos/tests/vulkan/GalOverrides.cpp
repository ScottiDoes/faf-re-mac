// Test-only replacements for the two services DeviceVulkan::Setup insists on and the
// clear/present test never uses, while their POSIX ports (shaders package: DXC;
// textures package: the image codec) are not in yet. The test's CMakeLists hides the
// engine's own definitions of exactly these symbols (ld -r -unexported_symbol) so
// these win; everything else in those objects is the engine's.
//
//   ShaderCompiler::Available(): true (no effect is created, nothing is compiled).
//   ImageCodec::Instance(): a codec object (no texture file is decoded).

#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/effects/ShaderCompiler.h"

namespace gpg::gal::effects
{
  bool ShaderCompiler::Available() const noexcept
  {
    return true;
  }
} // namespace gpg::gal::effects

namespace gpg::gal::imaging
{
  ImageCodec* ImageCodec::Instance()
  {
    static ImageCodec codec;
    return &codec;
  }
} // namespace gpg::gal::imaging
