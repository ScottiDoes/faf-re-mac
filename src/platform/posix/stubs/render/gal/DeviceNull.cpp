// POSIX build (FAF_POSIX): the null gal device. See DeviceNull.h.

#include "platform/posix/stubs/render/gal/DeviceNull.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/CursorContext.hpp"
#include "gpg/gal/backends/common/BackendErrors.h"
#include "gpg/gal/backends/d3d11/D3D11VertexLayouts.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"

namespace gpg::gal
{
  using backend::ThrowError;

  namespace null_device
  {
    namespace
    {
      // ------------------------------------------------------------------------
      // Image headers

      [[nodiscard]] std::uint16_t Le16(const std::uint8_t* const p) noexcept
      {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
      }

      [[nodiscard]] std::uint32_t Le32(const std::uint8_t* const p) noexcept
      {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) | (static_cast<std::uint32_t>(p[2]) << 16U) |
          (static_cast<std::uint32_t>(p[3]) << 24U);
      }

      [[nodiscard]] std::uint16_t Be16(const std::uint8_t* const p) noexcept
      {
        return static_cast<std::uint16_t>((p[0] << 8U) | p[1]);
      }

      [[nodiscard]] std::uint32_t Be32(const std::uint8_t* const p) noexcept
      {
        return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) | (static_cast<std::uint32_t>(p[2]) << 8U) |
          static_cast<std::uint32_t>(p[3]);
      }

      constexpr std::uint32_t FourCC(const char a, const char b, const char c, const char d) noexcept
      {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
      }

      [[nodiscard]] std::uint32_t FullMipCount(std::uint32_t width, std::uint32_t height) noexcept
      {
        std::uint32_t levels = 1U;
        while (width > 1U || height > 1U) {
          width = std::max(width / 2U, 1U);
          height = std::max(height / 2U, 1U);
          ++levels;
        }
        return levels;
      }

      // The DDS pixel format D3DX hands back for a legacy (non-DX10) header.
      [[nodiscard]] DXGI_FORMAT LegacyDdsFormat(
        const std::uint32_t flags, const std::uint32_t fourCC, const std::uint32_t bitCount, const std::uint32_t redMask,
        const std::uint32_t greenMask, const std::uint32_t alphaMask
      ) noexcept
      {
        constexpr std::uint32_t kFourCC = 0x4U;
        constexpr std::uint32_t kRgb = 0x40U;
        constexpr std::uint32_t kLuminance = 0x20000U;
        constexpr std::uint32_t kAlpha = 0x2U;
        constexpr std::uint32_t kAlphaPixels = 0x1U;
        constexpr std::uint32_t kBumpDuDv = 0x80000U;
        if ((flags & kFourCC) != 0U) {
          switch (fourCC) {
          case FourCC('D', 'X', 'T', '1'):
            return DXGI_FORMAT_BC1_UNORM;
          case FourCC('D', 'X', 'T', '2'):
          case FourCC('D', 'X', 'T', '3'):
            return DXGI_FORMAT_BC2_UNORM;
          case FourCC('D', 'X', 'T', '4'):
          case FourCC('D', 'X', 'T', '5'):
            return DXGI_FORMAT_BC3_UNORM;
          case FourCC('A', 'T', 'I', '1'):
          case FourCC('B', 'C', '4', 'U'):
            return DXGI_FORMAT_BC4_UNORM;
          case FourCC('B', 'C', '4', 'S'):
            return DXGI_FORMAT_BC4_SNORM;
          case FourCC('A', 'T', 'I', '2'):
          case FourCC('B', 'C', '5', 'U'):
            return DXGI_FORMAT_BC5_UNORM;
          case FourCC('B', 'C', '5', 'S'):
            return DXGI_FORMAT_BC5_SNORM;
          case 36U: // D3DFMT_A16B16G16R16
            return DXGI_FORMAT_R16G16B16A16_UNORM;
          case 110U: // D3DFMT_Q16W16V16U16
            return DXGI_FORMAT_R16G16B16A16_SNORM;
          case 111U: // D3DFMT_R16F
            return DXGI_FORMAT_R16_FLOAT;
          case 112U: // D3DFMT_G16R16F
            return DXGI_FORMAT_R16G16_FLOAT;
          case 113U: // D3DFMT_A16B16G16R16F
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
          case 114U: // D3DFMT_R32F
            return DXGI_FORMAT_R32_FLOAT;
          case 115U: // D3DFMT_G32R32F
            return DXGI_FORMAT_R32G32_FLOAT;
          case 116U: // D3DFMT_A32B32G32R32F
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
          default:
            return DXGI_FORMAT_UNKNOWN;
          }
        }
        if ((flags & kRgb) != 0U) {
          switch (bitCount) {
          case 32U:
            if (redMask == 0x000000FFU) {
              return DXGI_FORMAT_R8G8B8A8_UNORM;
            }
            if (redMask == 0x0000FFFFU && greenMask == 0xFFFF0000U) {
              return DXGI_FORMAT_R16G16_UNORM;
            }
            if (redMask == 0x000003FFU) {
              return DXGI_FORMAT_R10G10B10A2_UNORM;
            }
            return ((flags & kAlphaPixels) != 0U && alphaMask != 0U) ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_B8G8R8X8_UNORM;
          case 24U:
            return DXGI_FORMAT_B8G8R8X8_UNORM; // D3DX widens R8G8B8 to X8R8G8B8
          case 16U:
            if (redMask == 0x7C00U) {
              return DXGI_FORMAT_B5G5R5A1_UNORM;
            }
            if (redMask == 0x0F00U) {
              return DXGI_FORMAT_B4G4R4A4_UNORM;
            }
            return DXGI_FORMAT_B5G6R5_UNORM;
          default:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
          }
        }
        if ((flags & kLuminance) != 0U) {
          if (bitCount == 16U) {
            return ((flags & kAlphaPixels) != 0U) ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R16_UNORM;
          }
          return DXGI_FORMAT_R8_UNORM;
        }
        if ((flags & kAlpha) != 0U) {
          return DXGI_FORMAT_A8_UNORM;
        }
        if ((flags & kBumpDuDv) != 0U) {
          return (bitCount == 16U) ? DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8B8A8_SNORM;
        }
        return DXGI_FORMAT_UNKNOWN;
      }

      bool ReadDds(const std::uint8_t* const p, const std::size_t bytes, ImageInfo& out)
      {
        if (bytes < 128U || std::memcmp(p, "DDS ", 4U) != 0) {
          return false;
        }
        const std::uint8_t* const header = p + 4U;
        if (Le32(header) != 124U) {
          return false;
        }
        constexpr std::uint32_t kMipMapCount = 0x20000U;
        constexpr std::uint32_t kDepth = 0x800000U;
        constexpr std::uint32_t kCubemap = 0x200U;
        constexpr std::uint32_t kVolume = 0x200000U;
        const std::uint32_t flags = Le32(header + 4U);
        const std::uint32_t height = Le32(header + 8U);
        const std::uint32_t width = Le32(header + 12U);
        const std::uint32_t depth = Le32(header + 20U);
        const std::uint32_t mips = Le32(header + 24U);
        const std::uint32_t pixelFlags = Le32(header + 76U);
        const std::uint32_t fourCC = Le32(header + 80U);
        const std::uint32_t bitCount = Le32(header + 84U);
        const std::uint32_t redMask = Le32(header + 88U);
        const std::uint32_t greenMask = Le32(header + 92U);
        const std::uint32_t alphaMask = Le32(header + 100U);
        const std::uint32_t caps2 = Le32(header + 108U);
        if (width == 0U || height == 0U) {
          return false;
        }

        ImageInfo info;
        info.width = width;
        info.height = height;
        info.mipLevels = ((flags & kMipMapCount) != 0U && mips != 0U) ? mips : 1U;
        info.cube = (caps2 & kCubemap) != 0U;
        info.volume = (caps2 & kVolume) != 0U && (flags & kDepth) != 0U && depth > 1U;
        info.depth = info.volume ? depth : 1U;
        info.arraySize = info.cube ? 6U : 1U;

        if ((pixelFlags & 0x4U) != 0U && fourCC == FourCC('D', 'X', '1', '0')) {
          if (bytes < 148U) {
            return false;
          }
          const std::uint8_t* const dx10 = p + 128U;
          info.format = static_cast<DXGI_FORMAT>(Le32(dx10));
          const std::uint32_t dimension = Le32(dx10 + 4U);
          const std::uint32_t misc = Le32(dx10 + 8U);
          const std::uint32_t arraySize = std::max(Le32(dx10 + 12U), 1U);
          info.cube = (misc & 0x4U) != 0U;
          info.volume = dimension == 4U;
          info.depth = info.volume ? std::max(depth, 1U) : 1U;
          info.arraySize = info.volume ? 1U : arraySize * (info.cube ? 6U : 1U);
        } else {
          info.format = LegacyDdsFormat(pixelFlags, fourCC, bitCount, redMask, greenMask, alphaMask);
        }
        if (info.format == DXGI_FORMAT_UNKNOWN) {
          return false;
        }
        info.mipLevels = std::min(info.mipLevels, FullMipCount(info.width, info.height));
        out = info;
        return true;
      }

      bool ReadPng(const std::uint8_t* const p, const std::size_t bytes, std::uint32_t& width, std::uint32_t& height)
      {
        static constexpr std::uint8_t kSignature[8] = {0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
        if (bytes < 24U || std::memcmp(p, kSignature, sizeof(kSignature)) != 0 || std::memcmp(p + 12U, "IHDR", 4U) != 0) {
          return false;
        }
        width = Be32(p + 16U);
        height = Be32(p + 20U);
        return true;
      }

      bool ReadJpeg(const std::uint8_t* const p, const std::size_t bytes, std::uint32_t& width, std::uint32_t& height)
      {
        if (bytes < 4U || p[0] != 0xFFU || p[1] != 0xD8U) {
          return false;
        }
        std::size_t at = 2U;
        while (at + 4U <= bytes) {
          if (p[at] != 0xFFU) {
            return false;
          }
          while (at < bytes && p[at] == 0xFFU) {
            ++at; // fill bytes
          }
          if (at >= bytes) {
            return false;
          }
          const std::uint8_t marker = p[at++];
          if (marker == 0xD8U || marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U)) {
            continue; // no length
          }
          if (marker == 0xD9U || marker == 0xDAU || at + 2U > bytes) {
            return false; // end of image or start of scan before any frame header
          }
          const std::size_t length = Be16(p + at);
          const bool frame = marker >= 0xC0U && marker <= 0xCFU && marker != 0xC4U && marker != 0xC8U && marker != 0xCCU;
          if (frame) {
            if (length < 7U || at + 7U > bytes) {
              return false;
            }
            height = Be16(p + at + 3U);
            width = Be16(p + at + 5U);
            return true;
          }
          if (length < 2U) {
            return false;
          }
          at += length;
        }
        return false;
      }

      bool ReadBmp(const std::uint8_t* const p, const std::size_t bytes, std::uint32_t& width, std::uint32_t& height)
      {
        if (bytes < 26U || p[0] != 'B' || p[1] != 'M') {
          return false;
        }
        const std::uint32_t headerSize = Le32(p + 14U);
        if (headerSize == 12U) {
          width = Le16(p + 18U);
          height = Le16(p + 20U);
          return true;
        }
        if (headerSize < 40U) {
          return false;
        }
        const auto signedWidth = static_cast<std::int32_t>(Le32(p + 18U));
        const auto signedHeight = static_cast<std::int32_t>(Le32(p + 22U));
        if (signedWidth <= 0 || signedHeight == 0 || signedHeight == INT32_MIN) {
          return false;
        }
        width = static_cast<std::uint32_t>(signedWidth);
        height = static_cast<std::uint32_t>((signedHeight < 0) ? -signedHeight : signedHeight);
        return true;
      }

      bool ReadTga(const std::uint8_t* const p, const std::size_t bytes, std::uint32_t& width, std::uint32_t& height)
      {
        if (bytes < 18U) {
          return false;
        }
        const std::uint8_t colorMapType = p[1];
        const std::uint8_t imageType = p[2];
        const std::uint8_t bitsPerPixel = p[16];
        const bool knownType = imageType == 1U || imageType == 2U || imageType == 3U || imageType == 9U || imageType == 10U || imageType == 11U;
        const bool knownDepth = bitsPerPixel == 8U || bitsPerPixel == 15U || bitsPerPixel == 16U || bitsPerPixel == 24U || bitsPerPixel == 32U;
        if (colorMapType > 1U || !knownType || !knownDepth) {
          return false;
        }
        width = Le16(p + 12U);
        height = Le16(p + 14U);
        return true;
      }
    } // namespace

    bool ReadImageInfo(const void* const data, const std::size_t bytes, ImageInfo& out)
    {
      if (data == nullptr || bytes == 0U) {
        return false;
      }
      const auto* const p = static_cast<const std::uint8_t*>(data);
      if (ReadDds(p, bytes, out)) {
        return true;
      }
      std::uint32_t width = 0U;
      std::uint32_t height = 0U;
      // TGA has no signature: tried last.
      if (!ReadPng(p, bytes, width, height) && !ReadJpeg(p, bytes, width, height) && !ReadBmp(p, bytes, width, height) &&
          !ReadTga(p, bytes, width, height)) {
        return false;
      }
      if (width == 0U || height == 0U || width > 0x10000U || height > 0x10000U) {
        return false;
      }
      ImageInfo info;
      info.format = DXGI_FORMAT_B8G8R8A8_UNORM;
      info.width = width;
      info.height = height;
      info.mipLevels = FullMipCount(width, height);
      out = info;
      return true;
    }

    LevelLayout ComputeLevelLayout(const DXGI_FORMAT format, const std::uint32_t galFormat, const std::uint32_t width, const std::uint32_t height) noexcept
    {
      LevelLayout layout;
      if (format != DXGI_FORMAT_UNKNOWN && dxgi::FormatElementBytes(format) != 0U) {
        const dxgi::LevelLayout dxgiLayout = dxgi::ComputeLevelLayout(format, width, height);
        layout.rowPitch = dxgiLayout.rowPitch;
        layout.rowCount = dxgiLayout.rowCount;
        return layout;
      }
      const std::uint32_t texelBytes = (galFormat == 1U) ? 3U : 4U; // gal 1 is D3DFMT_R8G8B8
      layout.rowPitch = std::max(width, 1U) * texelBytes;
      layout.rowCount = std::max(height, 1U);
      return layout;
    }

    // --------------------------------------------------------------------------
    // Effect source scan

    namespace
    {
      [[nodiscard]] bool IsIdentifierStart(const char c) noexcept
      {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
      }

      [[nodiscard]] bool IsIdentifierChar(const char c) noexcept
      {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
      }

      [[nodiscard]] bool EqualsNoCase(const std::string_view a, const std::string_view b) noexcept
      {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
                 return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
      }

      [[nodiscard]] std::string_view Trim(std::string_view text) noexcept
      {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
          text.remove_prefix(1U);
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
          text.remove_suffix(1U);
        }
        return text;
      }

      // Index just past the string literal that opens at `at` (a '"').
      [[nodiscard]] std::size_t SkipString(const std::string_view text, std::size_t at) noexcept
      {
        for (++at; at < text.size(); ++at) {
          if (text[at] == '\\') {
            ++at;
          } else if (text[at] == '"' || text[at] == '\n') {
            return at + 1U;
          }
        }
        return text.size();
      }

      // Comments become spaces (newlines stay, so lines keep their numbers); strings stay.
      [[nodiscard]] std::string StripComments(const std::string_view source)
      {
        std::string out;
        out.reserve(source.size());
        std::size_t at = 0U;
        while (at < source.size()) {
          const char c = source[at];
          if (c == '"') {
            const std::size_t end = SkipString(source, at);
            out.append(source.substr(at, end - at));
            at = end;
          } else if (c == '/' && at + 1U < source.size() && source[at + 1U] == '/') {
            while (at < source.size() && source[at] != '\n') {
              ++at;
            }
          } else if (c == '/' && at + 1U < source.size() && source[at + 1U] == '*') {
            at += 2U;
            out.push_back(' ');
            while (at < source.size() && !(source[at] == '*' && at + 1U < source.size() && source[at + 1U] == '/')) {
              if (source[at] == '\n') {
                out.push_back('\n');
              }
              ++at;
            }
            at = std::min(at + 2U, source.size());
          } else {
            out.push_back(c);
            ++at;
          }
        }
        return out;
      }

      struct Macros
      {
        std::unordered_map<std::string, std::string> values; // object-like
        std::unordered_set<std::string> defined;             // object- and function-like
      };

      // Replaces object-like macros, recursively (a macro is not expanded inside itself).
      void Expand(const std::string_view text, const Macros& macros, std::string& out, std::vector<std::string>& active)
      {
        std::size_t at = 0U;
        while (at < text.size()) {
          const char c = text[at];
          if (c == '"') {
            const std::size_t end = SkipString(text, at);
            out.append(text.substr(at, end - at));
            at = end;
            continue;
          }
          if (IsIdentifierStart(c)) {
            std::size_t end = at + 1U;
            while (end < text.size() && IsIdentifierChar(text[end])) {
              ++end;
            }
            const std::string name(text.substr(at, end - at));
            const auto found = macros.values.find(name);
            if (found != macros.values.end() && active.size() < 32U && std::find(active.begin(), active.end(), name) == active.end()) {
              active.push_back(name);
              out.push_back(' ');
              Expand(found->second, macros, out, active);
              out.push_back(' ');
              active.pop_back();
            } else {
              out.append(name);
            }
            at = end;
            continue;
          }
          if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            // A number with its suffix ("0x10", "1.5f", "2e-3"): never a macro name.
            std::size_t end = at + 1U;
            while (end < text.size() && (IsIdentifierChar(text[end]) || text[end] == '.' ||
                                         ((text[end] == '+' || text[end] == '-') && (text[end - 1U] == 'e' || text[end - 1U] == 'E')))) {
              ++end;
            }
            out.append(text.substr(at, end - at));
            at = end;
            continue;
          }
          out.push_back(c);
          ++at;
        }
      }

      [[nodiscard]] std::string Expand(const std::string_view text, const Macros& macros)
      {
        std::string out;
        std::vector<std::string> active;
        Expand(text, macros, out, active);
        return out;
      }

      /**
       * Constant expressions of preprocessor conditions and annotation values: numbers
       * (decimal, hex, octal, floating point, with C suffixes), true/false, parentheses,
       * ?:, || && | ^ & == != < > <= >= << >> + - * / % and unary ! ~ - +. Identifiers
       * left after macro expansion count as 0 in a condition, as in C, and fail a value.
       */
      class ConstantExpression
      {
      public:
        ConstantExpression(const std::string_view text, const bool unknownIsZero)
          : mText(text)
          , mUnknownIsZero(unknownIsZero)
        {}

        [[nodiscard]] bool Evaluate(double& value)
        {
          mAt = 0U;
          mOk = true;
          const double result = Conditional();
          SkipSpace();
          if (!mOk || mAt != mText.size()) {
            return false;
          }
          value = result;
          return true;
        }

      private:
        void SkipSpace() noexcept
        {
          while (mAt < mText.size() && std::isspace(static_cast<unsigned char>(mText[mAt])) != 0) {
            ++mAt;
          }
        }

        // Consumes `op` unless it is the start of a longer operator in `longer`.
        [[nodiscard]] bool Eat(const std::string_view op, const std::string_view longer = {}) noexcept
        {
          SkipSpace();
          if (mText.substr(mAt, op.size()) != op) {
            return false;
          }
          if (!longer.empty() && mText.substr(mAt, longer.size()) == longer) {
            return false;
          }
          mAt += op.size();
          return true;
        }

        [[nodiscard]] static std::int64_t Int(const double value) noexcept
        {
          return std::isfinite(value) ? static_cast<std::int64_t>(value) : 0;
        }

        double Conditional()
        {
          const double condition = LogicalOr();
          if (Eat("?")) {
            const double whenTrue = Conditional();
            if (!Eat(":")) {
              mOk = false;
              return 0.0;
            }
            const double whenFalse = Conditional();
            return (condition != 0.0) ? whenTrue : whenFalse;
          }
          return condition;
        }

        double LogicalOr()
        {
          double value = LogicalAnd();
          while (mOk && Eat("||")) {
            const double right = LogicalAnd();
            value = (value != 0.0 || right != 0.0) ? 1.0 : 0.0;
          }
          return value;
        }

        double LogicalAnd()
        {
          double value = BitOr();
          while (mOk && Eat("&&")) {
            const double right = BitOr();
            value = (value != 0.0 && right != 0.0) ? 1.0 : 0.0;
          }
          return value;
        }

        double BitOr()
        {
          double value = BitXor();
          while (mOk && Eat("|", "||")) {
            value = static_cast<double>(Int(value) | Int(BitXor()));
          }
          return value;
        }

        double BitXor()
        {
          double value = BitAnd();
          while (mOk && Eat("^")) {
            value = static_cast<double>(Int(value) ^ Int(BitAnd()));
          }
          return value;
        }

        double BitAnd()
        {
          double value = Equality();
          while (mOk && Eat("&", "&&")) {
            value = static_cast<double>(Int(value) & Int(Equality()));
          }
          return value;
        }

        double Equality()
        {
          double value = Relational();
          for (;;) {
            if (Eat("==")) {
              value = (value == Relational()) ? 1.0 : 0.0;
            } else if (Eat("!=")) {
              value = (value != Relational()) ? 1.0 : 0.0;
            } else {
              return value;
            }
          }
        }

        double Relational()
        {
          double value = Shift();
          for (;;) {
            if (Eat("<=")) {
              value = (value <= Shift()) ? 1.0 : 0.0;
            } else if (Eat(">=")) {
              value = (value >= Shift()) ? 1.0 : 0.0;
            } else if (Eat("<", "<<")) {
              value = (value < Shift()) ? 1.0 : 0.0;
            } else if (Eat(">", ">>")) {
              value = (value > Shift()) ? 1.0 : 0.0;
            } else {
              return value;
            }
          }
        }

        double Shift()
        {
          double value = Additive();
          for (;;) {
            if (Eat("<<")) {
              const std::int64_t by = Int(Additive());
              value = (by >= 0 && by < 63) ? static_cast<double>(Int(value) << by) : 0.0;
            } else if (Eat(">>")) {
              const std::int64_t by = Int(Additive());
              value = (by >= 0 && by < 63) ? static_cast<double>(Int(value) >> by) : 0.0;
            } else {
              return value;
            }
          }
        }

        double Additive()
        {
          double value = Multiplicative();
          for (;;) {
            if (Eat("+")) {
              value += Multiplicative();
            } else if (Eat("-")) {
              value -= Multiplicative();
            } else {
              return value;
            }
          }
        }

        double Multiplicative()
        {
          double value = Unary();
          for (;;) {
            if (Eat("*")) {
              value *= Unary();
            } else if (Eat("/")) {
              const double divisor = Unary();
              if (divisor == 0.0) {
                mOk = false;
                return 0.0;
              }
              value /= divisor;
            } else if (Eat("%")) {
              const std::int64_t divisor = Int(Unary());
              if (divisor == 0) {
                mOk = false;
                return 0.0;
              }
              value = static_cast<double>(Int(value) % divisor);
            } else {
              return value;
            }
          }
        }

        double Unary()
        {
          if (Eat("!")) {
            return (Unary() == 0.0) ? 1.0 : 0.0;
          }
          if (Eat("~")) {
            return static_cast<double>(~Int(Unary()));
          }
          if (Eat("-")) {
            return -Unary();
          }
          if (Eat("+")) {
            return Unary();
          }
          return Primary();
        }

        double Primary()
        {
          SkipSpace();
          if (mAt >= mText.size()) {
            mOk = false;
            return 0.0;
          }
          if (Eat("(")) {
            const double value = Conditional();
            if (!Eat(")")) {
              mOk = false;
            }
            return value;
          }
          const char c = mText[mAt];
          if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && mAt + 1U < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mAt + 1U])) != 0)) {
            return Number();
          }
          if (IsIdentifierStart(c)) {
            const std::size_t begin = mAt;
            while (mAt < mText.size() && IsIdentifierChar(mText[mAt])) {
              ++mAt;
            }
            const std::string_view name = mText.substr(begin, mAt - begin);
            if (name == "true" || name == "TRUE") {
              return 1.0;
            }
            if (name == "false" || name == "FALSE") {
              return 0.0;
            }
            if (!mUnknownIsZero) {
              mOk = false;
            }
            return 0.0;
          }
          mOk = false;
          return 0.0;
        }

        double Number()
        {
          const std::size_t begin = mAt;
          double value = 0.0;
          if (mText[mAt] == '0' && mAt + 1U < mText.size() && (mText[mAt + 1U] == 'x' || mText[mAt + 1U] == 'X')) {
            mAt += 2U;
            std::uint64_t bits = 0U;
            while (mAt < mText.size() && std::isxdigit(static_cast<unsigned char>(mText[mAt])) != 0) {
              const char digit = static_cast<char>(std::tolower(static_cast<unsigned char>(mText[mAt])));
              bits = bits * 16U + static_cast<std::uint64_t>((digit >= 'a') ? digit - 'a' + 10 : digit - '0');
              ++mAt;
            }
            value = static_cast<double>(bits);
          } else {
            bool floating = false;
            while (mAt < mText.size()) {
              const char c = mText[mAt];
              if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                ++mAt;
              } else if (c == '.') {
                floating = true;
                ++mAt;
              } else if ((c == 'e' || c == 'E') && mAt + 1U < mText.size() &&
                         (std::isdigit(static_cast<unsigned char>(mText[mAt + 1U])) != 0 || mText[mAt + 1U] == '+' || mText[mAt + 1U] == '-')) {
                floating = true;
                mAt += 2U;
              } else {
                break;
              }
            }
            const std::string digits(mText.substr(begin, mAt - begin));
            if (!floating && digits.size() > 1U && digits[0] == '0') {
              value = static_cast<double>(std::strtoull(digits.c_str(), nullptr, 8)); // C octal
            } else {
              value = std::strtod(digits.c_str(), nullptr);
            }
          }
          while (mAt < mText.size() && std::strchr("fFhHuUlL", mText[mAt]) != nullptr) {
            ++mAt; // suffixes
          }
          if (mAt < mText.size() && IsIdentifierChar(mText[mAt])) {
            mOk = false; // "12abc"
          }
          return value;
        }

        std::string_view mText;
        bool mUnknownIsZero = false;
        std::size_t mAt = 0U;
        bool mOk = true;
      };

      // `defined(NAME)` / `defined NAME` become 1 or 0, then macros expand, then the rest evaluates.
      [[nodiscard]] bool EvaluateCondition(const std::string_view text, const Macros& macros)
      {
        std::string replaced;
        std::size_t at = 0U;
        while (at < text.size()) {
          if (IsIdentifierStart(text[at])) {
            std::size_t end = at + 1U;
            while (end < text.size() && IsIdentifierChar(text[end])) {
              ++end;
            }
            if (text.substr(at, end - at) == "defined") {
              std::size_t cursor = end;
              while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                ++cursor;
              }
              const bool parenthesised = cursor < text.size() && text[cursor] == '(';
              if (parenthesised) {
                ++cursor;
              }
              while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                ++cursor;
              }
              const std::size_t nameBegin = cursor;
              while (cursor < text.size() && IsIdentifierChar(text[cursor])) {
                ++cursor;
              }
              const std::string name(text.substr(nameBegin, cursor - nameBegin));
              if (parenthesised) {
                while (cursor < text.size() && text[cursor] != ')') {
                  ++cursor;
                }
                cursor = std::min(cursor + 1U, text.size());
              }
              replaced.append(macros.defined.count(name) != 0U ? " 1 " : " 0 ");
              at = cursor;
              continue;
            }
            replaced.append(text.substr(at, end - at));
            at = end;
            continue;
          }
          replaced.push_back(text[at]);
          ++at;
        }
        double value = 0.0;
        ConstantExpression expression(Expand(replaced, macros), true);
        return expression.Evaluate(value) && value != 0.0;
      }

      [[nodiscard]] std::string_view ReadIdentifier(const std::string_view text, std::size_t& at) noexcept
      {
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) {
          ++at;
        }
        const std::size_t begin = at;
        if (at < text.size() && IsIdentifierStart(text[at])) {
          while (at < text.size() && IsIdentifierChar(text[at])) {
            ++at;
          }
        }
        return text.substr(begin, at - begin);
      }

      /**
       * The source with every line of an inactive #if branch blanked and every directive
       * line removed (both kept as empty lines); `macros` ends up holding the object-like
       * #defines of the active branches.
       */
      [[nodiscard]] std::string Preprocess(const std::string_view source, Macros& macros)
      {
        struct Branch
        {
          bool parentActive = true;
          bool taken = false;
          bool active = true;
        };
        std::vector<Branch> branches;
        const auto active = [&] { return branches.empty() || branches.back().active; };

        std::string out;
        out.reserve(source.size());
        std::size_t at = 0U;
        while (at < source.size()) {
          // One logical line: physical lines joined by trailing backslashes.
          std::string line;
          std::size_t newlines = 0U;
          for (;;) {
            const std::size_t end = std::min(source.find('\n', at), source.size());
            std::string_view physical = source.substr(at, end - at);
            at = (end < source.size()) ? end + 1U : source.size();
            ++newlines;
            if (!physical.empty() && physical.back() == '\r') {
              physical.remove_suffix(1U);
            }
            if (!physical.empty() && physical.back() == '\\' && at < source.size()) {
              line.append(physical.substr(0U, physical.size() - 1U));
              line.push_back(' ');
              continue;
            }
            line.append(physical);
            break;
          }

          const std::string_view trimmed = Trim(line);
          if (!trimmed.empty() && trimmed.front() == '#') {
            std::size_t cursor = 1U;
            const std::string_view directive = ReadIdentifier(trimmed, cursor);
            const std::string_view rest = Trim(trimmed.substr(cursor));
            if (directive == "ifdef" || directive == "ifndef") {
              std::size_t nameAt = 0U;
              const std::string name(ReadIdentifier(rest, nameAt));
              const bool parent = active();
              const bool isDefined = macros.defined.count(name) != 0U;
              const bool condition = parent && ((directive == "ifdef") ? isDefined : !isDefined);
              branches.push_back(Branch{parent, condition, condition});
            } else if (directive == "if") {
              const bool parent = active();
              const bool condition = parent && EvaluateCondition(rest, macros);
              branches.push_back(Branch{parent, condition, condition});
            } else if (directive == "elif") {
              if (!branches.empty()) {
                Branch& branch = branches.back();
                const bool condition = branch.parentActive && !branch.taken && EvaluateCondition(rest, macros);
                branch.active = condition;
                branch.taken = branch.taken || condition;
              }
            } else if (directive == "else") {
              if (!branches.empty()) {
                Branch& branch = branches.back();
                branch.active = branch.parentActive && !branch.taken;
                branch.taken = true;
              }
            } else if (directive == "endif") {
              if (!branches.empty()) {
                branches.pop_back();
              }
            } else if (active() && directive == "define") {
              std::size_t nameEnd = 0U;
              const std::string name(ReadIdentifier(rest, nameEnd));
              if (!name.empty()) {
                macros.defined.insert(name);
                if (nameEnd < rest.size() && rest[nameEnd] == '(') {
                  macros.values.erase(name); // function-like: not expanded here
                } else {
                  macros.values[name] = std::string(Trim(rest.substr(nameEnd)));
                }
              }
            } else if (active() && directive == "undef") {
              std::size_t nameEnd = 0U;
              const std::string name(ReadIdentifier(rest, nameEnd));
              macros.defined.erase(name);
              macros.values.erase(name);
            }
            out.append(newlines, '\n');
            continue;
          }
          if (active()) {
            out.append(line);
            out.append(newlines, '\n');
          } else {
            out.append(newlines, '\n');
          }
        }
        return out;
      }

      // Index of the bracket closing the one at `open` (strings skipped), or npos.
      [[nodiscard]] std::size_t Matching(const std::string_view text, const std::size_t open, const char opening, const char closing) noexcept
      {
        int depth = 0;
        for (std::size_t at = open; at < text.size(); ++at) {
          const char c = text[at];
          if (c == '"') {
            at = SkipString(text, at) - 1U;
          } else if (c == opening) {
            ++depth;
          } else if (c == closing && --depth == 0) {
            return at;
          }
        }
        return std::string_view::npos;
      }

      // `type name = value;` statements of one annotation block.
      [[nodiscard]] effects::Annotations ParseAnnotations(const std::string_view block, const Macros& macros)
      {
        effects::Annotations annotations;
        std::size_t at = 0U;
        while (at < block.size()) {
          std::size_t end = at;
          while (end < block.size() && block[end] != ';') {
            end = (block[end] == '"') ? SkipString(block, end) : end + 1U;
          }
          const std::string_view statement = Trim(block.substr(at, std::min(end, block.size()) - at));
          at = std::min(end + 1U, block.size());

          std::size_t cursor = 0U;
          const std::string_view type = ReadIdentifier(statement, cursor);
          const std::string_view name = ReadIdentifier(statement, cursor);
          while (cursor < statement.size() && std::isspace(static_cast<unsigned char>(statement[cursor])) != 0) {
            ++cursor;
          }
          if (type.empty() || name.empty() || cursor >= statement.size() || statement[cursor] != '=') {
            continue;
          }
          const std::string value = Expand(statement.substr(cursor + 1U), macros);

          effects::Annotation annotation;
          annotation.name = std::string(name);
          if (EqualsNoCase(type, "string")) {
            // Adjacent literals concatenate, as in C.
            bool any = false;
            for (std::size_t scan = 0U; scan < value.size();) {
              if (value[scan] != '"') {
                ++scan;
                continue;
              }
              const std::size_t close = SkipString(value, scan);
              const std::size_t contentEnd = (close > scan + 1U && value[close - 1U] == '"') ? close - 1U : close;
              annotation.text.append(value, scan + 1U, contentEnd - scan - 1U);
              any = true;
              scan = close;
            }
            if (!any) {
              continue;
            }
            annotation.kind = effects::Annotation::Kind::String;
          } else {
            double number = 0.0;
            ConstantExpression expression(value, false);
            if (!expression.Evaluate(number)) {
              continue;
            }
            if (EqualsNoCase(type, "bool")) {
              annotation.kind = effects::Annotation::Kind::Bool;
              number = (number != 0.0) ? 1.0 : 0.0;
            } else if (EqualsNoCase(type, "int") || EqualsNoCase(type, "uint") || EqualsNoCase(type, "dword")) {
              annotation.kind = effects::Annotation::Kind::Int;
              number = std::trunc(number);
            } else if (EqualsNoCase(type, "float") || EqualsNoCase(type, "half") || EqualsNoCase(type, "double")) {
              annotation.kind = effects::Annotation::Kind::Float;
            } else {
              continue; // vectors, matrices, textures: not read through the technique interface
            }
            annotation.number = number;
          }
          annotations.push_back(std::move(annotation));
        }
        return annotations;
      }

      [[nodiscard]] bool IsTechniqueKeyword(const std::string_view word) noexcept
      {
        return EqualsNoCase(word, "technique") || EqualsNoCase(word, "technique10") || EqualsNoCase(word, "technique11");
      }

      // `pass` keywords directly inside a technique body.
      [[nodiscard]] int CountPasses(const std::string_view body) noexcept
      {
        int passes = 0;
        int depth = 0;
        for (std::size_t at = 0U; at < body.size();) {
          const char c = body[at];
          if (c == '"') {
            at = SkipString(body, at);
          } else if (c == '{') {
            ++depth;
            ++at;
          } else if (c == '}') {
            --depth;
            ++at;
          } else if (IsIdentifierStart(c)) {
            std::size_t end = at + 1U;
            while (end < body.size() && IsIdentifierChar(body[end])) {
              ++end;
            }
            if (depth == 0 && EqualsNoCase(body.substr(at, end - at), "pass")) {
              ++passes;
            }
            at = end;
          } else {
            ++at;
          }
        }
        return passes;
      }
    } // namespace

    std::vector<TechniqueInfo> ScanEffectTechniques(const std::string_view source, const msvc8::vector<EffectMacro>& predefined)
    {
      Macros macros;
      for (const EffectMacro& macro : predefined) {
        const std::string name(macro.keyText_.c_str());
        macros.defined.insert(name);
        macros.values[name] = macro.valueText_.c_str();
      }
      const std::string text = Preprocess(StripComments(source), macros);
      const std::string_view view(text);

      std::vector<TechniqueInfo> techniques;
      int depth = 0;
      std::size_t at = 0U;
      while (at < view.size()) {
        const char c = view[at];
        if (c == '"') {
          at = SkipString(view, at);
          continue;
        }
        if (c == '{') {
          ++depth;
          ++at;
          continue;
        }
        if (c == '}') {
          depth = std::max(depth - 1, 0);
          ++at;
          continue;
        }
        if (!IsIdentifierStart(c)) {
          ++at;
          continue;
        }
        std::size_t end = at + 1U;
        while (end < view.size() && IsIdentifierChar(view[end])) {
          ++end;
        }
        std::string word(view.substr(at, end - at));
        at = end;
        if (depth != 0) {
          continue;
        }
        // `#define technique technique10` (d3d10states.compat) and the like.
        if (const auto alias = macros.values.find(word); alias != macros.values.end() && IsTechniqueKeyword(Trim(alias->second))) {
          word = alias->second;
        }
        if (!IsTechniqueKeyword(word)) {
          continue;
        }

        TechniqueInfo technique;
        std::size_t cursor = at;
        technique.name = std::string(ReadIdentifier(view, cursor));
        while (cursor < view.size() && std::isspace(static_cast<unsigned char>(view[cursor])) != 0) {
          ++cursor;
        }
        if (cursor < view.size() && view[cursor] == '<') {
          std::size_t close = cursor + 1U;
          while (close < view.size() && view[close] != '>') {
            close = (view[close] == '"') ? SkipString(view, close) : close + 1U;
          }
          if (close >= view.size()) {
            break;
          }
          technique.annotations = ParseAnnotations(view.substr(cursor + 1U, close - cursor - 1U), macros);
          cursor = close + 1U;
          while (cursor < view.size() && std::isspace(static_cast<unsigned char>(view[cursor])) != 0) {
            ++cursor;
          }
        }
        if (cursor >= view.size() || view[cursor] != '{') {
          continue; // not a technique definition
        }
        const std::size_t close = Matching(view, cursor, '{', '}');
        if (close == std::string_view::npos) {
          break;
        }
        technique.passCount = CountPasses(view.substr(cursor + 1U, close - cursor - 1U));
        techniques.push_back(std::move(technique));
        at = close + 1U;
      }
      return techniques;
    }
  } // namespace null_device

  namespace
  {
    constexpr const char* kFile = "DeviceNull.cpp";
    constexpr std::uint32_t kDefaultHeadWidth = 1024U;
    constexpr std::uint32_t kDefaultHeadHeight = 768U;
    constexpr std::uint32_t kBackBufferFormat = 2U;   // gal B8G8R8A8
    constexpr std::uint32_t kDepthStencilFormat = 3U; // gal D24S8

    [[nodiscard]] null_device::ImageInfo ShapeOfEngineTexture(const TextureContext& context)
    {
      null_device::ImageInfo info;
      info.format = dxgi::TextureFormatToDxgi(context.format_);
      info.width = std::max(context.width_, 1U);
      info.height = std::max(context.height_, 1U);
      info.mipLevels = (context.mipmapLevels_ != 0U) ? context.mipmapLevels_ : 1U;
      return info;
    }

    // RECT of a level in texels, clipped to it; false when empty.
    [[nodiscard]] bool ClipRect(const RECT* const rect, const std::uint32_t width, const std::uint32_t height, RECT& out) noexcept
    {
      out = (rect != nullptr) ? *rect : RECT{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
      out.left = std::max<LONG>(out.left, 0);
      out.top = std::max<LONG>(out.top, 0);
      out.right = std::min<LONG>(out.right, static_cast<LONG>(width));
      out.bottom = std::min<LONG>(out.bottom, static_cast<LONG>(height));
      return out.left < out.right && out.top < out.bottom;
    }
  } // namespace

  // ---------------------------------------------------------------------------
  // TextureNull

  TextureNull::TextureNull(const TextureContext& context, const null_device::ImageInfo& info)
    : mContext(context)
    , mInfo(info)
  {
    mGalFormat = (info.format != DXGI_FORMAT_UNKNOWN) ? dxgi::DxgiToTextureFormat(info.format) : context.format_;
    mContext.ClearDataBuffer();
    mContext.width_ = mInfo.width;
    mContext.height_ = mInfo.height;
    mContext.mipmapLevels_ = mInfo.mipLevels;
    mContext.format_ = mGalFormat;
    mContext.type_ = mInfo.volume ? 3U : (mInfo.cube ? 2U : 1U);
    mLevels.resize(mInfo.mipLevels);
  }

  TextureNull::~TextureNull() = default;

  TextureContext* TextureNull::GetContext()
  {
    return &mContext;
  }

  null_device::LevelLayout TextureNull::LayoutOf(const std::uint32_t level) const noexcept
  {
    return null_device::ComputeLevelLayout(
      mInfo.format, mGalFormat, std::max(1U, mInfo.width >> level), std::max(1U, mInfo.height >> level)
    );
  }

  std::vector<std::uint8_t>& TextureNull::Level(const std::uint32_t level)
  {
    std::vector<std::uint8_t>& bytes = mLevels[level];
    const std::size_t size = LayoutOf(level).ByteSize();
    if (bytes.size() != size) {
      bytes.assign(size, 0U);
    }
    return bytes;
  }

  TextureLockRect TextureNull::Lock(const int level, const RECT& rect, const int flags)
  {
    if (mInfo.volume) {
      ThrowError(kFile, __LINE__, "attempt to map invalid texture");
    }
    if (level < 0 || level >= static_cast<int>(mInfo.mipLevels)) {
      ThrowError(kFile, __LINE__, "attempt to map invalid texture level");
    }
    if (mLocked) {
      ThrowError(kFile, __LINE__, "texture map/unmap mismatch");
    }
    const auto levelIndex = static_cast<std::uint32_t>(level);
    std::vector<std::uint8_t>& bytes = Level(levelIndex);
    const null_device::LevelLayout layout = LayoutOf(levelIndex);
    std::size_t offset = 0U;
    if (rect.left != rect.right) {
      // As the Direct3D 11 / Vulkan backends: the rectangle's corner, in blocks for BC formats.
      const std::uint32_t elementBytes = (dxgi::FormatElementBytes(mInfo.format) != 0U)
        ? dxgi::FormatElementBytes(mInfo.format)
        : ((mGalFormat == 1U) ? 3U : 4U);
      const bool blocks = dxgi::IsBlockCompressed(mInfo.format);
      const std::size_t row = static_cast<std::size_t>(std::max<LONG>(rect.top, 0) / (blocks ? 4 : 1));
      const std::size_t column = static_cast<std::size_t>(std::max<LONG>(rect.left, 0) / (blocks ? 4 : 1));
      offset = row * layout.rowPitch + column * elementBytes;
      if (offset >= bytes.size()) {
        ThrowError(kFile, __LINE__, "texture lock rectangle out of range");
      }
    }
    mLocked = true;
    mLockedLevel = level;
    TextureLockRect lock{};
    lock.flags = flags;
    lock.level = level;
    lock.pitch = static_cast<int>(layout.rowPitch);
    lock.bits = bytes.data() + offset;
    return lock;
  }

  int TextureNull::Unlock(const TextureLockRect lock)
  {
    return Unlock(lock.level);
  }

  int TextureNull::Unlock(const int level)
  {
    if (!mLocked || level != mLockedLevel) {
      ThrowError(kFile, __LINE__, "texture lock/unlock mismatch");
    }
    mLocked = false;
    mLockedLevel = 0;
    return 0;
  }

  void TextureNull::SaveToBuffer(gpg::MemBuffer<char>* const outBuffer)
  {
    FAF_POSIX_STUB("TextureNull::SaveToBuffer: the null device has no image encoder; the buffer is left empty");
    if (outBuffer != nullptr) {
      *outBuffer = gpg::MemBuffer<char>();
    }
  }

  // ---------------------------------------------------------------------------
  // Targets

  RenderTargetNull::RenderTargetNull(const RenderTargetContext& context)
    : mContext(context)
  {
    mContext.width_ = std::max(mContext.width_, 1U);
    mContext.height_ = std::max(mContext.height_, 1U);
  }

  RenderTargetNull::~RenderTargetNull() = default;

  RenderTargetContext* RenderTargetNull::GetContext()
  {
    return &mContext;
  }

  HDC RenderTargetNull::GetDC()
  {
    return nullptr; // as the Direct3D 10/11/12 and Vulkan targets
  }

  DepthStencilTargetNull::DepthStencilTargetNull(const DepthStencilTargetContext& context)
    : mContext(context)
  {}

  DepthStencilTargetNull::~DepthStencilTargetNull() = default;

  DepthStencilTargetContext* DepthStencilTargetNull::GetContext()
  {
    return &mContext;
  }

  CubeRenderTargetNull::CubeRenderTargetNull(const CubeRenderTargetContext& context)
    : mContext(context)
  {}

  CubeRenderTargetNull::~CubeRenderTargetNull() = default;

  CubeRenderTargetContext* CubeRenderTargetNull::GetContext()
  {
    return &mContext;
  }

  // ---------------------------------------------------------------------------
  // Buffers

  BufferStorageNull::BufferStorageNull(const std::uint32_t byteWidth, const char* const kind)
    : mByteWidth(std::max(byteWidth, 4U))
    , mKind(kind)
  {
    mShadow.Reset(mByteWidth);
  }

  std::uint8_t* BufferStorageNull::Lock(const std::uint32_t offset, const std::uint32_t size)
  {
    if (mLocked) {
      ThrowError(mKind, __LINE__, "buffer lock/unlock mismatch");
    }
    if (offset >= mByteWidth) {
      ThrowError(mKind, __LINE__, "buffer lock offset out of range");
    }
    mLocked = true;
    return mShadow.Map(offset, size, mKind);
  }

  void BufferStorageNull::Unlock()
  {
    if (!mLocked) {
      ThrowError(mKind, __LINE__, "buffer lock/unlock mismatch");
    }
    mLocked = false;
    mShadow.CheckGuard(mKind);
  }

  VertexBufferNull::VertexBufferNull(const VertexBufferContext& context)
    : mContext(context)
    , mStorage(context.vertexCount_ * context.stride_, "VertexBufferNull")
  {}

  VertexBufferNull::~VertexBufferNull() = default;

  VertexBufferContext* VertexBufferNull::GetContext()
  {
    return &mContext;
  }

  void* VertexBufferNull::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
  {
    static_cast<void>(lockFlags); // the CPU copy is the buffer: every flag keeps its contents
    return mStorage.Lock(offset, size);
  }

  void VertexBufferNull::Unlock()
  {
    mStorage.Unlock();
  }

  IndexBufferNull::IndexBufferNull(const IndexBufferContext& context)
    : mContext(context)
    , mStorage(context.size_ * ((context.format_ == 1U) ? 2U : 4U), "IndexBufferNull")
  {}

  IndexBufferNull::~IndexBufferNull() = default;

  IndexBufferContext* IndexBufferNull::GetContext()
  {
    return &mContext;
  }

  std::int16_t* IndexBufferNull::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
  {
    static_cast<void>(lockFlags);
    return reinterpret_cast<std::int16_t*>(mStorage.Lock(offset, size));
  }

  void IndexBufferNull::Unlock()
  {
    mStorage.Unlock();
  }

  VertexFormatNull::VertexFormatNull(const std::uint32_t formatCode)
  {
    // The per-stream strides the Vulkan backend derives from the same D3D11 layouts.
    formatCode_ = formatCode;
    for (const D3D11_INPUT_ELEMENT_DESC& element : d3d11::VertexLayoutElements(formatCode)) {
      if (streamStrides_.size() <= element.InputSlot) {
        streamStrides_.resize(element.InputSlot + 1U, 0U);
      }
      std::uint32_t& stride = streamStrides_[element.InputSlot];
      stride = std::max(stride, element.AlignedByteOffset + dxgi::FormatElementBytes(element.Format));
    }
  }

  VertexFormatNull::~VertexFormatNull() = default;

  PipelineStateNull::~PipelineStateNull() = default;

  // ---------------------------------------------------------------------------
  // Effects

  EffectTechniqueNull::EffectTechniqueNull(const char* const name, std::shared_ptr<const null_device::TechniqueInfo> info)
    : mName((name != nullptr) ? name : "")
    , mInfo(std::move(info))
  {}

  EffectTechniqueNull::~EffectTechniqueNull() = default;

  msvc8::string* EffectTechniqueNull::GetName()
  {
    return &mName;
  }

  int EffectTechniqueNull::BeginTechnique()
  {
    if (Device* const device = Device::GetInstance()) {
      device->BeginTechnique();
    }
    return (mInfo != nullptr) ? mInfo->passCount : 1;
  }

  void EffectTechniqueNull::EndTechnique()
  {
    if (Device* const device = Device::GetInstance()) {
      device->EndTechnique();
    }
  }

  void EffectTechniqueNull::BeginPass(const int pass)
  {
    static_cast<void>(pass);
  }

  void EffectTechniqueNull::EndPass() {}

  const effects::Annotation* EffectTechniqueNull::Find(const msvc8::string& annotationName) const
  {
    if (mInfo == nullptr) {
      return nullptr;
    }
    const std::string_view wanted(annotationName.c_str());
    for (const effects::Annotation& annotation : mInfo->annotations) {
      if (annotation.name == wanted) {
        return &annotation;
      }
    }
    return nullptr;
  }

  bool EffectTechniqueNull::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
  {
    const effects::Annotation* const annotation = Find(annotationName);
    if (annotation == nullptr || annotation->kind == effects::Annotation::Kind::String) {
      return false;
    }
    *outValue = annotation->number != 0.0;
    return true;
  }

  bool EffectTechniqueNull::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
  {
    const effects::Annotation* const annotation = Find(annotationName);
    if (annotation == nullptr || annotation->kind == effects::Annotation::Kind::String) {
      return false;
    }
    *outValue = static_cast<int>(annotation->number);
    return true;
  }

  bool EffectTechniqueNull::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
  {
    const effects::Annotation* const annotation = Find(annotationName);
    if (annotation == nullptr || annotation->kind == effects::Annotation::Kind::String) {
      return false;
    }
    *outValue = static_cast<float>(annotation->number);
    return true;
  }

  bool EffectTechniqueNull::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
  {
    const effects::Annotation* const annotation = Find(annotationName);
    if (annotation == nullptr || annotation->kind != effects::Annotation::Kind::String) {
      return false;
    }
    outValue->assign_owned(annotation->text.c_str());
    return true;
  }

  EffectVariableNull::EffectVariableNull(const char* const name)
    : mName((name != nullptr) ? name : "")
  {}

  EffectVariableNull::~EffectVariableNull() = default;

  msvc8::string* EffectVariableNull::GetName()
  {
    return &mName;
  }

  void EffectVariableNull::SetCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget> cubeTarget)
  {
    static_cast<void>(cubeTarget);
  }

  void EffectVariableNull::SetRenderTarget(const boost::shared_ptr<RenderTarget> renderTarget)
  {
    static_cast<void>(renderTarget);
  }

  void EffectVariableNull::SetTexture(const boost::shared_ptr<Texture> texture)
  {
    static_cast<void>(texture);
  }

  void EffectVariableNull::SetMatrix4x4(const Matrix* const matrix)
  {
    static_cast<void>(matrix);
  }

  void EffectVariableNull::SetFloatArray(const std::uint32_t count, const float* const values)
  {
    static_cast<void>(count);
    static_cast<void>(values);
  }

  void EffectVariableNull::SetVector(const float* const vector4)
  {
    static_cast<void>(vector4);
  }

  void EffectVariableNull::SetValue(const void* const data, const std::uint32_t byteCount)
  {
    static_cast<void>(data);
    static_cast<void>(byteCount);
  }

  void EffectVariableNull::SetFloat(const float value)
  {
    static_cast<void>(value);
  }

  void EffectVariableNull::SetInt(const int value)
  {
    static_cast<void>(value);
  }

  void EffectVariableNull::SetBool(const bool value)
  {
    static_cast<void>(value);
  }

  void EffectVariableNull::SetMatrixArray(const std::uint32_t count, const Matrix* const matrices)
  {
    static_cast<void>(count);
    static_cast<void>(matrices);
  }

  void EffectVariableNull::SetVectorArray(const std::uint32_t count, const float* const vectors4)
  {
    static_cast<void>(count);
    static_cast<void>(vectors4);
  }

  bool EffectVariableNull::GetAnnotationBool(bool* const outValue, const msvc8::string& annotationName)
  {
    static_cast<void>(outValue);
    static_cast<void>(annotationName);
    return false;
  }

  bool EffectVariableNull::GetAnnotationInt(int* const outValue, const msvc8::string& annotationName)
  {
    static_cast<void>(outValue);
    static_cast<void>(annotationName);
    return false;
  }

  bool EffectVariableNull::GetAnnotationFloat(float* const outValue, const msvc8::string& annotationName)
  {
    static_cast<void>(outValue);
    static_cast<void>(annotationName);
    return false;
  }

  bool EffectVariableNull::GetAnnotationString(msvc8::string* const outValue, const msvc8::string& annotationName)
  {
    static_cast<void>(outValue);
    static_cast<void>(annotationName);
    return false;
  }

  EffectNull::EffectNull(const EffectContext& context, std::vector<null_device::TechniqueInfo> techniques)
    : mContext(context)
  {
    mContext.mSourceBuffer.Reset(); // no source is kept once the effect exists (as ProgramEffect)
    mTechniques.reserve(techniques.size());
    for (null_device::TechniqueInfo& technique : techniques) {
      mTechniques.push_back(std::make_shared<const null_device::TechniqueInfo>(std::move(technique)));
    }
  }

  EffectNull::~EffectNull() = default;

  EffectContext* EffectNull::GetContext()
  {
    return &mContext;
  }

  void EffectNull::GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques)
  {
    for (const std::shared_ptr<const null_device::TechniqueInfo>& technique : mTechniques) {
      outTechniques.push_back(boost::shared_ptr<EffectTechnique>(new EffectTechniqueNull(technique->name.c_str(), technique)));
    }
  }

  boost::shared_ptr<EffectVariable> EffectNull::GetVariable(const char* const name)
  {
    return boost::shared_ptr<EffectVariable>(new EffectVariableNull(name));
  }

  boost::shared_ptr<EffectTechnique> EffectNull::GetTechnique(const char* const name)
  {
    const std::string_view wanted = (name != nullptr) ? name : "";
    for (const std::shared_ptr<const null_device::TechniqueInfo>& technique : mTechniques) {
      if (technique->name == wanted) {
        return boost::shared_ptr<EffectTechnique>(new EffectTechniqueNull(name, technique));
      }
    }
    return boost::shared_ptr<EffectTechnique>(new EffectTechniqueNull(name, nullptr));
  }

  void EffectNull::OnReset() {}

  void EffectNull::OnLost() {}

  // ---------------------------------------------------------------------------
  // DeviceNull

  DeviceNull::DeviceNull() = default;

  DeviceNull::~DeviceNull()
  {
    outputContext_ = OutputContext();
    mHeadOutputContexts.clear();
  }

  void DeviceNull::Setup(DeviceContext* const context)
  {
    mCurThreadId = static_cast<int>(::GetCurrentThreadId());
    mLog.clear();
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "no device context");
    }
    mDeviceContext = *context;
    mDeviceContext.mDeviceType = DeviceApi::Null;
    // The Vulkan backend's caps (DeviceVulkan::CheckAvailableFormats).
    mDeviceContext.mMaxPrimitiveCount = 0x10000U;
    mDeviceContext.mMaxVertexCount = 0xFFFFU;
    mDeviceContext.mHWBasedInstancing = true;
    mDeviceContext.mVertexShaderProfile = 4;
    mDeviceContext.mPixelShaderProfile = 8;
    for (int headIndex = 0; headIndex < mDeviceContext.GetHeadCount(); ++headIndex) {
      Head& head = mDeviceContext.GetHead(static_cast<std::uint32_t>(headIndex));
      if (head.mWidth == 0U || head.mHeight == 0U) {
        head.mWidth = kDefaultHeadWidth;
        head.mHeight = kDefaultHeadHeight;
      }
      head.adapterModes.clear();
      head.validFormats1.clear();
      for (std::int32_t format = 1; format < 8; ++format) {
        if (dxgi::RenderTargetFormatToDxgi(static_cast<std::uint32_t>(format)) != DXGI_FORMAT_UNKNOWN) {
          head.validFormats1.push_back(format);
        }
      }
      head.validFormats2.clear();
      for (std::int32_t format = 1; format < 20; ++format) {
        if (dxgi::TextureFormatToDxgi(static_cast<std::uint32_t>(format)) != DXGI_FORMAT_UNKNOWN) {
          head.validFormats2.push_back(format);
        }
      }
      head.mStrs.clear();
    }
    mPipelineState.reset(new PipelineStateNull());
    CreateHeadTargets();
    const Head* const first = (mDeviceContext.GetHeadCount() > 0) ? &mDeviceContext.GetHead(0U) : nullptr;
    gpg::Logf(
      "Null: ready (%d head(s), %ux%u; no window: draws, clears and presents do nothing)", mDeviceContext.GetHeadCount(),
      (first != nullptr) ? first->mWidth : 0U, (first != nullptr) ? first->mHeight : 0U
    );
  }

  void DeviceNull::CreateHeadTargets()
  {
    outputContext_ = OutputContext();
    mHeadOutputContexts.clear();
    mHeadOutputContexts.resize(static_cast<std::size_t>(std::max(mDeviceContext.GetHeadCount(), 0)));
    for (std::size_t headIndex = 0U; headIndex < mHeadOutputContexts.size(); ++headIndex) {
      const Head& head = mDeviceContext.GetHead(static_cast<std::uint32_t>(headIndex));
      OutputContext& output = mHeadOutputContexts[headIndex];
      const RenderTargetContext targetContext(head.mWidth, head.mHeight, kBackBufferFormat);
      output.surface.reset(new RenderTargetNull(targetContext));
      const DepthStencilTargetContext depthContext(head.mWidth, head.mHeight, kDepthStencilFormat, false);
      output.depthStencil = CreateDepthStencilTarget(&depthContext);
    }
    if (!mHeadOutputContexts.empty()) {
      ClearTarget(&mHeadOutputContexts.front());
    }
  }

  void* DeviceNull::GetLog()
  {
    return &mLog;
  }

  DeviceContext* DeviceNull::GetDeviceContext()
  {
    return &mDeviceContext;
  }

  int DeviceNull::GetCurThreadId()
  {
    return mCurThreadId;
  }

  void DeviceNull::Func1() const {}

  void DeviceNull::GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, const int adapterIndex)
  {
    static_cast<void>(adapterIndex);
    outModes.clear(); // no display, no exclusive modes
  }

  OutputContext* DeviceNull::GetHeadOutputContext(const unsigned int headIndex)
  {
    if (headIndex >= mHeadOutputContexts.size()) {
      ThrowError(kFile, __LINE__, "invalid head index specified");
    }
    return &mHeadOutputContexts[headIndex];
  }

  const OutputContext* DeviceNull::GetHeadOutputContext(const unsigned int headIndex) const
  {
    if (headIndex >= mHeadOutputContexts.size()) {
      ThrowError(kFile, __LINE__, "invalid head index specified");
    }
    return &mHeadOutputContexts[headIndex];
  }

  boost::shared_ptr<PipelineState> DeviceNull::GetPipelineState()
  {
    return mPipelineState;
  }

  boost::shared_ptr<Effect> DeviceNull::CreateEffect(const EffectContext& context)
  {
    const char* const begin = context.mSourceBuffer.mBegin;
    const std::size_t bytes = (begin != nullptr) ? static_cast<std::size_t>(context.mSourceBuffer.mEnd - begin) : 0U;
    std::vector<null_device::TechniqueInfo> techniques;
    if (bytes != 0U) {
      techniques = null_device::ScanEffectTechniques(std::string_view(begin, bytes), context.mMacros);
    }
    gpg::Logf("Null: effect %s: %u techniques", context.mSourcePath.c_str(), static_cast<unsigned>(techniques.size()));
    return boost::shared_ptr<Effect>(new EffectNull(context, std::move(techniques)));
  }

  boost::shared_ptr<Texture> DeviceNull::CreateTexture(const TextureContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid source specified for texture data");
    }
    null_device::ImageInfo info;
    if (context->source_ == 2U) {
      info = ShapeOfEngineTexture(*context);
    } else if (context->source_ == 1U) {
      if (context->dataBuffer_.mEnd == context->dataBuffer_.mBegin) {
        ThrowError(kFile, __LINE__, "attempt to create texture from uninitialized memory");
      }
      if (!null_device::ReadImageInfo(context->dataBuffer_.mBegin, context->DataSizeBytes(), info)) {
        std::string message = std::string("unable to load texture ") + context->location_.c_str() + ": unrecognised image header";
        ThrowError(kFile, __LINE__, message.c_str());
      }
      // D3DX_SKIP_DDS_MIP_LEVELS (the resource's skip-mip setting) drops the top levels.
      const std::uint32_t skip = context->reserved0x44_ & 0x1FU;
      if (skip != 0U && info.mipLevels > skip) {
        info.width = std::max(info.width >> skip, 1U);
        info.height = std::max(info.height >> skip, 1U);
        info.mipLevels -= skip;
      }
    } else {
      ThrowError(kFile, __LINE__, "invalid source specified for texture data");
    }
    return boost::shared_ptr<Texture>(new TextureNull(*context, info));
  }

  boost::shared_ptr<RenderTarget> DeviceNull::CreateRenderTarget(const RenderTargetContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid render target context");
    }
    // A new target resets the viewport to its size, as on the other backends.
    D3DVIEWPORT9 viewport{};
    viewport.Width = std::max(context->width_, 1U);
    viewport.Height = std::max(context->height_, 1U);
    viewport.MaxZ = 1.0f;
    SetViewport(&viewport);
    return boost::shared_ptr<RenderTarget>(new RenderTargetNull(*context));
  }

  boost::shared_ptr<CubeRenderTarget> DeviceNull::CreateCubeRenderTarget(const CubeRenderTargetContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid cube render target context");
    }
    return boost::shared_ptr<CubeRenderTarget>(new CubeRenderTargetNull(*context));
  }

  boost::shared_ptr<DepthStencilTarget> DeviceNull::CreateDepthStencilTarget(const DepthStencilTargetContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid depth stencil context");
    }
    return boost::shared_ptr<DepthStencilTarget>(new DepthStencilTargetNull(*context));
  }

  boost::shared_ptr<VertexFormat> DeviceNull::CreateVertexFormat(const std::uint32_t formatCode)
  {
    if (formatCode >= d3d11::kVertexFormatCount || d3d11::VertexLayoutElements(formatCode).empty()) {
      ThrowError(kFile, __LINE__, "invalid vertex format specified");
    }
    return boost::shared_ptr<VertexFormat>(new VertexFormatNull(formatCode));
  }

  boost::shared_ptr<VertexBuffer> DeviceNull::CreateVertexBuffer(const VertexBufferContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid vertex buffer context");
    }
    return boost::shared_ptr<VertexBuffer>(new VertexBufferNull(*context));
  }

  boost::shared_ptr<IndexBuffer> DeviceNull::CreateIndexBuffer(const IndexBufferContext* const context)
  {
    if (context == nullptr) {
      ThrowError(kFile, __LINE__, "invalid index buffer context");
    }
    return boost::shared_ptr<IndexBuffer>(new IndexBufferNull(*context));
  }

  void DeviceNull::GetRenderTargetData(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination)
  {
    if (source == nullptr) {
      ThrowError(kFile, __LINE__, "Missing source texture");
    }
    if (destination == nullptr) {
      ThrowError(kFile, __LINE__, "Missing dest   texture");
    }
    // Nothing was rendered: the destination keeps its texels.
  }

  void DeviceNull::StretchRect(
    const boost::shared_ptr<RenderTarget>& source,
    const boost::shared_ptr<RenderTarget>& destination,
    const RECT* const sourceRect,
    const RECT* const destinationRect
  )
  {
    static_cast<void>(sourceRect);
    static_cast<void>(destinationRect);
    if (source == nullptr) {
      ThrowError(kFile, __LINE__, "Missing source texture");
    }
    if (destination == nullptr) {
      ThrowError(kFile, __LINE__, "Missing dest   texture");
    }
  }

  void DeviceNull::UpdateSurface(
    const boost::shared_ptr<Texture>& source,
    const boost::shared_ptr<Texture>& destination,
    const RECT* const sourceRect,
    const RECT* const destinationRect
  )
  {
    if (source == nullptr) {
      ThrowError(kFile, __LINE__, "Missing source texture");
    }
    if (destination == nullptr) {
      ThrowError(kFile, __LINE__, "Missing dest   texture");
    }
    auto* const from = static_cast<TextureNull*>(source.get());
    auto* const to = static_cast<TextureNull*>(destination.get());
    if (from == to) {
      return;
    }
    if (from->DxgiFormat() != to->DxgiFormat() || from->GetContext()->format_ != to->GetContext()->format_) {
      // The GPU backends re-encode through the image codec; the null device has none.
      FAF_POSIX_STUB("DeviceNull::UpdateSurface: formats differ; the destination keeps its texels (no image codec)");
      return;
    }
    // The top level: the source rectangle (default whole) to the destination rectangle's
    // corner (default the origin), clipped to both, in 4x4 blocks for BC formats.
    const null_device::ImageInfo& fromInfo = from->Info();
    const null_device::ImageInfo& toInfo = to->Info();
    RECT sourceArea{};
    if (!ClipRect(sourceRect, fromInfo.width, fromInfo.height, sourceArea)) {
      return;
    }
    const LONG destinationLeft = (destinationRect != nullptr) ? std::max<LONG>(destinationRect->left, 0) : 0;
    const LONG destinationTop = (destinationRect != nullptr) ? std::max<LONG>(destinationRect->top, 0) : 0;
    const LONG width = std::min<LONG>(sourceArea.right - sourceArea.left, static_cast<LONG>(toInfo.width) - destinationLeft);
    const LONG height = std::min<LONG>(sourceArea.bottom - sourceArea.top, static_cast<LONG>(toInfo.height) - destinationTop);
    if (width <= 0 || height <= 0) {
      return;
    }
    const bool blocks = dxgi::IsBlockCompressed(fromInfo.format);
    const LONG unit = blocks ? 4 : 1;
    const std::uint32_t elementBytes = (dxgi::FormatElementBytes(fromInfo.format) != 0U)
      ? dxgi::FormatElementBytes(fromInfo.format)
      : ((from->GetContext()->format_ == 1U) ? 3U : 4U);
    const null_device::LevelLayout fromLayout = from->LayoutOf(0U);
    const null_device::LevelLayout toLayout = to->LayoutOf(0U);
    const std::vector<std::uint8_t>& fromBytes = from->Level(0U);
    std::vector<std::uint8_t>& toBytes = to->Level(0U);
    const std::size_t rowBytes = static_cast<std::size_t>((width + unit - 1) / unit) * elementBytes;
    const LONG rows = (height + unit - 1) / unit;
    for (LONG row = 0; row < rows; ++row) {
      const std::size_t fromOffset = static_cast<std::size_t>(sourceArea.top / unit + row) * fromLayout.rowPitch +
        static_cast<std::size_t>(sourceArea.left / unit) * elementBytes;
      const std::size_t toOffset = static_cast<std::size_t>(destinationTop / unit + row) * toLayout.rowPitch +
        static_cast<std::size_t>(destinationLeft / unit) * elementBytes;
      if (fromOffset + rowBytes > fromBytes.size() || toOffset + rowBytes > toBytes.size()) {
        break;
      }
      std::memcpy(toBytes.data() + toOffset, fromBytes.data() + fromOffset, rowBytes);
    }
  }

  void DeviceNull::SaveCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget>& cubeTarget, const msvc8::string& filePath)
  {
    static_cast<void>(cubeTarget);
    static_cast<void>(filePath);
    FAF_POSIX_STUB("DeviceNull::SaveCubeRenderTarget: nothing is rendered; no file written");
  }

  void DeviceNull::SaveRenderTarget(const boost::shared_ptr<RenderTarget>& renderTarget, const msvc8::string& filePath, const int fileFormat)
  {
    static_cast<void>(renderTarget);
    static_cast<void>(filePath);
    static_cast<void>(fileFormat);
    FAF_POSIX_STUB("DeviceNull::SaveRenderTarget: nothing is rendered; no file written");
  }

  void DeviceNull::SaveTexture(
    const boost::shared_ptr<Texture>& texture, const msvc8::string& filePath, const int fileFormat, gpg::MemBuffer<char>* const outBuffer
  )
  {
    static_cast<void>(texture);
    static_cast<void>(filePath);
    static_cast<void>(fileFormat);
    FAF_POSIX_STUB("DeviceNull::SaveTexture: the null device has no image encoder; nothing is saved");
    if (outBuffer != nullptr) {
      *outBuffer = gpg::MemBuffer<char>();
    }
  }

  void DeviceNull::GetTexture2D(
    const void* const sourceData, const std::uint32_t sourceBytes, gpg::MemBuffer<char>* const outTextureData, std::uint32_t* const outWidth,
    int* const outHeight
  )
  {
    if (sourceData == nullptr) {
      return;
    }
    null_device::ImageInfo info;
    if (!null_device::ReadImageInfo(sourceData, sourceBytes, info)) {
      gpg::Warnf("Null: GetTexture2D: unrecognised image header (%u bytes)", static_cast<unsigned>(sourceBytes));
      *outWidth = 0U;
      *outHeight = 0;
      *outTextureData = gpg::MemBuffer<char>();
      return;
    }
    // The file's size, and zeroed BC3 blocks padded to whole blocks (as DeviceD3D9 / Vulkan).
    *outWidth = info.width;
    *outHeight = static_cast<int>(info.height);
    const std::size_t blocksWide = (static_cast<std::size_t>(info.width) + 3U) / 4U;
    const std::size_t blocksHigh = (static_cast<std::size_t>(info.height) + 3U) / 4U;
    const std::size_t total = blocksWide * blocksHigh * 16U;
    if (outTextureData->Size() != total) {
      *outTextureData = gpg::AllocMemBuffer(total);
    }
    std::memset(outTextureData->GetPtr(0U, 0U), 0, total);
  }

  boost::weak_ptr<void>* DeviceNull::Func7(boost::weak_ptr<void>* const outWeakHandle, boost::shared_ptr<void> temporarySharedHandle)
  {
    static_cast<void>(temporarySharedHandle);
    outWeakHandle->reset();
    return outWeakHandle;
  }

  void DeviceNull::Reset(DeviceContext* const context)
  {
    if (context != nullptr && context != &mDeviceContext) {
      mDeviceContext.mVSync = context->mVSync;
      const int heads = std::min(context->GetHeadCount(), mDeviceContext.GetHeadCount());
      for (int headIndex = 0; headIndex < heads; ++headIndex) {
        const Head& requested = context->GetHead(static_cast<std::uint32_t>(headIndex));
        Head& head = mDeviceContext.GetHead(static_cast<std::uint32_t>(headIndex));
        head.mWidth = (requested.mWidth != 0U) ? requested.mWidth : head.mWidth;
        head.mHeight = (requested.mHeight != 0U) ? requested.mHeight : head.mHeight;
      }
    }
    CreateHeadTargets();
  }

  void DeviceNull::Reset()
  {
    Reset(&mDeviceContext);
  }

  int DeviceNull::TestCooperativeLevel()
  {
    return 0; // never lost
  }

  void DeviceNull::BeginScene() {}

  void DeviceNull::EndScene() {}

  void DeviceNull::Present() {}

  void DeviceNull::SetCursor(const CursorContext* const context)
  {
    static_cast<void>(context);
  }

  void DeviceNull::InitCursor() {}

  int DeviceNull::ShowCursor(const bool show)
  {
    const bool previous = mCursorShown;
    mCursorShown = show;
    return previous ? 1 : 0;
  }

  void DeviceNull::SetViewport(const D3DVIEWPORT9* const viewport)
  {
    if (viewport != nullptr) {
      mViewport = *viewport;
    }
  }

  void DeviceNull::GetViewport(D3DVIEWPORT9* const outViewport)
  {
    if (outViewport != nullptr) {
      *outViewport = mViewport;
    }
  }

  void DeviceNull::ClearTarget(const OutputContext* const context)
  {
    const RenderTarget* const previousSurface = outputContext_.surface.get();
    Device::ClearTarget(context);
    // A new colour target resets the viewport to its size, as DeviceD3D9 / DeviceVulkan.
    if (outputContext_.surface != nullptr && outputContext_.surface.get() != previousSurface) {
      const RenderTargetContext* const target = outputContext_.surface->GetContext();
      D3DVIEWPORT9 viewport{};
      viewport.Width = target->width_;
      viewport.Height = target->height_;
      viewport.MinZ = 0.0f;
      viewport.MaxZ = 1.0f;
      SetViewport(&viewport);
    }
  }

  void DeviceNull::Clear(
    const bool clearColor, const bool clearDepth, const bool clearStencil, const std::uint32_t packedColor, const float depth, const int stencil
  )
  {
    static_cast<void>(clearColor);
    static_cast<void>(clearDepth);
    static_cast<void>(clearStencil);
    static_cast<void>(packedColor);
    static_cast<void>(depth);
    static_cast<void>(stencil);
  }

  void DeviceNull::ClearTextures() {}

  void DeviceNull::SetVertexDeclaration(const boost::shared_ptr<VertexFormat> vertexFormat)
  {
    static_cast<void>(vertexFormat);
  }

  void DeviceNull::SetVertexBuffer(
    const std::uint32_t streamSlot, const boost::shared_ptr<VertexBuffer> vertexBuffer, const int streamFrequencyToken, const int startVertex
  )
  {
    static_cast<void>(vertexBuffer);
    static_cast<void>(streamFrequencyToken);
    static_cast<void>(startVertex);
    if (streamSlot >= 16U) {
      ThrowError(kFile, __LINE__, "invalid vertex stream");
    }
  }

  void DeviceNull::SetBufferIndices(const boost::shared_ptr<IndexBuffer> indexBuffer)
  {
    static_cast<void>(indexBuffer);
  }

  void DeviceNull::SetFogState(const bool enable, const Matrix* const projection, const float fogStart, const float fogEnd, const int fogColor)
  {
    static_cast<void>(enable);
    static_cast<void>(projection);
    static_cast<void>(fogStart);
    static_cast<void>(fogEnd);
    static_cast<void>(fogColor);
  }

  void DeviceNull::SetWireframeState(const bool enabled)
  {
    static_cast<void>(enabled);
  }

  void DeviceNull::SetColorWriteState(const bool writeColor, const bool writeAlpha)
  {
    static_cast<void>(writeColor);
    static_cast<void>(writeAlpha);
  }

  void DeviceNull::DrawIndexedPrimitive(const DrawIndexedContext* const context)
  {
    static_cast<void>(context);
  }

  void DeviceNull::DrawPrimitive(const DrawContext* const context)
  {
    static_cast<void>(context);
  }

  void DeviceNull::BeginTechnique() {}

  void DeviceNull::EndTechnique() {}

  bool DeviceNull::GetRuntimeInfo(DeviceRuntimeInfo& out)
  {
    out = DeviceRuntimeInfo{};
    out.api = "Null";
    out.adapter = "none (headless)";
    out.vsync = mDeviceContext.mVSync;
    return true;
  }

  bool DeviceNull::SupportsVertexTextureFormat(const std::uint32_t textureFormat)
  {
    return dxgi::TextureFormatToDxgi(textureFormat) != DXGI_FORMAT_UNKNOWN;
  }
} // namespace gpg::gal
