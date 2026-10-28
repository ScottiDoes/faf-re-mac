// Lays PNG/DDS/... files out on one PNG for looking at: each image scaled (nearest, integer
// factor, or halved until it fits) into a `cell` x `cell` square over a grey checkerboard, so
// alpha shows. Decodes through the POSIX codec (ImageCodec::Decode, top level).
//
//   contact_sheet <out.png> <cell> <columns> <image>...

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gpg/gal/backends/common/ImageCodec.h"
#include "platform/posix/gal/ImageFiles.h"
#include "platform/posix/gal/ImagePixels.h"

using namespace faf_posix::gal::image;
using gpg::gal::imaging::DecodeOptions;
using gpg::gal::imaging::Image;
using gpg::gal::imaging::ImageCodec;

int main(int argc, char** argv)
{
  if (argc < 5) {
    std::printf("usage: contact_sheet <out.png> <cell> <columns> <image>...\n");
    return 2;
  }
  const auto cell = static_cast<std::uint32_t>(std::atoi(argv[2]));
  const auto columns = static_cast<std::uint32_t>(std::atoi(argv[3]));
  const auto count = static_cast<std::uint32_t>(argc - 4);
  const std::uint32_t rows = (count + columns - 1U) / columns;
  const std::uint32_t gap = 4U;
  Rgba8Image sheet;
  sheet.Resize(columns * (cell + gap) + gap, rows * (cell + gap) + gap);
  std::fill(sheet.pixels.begin(), sheet.pixels.end(), static_cast<std::uint8_t>(255));
  for (std::uint32_t index = 0U; index < count; ++index) {
    std::ifstream stream(argv[4 + index], std::ios::binary);
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    Image image;
    DecodeOptions options;
    options.mipLevels = 1U;
    Rgba8Image pixels;
    if (file.empty() || FAILED(ImageCodec::Instance()->Decode(file.data(), file.size(), options, image)) ||
        !DecodeToRgba8(image.format, image.bytes.data(), image.subresources[0].rowPitch, image.subresources[0].width,
                       image.subresources[0].height, pixels)) {
      std::printf("cannot read %s\n", argv[4 + index]);
      continue;
    }
    // Integer up-scale, or a power-of-two down-scale.
    double scale = std::min(static_cast<double>(cell) / pixels.width, static_cast<double>(cell) / pixels.height);
    scale = scale >= 1.0 ? static_cast<double>(static_cast<int>(scale)) : 1.0 / static_cast<double>(1 << static_cast<int>(std::ceil(std::log2(1.0 / scale))));
    const auto width = static_cast<std::uint32_t>(pixels.width * scale);
    const auto height = static_cast<std::uint32_t>(pixels.height * scale);
    const std::uint32_t originX = gap + (index % columns) * (cell + gap);
    const std::uint32_t originY = gap + (index / columns) * (cell + gap);
    for (std::uint32_t y = 0U; y < cell; ++y) {
      for (std::uint32_t x = 0U; x < cell; ++x) {
        std::uint8_t* const out = sheet.pixels.data() + (static_cast<std::size_t>(originY + y) * sheet.width + originX + x) * 4U;
        const std::uint8_t checker = (((x / 8U) + (y / 8U)) & 1U) != 0U ? 150U : 105U;
        std::uint8_t rgba[4] = {checker, checker, checker, 255U};
        if (x < width && y < height) {
          const auto sx = static_cast<std::uint32_t>(x / scale);
          const auto sy = static_cast<std::uint32_t>(y / scale);
          const std::uint8_t* const in = pixels.pixels.data() + (static_cast<std::size_t>(sy) * pixels.width + sx) * 4U;
          for (int channel = 0; channel < 3; ++channel) {
            rgba[channel] = static_cast<std::uint8_t>((in[channel] * in[3] + checker * (255 - in[3]) + 127) / 255);
          }
        }
        std::copy(rgba, rgba + 4, out);
      }
    }
    std::printf("%2u: %s %ux%u (x%.3g)\n", index, argv[4 + index], pixels.width, pixels.height, scale);
  }
  std::vector<std::uint8_t> png;
  if (!EncodeImageFile(sheet, EncodedFormat::Png, png)) {
    std::printf("encode failed\n");
    return 1;
  }
  std::ofstream(argv[1], std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return 0;
}
