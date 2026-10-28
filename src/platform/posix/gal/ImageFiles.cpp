// POSIX texture codec: image files through ImageIO. See ImageFiles.h.

#include "ImageFiles.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cstring>

namespace faf_posix::gal::image
{
  namespace
  {
    /** Owns one Core Foundation reference. */
    template <typename T>
    class CfRef
    {
    public:
      CfRef() = default;
      explicit CfRef(T value) noexcept
        : mValue(value)
      {
      }
      ~CfRef()
      {
        if (mValue != nullptr) {
          CFRelease(mValue);
        }
      }
      CfRef(const CfRef&) = delete;
      CfRef& operator=(const CfRef&) = delete;
      void Reset(T value) noexcept
      {
        if (mValue != nullptr) {
          CFRelease(mValue);
        }
        mValue = value;
      }
      [[nodiscard]] T Get() const noexcept { return mValue; }
      explicit operator bool() const noexcept { return mValue != nullptr; }

    private:
      T mValue = nullptr;
    };

    [[nodiscard]] CGImageSourceRef CreateSource(const void* const data, const std::size_t bytes)
    {
      CfRef<CFDataRef> wrapped(
        CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, static_cast<const UInt8*>(data), static_cast<CFIndex>(bytes), kCFAllocatorNull)
      );
      if (!wrapped) {
        return nullptr;
      }
      const void* keys[] = {kCGImageSourceShouldCache};
      const void* values[] = {kCFBooleanFalse};
      CfRef<CFDictionaryRef> options(
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
      );
      CGImageSourceRef source = CGImageSourceCreateWithData(wrapped.Get(), options.Get());
      if (source != nullptr && CGImageSourceGetType(source) != nullptr && CGImageSourceGetCount(source) > 0) {
        return source;
      }
      if (source != nullptr) {
        CFRelease(source);
      }
      // TGA files carry no signature: name the type.
      const void* hintKeys[] = {kCGImageSourceShouldCache, kCGImageSourceTypeIdentifierHint};
      const void* hintValues[] = {kCFBooleanFalse, CFSTR("com.truevision.tga-image")};
      CfRef<CFDictionaryRef> hinted(
        CFDictionaryCreate(kCFAllocatorDefault, hintKeys, hintValues, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
      );
      source = CGImageSourceCreateWithData(wrapped.Get(), hinted.Get());
      if (source != nullptr && CGImageSourceGetCount(source) > 0) {
        return source;
      }
      if (source != nullptr) {
        CFRelease(source);
      }
      return nullptr;
    }

    void Unpremultiply(Rgba8Image& image) noexcept
    {
      for (std::size_t at = 0U; at < image.pixels.size(); at += 4U) {
        const std::uint32_t alpha = image.pixels[at + 3];
        if (alpha == 0U || alpha == 255U) {
          continue;
        }
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t value = (image.pixels[at + channel] * 255U + alpha / 2U) / alpha;
          image.pixels[at + channel] = static_cast<std::uint8_t>(std::min(value, 255U));
        }
      }
    }

    /** Any image ImageIO decodes, drawn into an RGBA context (colour converted, premultiplied, then undone). */
    [[nodiscard]] bool DrawToRgba8(CGImageRef const image, Rgba8Image& out)
    {
      const auto width = static_cast<std::uint32_t>(CGImageGetWidth(image));
      const auto height = static_cast<std::uint32_t>(CGImageGetHeight(image));
      out.Resize(width, height);
      CGColorSpaceRef space = CGImageGetColorSpace(image);
      CfRef<CGColorSpaceRef> fallback;
      if (space == nullptr || CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB) {
        fallback.Reset(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
        space = fallback.Get();
      }
      CfRef<CGContextRef> context(CGBitmapContextCreate(
        out.pixels.data(), width, height, 8, static_cast<std::size_t>(width) * 4U, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big
      ));
      if (!context) {
        return false;
      }
      CGContextSetBlendMode(context.Get(), kCGBlendModeCopy);
      CGContextDrawImage(context.Get(), CGRectMake(0, 0, width, height), image);
      Unpremultiply(out);
      return true;
    }

    /** The image's own samples (no colour conversion), for the layouts ImageIO hands back for PNG/JPEG/BMP/TGA. */
    [[nodiscard]] bool CopySamples(CGImageRef const image, Rgba8Image& out)
    {
      CGColorSpaceRef const space = CGImageGetColorSpace(image);
      if (space == nullptr) {
        return false;
      }
      const CGColorSpaceModel model = CGColorSpaceGetModel(space);
      const std::size_t bitsPerComponent = CGImageGetBitsPerComponent(image);
      const std::size_t bitsPerPixel = CGImageGetBitsPerPixel(image);
      const CGBitmapInfo info = CGImageGetBitmapInfo(image);
      if ((info & kCGBitmapFloatComponents) != 0U || (bitsPerComponent != 8U && bitsPerComponent != 16U)) {
        return false;
      }
      const auto alphaInfo = static_cast<CGImageAlphaInfo>(info & kCGBitmapAlphaInfoMask);
      const CGBitmapInfo byteOrder = info & kCGBitmapByteOrderMask;
      const std::size_t componentBytes = bitsPerComponent / 8U;
      const std::size_t pixelBytes = bitsPerPixel / 8U;
      if (bitsPerPixel % 8U != 0U || pixelBytes == 0U || pixelBytes % componentBytes != 0U) {
        return false;
      }
      const std::size_t components = pixelBytes / componentBytes;

      // Colour channels in the image, and where alpha sits in logical (big-endian) order.
      std::size_t colourChannels = 0U;
      std::vector<std::uint8_t> palette; // RGB per entry
      if (model == kCGColorSpaceModelRGB) {
        colourChannels = 3U;
      } else if (model == kCGColorSpaceModelMonochrome) {
        colourChannels = 1U;
      } else if (model == kCGColorSpaceModelIndexed && bitsPerComponent == 8U) {
        colourChannels = 1U;
        CGColorSpaceRef const base = CGColorSpaceGetBaseColorSpace(space);
        const std::size_t count = CGColorSpaceGetColorTableCount(space);
        if (base == nullptr || CGColorSpaceGetNumberOfComponents(base) != 3U || count == 0U) {
          return false;
        }
        palette.resize(count * 3U);
        CGColorSpaceGetColorTable(space, palette.data());
        palette.resize(256U * 3U, 0U);
      } else {
        return false;
      }
      bool alphaFirst = false;
      bool hasAlpha = false;
      bool premultiplied = false;
      switch (alphaInfo) {
      case kCGImageAlphaNone:
        break;
      case kCGImageAlphaLast:
        hasAlpha = true;
        break;
      case kCGImageAlphaFirst:
        hasAlpha = true;
        alphaFirst = true;
        break;
      case kCGImageAlphaPremultipliedLast:
        hasAlpha = true;
        premultiplied = true;
        break;
      case kCGImageAlphaPremultipliedFirst:
        hasAlpha = true;
        premultiplied = true;
        alphaFirst = true;
        break;
      case kCGImageAlphaNoneSkipLast:
        break;
      case kCGImageAlphaNoneSkipFirst:
        alphaFirst = true;
        break;
      default:
        return false;
      }
      const bool skip = alphaInfo == kCGImageAlphaNoneSkipLast || alphaInfo == kCGImageAlphaNoneSkipFirst;
      if (components != colourChannels + ((hasAlpha || skip) ? 1U : 0U)) {
        return false;
      }
      // Packed little-endian pixels (32Little with 8-bit components) store the logical order reversed.
      const bool reversed = componentBytes == 1U && pixelBytes == 4U && byteOrder == kCGBitmapByteOrder32Little;
      const bool reversed16 = componentBytes == 2U && byteOrder == kCGBitmapByteOrder16Little;
      if ((componentBytes == 1U && byteOrder != kCGBitmapByteOrderDefault && byteOrder != kCGBitmapByteOrder32Big && !reversed &&
           byteOrder != kCGBitmapByteOrder16Big) ||
          (componentBytes == 2U && byteOrder != kCGBitmapByteOrderDefault && byteOrder != kCGBitmapByteOrder16Big && !reversed16)) {
        return false;
      }

      CGDataProviderRef const provider = CGImageGetDataProvider(image);
      CfRef<CFDataRef> data(provider != nullptr ? CGDataProviderCopyData(provider) : nullptr);
      if (!data) {
        return false;
      }
      const auto width = static_cast<std::uint32_t>(CGImageGetWidth(image));
      const auto height = static_cast<std::uint32_t>(CGImageGetHeight(image));
      const std::size_t rowBytes = CGImageGetBytesPerRow(image);
      if (static_cast<std::size_t>(CFDataGetLength(data.Get())) < rowBytes * (height - 1U) + pixelBytes * width) {
        return false;
      }
      const UInt8* const samples = CFDataGetBytePtr(data.Get());
      out.Resize(width, height);
      const std::size_t colourStart = alphaFirst ? 1U : 0U;
      const std::size_t alphaIndex = alphaFirst ? 0U : colourChannels;
      for (std::uint32_t y = 0U; y < height; ++y) {
        const UInt8* in = samples + static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t* target = out.pixels.data() + static_cast<std::size_t>(y) * width * 4U;
        for (std::uint32_t x = 0U; x < width; ++x, in += pixelBytes, target += 4) {
          const auto component = [&](const std::size_t logical) -> std::uint8_t {
            if (componentBytes == 1U) {
              return in[reversed ? (components - 1U - logical) : logical];
            }
            const UInt8* const at = in + logical * 2U;
            const std::uint32_t value = reversed16 ? (at[0] | (at[1] << 8U)) : ((at[0] << 8U) | at[1]);
            return static_cast<std::uint8_t>((value * 255U + 32767U) / 65535U);
          };
          if (!palette.empty()) {
            const std::uint8_t index = component(colourStart);
            target[0] = palette[index * 3U];
            target[1] = palette[index * 3U + 1U];
            target[2] = palette[index * 3U + 2U];
          } else if (colourChannels == 1U) {
            target[0] = target[1] = target[2] = component(colourStart);
          } else {
            target[0] = component(colourStart);
            target[1] = component(colourStart + 1U);
            target[2] = component(colourStart + 2U);
          }
          target[3] = hasAlpha ? component(alphaIndex) : 255U;
        }
      }
      if (premultiplied) {
        Unpremultiply(out);
      }
      return true;
    }
  } // namespace

  bool DecodeImageFile(const void* const data, const std::size_t bytes, Rgba8Image& outImage)
  {
    if (data == nullptr || bytes == 0U) {
      return false;
    }
    CfRef<CGImageSourceRef> source(CreateSource(data, bytes));
    if (!source) {
      return false;
    }
    CfRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.Get(), 0, nullptr));
    if (!image || CGImageGetWidth(image.Get()) == 0U || CGImageGetHeight(image.Get()) == 0U) {
      return false;
    }
    return CopySamples(image.Get(), outImage) || DrawToRgba8(image.Get(), outImage);
  }

  bool ReadImageFileSize(const void* const data, const std::size_t bytes, std::uint32_t& outWidth, std::uint32_t& outHeight)
  {
    if (data == nullptr || bytes == 0U) {
      return false;
    }
    CfRef<CGImageSourceRef> source(CreateSource(data, bytes));
    if (!source) {
      return false;
    }
    CfRef<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.Get(), 0, nullptr));
    if (!properties) {
      return false;
    }
    const auto number = [&](CFStringRef const key, std::uint32_t& value) {
      const auto found = static_cast<CFNumberRef>(CFDictionaryGetValue(properties.Get(), key));
      long long result = 0;
      if (found == nullptr || CFGetTypeID(found) != CFNumberGetTypeID() || !CFNumberGetValue(found, kCFNumberLongLongType, &result) || result <= 0) {
        return false;
      }
      value = static_cast<std::uint32_t>(result);
      return true;
    };
    return number(kCGImagePropertyPixelWidth, outWidth) && number(kCGImagePropertyPixelHeight, outHeight);
  }

  bool EncodeImageFile(const Rgba8Image& image, const EncodedFormat format, std::vector<std::uint8_t>& outBytes)
  {
    outBytes.clear();
    if (image.width == 0U || image.height == 0U || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4U) {
      return false;
    }
    if (format == EncodedFormat::Bmp) {
      // BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40 bytes), 32 bpp, negative height: top-down.
      const std::uint32_t pixelBytes = image.width * image.height * 4U;
      outBytes.assign(54U + pixelBytes, 0U);
      const auto put16 = [&](const std::size_t at, const std::uint32_t value) {
        outBytes[at] = static_cast<std::uint8_t>(value & 0xFFU);
        outBytes[at + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
      };
      const auto put32 = [&](const std::size_t at, const std::uint32_t value) {
        put16(at, value & 0xFFFFU);
        put16(at + 2, value >> 16U);
      };
      put16(0, 0x4D42U); // "BM"
      put32(2, 54U + pixelBytes);
      put32(10, 54U);
      put32(14, 40U);
      put32(18, image.width);
      put32(22, static_cast<std::uint32_t>(-static_cast<std::int32_t>(image.height)));
      put16(26, 1U);
      put16(28, 32U);
      put32(30, 0U); // BI_RGB
      put32(34, pixelBytes);
      for (std::size_t at = 0U; at < pixelBytes; at += 4U) {
        outBytes[54U + at] = image.pixels[at + 2];
        outBytes[54U + at + 1] = image.pixels[at + 1];
        outBytes[54U + at + 2] = image.pixels[at];
        outBytes[54U + at + 3] = image.pixels[at + 3];
      }
      return true;
    }

    const bool jpeg = format == EncodedFormat::Jpeg;
    CfRef<CFDataRef> pixels(CFDataCreate(kCFAllocatorDefault, image.pixels.data(), static_cast<CFIndex>(image.pixels.size())));
    CfRef<CGDataProviderRef> provider(pixels ? CGDataProviderCreateWithCFData(pixels.Get()) : nullptr);
    CfRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!provider || !space) {
      return false;
    }
    const CGBitmapInfo info =
      static_cast<CGBitmapInfo>(jpeg ? kCGImageAlphaNoneSkipLast : kCGImageAlphaLast) | kCGBitmapByteOrderDefault;
    CfRef<CGImageRef> cgImage(CGImageCreate(
      image.width, image.height, 8, 32, static_cast<std::size_t>(image.width) * 4U, space.Get(), info, provider.Get(), nullptr, false,
      kCGRenderingIntentDefault
    ));
    CfRef<CFMutableDataRef> encoded(CFDataCreateMutable(kCFAllocatorDefault, 0));
    if (!cgImage || !encoded) {
      return false;
    }
    CfRef<CGImageDestinationRef> destination(
      CGImageDestinationCreateWithData(encoded.Get(), jpeg ? CFSTR("public.jpeg") : CFSTR("public.png"), 1, nullptr)
    );
    if (!destination) {
      return false;
    }
    if (jpeg) {
      const float quality = 0.9F;
      CfRef<CFNumberRef> qualityNumber(CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &quality));
      const void* keys[] = {kCGImageDestinationLossyCompressionQuality};
      const void* values[] = {qualityNumber.Get()};
      CfRef<CFDictionaryRef> properties(
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
      );
      CGImageDestinationAddImage(destination.Get(), cgImage.Get(), properties.Get());
    } else {
      CGImageDestinationAddImage(destination.Get(), cgImage.Get(), nullptr);
    }
    if (!CGImageDestinationFinalize(destination.Get())) {
      return false;
    }
    const UInt8* const begin = CFDataGetBytePtr(encoded.Get());
    outBytes.assign(begin, begin + CFDataGetLength(encoded.Get()));
    return true;
  }
} // namespace faf_posix::gal::image
