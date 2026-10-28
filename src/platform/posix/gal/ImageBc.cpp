// POSIX texture codec: BC1-BC5 blocks. See ImageBc.h.

#include "ImageBc.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace faf_posix::gal::image
{
  namespace
  {
    [[nodiscard]] inline int Expand5(const int value) noexcept
    {
      return (value << 3) | (value >> 2);
    }

    [[nodiscard]] inline int Expand6(const int value) noexcept
    {
      return (value << 2) | (value >> 4);
    }

    [[nodiscard]] inline std::uint32_t Read16(const std::uint8_t* const at) noexcept
    {
      return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8U);
    }

    [[nodiscard]] inline std::uint32_t Read32(const std::uint8_t* const at) noexcept
    {
      return Read16(at) | (Read16(at + 2) << 16U);
    }

    inline void Write16(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      at[0] = static_cast<std::uint8_t>(value & 0xFFU);
      at[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    inline void Write32(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      Write16(at, value & 0xFFFFU);
      Write16(at + 2, value >> 16U);
    }

    struct Rgb
    {
      int r = 0;
      int g = 0;
      int b = 0;
    };

    [[nodiscard]] inline Rgb Unpack565(const std::uint32_t color) noexcept
    {
      return {Expand5(static_cast<int>((color >> 11U) & 31U)), Expand6(static_cast<int>((color >> 5U) & 63U)),
              Expand5(static_cast<int>(color & 31U))};
    }

    [[nodiscard]] inline int Quantize(const float value, const int maximum) noexcept
    {
      const float scaled = value * static_cast<float>(maximum) / 255.0F + 0.5F;
      return std::clamp(static_cast<int>(scaled), 0, maximum);
    }

    [[nodiscard]] inline std::uint32_t Pack565(const float r, const float g, const float b) noexcept
    {
      return (static_cast<std::uint32_t>(Quantize(r, 31)) << 11U) | (static_cast<std::uint32_t>(Quantize(g, 63)) << 5U) |
        static_cast<std::uint32_t>(Quantize(b, 31));
    }

    /** The four (or three plus transparent) colours of a colour block. */
    void Palette(const std::uint32_t c0, const std::uint32_t c1, const bool fourColour, Rgb (&palette)[4]) noexcept
    {
      palette[0] = Unpack565(c0);
      palette[1] = Unpack565(c1);
      if (fourColour) {
        palette[2] = {(2 * palette[0].r + palette[1].r + 1) / 3, (2 * palette[0].g + palette[1].g + 1) / 3,
                      (2 * palette[0].b + palette[1].b + 1) / 3};
        palette[3] = {(palette[0].r + 2 * palette[1].r + 1) / 3, (palette[0].g + 2 * palette[1].g + 1) / 3,
                      (palette[0].b + 2 * palette[1].b + 1) / 3};
      } else {
        palette[2] = {(palette[0].r + palette[1].r + 1) / 2, (palette[0].g + palette[1].g + 1) / 2,
                      (palette[0].b + palette[1].b + 1) / 2};
        palette[3] = {0, 0, 0};
      }
    }

    void DecodeColourBlock(const std::uint8_t* const block, std::uint8_t* const rgba, const bool alwaysFourColour) noexcept
    {
      const std::uint32_t c0 = Read16(block);
      const std::uint32_t c1 = Read16(block + 2);
      const bool fourColour = alwaysFourColour || c0 > c1;
      Rgb palette[4];
      Palette(c0, c1, fourColour, palette);
      const std::uint32_t indices = Read32(block + 4);
      for (int pixel = 0; pixel < 16; ++pixel) {
        const std::uint32_t index = (indices >> (2 * pixel)) & 3U;
        std::uint8_t* const out = rgba + pixel * 4;
        out[0] = static_cast<std::uint8_t>(palette[index].r);
        out[1] = static_cast<std::uint8_t>(palette[index].g);
        out[2] = static_cast<std::uint8_t>(palette[index].b);
        out[3] = (!fourColour && index == 3U) ? 0U : 255U;
      }
    }

    void AlphaPalette(const int a0, const int a1, int (&palette)[8]) noexcept
    {
      palette[0] = a0;
      palette[1] = a1;
      if (a0 > a1) {
        for (int step = 1; step <= 6; ++step) {
          palette[step + 1] = ((7 - step) * a0 + step * a1 + 3) / 7;
        }
      } else {
        for (int step = 1; step <= 4; ++step) {
          palette[step + 1] = ((5 - step) * a0 + step * a1 + 2) / 5;
        }
        palette[6] = 0;
        palette[7] = 255;
      }
    }

    void DecodeAlphaBlock(const std::uint8_t* const block, std::uint8_t* const out, const int stride) noexcept
    {
      int palette[8];
      AlphaPalette(block[0], block[1], palette);
      std::uint64_t indices = 0U;
      for (int byte = 0; byte < 6; ++byte) {
        indices |= static_cast<std::uint64_t>(block[2 + byte]) << (8 * byte);
      }
      for (int pixel = 0; pixel < 16; ++pixel) {
        out[pixel * stride] = static_cast<std::uint8_t>(palette[(indices >> (3 * pixel)) & 7U]);
      }
    }

    /** Index choice for one alpha palette; returns the summed squared error. */
    int MatchAlpha(const int (&values)[16], const int (&palette)[8], std::uint64_t& indices) noexcept
    {
      int error = 0;
      indices = 0U;
      for (int pixel = 0; pixel < 16; ++pixel) {
        int best = 0;
        int bestError = 1 << 30;
        for (int entry = 0; entry < 8; ++entry) {
          const int difference = values[pixel] - palette[entry];
          if (difference * difference < bestError) {
            bestError = difference * difference;
            best = entry;
          }
        }
        error += bestError;
        indices |= static_cast<std::uint64_t>(best) << (3 * pixel);
      }
      return error;
    }

    void EncodeAlphaValues(const int (&values)[16], std::uint8_t* const block) noexcept
    {
      int minimum = 255;
      int maximum = 0;
      int innerMinimum = 255;
      int innerMaximum = 0;
      bool hasExtremes = false;
      for (const int value : values) {
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        if (value == 0 || value == 255) {
          hasExtremes = true;
        } else {
          innerMinimum = std::min(innerMinimum, value);
          innerMaximum = std::max(innerMaximum, value);
        }
      }
      std::memset(block, 0, 8);
      if (minimum == maximum) {
        block[0] = static_cast<std::uint8_t>(maximum);
        block[1] = static_cast<std::uint8_t>(minimum);
        return;
      }
      // Eight-value mode: a0 > a1.
      int palette[8];
      AlphaPalette(maximum, minimum, palette);
      std::uint64_t indices = 0U;
      const int error8 = MatchAlpha(values, palette, indices);
      int a0 = maximum;
      int a1 = minimum;
      // Six-value mode (a0 <= a1, plus exact 0 and 255) when the block holds 0 or 255.
      if (hasExtremes && error8 != 0) {
        const int low = (innerMinimum <= innerMaximum) ? innerMinimum : 0;
        const int high = (innerMinimum <= innerMaximum) ? innerMaximum : 0;
        int palette6[8];
        AlphaPalette(low, high, palette6);
        std::uint64_t indices6 = 0U;
        if (MatchAlpha(values, palette6, indices6) < error8) {
          a0 = low;
          a1 = high;
          indices = indices6;
        }
      }
      block[0] = static_cast<std::uint8_t>(a0);
      block[1] = static_cast<std::uint8_t>(a1);
      for (int byte = 0; byte < 6; ++byte) {
        block[2 + byte] = static_cast<std::uint8_t>((indices >> (8 * byte)) & 0xFFU);
      }
    }

    /**
     * Endpoints that reproduce one 8-bit value best through the 2/3 + 1/3 interpolated entry
     * of a four-colour block; ties go to the closer pair (interpolation differs a little between
     * decoders, less so for close endpoints).
     */
    struct SingleColourTable
    {
      std::uint8_t first[256];
      std::uint8_t second[256];

      explicit SingleColourTable(const int bits) noexcept
      {
        const int maximum = (1 << bits) - 1;
        for (int value = 0; value < 256; ++value) {
          int bestCost = 1 << 30;
          for (int e0 = 0; e0 <= maximum; ++e0) {
            const int x0 = (bits == 5) ? Expand5(e0) : Expand6(e0);
            for (int e1 = 0; e1 <= maximum; ++e1) {
              const int x1 = (bits == 5) ? Expand5(e1) : Expand6(e1);
              const int interpolated = (2 * x0 + x1 + 1) / 3;
              const int cost = std::abs(interpolated - value) * 1024 + std::abs(x0 - x1);
              if (cost < bestCost) {
                bestCost = cost;
                first[value] = static_cast<std::uint8_t>(e0);
                second[value] = static_cast<std::uint8_t>(e1);
              }
            }
          }
        }
      }
    };

    const SingleColourTable& Table5()
    {
      static const SingleColourTable table(5);
      return table;
    }

    const SingleColourTable& Table6()
    {
      static const SingleColourTable table(6);
      return table;
    }

    /**
     * What a colour block is fitted to: the pixels in `opaqueMask` take one of the colours (the
     * rest are transparent black, 3-colour mode), each counted with its `weight` (BC2/BC3: the
     * pixel's alpha, so the colour of a transparent pixel does not pull the endpoints).
     */
    struct ColourInput
    {
      const std::uint8_t* rgba = nullptr;
      std::uint32_t opaqueMask = 0xFFFFU;
      float weight[16] = {};
    };

    /** Nearest-colour index for every pixel in the mask; returns the weighted squared error. */
    float MatchColours(const ColourInput& input, const std::uint32_t c0, const std::uint32_t c1, const bool fourColour, std::uint32_t& indices) noexcept
    {
      Rgb palette[4];
      Palette(c0, c1, fourColour, palette);
      const int entries = fourColour ? 4 : 3;
      float error = 0.0F;
      indices = 0U;
      for (int pixel = 0; pixel < 16; ++pixel) {
        if ((input.opaqueMask & (1U << pixel)) == 0U) {
          indices |= 3U << (2 * pixel);
          continue;
        }
        const std::uint8_t* const p = input.rgba + pixel * 4;
        int best = 0;
        int bestError = 1 << 30;
        for (int entry = 0; entry < entries; ++entry) {
          const int dr = p[0] - palette[entry].r;
          const int dg = p[1] - palette[entry].g;
          const int db = p[2] - palette[entry].b;
          const int distance = dr * dr + dg * dg + db * db;
          if (distance < bestError) {
            bestError = distance;
            best = entry;
          }
        }
        error += input.weight[pixel] * static_cast<float>(bestError);
        indices |= static_cast<std::uint32_t>(best) << (2 * pixel);
      }
      return error;
    }

    /** Weighted least-squares endpoints for fixed indices; false when the system is singular. */
    bool RefineEndpoints(const ColourInput& input, const std::uint32_t indices, const bool fourColour, std::uint32_t& c0, std::uint32_t& c1) noexcept
    {
      static constexpr float kWeights4[4] = {1.0F, 0.0F, 2.0F / 3.0F, 1.0F / 3.0F};
      static constexpr float kWeights3[4] = {1.0F, 0.0F, 0.5F, 0.0F};
      const float* const mix = fourColour ? kWeights4 : kWeights3;
      float aa = 0.0F;
      float ab = 0.0F;
      float bb = 0.0F;
      float ax[3] = {0.0F, 0.0F, 0.0F};
      float bx[3] = {0.0F, 0.0F, 0.0F};
      for (int pixel = 0; pixel < 16; ++pixel) {
        const float weight = input.weight[pixel];
        if ((input.opaqueMask & (1U << pixel)) == 0U || weight <= 0.0F) {
          continue;
        }
        const float w = mix[(indices >> (2 * pixel)) & 3U];
        const float v = 1.0F - w;
        aa += weight * w * w;
        ab += weight * w * v;
        bb += weight * v * v;
        for (int channel = 0; channel < 3; ++channel) {
          const float x = static_cast<float>(input.rgba[pixel * 4 + channel]);
          ax[channel] += weight * w * x;
          bx[channel] += weight * v * x;
        }
      }
      const float determinant = aa * bb - ab * ab;
      if (determinant < 1e-6F && determinant > -1e-6F) {
        return false;
      }
      float first[3];
      float second[3];
      for (int channel = 0; channel < 3; ++channel) {
        first[channel] = (ax[channel] * bb - bx[channel] * ab) / determinant;
        second[channel] = (bx[channel] * aa - ax[channel] * ab) / determinant;
      }
      c0 = Pack565(first[0], first[1], first[2]);
      c1 = Pack565(second[0], second[1], second[2]);
      return true;
    }

    /** Principal-axis endpoints: the extreme pixels (weight > 0) along the weighted principal axis. */
    void PrincipalEndpoints(const ColourInput& input, std::uint32_t& c0, std::uint32_t& c1) noexcept
    {
      float mean[3] = {0.0F, 0.0F, 0.0F};
      int minimum[3] = {255, 255, 255};
      int maximum[3] = {0, 0, 0};
      float total = 0.0F;
      for (int pixel = 0; pixel < 16; ++pixel) {
        const float weight = input.weight[pixel];
        if ((input.opaqueMask & (1U << pixel)) == 0U || weight <= 0.0F) {
          continue;
        }
        total += weight;
        for (int channel = 0; channel < 3; ++channel) {
          const int value = input.rgba[pixel * 4 + channel];
          mean[channel] += weight * static_cast<float>(value);
          minimum[channel] = std::min(minimum[channel], value);
          maximum[channel] = std::max(maximum[channel], value);
        }
      }
      for (float& value : mean) {
        value /= total;
      }
      float covariance[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}; // rr rg rb gg gb bb
      for (int pixel = 0; pixel < 16; ++pixel) {
        const float weight = input.weight[pixel];
        if ((input.opaqueMask & (1U << pixel)) == 0U || weight <= 0.0F) {
          continue;
        }
        const float r = static_cast<float>(input.rgba[pixel * 4 + 0]) - mean[0];
        const float g = static_cast<float>(input.rgba[pixel * 4 + 1]) - mean[1];
        const float b = static_cast<float>(input.rgba[pixel * 4 + 2]) - mean[2];
        covariance[0] += weight * r * r;
        covariance[1] += weight * r * g;
        covariance[2] += weight * r * b;
        covariance[3] += weight * g * g;
        covariance[4] += weight * g * b;
        covariance[5] += weight * b * b;
      }
      float axis[3] = {static_cast<float>(maximum[0] - minimum[0]), static_cast<float>(maximum[1] - minimum[1]),
                       static_cast<float>(maximum[2] - minimum[2])};
      for (int iteration = 0; iteration < 6; ++iteration) {
        const float x = axis[0] * covariance[0] + axis[1] * covariance[1] + axis[2] * covariance[2];
        const float y = axis[0] * covariance[1] + axis[1] * covariance[3] + axis[2] * covariance[4];
        const float z = axis[0] * covariance[2] + axis[1] * covariance[4] + axis[2] * covariance[5];
        const float largest = std::max({std::abs(x), std::abs(y), std::abs(z)});
        if (largest < 1e-6F) {
          break;
        }
        axis[0] = x / largest;
        axis[1] = y / largest;
        axis[2] = z / largest;
      }
      if (std::abs(axis[0]) + std::abs(axis[1]) + std::abs(axis[2]) < 1e-6F) {
        axis[0] = 0.299F;
        axis[1] = 0.587F;
        axis[2] = 0.114F;
      }
      float lowest = 1e30F;
      float highest = -1e30F;
      int lowPixel = 0;
      int highPixel = 0;
      for (int pixel = 0; pixel < 16; ++pixel) {
        if ((input.opaqueMask & (1U << pixel)) == 0U || input.weight[pixel] <= 0.0F) {
          continue;
        }
        const std::uint8_t* const p = input.rgba + pixel * 4;
        const float projection = static_cast<float>(p[0]) * axis[0] + static_cast<float>(p[1]) * axis[1] + static_cast<float>(p[2]) * axis[2];
        if (projection < lowest) {
          lowest = projection;
          lowPixel = pixel;
        }
        if (projection > highest) {
          highest = projection;
          highPixel = pixel;
        }
      }
      const std::uint8_t* const high = input.rgba + highPixel * 4;
      const std::uint8_t* const low = input.rgba + lowPixel * 4;
      c0 = Pack565(high[0], high[1], high[2]);
      c1 = Pack565(low[0], low[1], low[2]);
    }

    /** Colour block (8 bytes) for `input`; all weights 0 counts every pixel alike. */
    void EncodeColourBlock(ColourInput input, std::uint8_t* const block) noexcept
    {
      const std::uint32_t opaqueMask = input.opaqueMask;
      const bool fourColour = opaqueMask == 0xFFFFU;
      if (opaqueMask == 0U) {
        Write16(block, 0U);
        Write16(block + 2, 0U);
        Write32(block + 4, 0xFFFFFFFFU);
        return;
      }
      bool anyWeight = false;
      for (int pixel = 0; pixel < 16; ++pixel) {
        anyWeight = anyWeight || ((opaqueMask & (1U << pixel)) != 0U && input.weight[pixel] > 0.0F);
      }
      if (!anyWeight) {
        std::fill(std::begin(input.weight), std::end(input.weight), 1.0F);
      }
      const std::uint8_t* const rgba = input.rgba;

      bool solid = true;
      int first = -1;
      for (int pixel = 0; pixel < 16 && solid; ++pixel) {
        if ((opaqueMask & (1U << pixel)) == 0U || input.weight[pixel] <= 0.0F) {
          continue;
        }
        if (first < 0) {
          first = pixel;
        } else if (std::memcmp(rgba + pixel * 4, rgba + first * 4, 3) != 0) {
          solid = false;
        }
      }

      std::uint32_t c0 = 0U;
      std::uint32_t c1 = 0U;
      std::uint32_t indices = 0U;
      if (solid && fourColour) {
        const std::uint8_t* const p = rgba + first * 4;
        c0 = (static_cast<std::uint32_t>(Table5().first[p[0]]) << 11U) | (static_cast<std::uint32_t>(Table6().first[p[1]]) << 5U) |
          Table5().first[p[2]];
        c1 = (static_cast<std::uint32_t>(Table5().second[p[0]]) << 11U) | (static_cast<std::uint32_t>(Table6().second[p[1]]) << 5U) |
          Table5().second[p[2]];
        indices = 0xAAAAAAAAU; // entry 2: 2/3 c0 + 1/3 c1
        if (c0 < c1) {
          std::swap(c0, c1);
          indices = 0xFFFFFFFFU; // entry 3 of the swapped pair is the same colour
        }
      } else {
        PrincipalEndpoints(input, c0, c1);
        float error = MatchColours(input, c0, c1, fourColour, indices);
        for (int iteration = 0; iteration < 2 && error > 0.0F; ++iteration) {
          std::uint32_t r0 = 0U;
          std::uint32_t r1 = 0U;
          if (!RefineEndpoints(input, indices, fourColour, r0, r1) || (r0 == c0 && r1 == c1)) {
            break;
          }
          std::uint32_t refined = 0U;
          const float refinedError = MatchColours(input, r0, r1, fourColour, refined);
          if (refinedError >= error) {
            break;
          }
          c0 = r0;
          c1 = r1;
          indices = refined;
          error = refinedError;
        }
        if (fourColour) {
          if (c0 < c1) {
            std::swap(c0, c1);
            indices ^= 0x55555555U; // 0<->1, 2<->3
          }
        } else if (c0 > c1) {
          // Three-colour mode needs c0 <= c1: swap the endpoints and indices 0 and 1.
          std::swap(c0, c1);
          std::uint32_t swapped = 0U;
          for (int pixel = 0; pixel < 16; ++pixel) {
            std::uint32_t index = (indices >> (2 * pixel)) & 3U;
            if (index < 2U) {
              index ^= 1U;
            }
            swapped |= index << (2 * pixel);
          }
          indices = swapped;
        }
      }
      if (fourColour && c0 == c1) {
        indices = 0U; // equal endpoints read as 3-colour mode: entry 3 would be transparent
      }
      Write16(block, c0);
      Write16(block + 2, c1);
      Write32(block + 4, indices);
    }

    /** The colour input of a BC2/BC3 block: every pixel takes a colour, weighted by its alpha. */
    ColourInput AlphaWeighted(const std::uint8_t* const rgba) noexcept
    {
      ColourInput input;
      input.rgba = rgba;
      input.opaqueMask = 0xFFFFU;
      for (int pixel = 0; pixel < 16; ++pixel) {
        input.weight[pixel] = static_cast<float>(rgba[pixel * 4 + 3]) / 255.0F;
      }
      return input;
    }
  } // namespace

  void DecodeBc1Block(const std::uint8_t* const block, std::uint8_t* const rgba) noexcept
  {
    DecodeColourBlock(block, rgba, false);
  }

  void DecodeBc2Block(const std::uint8_t* const block, std::uint8_t* const rgba) noexcept
  {
    DecodeColourBlock(block + 8, rgba, true);
    for (int pixel = 0; pixel < 16; ++pixel) {
      const int nibble = (block[pixel / 2] >> ((pixel & 1) * 4)) & 0xF;
      rgba[pixel * 4 + 3] = static_cast<std::uint8_t>(nibble * 17);
    }
  }

  void DecodeBc3Block(const std::uint8_t* const block, std::uint8_t* const rgba) noexcept
  {
    DecodeColourBlock(block + 8, rgba, true);
    DecodeAlphaBlock(block, rgba + 3, 4);
  }

  void DecodeBc4Block(const std::uint8_t* const block, std::uint8_t* const rgba, const int channel) noexcept
  {
    DecodeAlphaBlock(block, rgba + channel, 4);
  }

  void EncodeBc1Block(const std::uint8_t* const rgba, std::uint8_t* const block, const bool allowTransparent) noexcept
  {
    ColourInput input;
    input.rgba = rgba;
    input.opaqueMask = 0xFFFFU;
    for (int pixel = 0; pixel < 16; ++pixel) {
      input.weight[pixel] = 1.0F;
      if (allowTransparent && rgba[pixel * 4 + 3] < 128U) {
        input.opaqueMask &= ~(1U << pixel);
      }
    }
    EncodeColourBlock(input, block);
  }

  void EncodeBc2Block(const std::uint8_t* const rgba, std::uint8_t* const block) noexcept
  {
    std::memset(block, 0, 8);
    for (int pixel = 0; pixel < 16; ++pixel) {
      const int nibble = (rgba[pixel * 4 + 3] * 15 + 127) / 255;
      block[pixel / 2] = static_cast<std::uint8_t>(block[pixel / 2] | (nibble << ((pixel & 1) * 4)));
    }
    EncodeColourBlock(AlphaWeighted(rgba), block + 8);
  }

  void EncodeBc3Block(const std::uint8_t* const rgba, std::uint8_t* const block) noexcept
  {
    EncodeBc4Block(rgba, block, 3);
    EncodeColourBlock(AlphaWeighted(rgba), block + 8);
  }

  void EncodeBc4Block(const std::uint8_t* const rgba, std::uint8_t* const block, const int channel) noexcept
  {
    int values[16];
    for (int pixel = 0; pixel < 16; ++pixel) {
      values[pixel] = rgba[pixel * 4 + channel];
    }
    EncodeAlphaValues(values, block);
  }

  bool TranscodeBlockToBc3(const int sourceKind, const std::uint8_t* const source, std::uint8_t* const block) noexcept
  {
    switch (sourceKind) {
    case 3:
      std::memcpy(block, source, 16);
      return true;
    case 2: {
      int values[16];
      for (int pixel = 0; pixel < 16; ++pixel) {
        values[pixel] = ((source[pixel / 2] >> ((pixel & 1) * 4)) & 0xF) * 17;
      }
      EncodeAlphaValues(values, block);
      std::memcpy(block + 8, source + 8, 8);
      return true;
    }
    case 1: {
      const std::uint32_t c0 = Read16(source);
      const std::uint32_t c1 = Read16(source + 2);
      if (c0 <= c1) {
        // Three-colour mode: entry 2 is a midpoint and entry 3 transparent, unlike BC3.
        const std::uint32_t indices = Read32(source + 4);
        bool usesThirdOrFourth = false;
        for (int pixel = 0; pixel < 16; ++pixel) {
          usesThirdOrFourth = usesThirdOrFourth || ((indices >> (2 * pixel)) & 3U) >= 2U;
        }
        if (c0 != c1 || usesThirdOrFourth) {
          return false;
        }
      }
      std::memset(block, 0, 8);
      block[0] = 255U;
      block[1] = 255U;
      std::memcpy(block + 8, source, 8);
      return true;
    }
    default:
      return false;
    }
  }
} // namespace faf_posix::gal::image
