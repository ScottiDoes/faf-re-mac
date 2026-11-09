// POSIX build: renderer-neutral definitions that live in translation units of the
// excluded Direct3D backends on Windows, so the rest of the engine (and the Vulkan
// backend) still finds them.
//
// - The base-class constructors/destructors of five gal interfaces: defined in
//   gpg/gal/backends/d3d9/D3D9Interfaces.cpp next to their Direct3D 9 subclasses
//   (0x00940xxx-0x00941xxx in the binary). Empty bodies there too.
// - The Direct3D 12 tuning switches the moho console variables bind to
//   (moho/render/GpuPathSettings.cpp): defined in
//   gpg/gal/backends/d3d12/D3D12Core.cpp and PipelineCacheD3D12.cpp, with the same
//   defaults. Nothing reads them without a Direct3D 12 device.

#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/PipelineDiskCache.h"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/PlacedResources.h"
#include "gpg/gal/RenderTarget.hpp"

namespace gpg::gal
{
  EffectTechnique::EffectTechnique() = default;

  EffectTechnique::~EffectTechnique() = default;

  IndexBuffer::IndexBuffer() = default;

  IndexBuffer::~IndexBuffer() = default;

  RenderTarget::RenderTarget() = default;

  RenderTarget::~RenderTarget() = default;

  CubeRenderTarget::CubeRenderTarget() = default;

  CubeRenderTarget::~CubeRenderTarget() = default;

  PipelineState::PipelineState() = default;

  PipelineState::~PipelineState() = default;

  bool gPlacedResources = true;
  bool gSharedBuffers = true;
  bool gPipelineDiskCache = true;
} // namespace gpg::gal
