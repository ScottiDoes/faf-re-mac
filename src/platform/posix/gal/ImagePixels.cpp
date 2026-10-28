// POSIX texture codec: pixel conversion. See ImagePixels.h.

#include "ImagePixels.h"

#include <algorithm>
#include <cstring>

#include "ImageBc.h"

namespace faf_posix::gal::image
{
  namespace
  {
    enum class Layout
    {
      None,
      Rgba8,
      Bgra8,
      Bgrx8,
      B5G6R5,
      B5G5R5A1,
      B4G4R4A4,
      R10G10B10A2,
      A8,
      R8,
      R8G8,
      R16,
      R16G16,
      R16G16B16A16,
      R16F,
      R16G16F,
      R16G16B16A16F,
      R32F,
      R32G32F,
      R32G32B32A32F,
      Block,
    };

    [[nodiscard]] Layout LayoutOf(const DXGI_FORMAT format) noexcept
    {
      switch (format) {
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return Layout::Rgba8;
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return Layout::Bgra8;
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return Layout::Bgrx8;
      case DXGI_FORMAT_B5G6R5_UNORM:
        return Layout::B5G6R5;
      case DXGI_FORMAT_B5G5R5A1_UNORM:
        return Layout::B5G5R5A1;
      case DXGI_FORMAT_B4G4R4A4_UNORM:
        return Layout::B4G4R4A4;
      case DXGI_FORMAT_R10G10B10A2_UNORM:
        return Layout::R10G10B10A2;
      case DXGI_FORMAT_A8_UNORM:
        return Layout::A8;
      case DXGI_FORMAT_R8_UNORM:
        return Layout::R8;
      case DXGI_FORMAT_R8G8_UNORM:
        return Layout::R8G8;
      case DXGI_FORMAT_R16_UNORM:
        return Layout::R16;
      case DXGI_FORMAT_R16G16_UNORM:
        return Layout::R16G16;
      case DXGI_FORMAT_R16G16B16A16_UNORM:
        return Layout::R16G16B16A16;
      case DXGI_FORMAT_R16_FLOAT:
        return Layout::R16F;
      case DXGI_FORMAT_R16G16_FLOAT:
        return Layout::R16G16F;
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return Layout::R16G16B16A16F;
      case DXGI_FORMAT_R32_FLOAT:
        return Layout::R32F;
      case DXGI_FORMAT_R32G32_FLOAT:
        return Layout::R32G32F;
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return Layout::R32G32B32A32F;
      default:
        return BlockKind(format) != 0 ? Layout::Block : Layout::None;
      }
    }

    [[nodiscard]] inline std::uint32_t Load16(const std::uint8_t* const at) noexcept
    {
      std::uint16_t value = 0U;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }

    [[nodiscard]] inline std::uint32_t Load32(const std::uint8_t* const at) noexcept
    {
      std::uint32_t value = 0U;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }

    [[nodiscard]] inline float LoadFloat(const std::uint8_t* const at) noexcept
    {
      float value = 0.0F;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }

    inline void Store16(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      const auto narrow = static_cast<std::uint16_t>(value);
      std::memcpy(at, &narrow, sizeof(narrow));
    }

    inline void Store32(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      std::memcpy(at, &value, sizeof(value));
    }

    inline void StoreFloat(std::uint8_t* const at, const float value) noexcept
    {
      std::memcpy(at, &value, sizeof(value));
    }

    [[nodiscard]] float HalfToFloat(const std::uint32_t half) noexcept
    {
      const std::uint32_t sign = (half & 0x8000U) << 16U;
      std::uint32_t exponent = (half >> 10U) & 0x1FU;
      std::uint32_t mantissa = half & 0x3FFU;
      std::uint32_t bits = 0U;
      if (exponent == 0U) {
        if (mantissa != 0U) {
          exponent = 127U - 15U + 1U;
          while ((mantissa & 0x400U) == 0U) {
            mantissa <<= 1U;
            --exponent;
          }
          mantissa &= 0x3FFU;
          bits = sign | (exponent << 23U) | (mantissa << 13U);
        } else {
          bits = sign;
        }
      } else if (exponent == 31U) {
        bits = sign | 0x7F800000U | (mantissa << 13U);
      } else {
        bits = sign | ((exponent + 127U - 15U) << 23U) | (mantissa << 13U);
      }
      float value = 0.0F;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }

    /** Round to nearest even; overflow saturates to infinity. */
    [[nodiscard]] std::uint32_t FloatToHalf(const float value) noexcept
    {
      std::uint32_t bits = 0U;
      std::memcpy(&bits, &value, sizeof(bits));
      const std::uint32_t sign = (bits >> 16U) & 0x8000U;
      const std::uint32_t exponent = (bits >> 23U) & 0xFFU;
      std::uint32_t mantissa = bits & 0x7FFFFFU;
      if (exponent == 0xFFU) {
        return sign | 0x7C00U | (mantissa != 0U ? 0x200U : 0U);
      }
      const int unbiased = static_cast<int>(exponent) - 127;
      if (unbiased > 15) {
        return sign | 0x7C00U;
      }
      if (unbiased < -25) {
        return sign;
      }
      std::uint32_t half = 0U;
      int shift = 13;
      if (unbiased < -14) {
        mantissa |= 0x800000U;
        shift = 13 + (-14 - unbiased);
        half = 0U;
      } else {
        half = static_cast<std::uint32_t>(unbiased + 15) << 10U;
      }
      const std::uint32_t truncated = mantissa >> shift;
      const std::uint32_t remainder = mantissa & ((1U << shift) - 1U);
      const std::uint32_t halfway = 1U << (shift - 1);
      std::uint32_t result = half + truncated;
      if (remainder > halfway || (remainder == halfway && (truncated & 1U) != 0U)) {
        ++result;
      }
      return sign | result;
    }

    [[nodiscard]] inline std::uint8_t FromUnit(const float value) noexcept
    {
      if (!(value > 0.0F)) {
        return 0U;
      }
      if (value >= 1.0F) {
        return 255U;
      }
      return static_cast<std::uint8_t>(value * 255.0F + 0.5F);
    }

    [[nodiscard]] inline std::uint8_t From16(const std::uint32_t value) noexcept
    {
      return static_cast<std::uint8_t>((value * 255U + 32767U) / 65535U);
    }

    void DecodePixel(const Layout layout, const std::uint8_t* const in, std::uint8_t* const out) noexcept
    {
      switch (layout) {
      case Layout::Rgba8:
        std::memcpy(out, in, 4);
        break;
      case Layout::Bgra8:
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = in[3];
        break;
      case Layout::Bgrx8:
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = 255U;
        break;
      case Layout::B5G6R5: {
        const std::uint32_t v = Load16(in);
        const std::uint32_t r = (v >> 11U) & 31U;
        const std::uint32_t g = (v >> 5U) & 63U;
        const std::uint32_t b = v & 31U;
        out[0] = static_cast<std::uint8_t>((r << 3U) | (r >> 2U));
        out[1] = static_cast<std::uint8_t>((g << 2U) | (g >> 4U));
        out[2] = static_cast<std::uint8_t>((b << 3U) | (b >> 2U));
        out[3] = 255U;
        break;
      }
      case Layout::B5G5R5A1: {
        const std::uint32_t v = Load16(in);
        const std::uint32_t r = (v >> 10U) & 31U;
        const std::uint32_t g = (v >> 5U) & 31U;
        const std::uint32_t b = v & 31U;
        out[0] = static_cast<std::uint8_t>((r << 3U) | (r >> 2U));
        out[1] = static_cast<std::uint8_t>((g << 3U) | (g >> 2U));
        out[2] = static_cast<std::uint8_t>((b << 3U) | (b >> 2U));
        out[3] = (v & 0x8000U) != 0U ? 255U : 0U;
        break;
      }
      case Layout::B4G4R4A4: {
        const std::uint32_t v = Load16(in);
        out[0] = static_cast<std::uint8_t>(((v >> 8U) & 15U) * 17U);
        out[1] = static_cast<std::uint8_t>(((v >> 4U) & 15U) * 17U);
        out[2] = static_cast<std::uint8_t>((v & 15U) * 17U);
        out[3] = static_cast<std::uint8_t>(((v >> 12U) & 15U) * 17U);
        break;
      }
      case Layout::R10G10B10A2: {
        const std::uint32_t v = Load32(in);
        out[0] = static_cast<std::uint8_t>(((v & 1023U) * 255U + 511U) / 1023U);
        out[1] = static_cast<std::uint8_t>((((v >> 10U) & 1023U) * 255U + 511U) / 1023U);
        out[2] = static_cast<std::uint8_t>((((v >> 20U) & 1023U) * 255U + 511U) / 1023U);
        out[3] = static_cast<std::uint8_t>((v >> 30U) * 85U);
        break;
      }
      case Layout::A8:
        out[0] = 0U;
        out[1] = 0U;
        out[2] = 0U;
        out[3] = in[0];
        break;
      case Layout::R8:
        out[0] = in[0];
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R8G8:
        out[0] = in[0];
        out[1] = in[1];
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R16:
        out[0] = From16(Load16(in));
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R16G16:
        out[0] = From16(Load16(in));
        out[1] = From16(Load16(in + 2));
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R16G16B16A16:
        for (int channel = 0; channel < 4; ++channel) {
          out[channel] = From16(Load16(in + channel * 2));
        }
        break;
      case Layout::R16F:
        out[0] = FromUnit(HalfToFloat(Load16(in)));
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R16G16F:
        out[0] = FromUnit(HalfToFloat(Load16(in)));
        out[1] = FromUnit(HalfToFloat(Load16(in + 2)));
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R16G16B16A16F:
        for (int channel = 0; channel < 4; ++channel) {
          out[channel] = FromUnit(HalfToFloat(Load16(in + channel * 2)));
        }
        break;
      case Layout::R32F:
        out[0] = FromUnit(LoadFloat(in));
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R32G32F:
        out[0] = FromUnit(LoadFloat(in));
        out[1] = FromUnit(LoadFloat(in + 4));
        out[2] = 0U;
        out[3] = 255U;
        break;
      case Layout::R32G32B32A32F:
        for (int channel = 0; channel < 4; ++channel) {
          out[channel] = FromUnit(LoadFloat(in + channel * 4));
        }
        break;
      default:
        std::memset(out, 0, 4);
        break;
      }
    }

    [[nodiscard]] inline std::uint32_t Scale(const std::uint32_t value, const std::uint32_t maximum) noexcept
    {
      return (value * maximum + 127U) / 255U;
    }

    void EncodePixel(const Layout layout, const std::uint8_t* const in, std::uint8_t* const out) noexcept
    {
      switch (layout) {
      case Layout::Rgba8:
        std::memcpy(out, in, 4);
        break;
      case Layout::Bgra8:
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = in[3];
        break;
      case Layout::Bgrx8:
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = 255U;
        break;
      case Layout::B5G6R5:
        Store16(out, (Scale(in[0], 31U) << 11U) | (Scale(in[1], 63U) << 5U) | Scale(in[2], 31U));
        break;
      case Layout::B5G5R5A1:
        Store16(out, (in[3] >= 128U ? 0x8000U : 0U) | (Scale(in[0], 31U) << 10U) | (Scale(in[1], 31U) << 5U) | Scale(in[2], 31U));
        break;
      case Layout::B4G4R4A4:
        Store16(out, (Scale(in[3], 15U) << 12U) | (Scale(in[0], 15U) << 8U) | (Scale(in[1], 15U) << 4U) | Scale(in[2], 15U));
        break;
      case Layout::R10G10B10A2:
        Store32(out, Scale(in[0], 1023U) | (Scale(in[1], 1023U) << 10U) | (Scale(in[2], 1023U) << 20U) | (Scale(in[3], 3U) << 30U));
        break;
      case Layout::A8:
        out[0] = in[3];
        break;
      case Layout::R8:
        out[0] = in[0];
        break;
      case Layout::R8G8:
        out[0] = in[0];
        out[1] = in[1];
        break;
      case Layout::R16:
        Store16(out, in[0] * 257U);
        break;
      case Layout::R16G16:
        Store16(out, in[0] * 257U);
        Store16(out + 2, in[1] * 257U);
        break;
      case Layout::R16G16B16A16:
        for (int channel = 0; channel < 4; ++channel) {
          Store16(out + channel * 2, in[channel] * 257U);
        }
        break;
      case Layout::R16F:
        Store16(out, FloatToHalf(static_cast<float>(in[0]) / 255.0F));
        break;
      case Layout::R16G16F:
        Store16(out, FloatToHalf(static_cast<float>(in[0]) / 255.0F));
        Store16(out + 2, FloatToHalf(static_cast<float>(in[1]) / 255.0F));
        break;
      case Layout::R16G16B16A16F:
        for (int channel = 0; channel < 4; ++channel) {
          Store16(out + channel * 2, FloatToHalf(static_cast<float>(in[channel]) / 255.0F));
        }
        break;
      case Layout::R32F:
        StoreFloat(out, static_cast<float>(in[0]) / 255.0F);
        break;
      case Layout::R32G32F:
        StoreFloat(out, static_cast<float>(in[0]) / 255.0F);
        StoreFloat(out + 4, static_cast<float>(in[1]) / 255.0F);
        break;
      case Layout::R32G32B32A32F:
        for (int channel = 0; channel < 4; ++channel) {
          StoreFloat(out + channel * 4, static_cast<float>(in[channel]) / 255.0F);
        }
        break;
      default:
        break;
      }
    }

    [[nodiscard]] std::uint32_t PixelBytes(const Layout layout) noexcept
    {
      switch (layout) {
      case Layout::A8:
      case Layout::R8:
        return 1U;
      case Layout::B5G6R5:
      case Layout::B5G5R5A1:
      case Layout::B4G4R4A4:
      case Layout::R8G8:
      case Layout::R16:
      case Layout::R16F:
        return 2U;
      case Layout::Rgba8:
      case Layout::Bgra8:
      case Layout::Bgrx8:
      case Layout::R10G10B10A2:
      case Layout::R16G16:
      case Layout::R16G16F:
      case Layout::R32F:
        return 4U;
      case Layout::R16G16B16A16:
      case Layout::R16G16B16A16F:
      case Layout::R32G32F:
        return 8U;
      case Layout::R32G32B32A32F:
        return 16U;
      default:
        return 0U;
      }
    }
  } // namespace

  int BlockKind(const DXGI_FORMAT format) noexcept
  {
    switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
      return 1;
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
      return 2;
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
      return 3;
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
      return 4;
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
      return 5;
    default:
      return 0;
    }
  }

  bool CanDecode(const DXGI_FORMAT format) noexcept
  {
    return LayoutOf(format) != Layout::None;
  }

  bool CanEncode(const DXGI_FORMAT format) noexcept
  {
    return LayoutOf(format) != Layout::None;
  }

  bool DecodeToRgba8(
    const DXGI_FORMAT format, const std::uint8_t* const data, const std::uint32_t rowPitch, const std::uint32_t width,
    const std::uint32_t height, Rgba8Image& out
  )
  {
    const Layout layout = LayoutOf(format);
    if (layout == Layout::None || width == 0U || height == 0U) {
      return false;
    }
    out.Resize(width, height);
    if (layout == Layout::Block) {
      const int kind = BlockKind(format);
      const std::uint32_t blockBytes = (kind == 1 || kind == 4) ? 8U : 16U;
      std::uint8_t pixels[64];
      for (std::uint32_t by = 0U; by < (height + 3U) / 4U; ++by) {
        for (std::uint32_t bx = 0U; bx < (width + 3U) / 4U; ++bx) {
          const std::uint8_t* const block = data + static_cast<std::size_t>(by) * rowPitch + static_cast<std::size_t>(bx) * blockBytes;
          switch (kind) {
          case 1:
            DecodeBc1Block(block, pixels);
            break;
          case 2:
            DecodeBc2Block(block, pixels);
            break;
          case 3:
            DecodeBc3Block(block, pixels);
            break;
          case 4:
            for (int pixel = 0; pixel < 16; ++pixel) {
              pixels[pixel * 4 + 1] = 0U;
              pixels[pixel * 4 + 2] = 0U;
              pixels[pixel * 4 + 3] = 255U;
            }
            DecodeBc4Block(block, pixels, 0);
            break;
          default:
            for (int pixel = 0; pixel < 16; ++pixel) {
              pixels[pixel * 4 + 2] = 0U;
              pixels[pixel * 4 + 3] = 255U;
            }
            DecodeBc4Block(block, pixels, 0);
            DecodeBc4Block(block + 8, pixels, 1);
            break;
          }
          for (std::uint32_t y = 0U; y < 4U && by * 4U + y < height; ++y) {
            const std::uint32_t columns = std::min(4U, width - bx * 4U);
            std::memcpy(out.pixels.data() + (static_cast<std::size_t>(by * 4U + y) * width + bx * 4U) * 4U, pixels + y * 16U, columns * 4U);
          }
        }
      }
      return true;
    }
    const std::uint32_t pixelBytes = PixelBytes(layout);
    for (std::uint32_t y = 0U; y < height; ++y) {
      const std::uint8_t* in = data + static_cast<std::size_t>(y) * rowPitch;
      std::uint8_t* target = out.pixels.data() + static_cast<std::size_t>(y) * width * 4U;
      for (std::uint32_t x = 0U; x < width; ++x, in += pixelBytes, target += 4) {
        DecodePixel(layout, in, target);
      }
    }
    return true;
  }

  bool EncodeFromRgba8(const Rgba8Image& image, const DXGI_FORMAT format, std::uint8_t* const out, const std::uint32_t rowPitch)
  {
    const Layout layout = LayoutOf(format);
    if (layout == Layout::None || image.width == 0U || image.height == 0U) {
      return false;
    }
    if (layout == Layout::Block) {
      const int kind = BlockKind(format);
      const std::uint32_t blockBytes = (kind == 1 || kind == 4) ? 8U : 16U;
      std::uint8_t pixels[64];
      for (std::uint32_t by = 0U; by < (image.height + 3U) / 4U; ++by) {
        for (std::uint32_t bx = 0U; bx < (image.width + 3U) / 4U; ++bx) {
          for (std::uint32_t y = 0U; y < 4U; ++y) {
            const std::uint32_t sy = std::min(by * 4U + y, image.height - 1U);
            for (std::uint32_t x = 0U; x < 4U; ++x) {
              const std::uint32_t sx = std::min(bx * 4U + x, image.width - 1U);
              std::memcpy(pixels + (y * 4U + x) * 4U, image.pixels.data() + (static_cast<std::size_t>(sy) * image.width + sx) * 4U, 4U);
            }
          }
          std::uint8_t* const block = out + static_cast<std::size_t>(by) * rowPitch + static_cast<std::size_t>(bx) * blockBytes;
          switch (kind) {
          case 1:
            EncodeBc1Block(pixels, block, true);
            break;
          case 2:
            EncodeBc2Block(pixels, block);
            break;
          case 3:
            EncodeBc3Block(pixels, block);
            break;
          case 4:
            EncodeBc4Block(pixels, block, 0);
            break;
          default:
            EncodeBc4Block(pixels, block, 0);
            EncodeBc4Block(pixels, block + 8, 1);
            break;
          }
        }
      }
      return true;
    }
    const std::uint32_t pixelBytes = PixelBytes(layout);
    for (std::uint32_t y = 0U; y < image.height; ++y) {
      const std::uint8_t* in = image.pixels.data() + static_cast<std::size_t>(y) * image.width * 4U;
      std::uint8_t* target = out + static_cast<std::size_t>(y) * rowPitch;
      for (std::uint32_t x = 0U; x < image.width; ++x, in += 4, target += pixelBytes) {
        EncodePixel(layout, in, target);
      }
    }
    return true;
  }
} // namespace faf_posix::gal::image
