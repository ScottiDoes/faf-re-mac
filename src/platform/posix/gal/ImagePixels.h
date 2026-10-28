#pragma once

// POSIX texture codec (the role D3DX11 plays on Windows, see
// stubs/render/gal/ImageCodecPosix.cpp): pixel conversion between the DXGI formats the
// engine's textures and render targets use and 8-bit RGBA, the working format for
// resizing, mip generation and block compression.
//
// Decoding expands narrow channels by bit replication (5 and 6 bit) or exact scaling,
// fills missing channels as Direct3D samples them (0 for G/B, 1 for A; A8 is 0,0,0,A) and
// clamps float formats to [0, 1]. Encoding rounds to nearest; BC1 keeps punch-through
// alpha (alpha < 128).

#include <cstdint>
#include <vector>

#include <dxgiformat.h>

namespace faf_posix::gal::image
{
  /** An RGBA image, 8 bits per channel, rows tightly packed. */
  struct Rgba8Image
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;

    void Resize(std::uint32_t newWidth, std::uint32_t newHeight)
    {
      width = newWidth;
      height = newHeight;
      pixels.assign(static_cast<std::size_t>(newWidth) * newHeight * 4U, 0U);
    }
  };

  [[nodiscard]] bool CanDecode(DXGI_FORMAT format) noexcept;
  [[nodiscard]] bool CanEncode(DXGI_FORMAT format) noexcept;

  /** 1-5 for BC1-BC5 (UNORM, SRGB, TYPELESS), 0 otherwise. */
  [[nodiscard]] int BlockKind(DXGI_FORMAT format) noexcept;

  /** One `width` x `height` level of `format` (rows `rowPitch` apart: of blocks for BC) to RGBA. */
  [[nodiscard]] bool DecodeToRgba8(
    DXGI_FORMAT format, const std::uint8_t* data, std::uint32_t rowPitch, std::uint32_t width, std::uint32_t height, Rgba8Image& out
  );

  /**
   * `image` into one level of `format`, rows `rowPitch` apart. Block-compressed levels whose
   * size is not a multiple of 4 repeat the last row and column into the unused texels.
   */
  [[nodiscard]] bool EncodeFromRgba8(const Rgba8Image& image, DXGI_FORMAT format, std::uint8_t* out, std::uint32_t rowPitch);
} // namespace faf_posix::gal::image
