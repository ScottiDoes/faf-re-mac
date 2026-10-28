// POSIX texture codec: the decode pipeline. See ImageDecode.h.

#include "ImageDecode.h"

#include <algorithm>
#include <cstring>

#include "ImageBc.h"
#include "ImageDds.h"
#include "ImageFiles.h"
#include "ImagePixels.h"
#include "ImageResample.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"

namespace faf_posix::gal::image
{
  using gpg::gal::imaging::DecodeOptions;
  using gpg::gal::imaging::FileFormat;
  using gpg::gal::imaging::Image;
  using gpg::gal::imaging::ResizeFilter;
  namespace dxgi = gpg::gal::dxgi;

  namespace
  {
    [[nodiscard]] std::uint32_t FullChain(const std::uint32_t width, const std::uint32_t height) noexcept
    {
      std::uint32_t levels = 1U;
      for (std::uint32_t side = std::max(width, height); side > 1U; side >>= 1U) {
        ++levels;
      }
      return levels;
    }

    [[nodiscard]] std::uint32_t RoundUp4(const std::uint32_t value) noexcept
    {
      return (value + 3U) & ~3U;
    }

    [[nodiscard]] std::uint32_t BlockBytes(const int kind) noexcept
    {
      return (kind == 1 || kind == 4) ? 8U : 16U;
    }

    [[nodiscard]] bool DecodeSubresource(const Image& image, const std::uint32_t index, Rgba8Image& out)
    {
      const Image::Subresource& subresource = image.subresources[index];
      return DecodeToRgba8(image.format, image.bytes.data() + subresource.offset, subresource.rowPitch, subresource.width,
                           subresource.height, out);
    }

    [[nodiscard]] bool EncodeSubresource(const Rgba8Image& pixels, Image& image, const std::uint32_t index)
    {
      const Image::Subresource& subresource = image.subresources[index];
      if (pixels.width != subresource.width || pixels.height != subresource.height) {
        return false;
      }
      return EncodeFromRgba8(pixels, image.format, image.bytes.data() + subresource.offset, subresource.rowPitch);
    }

    /**
     * One level of a BC1/BC2/BC3 source into a BC1/BC2/BC3 target of the same block grid origin.
     * Blocks whose texels (inside the target level) all lie inside `contentWidth` x
     * `contentHeight` are carried over (copied, or re-packed into BC3); the others are decoded,
     * cleared to transparent black outside the content and encoded.
     */
    void TranscodeLevel(
      const Image& source, const std::uint32_t sourceIndex, const std::uint32_t contentWidth, const std::uint32_t contentHeight, Image& target,
      const std::uint32_t targetIndex
    )
    {
      const Image::Subresource& from = source.subresources[sourceIndex];
      const Image::Subresource& to = target.subresources[targetIndex];
      const int sourceKind = BlockKind(source.format);
      const int targetKind = BlockKind(target.format);
      const std::uint32_t sourceBlocksWide = (from.width + 3U) / 4U;
      const std::uint32_t sourceBlocksHigh = (from.height + 3U) / 4U;
      const std::uint8_t* const sourceBytes = source.bytes.data() + from.offset;
      std::uint8_t* const targetBytes = target.bytes.data() + to.offset;
      std::uint8_t decoded[64];
      std::uint8_t pixels[64];
      for (std::uint32_t by = 0U; by < (to.height + 3U) / 4U; ++by) {
        for (std::uint32_t bx = 0U; bx < (to.width + 3U) / 4U; ++bx) {
          std::uint8_t* const out = targetBytes + static_cast<std::size_t>(by) * to.rowPitch + static_cast<std::size_t>(bx) * BlockBytes(targetKind);
          const bool haveSource = bx < sourceBlocksWide && by < sourceBlocksHigh;
          const std::uint8_t* const in =
            haveSource ? sourceBytes + static_cast<std::size_t>(by) * from.rowPitch + static_cast<std::size_t>(bx) * BlockBytes(sourceKind) : nullptr;
          const bool inside = std::min(bx * 4U + 4U, to.width) <= contentWidth && std::min(by * 4U + 4U, to.height) <= contentHeight;
          if (inside && haveSource) {
            if (sourceKind == targetKind) {
              std::memcpy(out, in, BlockBytes(targetKind));
              continue;
            }
            if (targetKind == 3 && TranscodeBlockToBc3(sourceKind, in, out)) {
              continue;
            }
          }
          std::memset(decoded, 0, sizeof(decoded));
          if (haveSource) {
            switch (sourceKind) {
            case 1:
              DecodeBc1Block(in, decoded);
              break;
            case 2:
              DecodeBc2Block(in, decoded);
              break;
            default:
              DecodeBc3Block(in, decoded);
              break;
            }
          }
          for (std::uint32_t y = 0U; y < 4U; ++y) {
            for (std::uint32_t x = 0U; x < 4U; ++x) {
              const bool content = bx * 4U + x < contentWidth && by * 4U + y < contentHeight;
              std::uint8_t* const pixel = pixels + (y * 4U + x) * 4U;
              if (content) {
                std::memcpy(pixel, decoded + (y * 4U + x) * 4U, 4U);
              } else {
                std::memset(pixel, 0, 4U);
              }
            }
          }
          switch (targetKind) {
          case 1:
            EncodeBc1Block(pixels, out, true);
            break;
          case 2:
            EncodeBc2Block(pixels, out);
            break;
          default:
            EncodeBc3Block(pixels, out);
            break;
          }
        }
      }
    }

    [[nodiscard]] bool Transcodable(const DXGI_FORMAT source, const DXGI_FORMAT target) noexcept
    {
      const int sourceKind = BlockKind(source);
      const int targetKind = BlockKind(target);
      return sourceKind >= 1 && sourceKind <= 3 && targetKind >= 1 && targetKind <= 3;
    }

    HRESULT ShapeVolume(const Image& source, const DecodeOptions& options, Image& out)
    {
      if ((options.format != DXGI_FORMAT_UNKNOWN && options.format != source.format) || (options.width != 0U && options.width != source.width) ||
          (options.height != 0U && options.height != source.height)) {
        return E_NOTIMPL;
      }
      std::uint32_t skip = options.skipMipLevels;
      if (skip >= source.mipLevels) {
        skip = 0U;
      }
      std::uint32_t levels = source.mipLevels - skip;
      if (options.mipLevels != 0U) {
        levels = std::min(levels, options.mipLevels);
      }
      out = Image{};
      out.format = source.format;
      out.width = source.subresources[skip].width;
      out.height = source.subresources[skip].height;
      out.depth = source.subresources[skip].depth;
      out.mipLevels = levels;
      out.arraySize = 1U;
      out.volume = true;
      const std::size_t begin = source.subresources[skip].offset;
      for (std::uint32_t level = 0U; level < levels; ++level) {
        Image::Subresource subresource = source.subresources[skip + level];
        subresource.offset -= begin;
        out.subresources.push_back(subresource);
      }
      const Image::Subresource& last = source.subresources[skip + levels - 1U];
      const std::size_t end = last.offset + static_cast<std::size_t>(last.slicePitch) * last.depth;
      out.bytes.assign(source.bytes.begin() + static_cast<std::ptrdiff_t>(begin), source.bytes.begin() + static_cast<std::ptrdiff_t>(end));
      return S_OK;
    }

    /** `source` (every level the file has) shaped as D3DX11 loads it with `options`. */
    HRESULT Shape(const Image& source, const Rgba8Image* const decodedTop, const DecodeOptions& options, Image& out)
    {
      if (source.volume) {
        return ShapeVolume(source, options, out);
      }
      std::uint32_t skip = options.skipMipLevels;
      if (skip >= source.mipLevels) {
        skip = 0U;
      }
      const std::uint32_t sourceLevels = source.mipLevels - skip;
      const std::uint32_t fileWidth = source.subresources[skip].width;
      const std::uint32_t fileHeight = source.subresources[skip].height;
      const DXGI_FORMAT format = options.format != DXGI_FORMAT_UNKNOWN ? options.format : source.format;
      if (dxgi::FormatElementBytes(format) == 0U) {
        return E_INVALIDARG;
      }
      const bool blocks = dxgi::IsBlockCompressed(format);
      std::uint32_t width = options.width != 0U ? options.width : fileWidth;
      std::uint32_t height = options.height != 0U ? options.height : fileHeight;
      if (blocks) {
        width = RoundUp4(width);
        height = RoundUp4(height);
      }
      const bool stretch = options.filter != ResizeFilter::None;
      const bool sameShape = width == fileWidth && height == fileHeight;
      const std::uint32_t fullChain = FullChain(width, height);
      std::uint32_t levels = options.mipLevels != 0U ? std::min(options.mipLevels, fullChain) : fullChain;
      const std::uint32_t faces = source.arraySize;
      if (format == source.format && sameShape && !CanDecode(format)) {
        levels = std::min(levels, sourceLevels); // no texels to build more from (BC6H/BC7, ...): the file's levels
      }

      // Anything other than copying the file's levels needs texels.
      const bool copiesOnly = format == source.format && sameShape && levels <= sourceLevels;
      if (!copiesOnly && (!CanDecode(source.format) || !CanEncode(format))) {
        return E_NOTIMPL;
      }

      out = Image{};
      out.Allocate(format, width, height, levels, faces);
      out.cube = source.cube;
      out.volume = false;

      for (std::uint32_t face = 0U; face < faces; ++face) {
        const auto sourceIndex = [&](const std::uint32_t level) { return face * source.mipLevels + skip + level; };
        // The top level as the target holds it, before encoding.
        const auto topPixels = [&](Rgba8Image& pixels) {
          if (face == 0U && skip == 0U && decodedTop != nullptr) {
            pixels = *decodedTop;
          } else if (!DecodeSubresource(source, sourceIndex(0U), pixels)) {
            return false;
          }
          if (pixels.width != width || pixels.height != height) {
            pixels = stretch ? ResizeArea(pixels, width, height) : PlaceUnscaled(pixels, width, height);
          }
          return true;
        };

        Rgba8Image previous;
        bool previousValid = false;
        for (std::uint32_t level = 0U; level < levels; ++level) {
          const std::uint32_t targetIndex = face * levels + level;
          const Image::Subresource& target = out.subresources[targetIndex];
          const bool fileHasLevel = sameShape && level < sourceLevels;
          if (fileHasLevel && format == source.format) {
            const Image::Subresource& from = source.subresources[sourceIndex(level)];
            std::memcpy(out.bytes.data() + target.offset, source.bytes.data() + from.offset, target.slicePitch);
            previousValid = false;
            continue;
          }
          if (fileHasLevel && Transcodable(source.format, format)) {
            TranscodeLevel(source, sourceIndex(level), target.width, target.height, out, targetIndex);
            previousValid = false;
            continue;
          }
          if (fileHasLevel) {
            if (!DecodeSubresource(source, sourceIndex(level), previous) || !EncodeSubresource(previous, out, targetIndex)) {
              return E_FAIL;
            }
            previousValid = true;
            continue;
          }
          if (level == 0U) {
            if (!stretch && Transcodable(source.format, format)) {
              TranscodeLevel(source, sourceIndex(0U), fileWidth, fileHeight, out, targetIndex);
              previousValid = false;
              continue;
            }
            if (!topPixels(previous) || !EncodeSubresource(previous, out, targetIndex)) {
              return E_FAIL;
            }
            previousValid = true;
            continue;
          }
          // A level the file lacks: the box filter of the one above, before that one's encoding.
          if (!previousValid) {
            const bool aboveFromFile = sameShape && level - 1U < sourceLevels;
            if (aboveFromFile ? !DecodeSubresource(source, sourceIndex(level - 1U), previous) : !topPixels(previous)) {
              return E_FAIL;
            }
            previousValid = true;
          }
          previous = NextMipLevel(previous);
          if (!EncodeSubresource(previous, out, targetIndex)) {
            return E_FAIL;
          }
        }
      }
      return S_OK;
    }
  } // namespace

  HRESULT DecodeTexture(const void* const data, const std::size_t bytes, const DecodeOptions& options, Image& outImage)
  {
    if (data == nullptr || bytes == 0U) {
      return E_INVALIDARG;
    }
    Image source;
    Rgba8Image decoded;
    const Rgba8Image* decodedTop = nullptr;
    if (IsDds(data, bytes)) {
      if (!ReadDds(data, bytes, source)) {
        return E_FAIL;
      }
    } else {
      if (!DecodeImageFile(data, bytes, decoded)) {
        return E_FAIL;
      }
      source.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, decoded.width, decoded.height, 1U, 1U);
      if (!EncodeFromRgba8(decoded, source.format, source.bytes.data(), source.subresources.front().rowPitch)) {
        return E_FAIL;
      }
      decodedTop = &decoded;
    }
    return Shape(source, decodedTop, options, outImage);
  }

  HRESULT ReadTextureSize(const void* const data, const std::size_t bytes, std::uint32_t& outWidth, std::uint32_t& outHeight)
  {
    if (data == nullptr || bytes == 0U) {
      return E_INVALIDARG;
    }
    if (IsDds(data, bytes)) {
      DdsInfo info;
      if (!ReadDdsInfo(data, bytes, info)) {
        return E_FAIL;
      }
      outWidth = info.width;
      outHeight = info.height;
      return S_OK;
    }
    return ReadImageFileSize(data, bytes, outWidth, outHeight) ? S_OK : E_FAIL;
  }

  HRESULT EncodeTexture(const Image& image, const FileFormat format, std::vector<std::uint8_t>& outBytes)
  {
    outBytes.clear();
    if (image.subresources.empty()) {
      return E_INVALIDARG;
    }
    if (format == FileFormat::Dds) {
      return WriteDds(image, outBytes) ? S_OK : E_FAIL;
    }
    Rgba8Image pixels;
    if (!DecodeSubresource(image, 0U, pixels)) {
      return E_NOTIMPL;
    }
    EncodedFormat encoded = EncodedFormat::Png;
    switch (format) {
    case FileFormat::Bmp:
      encoded = EncodedFormat::Bmp;
      break;
    case FileFormat::Jpg:
      encoded = EncodedFormat::Jpeg;
      break;
    case FileFormat::Png:
      encoded = EncodedFormat::Png;
      break;
    default:
      return E_NOTIMPL;
    }
    return EncodeImageFile(pixels, encoded, outBytes) ? S_OK : E_FAIL;
  }
} // namespace faf_posix::gal::image
