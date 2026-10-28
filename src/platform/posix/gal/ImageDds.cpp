// POSIX texture codec: DDS files. See ImageDds.h.

#include "ImageDds.h"

#include <algorithm>
#include <cstring>

#include "gpg/gal/backends/dxgi/DxgiFormats.h"

namespace faf_posix::gal::image
{
  using gpg::gal::imaging::Image;
  namespace dxgi = gpg::gal::dxgi;

  namespace
  {
    constexpr std::uint32_t FourCc(const char a, const char b, const char c, const char d) noexcept
    {
      return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
    }

    [[nodiscard]] std::uint32_t ReadU32(const std::uint8_t* const at) noexcept
    {
      std::uint32_t value = 0U;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }

    void WriteU32(std::vector<std::uint8_t>& out, const std::size_t at, const std::uint32_t value)
    {
      std::memcpy(out.data() + at, &value, sizeof(value));
    }

    constexpr std::size_t kHeaderBytes = 4U + 124U;
    constexpr std::size_t kDx10Bytes = 20U;

    // DDS_HEADER.dwFlags
    constexpr std::uint32_t kCaps = 0x1U;
    constexpr std::uint32_t kHeight = 0x2U;
    constexpr std::uint32_t kWidth = 0x4U;
    constexpr std::uint32_t kPitch = 0x8U;
    constexpr std::uint32_t kPixelFormat = 0x1000U;
    constexpr std::uint32_t kMipMapCount = 0x20000U;
    constexpr std::uint32_t kLinearSize = 0x80000U;
    constexpr std::uint32_t kDepth = 0x800000U;
    // DDS_PIXELFORMAT.dwFlags
    constexpr std::uint32_t kAlphaPixels = 0x1U;
    constexpr std::uint32_t kAlpha = 0x2U;
    constexpr std::uint32_t kFourCcFlag = 0x4U;
    constexpr std::uint32_t kRgb = 0x40U;
    constexpr std::uint32_t kLuminance = 0x20000U;
    // caps
    constexpr std::uint32_t kComplex = 0x8U;
    constexpr std::uint32_t kTexture = 0x1000U;
    constexpr std::uint32_t kMipMap = 0x400000U;
    constexpr std::uint32_t kCubemap = 0x200U;
    constexpr std::uint32_t kAllFaces = 0xFC00U;
    constexpr std::uint32_t kVolume = 0x200000U;

    enum class Conversion
    {
      None,
      ForceAlpha32, // 32-bit texels whose fourth byte is unused: set it to 255
      Bgr24,        // bytes B, G, R
      Rgb24,        // bytes R, G, B
      A1R5G5B5,
      X1R5G5B5,
      R5G6B5,
      A4R4G4B4,
      X4R4G4B4,
    };

    struct Source
    {
      DdsInfo info;
      Conversion conversion = Conversion::None;
      std::uint32_t fileTexelBytes = 0; // for conversions
      std::size_t dataOffset = kHeaderBytes;
    };

    [[nodiscard]] bool Masks(
      const std::uint8_t* const pf, const std::uint32_t r, const std::uint32_t g, const std::uint32_t b, const std::uint32_t a
    ) noexcept
    {
      return ReadU32(pf + 16) == r && ReadU32(pf + 20) == g && ReadU32(pf + 24) == b && ReadU32(pf + 28) == a;
    }

    /** DDS_PIXELFORMAT (file offset 76) -> format and conversion. */
    [[nodiscard]] bool ClassifyLegacy(const std::uint8_t* const pf, Source& source) noexcept
    {
      // DDS_PIXELFORMAT: dwSize(0) dwFlags(4) dwFourCC(8) dwRGBBitCount(12) R(16) G(20) B(24) A(28) masks.
      const std::uint32_t flags = ReadU32(pf + 4);
      const std::uint32_t fourCc = ReadU32(pf + 8);
      const std::uint32_t rgbBits = ReadU32(pf + 12);
      const std::uint8_t* const masks = pf;
      DXGI_FORMAT& format = source.info.format;
      if ((flags & kFourCcFlag) != 0U) {
        switch (fourCc) {
        case FourCc('D', 'X', 'T', '1'):
          format = DXGI_FORMAT_BC1_UNORM;
          return true;
        case FourCc('D', 'X', 'T', '2'):
        case FourCc('D', 'X', 'T', '3'):
          format = DXGI_FORMAT_BC2_UNORM;
          return true;
        case FourCc('D', 'X', 'T', '4'):
        case FourCc('D', 'X', 'T', '5'):
          format = DXGI_FORMAT_BC3_UNORM;
          return true;
        case FourCc('A', 'T', 'I', '1'):
        case FourCc('B', 'C', '4', 'U'):
          format = DXGI_FORMAT_BC4_UNORM;
          return true;
        case FourCc('B', 'C', '4', 'S'):
          format = DXGI_FORMAT_BC4_SNORM;
          return true;
        case FourCc('A', 'T', 'I', '2'):
        case FourCc('B', 'C', '5', 'U'):
          format = DXGI_FORMAT_BC5_UNORM;
          return true;
        case FourCc('B', 'C', '5', 'S'):
          format = DXGI_FORMAT_BC5_SNORM;
          return true;
        case 36U: // D3DFMT_A16B16G16R16
          format = DXGI_FORMAT_R16G16B16A16_UNORM;
          return true;
        case 110U: // D3DFMT_Q16W16V16U16
          format = DXGI_FORMAT_R16G16B16A16_SNORM;
          return true;
        case 111U: // D3DFMT_R16F
          format = DXGI_FORMAT_R16_FLOAT;
          return true;
        case 112U: // D3DFMT_G16R16F
          format = DXGI_FORMAT_R16G16_FLOAT;
          return true;
        case 113U: // D3DFMT_A16B16G16R16F
          format = DXGI_FORMAT_R16G16B16A16_FLOAT;
          return true;
        case 114U: // D3DFMT_R32F
          format = DXGI_FORMAT_R32_FLOAT;
          return true;
        case 115U: // D3DFMT_G32R32F
          format = DXGI_FORMAT_R32G32_FLOAT;
          return true;
        case 116U: // D3DFMT_A32B32G32R32F
          format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          return true;
        default:
          return false;
        }
      }
      const bool alpha = (flags & kAlphaPixels) != 0U;
      if ((flags & kRgb) != 0U) {
        switch (rgbBits) {
        case 32U:
          if (Masks(masks, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0xFF000000U) && alpha) {
            format = DXGI_FORMAT_B8G8R8A8_UNORM;
            return true;
          }
          if (Masks(masks, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0xFF000000U) || Masks(masks, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0U)) {
            format = DXGI_FORMAT_B8G8R8X8_UNORM;
            return true;
          }
          if (Masks(masks, 0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0xFF000000U) && alpha) {
            format = DXGI_FORMAT_R8G8B8A8_UNORM;
            return true;
          }
          if (Masks(masks, 0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0xFF000000U) || Masks(masks, 0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0U)) {
            format = DXGI_FORMAT_R8G8B8A8_UNORM;
            source.conversion = Conversion::ForceAlpha32;
            source.fileTexelBytes = 4U;
            return true;
          }
          // D3DX writes 10:10:10:2 with red and blue swapped; both read as R10G10B10A2.
          if (Masks(masks, 0x000003FFU, 0x000FFC00U, 0x3FF00000U, 0xC0000000U) || Masks(masks, 0x3FF00000U, 0x000FFC00U, 0x000003FFU, 0xC0000000U)) {
            format = DXGI_FORMAT_R10G10B10A2_UNORM;
            return true;
          }
          if (Masks(masks, 0x0000FFFFU, 0xFFFF0000U, 0U, 0U)) {
            format = DXGI_FORMAT_R16G16_UNORM;
            return true;
          }
          if (Masks(masks, 0xFFFFFFFFU, 0U, 0U, 0U)) {
            format = DXGI_FORMAT_R32_FLOAT; // D3DX writes R32F this way
            return true;
          }
          return false;
        case 24U:
          format = DXGI_FORMAT_B8G8R8A8_UNORM;
          source.fileTexelBytes = 3U;
          if (Masks(masks, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0U)) {
            source.conversion = Conversion::Bgr24;
            return true;
          }
          if (Masks(masks, 0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0U)) {
            source.conversion = Conversion::Rgb24;
            return true;
          }
          return false;
        case 16U:
          format = DXGI_FORMAT_B8G8R8A8_UNORM;
          source.fileTexelBytes = 2U;
          if (Masks(masks, 0x7C00U, 0x03E0U, 0x001FU, 0x8000U)) {
            source.conversion = alpha ? Conversion::A1R5G5B5 : Conversion::X1R5G5B5;
            return true;
          }
          if (Masks(masks, 0x7C00U, 0x03E0U, 0x001FU, 0U)) {
            source.conversion = Conversion::X1R5G5B5;
            return true;
          }
          if (Masks(masks, 0xF800U, 0x07E0U, 0x001FU, 0U)) {
            source.conversion = Conversion::R5G6B5;
            return true;
          }
          if (Masks(masks, 0x0F00U, 0x00F0U, 0x000FU, 0xF000U)) {
            source.conversion = alpha ? Conversion::A4R4G4B4 : Conversion::X4R4G4B4;
            return true;
          }
          if (Masks(masks, 0x0F00U, 0x00F0U, 0x000FU, 0U)) {
            source.conversion = Conversion::X4R4G4B4;
            return true;
          }
          if (Masks(masks, 0x00FFU, 0U, 0U, 0xFF00U)) { // A8L8 written as RGB
            format = DXGI_FORMAT_R8G8_UNORM;
            source.fileTexelBytes = 0U;
            return true;
          }
          if (Masks(masks, 0xFFFFU, 0U, 0U, 0U)) {
            format = DXGI_FORMAT_R16_UNORM;
            source.fileTexelBytes = 0U;
            return true;
          }
          return false;
        case 8U:
          if (Masks(masks, 0xFFU, 0U, 0U, 0U)) {
            format = DXGI_FORMAT_R8_UNORM;
            return true;
          }
          return false;
        default:
          return false;
        }
      }
      if ((flags & kLuminance) != 0U) {
        if (rgbBits == 8U && Masks(masks, 0xFFU, 0U, 0U, 0U)) {
          format = DXGI_FORMAT_R8_UNORM;
          return true;
        }
        if (rgbBits == 16U && Masks(masks, 0x00FFU, 0U, 0U, 0xFF00U)) {
          format = DXGI_FORMAT_R8G8_UNORM;
          return true;
        }
        if (rgbBits == 16U && Masks(masks, 0xFFFFU, 0U, 0U, 0U)) {
          format = DXGI_FORMAT_R16_UNORM;
          return true;
        }
        return false;
      }
      if ((flags & kAlpha) != 0U && rgbBits == 8U) {
        format = DXGI_FORMAT_A8_UNORM;
        return true;
      }
      return false;
    }

    [[nodiscard]] bool ReadSource(const std::uint8_t* const data, const std::size_t bytes, Source& source) noexcept
    {
      // dwSize and the dwFlags validity bits are not checked: FAF's
      // textures/ui/common/hotstats/panel_bmp.dds says 24 and sets only DDSD_LINEARSIZE, and
      // D3DX loads it.
      if (!IsDds(data, bytes)) {
        return false;
      }
      const std::uint32_t flags = ReadU32(data + 8);
      DdsInfo& info = source.info;
      info.height = ReadU32(data + 12);
      info.width = ReadU32(data + 16);
      info.depth = std::max(ReadU32(data + 24), 1U);
      info.mipLevels = (flags & kMipMapCount) != 0U ? std::max(ReadU32(data + 28), 1U) : 1U;
      const std::uint8_t* const pf = data + 76;
      const std::uint32_t caps2 = ReadU32(data + 112);
      if (info.width == 0U || info.height == 0U || info.width > 16384U || info.height > 16384U || info.mipLevels > 32U) {
        return false;
      }
      info.cube = false;
      info.volume = false;
      info.arraySize = 1U;
      if ((ReadU32(pf + 4) & kFourCcFlag) != 0U && ReadU32(pf + 8) == FourCc('D', 'X', '1', '0')) {
        if (bytes < kHeaderBytes + kDx10Bytes) {
          return false;
        }
        const std::uint8_t* const dx10 = data + kHeaderBytes;
        info.format = static_cast<DXGI_FORMAT>(ReadU32(dx10));
        const std::uint32_t dimension = ReadU32(dx10 + 4);
        const std::uint32_t misc = ReadU32(dx10 + 8);
        info.arraySize = std::max(ReadU32(dx10 + 12), 1U);
        source.dataOffset = kHeaderBytes + kDx10Bytes;
        if (dxgi::FormatElementBytes(info.format) == 0U) {
          return false;
        }
        if (dimension == 4U) { // D3D10_RESOURCE_DIMENSION_TEXTURE3D
          info.volume = true;
          info.arraySize = 1U;
        } else if (dimension == 3U || dimension == 2U) {
          info.depth = 1U;
          if ((misc & 0x4U) != 0U) { // D3D10_RESOURCE_MISC_TEXTURECUBE
            info.cube = true;
            info.arraySize *= 6U;
          }
        } else {
          return false;
        }
        if (info.arraySize > 6U * 64U) {
          return false;
        }
        return true;
      }
      if (!ClassifyLegacy(pf, source)) {
        return false;
      }
      if ((caps2 & kCubemap) != 0U) {
        if ((caps2 & kAllFaces) != kAllFaces) {
          return false; // Direct3D 10+ has no partial cubes
        }
        info.cube = true;
        info.arraySize = 6U;
        info.depth = 1U;
      } else if ((caps2 & kVolume) != 0U && (flags & kDepth) != 0U) {
        info.volume = true;
      } else {
        info.depth = 1U;
      }
      return true;
    }

    /** Bytes one level takes in the file (one depth slice). */
    [[nodiscard]] std::size_t FileLevelBytes(const Source& source, const std::uint32_t width, const std::uint32_t height) noexcept
    {
      if (source.conversion != Conversion::None) {
        return static_cast<std::size_t>(width) * height * source.fileTexelBytes;
      }
      return dxgi::ComputeLevelLayout(source.info.format, width, height).ByteSize();
    }

    void ConvertLevel(const Source& source, const std::uint8_t* in, const std::uint32_t width, const std::uint32_t height, std::uint8_t* out) noexcept
    {
      const std::size_t texels = static_cast<std::size_t>(width) * height;
      const auto expand5 = [](const std::uint32_t value) { return static_cast<std::uint8_t>((value << 3U) | (value >> 2U)); };
      const auto expand6 = [](const std::uint32_t value) { return static_cast<std::uint8_t>((value << 2U) | (value >> 4U)); };
      for (std::size_t texel = 0U; texel < texels; ++texel, out += 4) {
        switch (source.conversion) {
        case Conversion::ForceAlpha32:
          std::memcpy(out, in, 3);
          out[3] = 255U;
          in += 4;
          break;
        case Conversion::Bgr24:
          out[0] = in[0];
          out[1] = in[1];
          out[2] = in[2];
          out[3] = 255U;
          in += 3;
          break;
        case Conversion::Rgb24:
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
          out[3] = 255U;
          in += 3;
          break;
        case Conversion::A1R5G5B5:
        case Conversion::X1R5G5B5: {
          const std::uint32_t v = static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U);
          out[0] = expand5(v & 31U);
          out[1] = expand5((v >> 5U) & 31U);
          out[2] = expand5((v >> 10U) & 31U);
          out[3] = (source.conversion == Conversion::X1R5G5B5 || (v & 0x8000U) != 0U) ? 255U : 0U;
          in += 2;
          break;
        }
        case Conversion::R5G6B5: {
          const std::uint32_t v = static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U);
          out[0] = expand5(v & 31U);
          out[1] = expand6((v >> 5U) & 63U);
          out[2] = expand5((v >> 11U) & 31U);
          out[3] = 255U;
          in += 2;
          break;
        }
        case Conversion::A4R4G4B4:
        case Conversion::X4R4G4B4: {
          const std::uint32_t v = static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U);
          out[0] = static_cast<std::uint8_t>((v & 15U) * 17U);
          out[1] = static_cast<std::uint8_t>(((v >> 4U) & 15U) * 17U);
          out[2] = static_cast<std::uint8_t>(((v >> 8U) & 15U) * 17U);
          out[3] = source.conversion == Conversion::X4R4G4B4 ? 255U : static_cast<std::uint8_t>(((v >> 12U) & 15U) * 17U);
          in += 2;
          break;
        }
        default:
          break;
        }
      }
    }

    /** Legacy pixel format for a DXGI format; false when only a DX10 header describes it. */
    [[nodiscard]] bool LegacyPixelFormat(const DXGI_FORMAT format, std::uint32_t (&pf)[8]) noexcept
    {
      std::fill(std::begin(pf), std::end(pf), 0U);
      pf[0] = 32U;
      const auto fourCc = [&pf](const std::uint32_t code) {
        pf[1] = kFourCcFlag;
        pf[2] = code;
      };
      const auto rgb = [&pf](const std::uint32_t flags, const std::uint32_t bits, const std::uint32_t r, const std::uint32_t g,
                             const std::uint32_t b, const std::uint32_t a) {
        pf[1] = flags;
        pf[3] = bits;
        pf[4] = r;
        pf[5] = g;
        pf[6] = b;
        pf[7] = a;
      };
      switch (format) {
      case DXGI_FORMAT_BC1_UNORM:
        fourCc(FourCc('D', 'X', 'T', '1'));
        return true;
      case DXGI_FORMAT_BC2_UNORM:
        fourCc(FourCc('D', 'X', 'T', '3'));
        return true;
      case DXGI_FORMAT_BC3_UNORM:
        fourCc(FourCc('D', 'X', 'T', '5'));
        return true;
      case DXGI_FORMAT_B8G8R8A8_UNORM:
        rgb(kRgb | kAlphaPixels, 32U, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0xFF000000U);
        return true;
      case DXGI_FORMAT_B8G8R8X8_UNORM:
        rgb(kRgb, 32U, 0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0U);
        return true;
      case DXGI_FORMAT_R8G8B8A8_UNORM:
        rgb(kRgb | kAlphaPixels, 32U, 0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0xFF000000U);
        return true;
      case DXGI_FORMAT_R10G10B10A2_UNORM:
        rgb(kRgb | kAlphaPixels, 32U, 0x000003FFU, 0x000FFC00U, 0x3FF00000U, 0xC0000000U);
        return true;
      case DXGI_FORMAT_R16G16_UNORM:
        rgb(kRgb, 32U, 0x0000FFFFU, 0xFFFF0000U, 0U, 0U);
        return true;
      case DXGI_FORMAT_A8_UNORM:
        rgb(kAlpha, 8U, 0U, 0U, 0U, 0xFFU);
        return true;
      case DXGI_FORMAT_R8_UNORM:
        rgb(kLuminance, 8U, 0xFFU, 0U, 0U, 0U);
        return true;
      case DXGI_FORMAT_R16_UNORM:
        rgb(kLuminance, 16U, 0xFFFFU, 0U, 0U, 0U);
        return true;
      case DXGI_FORMAT_R16G16B16A16_UNORM:
        fourCc(36U);
        return true;
      case DXGI_FORMAT_R16_FLOAT:
        fourCc(111U);
        return true;
      case DXGI_FORMAT_R16G16_FLOAT:
        fourCc(112U);
        return true;
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
        fourCc(113U);
        return true;
      case DXGI_FORMAT_R32_FLOAT:
        fourCc(114U);
        return true;
      case DXGI_FORMAT_R32G32_FLOAT:
        fourCc(115U);
        return true;
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
        fourCc(116U);
        return true;
      default:
        return false;
      }
    }
  } // namespace

  bool IsDds(const void* const data, const std::size_t bytes) noexcept
  {
    const auto* const at = static_cast<const std::uint8_t*>(data);
    return bytes >= kHeaderBytes && ReadU32(at) == FourCc('D', 'D', 'S', ' ');
  }

  bool ReadDdsInfo(const void* const data, const std::size_t bytes, DdsInfo& outInfo) noexcept
  {
    Source source;
    if (!ReadSource(static_cast<const std::uint8_t*>(data), bytes, source)) {
      return false;
    }
    outInfo = source.info;
    return true;
  }

  bool ReadDds(const void* const data, const std::size_t bytes, Image& outImage)
  {
    const auto* const file = static_cast<const std::uint8_t*>(data);
    Source source;
    if (!ReadSource(file, bytes, source)) {
      return false;
    }
    const DdsInfo& info = source.info;
    // Where each level of one array slice starts in the file.
    std::vector<std::size_t> levelOffsets(info.mipLevels);
    std::size_t sliceBytes = 0U;
    for (std::uint32_t mip = 0U; mip < info.mipLevels; ++mip) {
      levelOffsets[mip] = sliceBytes;
      const std::uint32_t depth = info.volume ? std::max(1U, info.depth >> mip) : 1U;
      sliceBytes += FileLevelBytes(source, std::max(1U, info.width >> mip), std::max(1U, info.height >> mip)) * depth;
    }
    const std::size_t available = bytes - source.dataOffset;
    std::uint32_t levels = info.mipLevels;
    if (available < sliceBytes * info.arraySize) {
      if (info.arraySize != 1U || info.volume) {
        return false;
      }
      // A 2D file cut short: keep the complete levels.
      levels = 0U;
      while (levels < info.mipLevels &&
             levelOffsets[levels] + FileLevelBytes(source, std::max(1U, info.width >> levels), std::max(1U, info.height >> levels)) <= available) {
        ++levels;
      }
      if (levels == 0U) {
        return false;
      }
    }
    // A chain longer than the image allows (some exporters repeat 1x1 levels) keeps the real ones.
    std::uint32_t fullChain = 1U;
    for (std::uint32_t side = std::max({info.width, info.height, info.volume ? info.depth : 1U}); side > 1U; side >>= 1U) {
      ++fullChain;
    }
    levels = std::min(levels, fullChain);

    if (info.volume) {
      outImage = Image{};
      outImage.format = info.format;
      outImage.width = info.width;
      outImage.height = info.height;
      outImage.depth = info.depth;
      outImage.mipLevels = levels;
      outImage.arraySize = 1U;
      outImage.volume = true;
      std::size_t offset = 0U;
      for (std::uint32_t mip = 0U; mip < levels; ++mip) {
        Image::Subresource subresource;
        subresource.offset = offset;
        subresource.width = std::max(1U, info.width >> mip);
        subresource.height = std::max(1U, info.height >> mip);
        subresource.depth = std::max(1U, info.depth >> mip);
        const dxgi::LevelLayout layout = dxgi::ComputeLevelLayout(info.format, subresource.width, subresource.height);
        subresource.rowPitch = layout.rowPitch;
        subresource.rowCount = layout.rowCount;
        subresource.slicePitch = layout.ByteSize();
        offset += static_cast<std::size_t>(subresource.slicePitch) * subresource.depth;
        outImage.subresources.push_back(subresource);
      }
      outImage.bytes.assign(offset, 0U);
      for (std::uint32_t mip = 0U; mip < levels; ++mip) {
        const Image::Subresource& subresource = outImage.subresources[mip];
        const std::uint8_t* in = file + source.dataOffset + levelOffsets[mip];
        for (std::uint32_t slice = 0U; slice < subresource.depth; ++slice) {
          std::uint8_t* const out = outImage.bytes.data() + subresource.offset + static_cast<std::size_t>(slice) * subresource.slicePitch;
          if (source.conversion == Conversion::None) {
            std::memcpy(out, in, subresource.slicePitch);
          } else {
            ConvertLevel(source, in, subresource.width, subresource.height, out);
          }
          in += FileLevelBytes(source, subresource.width, subresource.height);
        }
      }
      return true;
    }

    outImage = Image{};
    outImage.Allocate(info.format, info.width, info.height, levels, info.arraySize);
    outImage.cube = info.cube;
    outImage.volume = false;
    for (std::uint32_t slice = 0U; slice < info.arraySize; ++slice) {
      for (std::uint32_t mip = 0U; mip < levels; ++mip) {
        const Image::Subresource& subresource = outImage.subresources[slice * levels + mip];
        const std::uint8_t* const in = file + source.dataOffset + slice * sliceBytes + levelOffsets[mip];
        std::uint8_t* const out = outImage.bytes.data() + subresource.offset;
        if (source.conversion == Conversion::None) {
          std::memcpy(out, in, subresource.slicePitch);
        } else {
          ConvertLevel(source, in, subresource.width, subresource.height, out);
        }
      }
    }
    return true;
  }

  bool WriteDds(const Image& image, std::vector<std::uint8_t>& outBytes)
  {
    outBytes.clear();
    if (image.subresources.empty() || dxgi::FormatElementBytes(image.format) == 0U) {
      return false;
    }
    std::uint32_t pf[8];
    const bool legacy = LegacyPixelFormat(image.format, pf) && (image.arraySize == 1U || (image.cube && image.arraySize == 6U));
    const std::size_t headerBytes = kHeaderBytes + (legacy ? 0U : kDx10Bytes);
    std::size_t payload = 0U;
    for (const Image::Subresource& subresource : image.subresources) {
      payload += static_cast<std::size_t>(subresource.slicePitch) * std::max(1U, subresource.depth);
    }
    outBytes.assign(headerBytes + payload, 0U);
    WriteU32(outBytes, 0, FourCc('D', 'D', 'S', ' '));
    WriteU32(outBytes, 4, 124U);
    const bool compressed = dxgi::IsBlockCompressed(image.format);
    std::uint32_t flags = kCaps | kHeight | kWidth | kPixelFormat | (compressed ? kLinearSize : kPitch);
    if (image.mipLevels > 1U) {
      flags |= kMipMapCount;
    }
    if (image.volume) {
      flags |= kDepth;
    }
    WriteU32(outBytes, 8, flags);
    WriteU32(outBytes, 12, image.height);
    WriteU32(outBytes, 16, image.width);
    const dxgi::LevelLayout top = dxgi::ComputeLevelLayout(image.format, image.width, image.height);
    WriteU32(outBytes, 20, compressed ? top.ByteSize() : top.rowPitch);
    WriteU32(outBytes, 24, image.volume ? image.depth : 0U);
    WriteU32(outBytes, 28, image.mipLevels);
    if (legacy) {
      for (int index = 0; index < 8; ++index) {
        WriteU32(outBytes, 76U + static_cast<std::size_t>(index) * 4U, pf[index]);
      }
    } else {
      WriteU32(outBytes, 76, 32U);
      WriteU32(outBytes, 80, kFourCcFlag);
      WriteU32(outBytes, 84, FourCc('D', 'X', '1', '0'));
    }
    std::uint32_t caps = kTexture;
    if (image.mipLevels > 1U) {
      caps |= kComplex | kMipMap;
    }
    if (image.cube || image.volume) {
      caps |= kComplex;
    }
    WriteU32(outBytes, 108, caps);
    WriteU32(outBytes, 112, image.cube ? (kCubemap | kAllFaces) : (image.volume ? kVolume : 0U));
    if (!legacy) {
      WriteU32(outBytes, kHeaderBytes, static_cast<std::uint32_t>(image.format));
      WriteU32(outBytes, kHeaderBytes + 4, image.volume ? 4U : 3U);
      WriteU32(outBytes, kHeaderBytes + 8, image.cube ? 0x4U : 0U);
      WriteU32(outBytes, kHeaderBytes + 12, image.cube ? std::max(1U, image.arraySize / 6U) : image.arraySize);
      WriteU32(outBytes, kHeaderBytes + 16, 0U);
    }
    std::size_t at = headerBytes;
    for (const Image::Subresource& subresource : image.subresources) {
      const std::size_t size = static_cast<std::size_t>(subresource.slicePitch) * std::max(1U, subresource.depth);
      if (subresource.offset + size > image.bytes.size()) {
        outBytes.clear();
        return false;
      }
      std::memcpy(outBytes.data() + at, image.bytes.data() + subresource.offset, size);
      at += size;
    }
    return true;
  }
} // namespace faf_posix::gal::image
