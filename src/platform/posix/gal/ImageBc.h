#pragma once

// POSIX texture codec (the role D3DX11 plays on Windows, see
// stubs/render/gal/ImageCodecPosix.cpp): block-compressed (BC1-BC5) 4x4 blocks.
//
// Pixels are RGBA, 8 bits per channel, 16 of them row by row (64 bytes).
//
// Decoding follows the Direct3D 10 block compression rules (what D3DX11 decodes with):
// a BC1 block whose first endpoint is not above the second has three colours and
// transparent black; BC2/BC3 colour blocks always have four colours. Endpoints expand by bit
// replication, the interpolated colours round to nearest.
//
// Encoding is this layer's own (D3DX11's compressor is not available): principal-axis
// endpoints refined by least squares for colour, optimal single-colour endpoints for flat
// blocks, both BC3 alpha modes tried. In BC2/BC3 blocks each pixel's colour counts by its
// alpha, so the (often white) colour of transparent texels does not drag the visible ones
// towards grey - the game's UI art and cursors have such edges. BC1 keeps punch-through
// alpha (alpha < 128 becomes transparent black, as D3DX does). The colour block of BC2/BC3
// is always written with the first endpoint above the second (or equal with every index 0),
// so decoders that treat it as a BC1 block (some D3D9-era hardware) read the same colours.

#include <cstdint>

namespace faf_posix::gal::image
{
  void DecodeBc1Block(const std::uint8_t* block, std::uint8_t* rgba) noexcept;
  void DecodeBc2Block(const std::uint8_t* block, std::uint8_t* rgba) noexcept;
  void DecodeBc3Block(const std::uint8_t* block, std::uint8_t* rgba) noexcept;
  /** BC4 into channel `channel` of `rgba` (stride 4); the other channels are left alone. */
  void DecodeBc4Block(const std::uint8_t* block, std::uint8_t* rgba, int channel) noexcept;

  /** `allowTransparent`: pixels with alpha < 128 become transparent black (3-colour mode). */
  void EncodeBc1Block(const std::uint8_t* rgba, std::uint8_t* block, bool allowTransparent) noexcept;
  void EncodeBc2Block(const std::uint8_t* rgba, std::uint8_t* block) noexcept;
  void EncodeBc3Block(const std::uint8_t* rgba, std::uint8_t* block) noexcept;
  /** BC4 from channel `channel` of `rgba` (stride 4). */
  void EncodeBc4Block(const std::uint8_t* rgba, std::uint8_t* block, int channel) noexcept;

  /**
   * Lossless re-packing of one block into a BC3 block where the source block's colours are
   * representable as is: BC3 -> BC3 (copy), BC1 in four-colour mode -> BC3 (colour copied,
   * alpha 255), BC2 -> BC3 (colour copied, alpha re-encoded). `sourceKind` 1/2/3 = BC1/2/3.
   * False when the block has to be decoded and encoded again.
   */
  [[nodiscard]] bool TranscodeBlockToBc3(int sourceKind, const std::uint8_t* source, std::uint8_t* block) noexcept;
} // namespace faf_posix::gal::image
