// POSIX texture codec: resizing and mip levels. See ImageResample.h.

#include "ImageResample.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace faf_posix::gal::image
{
  namespace
  {
    /** Source texels and weights of every destination texel along one axis. */
    struct AxisTaps
    {
      std::vector<std::uint32_t> first;  // per destination texel: first source texel
      std::vector<std::uint32_t> count;  // per destination texel: number of taps
      std::vector<std::uint32_t> offset; // per destination texel: into weights
      std::vector<float> weights;

      AxisTaps(const std::uint32_t sourceSize, const std::uint32_t targetSize)
      {
        const double scale = static_cast<double>(sourceSize) / static_cast<double>(targetSize);
        first.resize(targetSize);
        count.resize(targetSize);
        offset.resize(targetSize);
        for (std::uint32_t index = 0U; index < targetSize; ++index) {
          const double begin = static_cast<double>(index) * scale;
          const double end = std::min(static_cast<double>(index + 1U) * scale, static_cast<double>(sourceSize));
          const auto low = static_cast<std::uint32_t>(std::floor(begin));
          const auto high = std::min(static_cast<std::uint32_t>(std::ceil(end)), sourceSize);
          first[index] = low;
          offset[index] = static_cast<std::uint32_t>(weights.size());
          double total = 0.0;
          std::vector<double> taps;
          for (std::uint32_t source = low; source < high; ++source) {
            const double overlap = std::min(end, static_cast<double>(source) + 1.0) - std::max(begin, static_cast<double>(source));
            taps.push_back(std::max(overlap, 0.0));
            total += taps.back();
          }
          if (taps.empty() || total <= 0.0) {
            taps.assign(1U, 1.0);
            total = 1.0;
            first[index] = std::min(low, sourceSize - 1U);
          }
          for (const double tap : taps) {
            weights.push_back(static_cast<float>(tap / total));
          }
          count[index] = static_cast<std::uint32_t>(taps.size());
        }
      }
    };

    [[nodiscard]] inline std::uint8_t Round(const float value) noexcept
    {
      const float rounded = value + 0.5F;
      if (!(rounded > 0.0F)) {
        return 0U;
      }
      return rounded >= 255.0F ? 255U : static_cast<std::uint8_t>(rounded);
    }
  } // namespace

  Rgba8Image ResizeArea(const Rgba8Image& source, const std::uint32_t width, const std::uint32_t height)
  {
    if (source.width == width && source.height == height) {
      return source;
    }
    Rgba8Image result;
    result.Resize(width, height);
    if (source.width == 0U || source.height == 0U || width == 0U || height == 0U) {
      return result;
    }

    // Exact halving on both axes: the 2x2 box.
    if (source.width == width * 2U && source.height == height * 2U) {
      const std::size_t sourceRow = static_cast<std::size_t>(source.width) * 4U;
      for (std::uint32_t y = 0U; y < height; ++y) {
        const std::uint8_t* const top = source.pixels.data() + static_cast<std::size_t>(y) * 2U * sourceRow;
        const std::uint8_t* const bottom = top + sourceRow;
        std::uint8_t* const out = result.pixels.data() + static_cast<std::size_t>(y) * width * 4U;
        for (std::uint32_t x = 0U; x < width; ++x) {
          for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
            const std::uint32_t sum = top[x * 8U + channel] + top[x * 8U + 4U + channel] + bottom[x * 8U + channel] +
              bottom[x * 8U + 4U + channel];
            out[x * 4U + channel] = static_cast<std::uint8_t>((sum + 2U) >> 2U);
          }
        }
      }
      return result;
    }

    const AxisTaps horizontal(source.width, width);
    const AxisTaps vertical(source.height, height);
    std::vector<float> rows(static_cast<std::size_t>(source.height) * width * 4U);
    for (std::uint32_t y = 0U; y < source.height; ++y) {
      const std::uint8_t* const in = source.pixels.data() + static_cast<std::size_t>(y) * source.width * 4U;
      float* const out = rows.data() + static_cast<std::size_t>(y) * width * 4U;
      for (std::uint32_t x = 0U; x < width; ++x) {
        float sum[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        const float* const weights = horizontal.weights.data() + horizontal.offset[x];
        for (std::uint32_t tap = 0U; tap < horizontal.count[x]; ++tap) {
          const std::uint8_t* const texel = in + static_cast<std::size_t>(horizontal.first[x] + tap) * 4U;
          for (int channel = 0; channel < 4; ++channel) {
            sum[channel] += weights[tap] * static_cast<float>(texel[channel]);
          }
        }
        std::memcpy(out + x * 4U, sum, sizeof(sum));
      }
    }
    for (std::uint32_t y = 0U; y < height; ++y) {
      std::uint8_t* const out = result.pixels.data() + static_cast<std::size_t>(y) * width * 4U;
      const float* const weights = vertical.weights.data() + vertical.offset[y];
      for (std::uint32_t x = 0U; x < width; ++x) {
        float sum[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        for (std::uint32_t tap = 0U; tap < vertical.count[y]; ++tap) {
          const float* const texel = rows.data() + (static_cast<std::size_t>(vertical.first[y] + tap) * width + x) * 4U;
          for (int channel = 0; channel < 4; ++channel) {
            sum[channel] += weights[tap] * texel[channel];
          }
        }
        for (int channel = 0; channel < 4; ++channel) {
          out[x * 4U + static_cast<std::uint32_t>(channel)] = Round(sum[channel]);
        }
      }
    }
    return result;
  }

  Rgba8Image PlaceUnscaled(const Rgba8Image& source, const std::uint32_t width, const std::uint32_t height)
  {
    if (source.width == width && source.height == height) {
      return source;
    }
    Rgba8Image result;
    result.Resize(width, height);
    const std::uint32_t columns = std::min(source.width, width);
    for (std::uint32_t y = 0U; y < std::min(source.height, height); ++y) {
      std::memcpy(result.pixels.data() + static_cast<std::size_t>(y) * width * 4U,
                  source.pixels.data() + static_cast<std::size_t>(y) * source.width * 4U, static_cast<std::size_t>(columns) * 4U);
    }
    return result;
  }

  Rgba8Image NextMipLevel(const Rgba8Image& level)
  {
    return ResizeArea(level, std::max(1U, level.width / 2U), std::max(1U, level.height / 2U));
  }
} // namespace faf_posix::gal::image
