// A game effect drawn by the Vulkan gal device on a real window (macos/tests/vulkan, see
// vulkan.md): the effect file merged behind its compat preamble as
// CD3DEffectTechnique::LoadEffect does, Device::CreateEffect (HLSL preprocess,
// d3d11 translation, DXC to SPIR-V), then per frame two techniques of it on quads in
// vertex format 6 (the prim batcher's: POSITION float3, COLOR rgba8, TEXCOORD0 float2):
// one textured with an engine-written checkerboard through `texturedTechnique`, one
// with `plainTechnique`, and one more with `plainTechnique` drawn indexed (16-bit indices
// starting at byte 2).
//
//   vk_effect_test --compat d3d10states.compat --effect primbatcher.fx
//                  [--textured TAlphaBlendLinearSampleNoDepth] [--plain TRed] [--texture file.dds] [--frames N]
//
// Driver and validation as vk_device_test (FAF_VULKAN_DRIVER, FAF_VULKAN_VALIDATION).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ShellDouble.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/D3D9Utils.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/DrawContext.hpp"
#include "gpg/gal/Effect.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/EffectVariable.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/DrawIndexedContext.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/Matrix.h"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.h"
#include "gpg/gal/VertexBuffer.hpp"
#include "gpg/gal/VertexBufferContext.hpp"
#include "gpg/gal/VertexFormat.hpp"
#include "platform/posix/shell/Shell.h"

namespace
{
  class StderrLog final : public gpg::LogTarget
  {
  public:
    StderrLog()
      : gpg::LogTarget(true)
    {}

    void OnMessage(
      const gpg::LogSeverity level, const msvc8::string& message, const msvc8::vector<msvc8::string>& context, const int previousDepth
    ) override
    {
      static_cast<void>(context);
      static_cast<void>(previousDepth);
      std::fprintf(stderr, "%s: %s\n", (level == gpg::LogSeverity::Warn) ? "warning" : "info", message.c_str());
    }
  };

  const char* Argument(int argc, char** argv, const char* name, const char* fallback)
  {
    for (int index = 1; index + 1 < argc; ++index) {
      if (std::strcmp(argv[index], name) == 0) {
        return argv[index + 1];
      }
    }
    return fallback;
  }

  std::vector<char> ReadFile(const char* path)
  {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }

  struct Vertex
  {
    float x, y, z;
    std::uint32_t color; // D3DCOLOR (0xAARRGGBB); the effect's CompatSwizzle reorders it
    float u, v;
  };
  static_assert(sizeof(Vertex) == 24U, "vertex format 6 is 24 bytes");

  boost::shared_ptr<gpg::gal::EffectTechnique> FindTechnique(gpg::gal::Effect& effect, const char* name)
  {
    msvc8::vector<boost::shared_ptr<gpg::gal::EffectTechnique>> techniques;
    effect.GetTechniques(techniques);
    for (const boost::shared_ptr<gpg::gal::EffectTechnique>& technique : techniques) {
      if (technique != nullptr && std::strcmp(technique->GetName()->c_str(), name) == 0) {
        return technique;
      }
    }
    return {};
  }

  /** Two triangles from (x0, y0) to (x1, y1) in clip space, uv 0..uvMax. */
  void Quad(Vertex* out, float x0, float y0, float x1, float y1, std::uint32_t color, float uvMax)
  {
    const Vertex corners[4] = {
      {x0, y1, 0.5f, color, 0.0f, 0.0f},
      {x1, y1, 0.5f, color, uvMax, 0.0f},
      {x0, y0, 0.5f, color, 0.0f, uvMax},
      {x1, y0, 0.5f, color, uvMax, uvMax},
    };
    const int order[6] = {0, 1, 2, 2, 1, 3};
    for (int index = 0; index < 6; ++index) {
      out[index] = corners[order[index]];
    }
  }
} // namespace

int main(int argc, char** argv)
{
  static StderrLog* const log = new StderrLog();
  static_cast<void>(log);
  const char* const compatPath = Argument(argc, argv, "--compat", "d3d10states.compat");
  const char* const effectPath = Argument(argc, argv, "--effect", "primbatcher.fx");
  const char* const texturedName = Argument(argc, argv, "--textured", "TAlphaBlendLinearSampleNoDepth");
  const char* const plainName = Argument(argc, argv, "--plain", "TRed");
  const int frames = std::atoi(Argument(argc, argv, "--frames", "60"));

  const std::vector<char> compat = ReadFile(compatPath);
  const std::vector<char> source = ReadFile(effectPath);
  if (source.empty()) {
    std::fprintf(stderr, "test: cannot read %s\n", effectPath);
    std::_Exit(3);
  }

  if (!faf_posix::shell::Init()) {
    std::_Exit(3);
  }
  const HWND window = faf_vk_test::CreateTestWindow("faf-re Vulkan effect test", 320, 200);
  int width = 0;
  int height = 0;
  faf_vk_test::PixelSize(window, width, height);

  gpg::gal::DeviceContext context(gpg::gal::DeviceApi::Vulkan);
  context.mVSync = true;
  gpg::gal::Head head{};
  head.mHandle = window;
  head.mWindow = window;
  head.mWindowed = true;
  head.mWidth = static_cast<std::uint32_t>(width);
  head.mHeight = static_cast<std::uint32_t>(height);
  head.framesPerSecond = 60;
  context.AddHead(head);

  int status = 0;
  int drawn = 0;
  try {
    gpg::gal::Device::Create(&context);
    gpg::gal::Device* const device = gpg::gal::Device::GetInstance();

    // The effect, as CD3DEffectTechnique::LoadEffect builds it: compat preamble + source.
    gpg::MemBuffer<char> merged = gpg::AllocMemBuffer(compat.size() + source.size());
    if (!compat.empty()) {
      std::memcpy(merged.GetPtr(0U, 0U), compat.data(), compat.size());
    }
    std::memcpy(merged.GetPtr(compat.size(), 0U), source.data(), source.size());
    const gpg::gal::EffectContext effectContext(false, effectPath, "", merged, msvc8::vector<gpg::gal::EffectMacro>{});
    const boost::shared_ptr<gpg::gal::Effect> effect = device->CreateEffect(effectContext);
    const boost::shared_ptr<gpg::gal::EffectTechnique> textured = FindTechnique(*effect, texturedName);
    const boost::shared_ptr<gpg::gal::EffectTechnique> plain = FindTechnique(*effect, plainName);
    if (textured == nullptr || plain == nullptr) {
      throw std::runtime_error("technique not found");
    }

    // --texture <file>: a texture file decoded by the engine's image codec (source 1);
    // otherwise a 64x64 checkerboard written by the engine (Lock/Unlock), format 2 (B8G8R8A8).
    const char* const textureFile = Argument(argc, argv, "--texture", nullptr);
    std::vector<char> textureBytes;
    if (textureFile != nullptr) {
      textureBytes = ReadFile(textureFile);
    }
    gpg::gal::TextureContext textureContext;
    textureContext.source_ = 2U;
    textureContext.location_ = "checkerboard";
    textureContext.format_ = 2U;
    textureContext.width_ = 64U;
    textureContext.height_ = 64U;
    textureContext.mipmapLevels_ = 1U;
    if (!textureBytes.empty()) {
      gpg::MemBuffer<char> file = gpg::AllocMemBuffer(textureBytes.size());
      std::memcpy(file.GetPtr(0U, 0U), textureBytes.data(), textureBytes.size());
      // As RD3DTextureResource::LoadTexture: the file's bytes, format 0, source 1 (archive).
      textureContext.location_ = textureFile;
      textureContext.SetDataBuffer(gpg::MemBuffer<const char>(file));
      textureContext.format_ = 0U;
      textureContext.source_ = 1U;
    }
    const boost::shared_ptr<gpg::gal::Texture> texture = device->CreateTexture(&textureContext);
    if (textureBytes.empty()) {
      RECT all{0, 0, 64, 64};
      const gpg::gal::TextureLockRect lock = texture->Lock(0, all, 0);
      for (int y = 0; y < 64; ++y) {
        auto* const row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(lock.bits) + y * lock.pitch);
        for (int x = 0; x < 64; ++x) {
          const bool light = ((x / 8) + (y / 8)) % 2 == 0;
          row[x] = light ? 0xFFFFFFFFU : 0xFF2080F0U; // white / sky blue (0xAARRGGBB)
        }
      }
      texture->Unlock(lock);
    }

    gpg::gal::Matrix identity{};
    std::memset(&identity, 0, sizeof(identity));
    float* const cells = reinterpret_cast<float*>(&identity);
    cells[0] = cells[5] = cells[10] = cells[15] = 1.0f;
    effect->GetVariable("CompositeMatrix")->SetMatrix4x4(&identity);
    effect->GetVariable("Texture1")->SetTexture(texture);

    const boost::shared_ptr<gpg::gal::VertexFormat> format = device->CreateVertexFormat(6U);
    gpg::gal::VertexBufferContext bufferContext(18U, sizeof(Vertex), 0U, 0U);
    const boost::shared_ptr<gpg::gal::VertexBuffer> vertices = device->CreateVertexBuffer(&bufferContext);
    {
      auto* const out = static_cast<Vertex*>(vertices->Lock(0U, 18U * sizeof(Vertex), gpg::gal::MohoD3DLockFlags::None));
      Quad(out, -0.8f, -0.6f, 0.1f, 0.7f, 0xFFFFFFFFU, 2.0f);   // textured, uv 0..2 (the sampler's addressing)
      Quad(out + 6, 0.3f, -0.4f, 0.8f, 0.4f, 0xFFFFFFFFU, 1.0f);  // plain
      Quad(out + 12, 0.3f, -0.9f, 0.8f, -0.6f, 0xFFFFFFFFU, 1.0f); // plain, indexed
      vertices->Unlock();
    }
    // 16-bit indices after one pad index: the indexed draw starts at byte 2, an offset
    // Metal does not take for an index buffer as is (MoltenVK must handle it).
    gpg::gal::IndexBufferContext indexContext(7U, 1U, 0U);
    const boost::shared_ptr<gpg::gal::IndexBuffer> indices = device->CreateIndexBuffer(&indexContext);
    {
      std::int16_t* const out = indices->Lock(0U, 7U * sizeof(std::int16_t), gpg::gal::MohoD3DLockFlags::None);
      const std::int16_t values[7] = {0x7FFF, 0, 1, 2, 3, 4, 5};
      std::memcpy(out, values, sizeof(values));
      indices->Unlock();
    }

    for (int frame = 0; frame < frames; ++frame) {
      faf_posix::shell::PumpEvents();
      device->BeginScene();
      device->ClearTarget(device->GetHeadOutputContext(0));
      device->Clear(true, true, true, 0xFF303030U, 1.0f, 0);
      const gpg::gal::DrawContext draw(gpg::gal::DrawContext::TOPOLOGY_TRIANGLELIST, 6U, 0U);
      for (int quad = 0; quad < 2; ++quad) {
        gpg::gal::EffectTechnique& technique = (quad == 0) ? *textured : *plain;
        technique.BeginTechnique();
        technique.BeginPass(0);
        device->SetVertexDeclaration(format);
        device->SetVertexBuffer(0U, vertices, 0, quad * 6);
        device->DrawPrimitive(&draw);
        technique.EndPass();
        technique.EndTechnique();
        ++drawn;
      }
      plain->BeginTechnique();
      plain->BeginPass(0);
      device->SetVertexDeclaration(format);
      device->SetVertexBuffer(0U, vertices, 0, 0);
      device->SetBufferIndices(indices);
      const gpg::gal::DrawIndexedContext indexed(gpg::gal::DrawContext::TOPOLOGY_TRIANGLELIST, 6U, 6U, 1U, 12U);
      device->DrawIndexedPrimitive(&indexed);
      plain->EndPass();
      plain->EndTechnique();
      ++drawn;
      device->EndScene();
      device->Present();
    }
  } catch (const gpg::gal::Error& error) {
    std::fprintf(stderr, "test: gal error %s(%d): %s\n", error.GetFile(), error.GetLine(), error.what());
    status = 1;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "test: exception: %s\n", error.what());
    status = 1;
  }
  gpg::gal::Device::DestroyInstance();
  faf_vk_test::DestroyTestWindow(window);
  faf_posix::shell::Shutdown();
  std::fprintf(stderr, "test: %d draws, %s\n", drawn, (status == 0) ? "PASS" : "FAIL");
  std::fflush(stderr);
  std::_Exit(status);
}
