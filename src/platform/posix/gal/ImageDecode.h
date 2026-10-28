#pragma once

// POSIX texture codec: what D3DX11CreateTextureFromMemory (plus the staging copy) and
// D3DX11SaveTextureToMemory do for ImageCodec on Windows (gpg/gal/backends/common/ImageCodec.cpp),
// without Direct3D. stubs/render/gal/ImageCodecPosix.cpp puts the engine's own DDS direct
// path and decode cache in front of it.
//
// Decoding, with D3DX11_IMAGE_LOAD_INFO's defaults plus the DecodeOptions overrides:
// - the file's format (ImageDds.h; other image files come back as B8G8R8A8_UNORM, the order
//   D3D9's A8R8G8B8 had), or `options.format`;
// - a full mip chain (D3DX11_DEFAULT MipLevels), or `options.mipLevels`. Levels the file has
//   are kept as they are; the rest are built from the last one with ResizeArea's box filter
//   and encoded (BC: ImageBc.h);
// - `options.skipMipLevels` drops that many top levels (D3DX11's FirstMipLevel; none when the
//   file has no level that small, as the Windows caller's retry from 0);
// - a block-compressed result has its top level rounded up to a multiple of 4. With the
//   default filter the image is stretched over it (ResizeArea, D3DX11: triangle); with
//   ResizeFilter::None it is placed unscaled and the rest is transparent black. BC1/BC2/BC3
//   blocks that lie inside the image are carried over without decoding (BC3 targets take
//   BC1/BC2 blocks losslessly where the colours allow); only the edge blocks are encoded.
// - `options.width`/`height` resize the same way.
// Volume textures keep the file's levels (no 3D mip generation) and take no conversion.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gpg/gal/backends/common/ImageCodec.h"

namespace faf_posix::gal::image
{
  [[nodiscard]] HRESULT DecodeTexture(
    const void* data, std::size_t bytes, const gpg::gal::imaging::DecodeOptions& options, gpg::gal::imaging::Image& outImage
  );

  /** The file's own top-level size (DDS header, or the image file's). */
  [[nodiscard]] HRESULT ReadTextureSize(const void* data, std::size_t bytes, std::uint32_t& outWidth, std::uint32_t& outHeight);

  /** DDS: the whole image. BMP/JPG/PNG: its first subresource, converted to 8-bit RGBA. */
  [[nodiscard]] HRESULT EncodeTexture(
    const gpg::gal::imaging::Image& image, gpg::gal::imaging::FileFormat format, std::vector<std::uint8_t>& outBytes
  );
} // namespace faf_posix::gal::image
