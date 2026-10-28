// Unit tests of the POSIX ImageCodec (src/platform/posix/gal/Image*.cpp and
// stubs/render/gal/ImageCodecPosix.cpp). Built by build.sh; run build/m2-textures-tests/codec_tests.
// Exit status 0 when every check passes.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"
#include "platform/posix/gal/ImageBc.h"
#include "platform/posix/gal/ImageDds.h"
#include "platform/posix/gal/ImageFiles.h"
#include "platform/posix/gal/ImagePixels.h"
#include "platform/posix/gal/ImageResample.h"

using namespace faf_posix::gal::image;
using gpg::gal::imaging::DecodeOptions;
using gpg::gal::imaging::FileFormat;
using gpg::gal::imaging::Image;
using gpg::gal::imaging::ImageCodec;
using gpg::gal::imaging::ResizeFilter;
namespace dxgi = gpg::gal::dxgi;

namespace
{
  int gChecks = 0;
  int gFailures = 0;

#define CHECK(condition, ...)                                                \
  do {                                                                       \
    ++gChecks;                                                               \
    if (!(condition)) {                                                      \
      ++gFailures;                                                           \
      std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #condition);       \
      std::printf(__VA_ARGS__);                                              \
      std::printf("\n");                                                     \
    }                                                                        \
  } while (0)

  std::mt19937 gRandom(12345U);

  int RandomInt(const int low, const int high)
  {
    return std::uniform_int_distribution<int>(low, high)(gRandom);
  }

  /** A smooth test picture with an alpha ramp and a few hard edges. */
  Rgba8Image Picture(const std::uint32_t width, const std::uint32_t height, const bool alpha = true)
  {
    Rgba8Image image;
    image.Resize(width, height);
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        std::uint8_t* const p = image.pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
        const double u = static_cast<double>(x) / std::max(1U, width - 1U);
        const double v = static_cast<double>(y) / std::max(1U, height - 1U);
        p[0] = static_cast<std::uint8_t>(127.5 + 127.5 * std::sin(6.0 * u + 1.0));
        p[1] = static_cast<std::uint8_t>(255.0 * v);
        p[2] = static_cast<std::uint8_t>(((x / 8U + y / 8U) & 1U) != 0U ? 200 : 40);
        p[3] = alpha ? static_cast<std::uint8_t>(255.0 * u) : 255U;
      }
    }
    return image;
  }

  double Psnr(const Rgba8Image& a, const Rgba8Image& b, const int channels = 4)
  {
    double sum = 0.0;
    std::size_t count = 0U;
    for (std::size_t at = 0U; at < a.pixels.size(); at += 4U) {
      for (int channel = 0; channel < channels; ++channel) {
        const double difference = static_cast<double>(a.pixels[at + channel]) - static_cast<double>(b.pixels[at + channel]);
        sum += difference * difference;
        ++count;
      }
    }
    const double mse = sum / static_cast<double>(std::max<std::size_t>(count, 1U));
    return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
  }

  Image ImageOf(const Rgba8Image& pixels, const DXGI_FORMAT format, const std::uint32_t levels = 1U)
  {
    Image image;
    image.Allocate(format, pixels.width, pixels.height, levels, 1U);
    Rgba8Image level = pixels;
    for (std::uint32_t mip = 0U; mip < levels; ++mip) {
      const Image::Subresource& sub = image.subresources[mip];
      if (!EncodeFromRgba8(level, format, image.bytes.data() + sub.offset, sub.rowPitch)) {
        std::printf("cannot encode format %d\n", static_cast<int>(format));
      }
      level = NextMipLevel(level);
    }
    return image;
  }

  Rgba8Image Level(const Image& image, const std::uint32_t index)
  {
    Rgba8Image out;
    const Image::Subresource& sub = image.subresources[index];
    if (!DecodeToRgba8(image.format, image.bytes.data() + sub.offset, sub.rowPitch, sub.width, sub.height, out)) {
      std::printf("cannot decode format %d\n", static_cast<int>(image.format));
    }
    return out;
  }

  std::vector<std::uint8_t> Dds(const Image& image)
  {
    std::vector<std::uint8_t> bytes;
    if (!WriteDds(image, bytes)) {
      std::printf("WriteDds failed\n");
    }
    return bytes;
  }

  bool LayoutConsistent(const Image& image)
  {
    if (image.subresources.size() != static_cast<std::size_t>(image.mipLevels) * image.arraySize) {
      return false;
    }
    std::size_t offset = 0U;
    for (std::uint32_t slice = 0U; slice < image.arraySize; ++slice) {
      for (std::uint32_t mip = 0U; mip < image.mipLevels; ++mip) {
        const Image::Subresource& sub = image.subresources[slice * image.mipLevels + mip];
        const dxgi::LevelLayout layout = dxgi::ComputeLevelLayout(image.format, std::max(1U, image.width >> mip), std::max(1U, image.height >> mip));
        if (sub.offset != offset || sub.width != std::max(1U, image.width >> mip) || sub.height != std::max(1U, image.height >> mip) ||
            sub.rowPitch != layout.rowPitch || sub.rowCount != layout.rowCount || sub.slicePitch != layout.ByteSize()) {
          return false;
        }
        offset += sub.slicePitch;
      }
    }
    return offset == image.bytes.size();
  }

  std::uint32_t FullChain(const std::uint32_t width, const std::uint32_t height)
  {
    std::uint32_t levels = 1U;
    for (std::uint32_t side = std::max(width, height); side > 1U; side >>= 1U) {
      ++levels;
    }
    return levels;
  }

  // ---------------------------------------------------------------------------------------
  void TestBcBlocks()
  {
    // Flat blocks: the single-colour tables reproduce every grey and random colours closely.
    int worstFlat = 0;
    for (int trial = 0; trial < 4096; ++trial) {
      std::uint8_t rgba[64];
      const int r = trial < 256 ? trial : RandomInt(0, 255);
      const int g = trial < 256 ? trial : RandomInt(0, 255);
      const int b = trial < 256 ? trial : RandomInt(0, 255);
      for (int pixel = 0; pixel < 16; ++pixel) {
        rgba[pixel * 4] = static_cast<std::uint8_t>(r);
        rgba[pixel * 4 + 1] = static_cast<std::uint8_t>(g);
        rgba[pixel * 4 + 2] = static_cast<std::uint8_t>(b);
        rgba[pixel * 4 + 3] = 255U;
      }
      std::uint8_t block[16];
      std::uint8_t decoded[64];
      EncodeBc1Block(rgba, block, true);
      DecodeBc1Block(block, decoded);
      for (int pixel = 0; pixel < 16; ++pixel) {
        for (int channel = 0; channel < 3; ++channel) {
          worstFlat = std::max(worstFlat, std::abs(decoded[pixel * 4 + channel] - rgba[pixel * 4 + channel]));
        }
        CHECK(decoded[pixel * 4 + 3] == 255U, "flat BC1 block lost opacity");
      }
    }
    CHECK(worstFlat <= 2, "worst flat-colour error %d", worstFlat);
    std::printf("BC1 flat blocks: worst channel error %d\n", worstFlat);

    // Smooth content: PSNR of whole pictures through BC1 / BC3.
    const Rgba8Image picture = Picture(256, 256);
    for (const DXGI_FORMAT format : {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM}) {
      Rgba8Image opaque = Picture(256, 256, format == DXGI_FORMAT_BC1_UNORM ? false : true);
      const Image image = ImageOf(opaque, format);
      const Rgba8Image back = Level(image, 0U);
      const double rgb = Psnr(opaque, back, 3);
      const double all = Psnr(opaque, back, 4);
      std::printf("BC%d picture: RGB PSNR %.2f dB, RGBA PSNR %.2f dB\n", BlockKind(format), rgb, all);
      CHECK(rgb > 34.0, "BC%d RGB PSNR %.2f", BlockKind(format), rgb);
      CHECK(all > 33.0, "BC%d RGBA PSNR %.2f", BlockKind(format), all);
    }
    static_cast<void>(picture);

    // BC2/BC3 colour blocks always have c0 > c1, or c0 == c1 with every index 0.
    int ordered = 0;
    for (int trial = 0; trial < 2000; ++trial) {
      std::uint8_t rgba[64];
      for (int at = 0; at < 64; ++at) {
        rgba[at] = static_cast<std::uint8_t>(trial % 3 == 0 ? RandomInt(100, 104) : RandomInt(0, 255));
      }
      std::uint8_t block[16];
      EncodeBc3Block(rgba, block);
      const int c0 = block[8] | (block[9] << 8);
      const int c1 = block[10] | (block[11] << 8);
      const std::uint32_t indices = block[12] | (block[13] << 8) | (block[14] << 16) | (static_cast<std::uint32_t>(block[15]) << 24);
      ordered += (c0 > c1 || (c0 == c1 && indices == 0U)) ? 1 : 0;
    }
    CHECK(ordered == 2000, "%d of 2000 BC3 colour blocks ordered", ordered);

    // BC3 alpha: blocks of only 0 and 255 are exact (six-value mode); random alpha close.
    {
      std::uint8_t rgba[64] = {};
      for (int pixel = 0; pixel < 16; ++pixel) {
        rgba[pixel * 4 + 3] = (pixel % 3 == 0) ? 0U : ((pixel % 3 == 1) ? 255U : 128U);
      }
      std::uint8_t block[16];
      std::uint8_t decoded[64];
      EncodeBc3Block(rgba, block);
      DecodeBc3Block(block, decoded);
      int worst = 0;
      for (int pixel = 0; pixel < 16; ++pixel) {
        worst = std::max(worst, std::abs(decoded[pixel * 4 + 3] - rgba[pixel * 4 + 3]));
      }
      CHECK(worst == 0, "0/128/255 alpha block error %d", worst);
      // Random alpha: the mode with the smaller summed error wins, so one texel may be off by
      // more than half an eight-value step (255/14) when the six-value mode is chosen.
      int worstRandom = 0;
      double squared = 0.0;
      for (int trial = 0; trial < 1000; ++trial) {
        for (int pixel = 0; pixel < 16; ++pixel) {
          rgba[pixel * 4 + 3] = static_cast<std::uint8_t>(RandomInt(0, 255));
        }
        EncodeBc3Block(rgba, block);
        DecodeBc3Block(block, decoded);
        for (int pixel = 0; pixel < 16; ++pixel) {
          const int error = std::abs(decoded[pixel * 4 + 3] - rgba[pixel * 4 + 3]);
          worstRandom = std::max(worstRandom, error);
          squared += error * error;
        }
      }
      const double rms = std::sqrt(squared / 16000.0);
      std::printf("BC3 alpha, random blocks: worst %d, RMS %.2f\n", worstRandom, rms);
      CHECK(worstRandom <= 26 && rms < 11.0, "random alpha worst error %d, RMS %.2f", worstRandom, rms);
    }

    // BC1 punch-through alpha.
    {
      std::uint8_t rgba[64];
      for (int pixel = 0; pixel < 16; ++pixel) {
        rgba[pixel * 4] = static_cast<std::uint8_t>(pixel * 16);
        rgba[pixel * 4 + 1] = 100U;
        rgba[pixel * 4 + 2] = 50U;
        rgba[pixel * 4 + 3] = (pixel & 1) != 0 ? 0U : 255U;
      }
      std::uint8_t block[8];
      std::uint8_t decoded[64];
      EncodeBc1Block(rgba, block, true);
      DecodeBc1Block(block, decoded);
      bool ok = true;
      for (int pixel = 0; pixel < 16; ++pixel) {
        if ((pixel & 1) != 0) {
          ok = ok && decoded[pixel * 4 + 3] == 0U && decoded[pixel * 4] == 0U;
        } else {
          ok = ok && decoded[pixel * 4 + 3] == 255U && std::abs(decoded[pixel * 4] - rgba[pixel * 4]) < 40;
        }
      }
      CHECK(ok, "BC1 punch-through");
      // Fully transparent block.
      for (int pixel = 0; pixel < 16; ++pixel) {
        rgba[pixel * 4 + 3] = 0U;
      }
      EncodeBc1Block(rgba, block, true);
      DecodeBc1Block(block, decoded);
      bool clear = true;
      for (int pixel = 0; pixel < 16; ++pixel) {
        clear = clear && decoded[pixel * 4 + 3] == 0U;
      }
      CHECK(clear, "transparent BC1 block");
    }

    // Lossless re-packing into BC3.
    {
      int same = 0;
      int transcoded = 0;
      for (int trial = 0; trial < 500; ++trial) {
        std::uint8_t rgba[64];
        for (int at = 0; at < 64; ++at) {
          rgba[at] = static_cast<std::uint8_t>(RandomInt(0, 255));
        }
        for (int pixel = 0; pixel < 16; ++pixel) {
          rgba[pixel * 4 + 3] = 255U;
        }
        std::uint8_t bc1[8];
        std::uint8_t bc3[16];
        std::uint8_t a[64];
        std::uint8_t b[64];
        EncodeBc1Block(rgba, bc1, false);
        if (TranscodeBlockToBc3(1, bc1, bc3)) {
          ++transcoded;
          DecodeBc1Block(bc1, a);
          DecodeBc3Block(bc3, b);
          same += std::memcmp(a, b, 64) == 0 ? 1 : 0;
        }
      }
      CHECK(transcoded == 500 && same == 500, "BC1->BC3 re-pack %d/%d identical", same, transcoded);
      std::uint8_t rgba[64];
      for (int at = 0; at < 64; ++at) {
        rgba[at] = static_cast<std::uint8_t>(RandomInt(0, 255));
      }
      std::uint8_t bc2[16];
      std::uint8_t bc3[16];
      std::uint8_t a[64];
      std::uint8_t b[64];
      EncodeBc2Block(rgba, bc2);
      CHECK(TranscodeBlockToBc3(2, bc2, bc3), "BC2->BC3");
      DecodeBc2Block(bc2, a);
      DecodeBc3Block(bc3, b);
      int worstAlpha = 0;
      bool sameColour = true;
      for (int pixel = 0; pixel < 16; ++pixel) {
        sameColour = sameColour && std::memcmp(a + pixel * 4, b + pixel * 4, 3) == 0;
        worstAlpha = std::max(worstAlpha, std::abs(a[pixel * 4 + 3] - b[pixel * 4 + 3]));
      }
      CHECK(sameColour && worstAlpha <= 26, "BC2->BC3 colour same %d, alpha error %d", sameColour ? 1 : 0, worstAlpha);
    }
  }

  void TestPixelFormats()
  {
    const Rgba8Image picture = Picture(37, 23);
    const DXGI_FORMAT exact[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM,
                                 DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    for (const DXGI_FORMAT format : exact) {
      const Image image = ImageOf(picture, format);
      const Rgba8Image back = Level(image, 0U);
      CHECK(back.pixels == picture.pixels, "format %d round trip", static_cast<int>(format));
    }
    // Narrow formats: a second round trip changes nothing.
    const DXGI_FORMAT narrow[] = {DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B5G5R5A1_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
                                  DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R16_FLOAT,
                                  DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_R16G16_UNORM};
    for (const DXGI_FORMAT format : narrow) {
      const Rgba8Image once = Level(ImageOf(picture, format), 0U);
      const Rgba8Image twice = Level(ImageOf(once, format), 0U);
      CHECK(once.pixels == twice.pixels, "format %d stable", static_cast<int>(format));
    }
    // Expansion is exact: 5-bit 31 -> 255, 6-bit 32 -> 130, 4-bit 15 -> 255.
    {
      Image image;
      image.Allocate(DXGI_FORMAT_B5G6R5_UNORM, 1U, 1U, 1U, 1U);
      const std::uint16_t value = (31U << 11U) | (32U << 5U) | 1U;
      std::memcpy(image.bytes.data(), &value, 2U);
      const Rgba8Image texel = Level(image, 0U);
      CHECK(texel.pixels[0] == 255U && texel.pixels[1] == 130U && texel.pixels[2] == 8U && texel.pixels[3] == 255U, "565 expansion %d %d %d",
            texel.pixels[0], texel.pixels[1], texel.pixels[2]);
    }
  }

  void TestResample()
  {
    Rgba8Image image;
    image.Resize(4, 2);
    const std::uint8_t values[] = {0, 1, 2, 3, 10, 11, 12, 13};
    for (int pixel = 0; pixel < 8; ++pixel) {
      for (int channel = 0; channel < 4; ++channel) {
        image.pixels[pixel * 4 + channel] = static_cast<std::uint8_t>(values[pixel] * 10 + channel);
      }
    }
    const Rgba8Image half = NextMipLevel(image);
    CHECK(half.width == 2U && half.height == 1U, "half size");
    // (0 + 10 + 100 + 110) / 4 = 55, (20 + 30 + 120 + 130) / 4 = 75.
    CHECK(half.pixels[0] == 55U && half.pixels[4] == 75U && half.pixels[3] == 58U, "box %d %d %d", half.pixels[0], half.pixels[4], half.pixels[3]);

    Rgba8Image flat;
    flat.Resize(18, 21);
    std::fill(flat.pixels.begin(), flat.pixels.end(), static_cast<std::uint8_t>(77));
    for (const auto& size : {std::pair<std::uint32_t, std::uint32_t>{20, 24}, {9, 10}, {1, 1}, {5, 3}}) {
      const Rgba8Image resized = ResizeArea(flat, size.first, size.second);
      const bool constant = std::all_of(resized.pixels.begin(), resized.pixels.end(), [](const std::uint8_t v) { return v == 77U; });
      CHECK(constant && resized.width == size.first && resized.height == size.second, "flat stays flat at %ux%u", size.first, size.second);
    }
    const Rgba8Image placed = PlaceUnscaled(flat, 20, 24);
    CHECK(placed.pixels[(0 * 20 + 17) * 4] == 77U && placed.pixels[(0 * 20 + 18) * 4] == 0U && placed.pixels[(21 * 20 + 0) * 4 + 3] == 0U,
          "unscaled placement pads with transparent black");
  }

  void TestDds()
  {
    // Write -> read round trip of several shapes.
    const Rgba8Image picture = Picture(64, 32);
    for (const DXGI_FORMAT format : {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8_UNORM,
                                     DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_B5G6R5_UNORM}) {
      const Image image = ImageOf(picture, format, 7U);
      Image back;
      const std::vector<std::uint8_t> file = Dds(image);
      CHECK(ReadDds(file.data(), file.size(), back), "read back format %d", static_cast<int>(format));
      CHECK(back.format == image.format && back.mipLevels == 7U && back.bytes == image.bytes, "format %d round trip (%d)",
            static_cast<int>(format), static_cast<int>(back.format));
    }
    {
      Image cube;
      cube.Allocate(DXGI_FORMAT_BC3_UNORM, 16U, 16U, 5U, 6U);
      cube.cube = true;
      for (std::size_t at = 0U; at < cube.bytes.size(); ++at) {
        cube.bytes[at] = static_cast<std::uint8_t>(at * 7U);
      }
      const std::vector<std::uint8_t> file = Dds(cube);
      Image back;
      CHECK(ReadDds(file.data(), file.size(), back) && back.cube && back.arraySize == 6U && back.bytes == cube.bytes, "cube round trip");
    }

    // Legacy pixel formats, one texel each: (r, g, b, a) after decoding the result.
    struct Legacy
    {
      const char* name;
      std::uint32_t flags, bits, r, g, b, a;
      std::vector<std::uint8_t> texel;
      DXGI_FORMAT expected;
      std::uint8_t rgba[4];
    };
    const Legacy cases[] = {
      {"A8R8G8B8", 0x41, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000, {30, 20, 10, 40}, DXGI_FORMAT_B8G8R8A8_UNORM, {10, 20, 30, 40}},
      {"X8R8G8B8", 0x40, 32, 0xFF0000, 0xFF00, 0xFF, 0, {30, 20, 10, 40}, DXGI_FORMAT_B8G8R8X8_UNORM, {10, 20, 30, 255}},
      {"A8B8G8R8", 0x41, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000, {10, 20, 30, 40}, DXGI_FORMAT_R8G8B8A8_UNORM, {10, 20, 30, 40}},
      {"X8B8G8R8", 0x40, 32, 0xFF, 0xFF00, 0xFF0000, 0, {10, 20, 30, 40}, DXGI_FORMAT_R8G8B8A8_UNORM, {10, 20, 30, 255}},
      {"R8G8B8", 0x40, 24, 0xFF0000, 0xFF00, 0xFF, 0, {30, 20, 10}, DXGI_FORMAT_B8G8R8A8_UNORM, {10, 20, 30, 255}},
      {"A1R5G5B5", 0x41, 16, 0x7C00, 0x3E0, 0x1F, 0x8000, {0x1F, 0x80 | (1 << 2)}, DXGI_FORMAT_B8G8R8A8_UNORM, {8, 0, 255, 255}},
      {"R5G6B5", 0x40, 16, 0xF800, 0x7E0, 0x1F, 0, {0x00, 0xF8}, DXGI_FORMAT_B8G8R8A8_UNORM, {255, 0, 0, 255}},
      {"A4R4G4B4", 0x41, 16, 0xF00, 0xF0, 0xF, 0xF000, {0x21, 0x83}, DXGI_FORMAT_B8G8R8A8_UNORM, {51, 34, 17, 136}},
      {"L8", 0x20000, 8, 0xFF, 0, 0, 0, {99}, DXGI_FORMAT_R8_UNORM, {99, 0, 0, 255}},
      {"A8", 0x2, 8, 0, 0, 0, 0xFF, {99}, DXGI_FORMAT_A8_UNORM, {0, 0, 0, 99}},
      {"A8L8", 0x20001, 16, 0xFF, 0, 0, 0xFF00, {99, 7}, DXGI_FORMAT_R8G8_UNORM, {99, 7, 0, 255}},
    };
    for (const Legacy& legacy : cases) {
      std::vector<std::uint8_t> file(128, 0U);
      const auto put = [&file](const std::size_t at, const std::uint32_t value) { std::memcpy(file.data() + at, &value, 4); };
      put(0, 0x20534444U);
      put(4, 124U);
      put(8, 0x1007U);
      put(12, 1U);
      put(16, 1U);
      put(76, 32U);
      put(80, legacy.flags);
      put(88, legacy.bits);
      put(92, legacy.r);
      put(96, legacy.g);
      put(100, legacy.b);
      put(104, legacy.a);
      put(108, 0x1000U);
      file.insert(file.end(), legacy.texel.begin(), legacy.texel.end());
      Image image;
      const bool read = ReadDds(file.data(), file.size(), image);
      CHECK(read && image.format == legacy.expected, "%s -> format %d", legacy.name, static_cast<int>(image.format));
      if (read) {
        const Rgba8Image texel = Level(image, 0U);
        CHECK(std::memcmp(texel.pixels.data(), legacy.rgba, 4) == 0, "%s texel %d %d %d %d", legacy.name, texel.pixels[0], texel.pixels[1],
              texel.pixels[2], texel.pixels[3]);
      }
    }
  }

  void TestDecodeOptions()
  {
    ImageCodec* const codec = ImageCodec::Instance();
    CHECK(codec != nullptr, "codec instance");
    if (codec == nullptr) {
      return;
    }
    // A 1-level DXT5 512x512 comes back with ten levels, level 0 the file's own bytes.
    {
      const Image file = ImageOf(Picture(512, 512), DXGI_FORMAT_BC3_UNORM);
      const std::vector<std::uint8_t> dds = Dds(file);
      Image image;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, image)), "decode 1-level DXT5");
      CHECK(image.format == DXGI_FORMAT_BC3_UNORM && image.mipLevels == 10U && LayoutConsistent(image), "full chain built (%u)", image.mipLevels);
      CHECK(std::memcmp(image.bytes.data(), file.bytes.data(), file.subresources[0].slicePitch) == 0, "level 0 kept");
      const double psnr = Psnr(NextMipLevel(Picture(512, 512)), Level(image, 1U));
      CHECK(psnr > 30.0, "level 1 PSNR %.2f", psnr);
      // FirstMipLevel past the file's levels is ignored (the Windows caller retries from 0).
      DecodeOptions skip;
      skip.skipMipLevels = 2U;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), skip, image)) && image.width == 512U, "skip ignored on 1-level file");
    }
    // A full chain: the direct path and the full decode agree byte for byte; skip drops levels.
    {
      const Image file = ImageOf(Picture(256, 256), DXGI_FORMAT_BC1_UNORM, 9U);
      const std::vector<std::uint8_t> dds = Dds(file);
      Image direct;
      Image full;
      gpg::gal::imaging::gDdsDirectLoad = true;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, direct)), "direct");
      CHECK(std::strcmp(ImageCodec::LastDecodeRoute(), "direct") == 0, "route %s", ImageCodec::LastDecodeRoute());
      gpg::gal::imaging::gDdsDirectLoad = false;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, full)), "full");
      gpg::gal::imaging::gDdsDirectLoad = true;
      CHECK(direct.bytes == full.bytes && direct.mipLevels == full.mipLevels && direct.format == full.format, "direct == full");
      DecodeOptions skip;
      skip.skipMipLevels = 2U;
      Image skipped;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), skip, skipped)) && skipped.width == 64U && skipped.mipLevels == 7U, "skip 2: %ux%u %u",
            skipped.width, skipped.height, skipped.mipLevels);
    }
    // 18x18 DXT5 without levels: stretched over 20x20 with a full chain; GetTexture2D's
    // options give 20x20, one level, the 2 extra columns/rows transparent black, inner blocks the file's.
    {
      Rgba8Image picture = Picture(18, 18);
      for (std::size_t at = 3U; at < picture.pixels.size(); at += 4U) {
        picture.pixels[at] = 255U; // opaque, so the padding stands out
      }
      const Image file = ImageOf(picture, DXGI_FORMAT_BC3_UNORM);
      const std::vector<std::uint8_t> dds = Dds(file);
      Image stretched;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, stretched)) && stretched.width == 20U && stretched.height == 20U &&
              stretched.mipLevels == 5U && LayoutConsistent(stretched),
            "18x18 -> %ux%u, %u levels", stretched.width, stretched.height, stretched.mipLevels);
      const Rgba8Image top = Level(stretched, 0U);
      CHECK(top.pixels[(19 * 20 + 19) * 4 + 3] == 255U, "stretched: the corner is image, not padding");

      DecodeOptions ui;
      ui.format = DXGI_FORMAT_BC3_UNORM;
      ui.mipLevels = 1U;
      ui.filter = ResizeFilter::None;
      Image padded;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), ui, padded)) && padded.width == 20U && padded.mipLevels == 1U, "padded decode");
      const Rgba8Image texels = Level(padded, 0U);
      bool padding = true;
      for (std::uint32_t y = 0U; y < 20U; ++y) {
        for (std::uint32_t x = 0U; x < 20U; ++x) {
          if (x >= 18U || y >= 18U) {
            // Transparent; the colour of a transparent texel is the encoder's choice (alpha-weighted fit).
            padding = padding && texels.pixels[(y * 20U + x) * 4U + 3U] == 0U;
          }
        }
      }
      CHECK(padding, "padding transparent");
      bool inner = true;
      for (std::uint32_t by = 0U; by < 4U; ++by) {
        inner = inner && std::memcmp(padded.bytes.data() + by * padded.subresources[0].rowPitch, file.bytes.data() + by * file.subresources[0].rowPitch,
                                     4U * 16U) == 0;
      }
      CHECK(inner, "inner blocks carried over");
      std::uint32_t width = 0U;
      std::uint32_t height = 0U;
      CHECK(SUCCEEDED(codec->GetImageSize(dds.data(), dds.size(), width, height)) && width == 18U && height == 18U, "image size %ux%u", width, height);
    }
    // UpdateSurface: B8G8R8A8 512x512 render target -> DDS -> BC3 256x256 with 9 levels.
    {
      const Image target = ImageOf(Picture(512, 512), DXGI_FORMAT_B8G8R8A8_UNORM);
      std::vector<std::uint8_t> encoded;
      CHECK(SUCCEEDED(codec->Encode(target, FileFormat::Dds, encoded)), "encode DDS");
      DecodeOptions options;
      options.format = DXGI_FORMAT_BC3_UNORM;
      options.width = 256U;
      options.height = 256U;
      options.mipLevels = 9U;
      Image converted;
      CHECK(SUCCEEDED(codec->Decode(encoded.data(), encoded.size(), options, converted)) && converted.width == 256U && converted.mipLevels == 9U &&
              LayoutConsistent(converted),
            "converted %ux%u %u", converted.width, converted.height, converted.mipLevels);
      const double psnr = Psnr(ResizeArea(Picture(512, 512), 256, 256), Level(converted, 0U));
      CHECK(psnr > 30.0, "converted PSNR %.2f", psnr);
    }
    // Image files through ImageIO.
    {
      const Rgba8Image picture = Picture(45, 30);
      const Image image = ImageOf(picture, DXGI_FORMAT_R8G8B8A8_UNORM);
      for (const FileFormat format : {FileFormat::Png, FileFormat::Bmp, FileFormat::Jpg}) {
        std::vector<std::uint8_t> encoded;
        CHECK(SUCCEEDED(codec->Encode(image, format, encoded)) && !encoded.empty(), "encode file format %d", static_cast<int>(format));
        Image decoded;
        CHECK(SUCCEEDED(codec->Decode(encoded.data(), encoded.size(), DecodeOptions{}, decoded)), "decode file format %d", static_cast<int>(format));
        CHECK(decoded.format == DXGI_FORMAT_B8G8R8A8_UNORM && decoded.width == 45U && decoded.mipLevels == 6U && LayoutConsistent(decoded),
              "file format %d -> %d %ux%u %u", static_cast<int>(format), static_cast<int>(decoded.format), decoded.width, decoded.height,
              decoded.mipLevels);
        const Rgba8Image back = Level(decoded, 0U);
        if (format == FileFormat::Png) {
          CHECK(back.pixels == picture.pixels, "PNG lossless, alpha kept");
        } else if (format == FileFormat::Bmp) {
          CHECK(Psnr(picture, back, 3) > 90.0, "BMP colour lossless (%.1f dB)", Psnr(picture, back, 3));
        } else {
          // Quality 0.9 with chroma subsampling over the picture's hard-edged blue checkerboard.
          CHECK(Psnr(picture, back, 3) > 25.0, "JPEG close (%.1f dB)", Psnr(picture, back, 3));
        }
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        CHECK(SUCCEEDED(codec->GetImageSize(encoded.data(), encoded.size(), width, height)) && width == 45U && height == 30U, "file size");
      }
      // TGA (no signature): uncompressed 32 bpp, bottom-up, and RLE 24 bpp top-down.
      std::vector<std::uint8_t> tga(18, 0U);
      tga[2] = 2;
      tga[12] = 3;
      tga[14] = 2;
      tga[16] = 32;
      tga[17] = 8; // 8 alpha bits, origin bottom-left
      const std::uint8_t rows[2][3][4] = {{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}}, {{13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}};
      for (int row = 1; row >= 0; --row) { // bottom row first
        for (int x = 0; x < 3; ++x) {
          tga.insert(tga.end(), rows[row][x], rows[row][x] + 4); // B, G, R, A
        }
      }
      Image decoded;
      const HRESULT result = codec->Decode(tga.data(), tga.size(), DecodeOptions{}, decoded);
      CHECK(SUCCEEDED(result) && decoded.width == 3U && decoded.height == 2U, "TGA decode 0x%08X", static_cast<unsigned>(result));
      if (SUCCEEDED(result)) {
        const Rgba8Image back = Level(decoded, 0U);
        CHECK(back.pixels[0] == 3U && back.pixels[1] == 2U && back.pixels[2] == 1U && back.pixels[3] == 4U && back.pixels[12] == 15U,
              "TGA texels %d %d %d %d / %d", back.pixels[0], back.pixels[1], back.pixels[2], back.pixels[3], back.pixels[12]);
      }
    }
    // A cube with one level per face: six full chains.
    {
      Image cube;
      cube.Allocate(DXGI_FORMAT_BC1_UNORM, 32U, 32U, 1U, 6U);
      cube.cube = true;
      for (std::uint32_t face = 0U; face < 6U; ++face) {
        Rgba8Image flat;
        flat.Resize(32, 32);
        std::fill(flat.pixels.begin(), flat.pixels.end(), static_cast<std::uint8_t>(40 * face));
        for (std::size_t at = 3U; at < flat.pixels.size(); at += 4U) {
          flat.pixels[at] = 255U;
        }
        static_cast<void>(EncodeFromRgba8(flat, cube.format, cube.bytes.data() + cube.subresources[face].offset, cube.subresources[face].rowPitch));
      }
      const std::vector<std::uint8_t> dds = Dds(cube);
      Image image;
      CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, image)) && image.cube && image.arraySize == 6U && image.mipLevels == 6U &&
              LayoutConsistent(image),
            "cube chain");
      const Rgba8Image smallest = Level(image, 5U * 6U + 5U);
      CHECK(std::abs(smallest.pixels[0] - 200) <= 2, "face 5 smallest level %d", smallest.pixels[0]);
    }
  }

  /** Timings (printed, not checked): BC encoding and a 1024x1024 1-level DXT5 decode. */
  void Benchmark()
  {
    const Rgba8Image picture = Picture(1024, 1024);
    for (const DXGI_FORMAT format : {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM}) {
      const auto begin = std::chrono::steady_clock::now();
      const Image image = ImageOf(picture, format);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
      std::printf("encode 1024x1024 to BC%d: %.1f ms (%.1f Mpixel/s)\n", BlockKind(format), ms, 1.048576 / (ms / 1000.0));
      static_cast<void>(image);
    }
    const std::vector<std::uint8_t> dds = Dds(ImageOf(picture, DXGI_FORMAT_BC3_UNORM));
    gpg::gal::imaging::gTextureDecodeCache = false;
    const auto begin = std::chrono::steady_clock::now();
    Image image;
    static_cast<void>(ImageCodec::Instance()->Decode(dds.data(), dds.size(), DecodeOptions{}, image));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("decode 1024x1024 DXT5 without levels (builds 10 levels): %.1f ms\n", ms);
    gpg::gal::imaging::gTextureDecodeCache = true;
  }

  void TestDecodeCache()
  {
    const char* const directory = std::getenv("FAF_TEST_CACHE_DIR");
    if (directory == nullptr) {
      std::printf("decode cache: FAF_TEST_CACHE_DIR not set, skipped\n");
      return;
    }
    ImageCodec* const codec = ImageCodec::Instance();
    // Different content each run, so the first decode is never a cache hit.
    Rgba8Image picture = Picture(1024, 1024);
    const auto stamp = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::memcpy(picture.pixels.data(), &stamp, sizeof(stamp));
    const Image file = ImageOf(picture, DXGI_FORMAT_BC3_UNORM);
    const std::vector<std::uint8_t> dds = Dds(file);
    Image first;
    CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, first)), "first decode");
    const std::string route = ImageCodec::LastDecodeRoute();
    std::printf("decode cache: first route \"%s\"\n", route.c_str());
    CHECK(route == "decode, cached for later sessions", "first route %s", route.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    Image second;
    CHECK(SUCCEEDED(codec->Decode(dds.data(), dds.size(), DecodeOptions{}, second)), "second decode");
    CHECK(std::strcmp(ImageCodec::LastDecodeRoute(), "cache") == 0 && second.bytes == first.bytes && second.mipLevels == first.mipLevels,
          "second route %s", ImageCodec::LastDecodeRoute());
  }
} // namespace

int main()
{
  TestBcBlocks();
  TestPixelFormats();
  TestResample();
  TestDds();
  TestDecodeOptions();
  Benchmark();
  TestDecodeCache();
  std::printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
