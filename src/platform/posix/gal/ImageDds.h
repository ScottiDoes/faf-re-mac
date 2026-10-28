#pragma once

// POSIX texture codec (the role D3DX11 plays on Windows, see
// stubs/render/gal/ImageCodecPosix.cpp): DDS files.
//
// Reading maps every pixel format the game data uses (and the other common ones) onto a
// DXGI format the Vulkan backend samples:
//   DXT1/DXT2/DXT3/DXT4/DXT5, ATI1/ATI2/BC4U/BC5U  -> BC1/BC2/BC2/BC3/BC3, BC4, BC5 (as is)
//   A8R8G8B8 / X8R8G8B8                             -> B8G8R8A8 / B8G8R8X8 (as is)
//   A8B8G8R8 / X8B8G8R8                             -> R8G8B8A8 (X: alpha set to 255)
//   R8G8B8 / B8G8R8 (24 bit)                        -> B8G8R8A8, alpha 255
//   A1R5G5B5, X1R5G5B5, R5G6B5, A4R4G4B4, X4R4G4B4  -> B8G8R8A8 (bit-replicated, exact)
//   L8, A8, A8L8, L16, G16R16, A2B10G10R10, the float fourCCs -> R8, A8, R8G8, R16, R16G16,
//                                                     R10G10B10A2, R16F..R32G32B32A32F
//   DX10 header                                     -> its format (as is)
// The 32-bit layouts keep the file's bytes, so a texture the engine locks reads in the D3D9
// byte order it was written in (D3DX11 may hand these back as R8G8B8A8; the sampled texels
// are the same). The 16-bit layouts expand to 8 bits because Direct3D 11.0 (and so D3DX11)
// has no such texture formats.
//
// Rows of uncompressed legacy formats are read tightly packed (width * bytes per texel).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpg/gal/backends/common/ImageCodec.h"

namespace faf_posix::gal::image
{
  struct DdsInfo
  {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // after conversion
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;  // as the header says (at least 1)
    std::uint32_t arraySize = 1;  // 6 for a cube
    bool cube = false;
    bool volume = false;
  };

  /** True when `data` starts with "DDS " and is long enough for the header (its dwSize is not checked, as D3DX). */
  [[nodiscard]] bool IsDds(const void* data, std::size_t bytes) noexcept;

  /** The header (and DX10 header) of a DDS file; false when not a DDS file or a layout this reader does not take. */
  [[nodiscard]] bool ReadDdsInfo(const void* data, std::size_t bytes, DdsInfo& outInfo) noexcept;

  /**
   * Every subresource the file holds, converted to `DdsInfo::format`. A 2D file shorter than its
   * header says keeps the levels that are complete; any other short file fails.
   */
  [[nodiscard]] bool ReadDds(const void* data, std::size_t bytes, gpg::gal::imaging::Image& outImage);

  /** A DDS file of `image`: a legacy header when its format has one, else a DX10 header. */
  [[nodiscard]] bool WriteDds(const gpg::gal::imaging::Image& image, std::vector<std::uint8_t>& outBytes);
} // namespace faf_posix::gal::image
