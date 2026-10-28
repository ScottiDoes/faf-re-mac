#pragma once

// Test doubles for fonts_test: what CD3DFont.cpp calls outside the Gdi area, recorded so the
// test can rebuild exactly what the game would draw.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fonts_test
{
  // One CD3DBatchTexture::FromDXT5 call: the glyph texture (DXT5 blocks, as uploaded).
  struct Dxt5Texture
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint64_t> blocks; // two words per 4x4 block: alpha, colour
  };

  // One CD3DPrimBatcher::DrawChar call: the glyph quad in screen space.
  struct Quad
  {
    const void* texture = nullptr;
    float left = 0.0f;   // top-left corner
    float top = 0.0f;
    float right = 0.0f;  // bottom-right corner
    float bottom = 0.0f;
    std::uint32_t color = 0;
  };

  std::map<const void*, Dxt5Texture>& Textures();
  std::vector<Quad>& Quads();
  std::vector<std::string>& Warnings();

  // What the test is doing (face and size), prefixed to each recorded warning.
  std::string& Context();
} // namespace fonts_test
