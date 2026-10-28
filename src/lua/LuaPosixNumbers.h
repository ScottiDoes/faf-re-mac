#pragma once

// FAF_POSIX only: Lua-visible number behaviour that the Windows x64 build gets from x86
// hardware and the static UCRT, reproduced for the native arm64 build (platform package;
// see macos/porting/platform.md). Used by lua/LuaObject.cpp.
//
// 1. NaN encoding. An invalid operation (0/0, inf-inf, 0*inf, sqrt(-1), ...) produces x86's
//    default NaN 0xFFC00000 (negative) under SSE and x87, but arm64's 0x7FC00000 (positive)
//    (macos/docs/DETERMINISM.md §2.3). Propagating a NaN operand behaves the same on both,
//    so only a freshly generated NaN - a NaN result from non-NaN operands - is re-encoded.
// 2. Spelling. The UCRT printf writes non-finite values as inf, -inf, nan, -nan(ind) (the
//    x86 default NaN), -nan (another negative quiet NaN), nan(snan); upper case for %E/%G.
//    macOS writes "nan" for every NaN.
// 3. Parsing. UCRT strtod reads "nan(ind)" as the x86 default NaN and drops an n-char
//    payload; macOS strtod reads "nan(ind)" as a positive NaN and keeps the payload.

#if defined(FAF_POSIX)

#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace faf_lua_posix
{
  /// x86's default NaN ("real indefinite") as a float.
  inline constexpr std::uint32_t kX86DefaultNaNBits = 0xFFC00000u;
  inline constexpr std::uint32_t kX86PositiveQuietNaNBits = 0x7FC00000u;

  [[nodiscard]] inline float X86DefaultNaN() noexcept
  {
    return std::bit_cast<float>(kX86DefaultNaNBits);
  }

  /// `result` of an operation on `a`: a NaN made from a non-NaN operand gets x86's encoding.
  /// The common path is one compare on the result.
  [[nodiscard]] inline float FreshNaNToX86(const float result, const float a) noexcept
  {
    if (result != result) [[unlikely]] {
      if (a == a) {
        return X86DefaultNaN();
      }
    }
    return result;
  }

  /// `result` of an operation on `a` and `b`, as above.
  [[nodiscard]] inline float FreshNaNToX86(const float result, const float a, const float b) noexcept
  {
    if (result != result) [[unlikely]] {
      if (a == a && b == b) {
        return X86DefaultNaN();
      }
    }
    return result;
  }

  /// The UCRT printf body for a non-finite double, sign included: "inf", "-inf", "nan",
  /// "-nan", "-nan(ind)", "nan(snan)", "-nan(snan)" (upper case when `upper`); a '+' or ' '
  /// stands in front of a non-negative value when `positiveSign` is not 0. Writes at most 12
  /// characters plus the terminator; returns the length.
  inline int UcrtNonFiniteText(const double value, const bool upper, const char positiveSign, char* const out) noexcept
  {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    const bool negative = (bits >> 63) != 0;
    const std::uint64_t mantissa = bits & ((std::uint64_t{1} << 52) - 1);
    constexpr std::uint64_t kQuietBit = std::uint64_t{1} << 51;
    const char* body = nullptr;
    if (mantissa == 0) {
      body = upper ? "INF" : "inf";
    } else if ((mantissa & kQuietBit) == 0) {
      body = upper ? "NAN(SNAN)" : "nan(snan)";
    } else if (negative && mantissa == kQuietBit) {
      body = upper ? "NAN(IND)" : "nan(ind)"; // the indeterminate: x86's default NaN
    } else {
      body = upper ? "NAN" : "nan";
    }
    int length = 0;
    if (negative) {
      out[length++] = '-';
    } else if (positiveSign != 0) {
      out[length++] = positiveSign;
    }
    const std::size_t bodyLength = std::strlen(body);
    std::memcpy(out + length, body, bodyLength + 1);
    return length + static_cast<int>(bodyLength);
  }

  /// Formats a non-finite `value` for one printf directive `form` ("%[flags][width]
  /// [.precision]<e|E|f|g|G>", as string.format assembles it) the way the UCRT does: its
  /// spelling, the sign flags ('+', ' '), width with '-' for left justification. Precision
  /// does not apply. Padding is with spaces, also under '0', and '#' changes nothing: the C
  /// standard's rule for infinities and NaNs (not verified against the UCRT itself).
  /// Returns the length, as snprintf does.
  inline int FormatNonFiniteUcrt(char* const out, const std::size_t outSize, const char* const form,
                                 const double value) noexcept
  {
    const char* p = form;
    if (*p == '%') {
      ++p;
    }
    bool left = false;
    char positiveSign = 0;
    for (;; ++p) {
      if (*p == '-') {
        left = true;
      } else if (*p == '+') {
        positiveSign = '+';
      } else if (*p == ' ') {
        if (positiveSign == 0) {
          positiveSign = ' ';
        }
      } else if (*p != '#' && *p != '0') {
        break;
      }
    }
    int width = 0;
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      ++p;
    }
    if (*p == '.') {
      ++p;
      while (*p >= '0' && *p <= '9') {
        ++p;
      }
    }
    const char conversion = *p;
    const bool upper = conversion == 'E' || conversion == 'G' || conversion == 'F' || conversion == 'A';
    char text[16];
    (void)UcrtNonFiniteText(value, upper, positiveSign, text);
    return std::snprintf(out, outSize, left ? "%-*s" : "%*s", width, text);
  }

  /// The float that UCRT strtod would have produced (then narrowed to float) for `text`,
  /// given that the C library's strtod parsed it as a NaN: "[space][sign]nan(ind)" in any
  /// case is the x86 default NaN; every other NaN spelling is a quiet NaN with the parsed
  /// sign and no payload.
  [[nodiscard]] inline float UcrtStrtodNaN(const char* const text) noexcept
  {
    const char* p = text;
    while (std::isspace(static_cast<unsigned char>(*p)) != 0) {
      ++p;
    }
    bool negative = false;
    if (*p == '+' || *p == '-') {
      negative = *p == '-';
      ++p;
    }
    constexpr char kIndeterminate[] = "nan(ind)";
    bool indeterminate = true;
    for (std::size_t i = 0; i + 1 < sizeof(kIndeterminate) && indeterminate; ++i) {
      indeterminate = std::tolower(static_cast<unsigned char>(p[i])) == kIndeterminate[i];
    }
    if (indeterminate) {
      return X86DefaultNaN();
    }
    return std::bit_cast<float>(negative ? kX86DefaultNaNBits : kX86PositiveQuietNaNBits);
  }
} // namespace faf_lua_posix

#endif // FAF_POSIX
