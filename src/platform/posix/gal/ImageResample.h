#pragma once

// POSIX texture codec (the role D3DX11 plays on Windows, see
// stubs/render/gal/ImageCodecPosix.cpp): resizing and mip levels for 8-bit RGBA images.
//
// D3DX11's default mip filter is a box filter (each texel of the next level the average of
// a 2x2 block). `ResizeArea` weights every source texel by the area it shares with the
// destination texel ("fant"), which is exactly that box for a halving and extends it to odd
// sizes (a 21-high level becomes 10 high, each new row covering 2.1 old ones) and to
// stretching (D3DX11's default load filter, triangle, fits an 18x18 image going to BC3
// over 20x20; this does the same with area weights). Channels are filtered independently,
// alpha is not premultiplied, results round to nearest - as D3DX.

#include <cstdint>

#include "ImagePixels.h"

namespace faf_posix::gal::image
{
  /** `source` resampled to `width` x `height` with area weights. */
  [[nodiscard]] Rgba8Image ResizeArea(const Rgba8Image& source, std::uint32_t width, std::uint32_t height);

  /** `source` placed at the top left of a `width` x `height` image, unscaled; the rest transparent black (D3DX11_FILTER_NONE). */
  [[nodiscard]] Rgba8Image PlaceUnscaled(const Rgba8Image& source, std::uint32_t width, std::uint32_t height);

  /** The next mip level: max(1, width / 2) x max(1, height / 2). */
  [[nodiscard]] Rgba8Image NextMipLevel(const Rgba8Image& level);
} // namespace faf_posix::gal::image
