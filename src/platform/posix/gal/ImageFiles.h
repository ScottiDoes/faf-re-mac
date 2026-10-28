#pragma once

// POSIX texture codec (the role D3DX11 plays on Windows, see
// stubs/render/gal/ImageCodecPosix.cpp): image files other than DDS, through Apple's
// ImageIO (CGImageSource / CGImageDestination).
//
// Decoding takes every format ImageIO reads (PNG, JPEG, BMP, GIF, TIFF, TGA ...; a TGA has no
// signature, so data ImageIO does not recognise is tried again as TGA) and returns the raw
// samples as 8-bit RGBA: no colour management and alpha not premultiplied, like D3DX11 (WIC)
// on Windows. 16-bit samples round to 8 bits; grey and palette images expand to RGB.
// Encoding writes PNG and JPEG through ImageIO and BMP itself (32 bpp, top-down, as the
// engine's own BMP writer on Windows).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ImagePixels.h"

namespace faf_posix::gal::image
{
  [[nodiscard]] bool DecodeImageFile(const void* data, std::size_t bytes, Rgba8Image& outImage);

  /** The file's width and height without decoding its pixels. */
  [[nodiscard]] bool ReadImageFileSize(const void* data, std::size_t bytes, std::uint32_t& outWidth, std::uint32_t& outHeight);

  enum class EncodedFormat
  {
    Png,
    Jpeg,
    Bmp,
  };

  [[nodiscard]] bool EncodeImageFile(const Rgba8Image& image, EncodedFormat format, std::vector<std::uint8_t>& outBytes);
} // namespace faf_posix::gal::image
