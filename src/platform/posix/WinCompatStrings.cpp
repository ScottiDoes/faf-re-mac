// Win32 on POSIX: Strings (declarations in compat/Strings.h).

#include "WinCompatInternal.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

using namespace faf_posix;

namespace
{
  constexpr char32_t kReplacement = 0xFFFD;

  // --- UTF-8 ---------------------------------------------------------------------------------
  // Decodes one code point; returns the bytes consumed (>= 1) and sets `valid`.
  std::size_t DecodeUtf8(const unsigned char* const text, const std::size_t available, char32_t* const out,
                         bool* const valid) noexcept
  {
    const unsigned char lead = text[0];
    *valid = true;
    if (lead < 0x80) {
      *out = lead;
      return 1;
    }
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
      value = lead & 0x1F;
      minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      value = lead & 0x0F;
      minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      value = lead & 0x07;
      minimum = 0x10000;
    } else {
      *valid = false;
      *out = kReplacement;
      return 1;
    }
    if (length > available) {
      *valid = false;
      *out = kReplacement;
      return 1;
    }
    for (std::size_t i = 1; i < length; ++i) {
      if ((text[i] & 0xC0) != 0x80) {
        *valid = false;
        *out = kReplacement;
        return i;
      }
      value = (value << 6) | (text[i] & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
      *valid = false;
      *out = kReplacement;
      return length;
    }
    *out = value;
    return length;
  }

  // Appends one code point; false (and U+FFFD) for a surrogate or out-of-range value.
  bool EncodeUtf8(char32_t value, std::string& out)
  {
    bool valid = true;
    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
      value = kReplacement;
      valid = false;
    }
    if (value < 0x80) {
      out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (value >> 6)));
      out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (value >> 12)));
      out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (value >> 18)));
      out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
    return valid;
  }

  // Windows-1252 0x80..0x9F (the rest is Latin-1).
  constexpr char16_t kCp1252High[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

  enum class Charset
  {
    Utf8,
    Cp1252,
    Latin1,
    Invalid
  };

  Charset CharsetOf(const UINT codePage) noexcept
  {
    switch (codePage) {
      case CP_ACP:
      case CP_OEMCP:
      case CP_MACCP:
      case CP_THREAD_ACP:
      case CP_UTF8:
        return Charset::Utf8;
      case 1252:
        return Charset::Cp1252;
      case 28591:
        return Charset::Latin1;
      default:
        return Charset::Invalid;
    }
  }

  // Decodes `source` into code points; false if invalid and `strict`.
  bool DecodeMultiByte(const Charset charset, const unsigned char* const source, const std::size_t length,
                       const bool strict, std::u32string& out)
  {
    if (charset == Charset::Utf8) {
      std::size_t i = 0;
      while (i < length) {
        char32_t value = 0;
        bool valid = true;
        i += DecodeUtf8(source + i, length - i, &value, &valid);
        if (!valid && strict) {
          return false;
        }
        out.push_back(value);
      }
      return true;
    }
    for (std::size_t i = 0; i < length; ++i) {
      const unsigned char c = source[i];
      if (charset == Charset::Cp1252 && c >= 0x80 && c < 0xA0) {
        const char16_t mapped = kCp1252High[c - 0x80];
        if (mapped == 0xFFFD && strict) {
          return false;
        }
        out.push_back(mapped);
      } else {
        out.push_back(c);
      }
    }
    return true;
  }

  // Encodes code points; `usedDefault` reports unrepresentable ones.
  std::string EncodeMultiByte(const Charset charset, const std::u32string& text, const char defaultChar,
                              bool* const usedDefault)
  {
    std::string out;
    *usedDefault = false;
    for (const char32_t value : text) {
      if (charset == Charset::Utf8) {
        if (!EncodeUtf8(value, out)) {
          *usedDefault = true;
        }
        continue;
      }
      if (value < 0x80 || (value >= 0xA0 && value <= 0xFF)) {
        out.push_back(static_cast<char>(value));
        continue;
      }
      bool mapped = false;
      if (charset == Charset::Cp1252) {
        for (int i = 0; i < 32; ++i) {
          if (static_cast<char32_t>(kCp1252High[i]) == value && value != 0xFFFD) {
            out.push_back(static_cast<char>(0x80 + i));
            mapped = true;
            break;
          }
        }
      } else if (value >= 0x80 && value < 0xA0) {
        out.push_back(static_cast<char>(value));
        mapped = true;
      }
      if (!mapped) {
        out.push_back(defaultChar);
        *usedDefault = true;
      }
    }
    return out;
  }

  // --- MSVC printf formats -------------------------------------------------------------------
  // Rewrites an MSVC format for the POSIX printf of the same width (see Strings.h).
  template <class Char>
  std::basic_string<Char> PosixFormat(const Char* const format)
  {
    std::basic_string<Char> out;
    const Char* p = format;
    while (*p != 0) {
      if (*p != '%') {
        out.push_back(*p++);
        continue;
      }
      out.push_back(*p++);
      if (*p == '%') {
        out.push_back(*p++);
        continue;
      }
      // flags, width, precision
      while (*p != 0 && std::wcschr(L"-+ #0", static_cast<wchar_t>(*p)) != nullptr) {
        out.push_back(*p++);
      }
      while (*p == '*' || (*p >= '0' && *p <= '9')) {
        out.push_back(*p++);
      }
      if (*p == '.') {
        out.push_back(*p++);
        while (*p == '*' || (*p >= '0' && *p <= '9')) {
          out.push_back(*p++);
        }
      }
      // size prefix
      bool sized = false;
      bool narrowPrefix = false;
      bool widePrefix = false;
      if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
        out.push_back('l');
        out.push_back('l');
        p += 3;
        sized = true;
      } else if (p[0] == 'I' && p[1] == '3' && p[2] == '2') {
        p += 3;
        sized = true;
      } else if (p[0] == 'I') {
        out.push_back('z');
        p += 1;
        sized = true;
      } else if (p[0] == 'h' && (p[1] == 's' || p[1] == 'S' || p[1] == 'c' || p[1] == 'C')) {
        narrowPrefix = true;
        p += 1;
      } else if ((p[0] == 'l' || p[0] == 'w') && (p[1] == 's' || p[1] == 'S' || p[1] == 'c' || p[1] == 'C')) {
        widePrefix = true;
        p += 1;
      } else {
        while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'q') {
          out.push_back(*p++);
          sized = true;
        }
      }
      const Char conversion = *p;
      if (conversion == 0) {
        break;
      }
      ++p;
      const bool isString = conversion == 's' || conversion == 'S';
      const bool isChar = conversion == 'c' || conversion == 'C';
      if ((isString || isChar) && !sized) {
        bool wide = false;
        if (widePrefix) {
          wide = true;
        } else if (narrowPrefix) {
          wide = false;
        } else if constexpr (sizeof(Char) == 1) {
          wide = conversion == 'S' || conversion == 'C';
        } else {
          wide = conversion == 's' || conversion == 'c';
        }
        if (wide) {
          out.push_back('l');
        }
        out.push_back(isString ? 's' : 'c');
        continue;
      }
      out.push_back(conversion);
    }
    return out;
  }

  bool NeedsTranslation(const char* const format) noexcept
  {
    return std::strpbrk(format, "SCI") != nullptr || std::strstr(format, "%h") != nullptr ||
           std::strstr(format, "%w") != nullptr;
  }

  // Length the formatted text needs (without terminator), or -1.
  int FormattedLength(const char* const format, va_list arguments) noexcept
  {
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    return length;
  }

  int FormatNarrow(char* const buffer, const std::size_t size, const char* const format, va_list arguments) noexcept
  {
    try {
      if (NeedsTranslation(format)) {
        const std::string translated = PosixFormat(format);
        return std::vsnprintf(buffer, size, translated.c_str(), arguments);
      }
      return std::vsnprintf(buffer, size, format, arguments);
    } catch (...) {
      errno = ENOMEM;
      return -1;
    }
  }

  // vswprintf has no "measure only" mode: grow a scratch buffer until it fits.
  int FormatWideMeasured(const wchar_t* const format, va_list arguments, std::wstring* const out) noexcept
  {
    try {
      const std::wstring translated = PosixFormat(format);
      std::size_t capacity = 256;
      for (;;) {
        out->assign(capacity, L'\0');
        va_list copy;
        va_copy(copy, arguments);
        const int length = std::vswprintf(out->data(), capacity, translated.c_str(), copy);
        va_end(copy);
        if (length >= 0) {
          out->resize(static_cast<std::size_t>(length));
          return length;
        }
        if (capacity >= (1u << 24)) {
          return -1; // encoding error rather than lack of room
        }
        capacity *= 4;
      }
    } catch (...) {
      errno = ENOMEM;
      return -1;
    }
  }

  std::atomic<_invalid_parameter_handler> gInvalidParameterHandler{nullptr};

  void InvalidParameter(const char* const what) noexcept
  {
    if (const _invalid_parameter_handler handler = gInvalidParameterHandler.load()) {
      handler(nullptr, nullptr, nullptr, 0, 0);
      return;
    }
    std::fprintf(stderr, "[faf-posix] CRT invalid parameter: %s\n", what);
  }

  template <class Char>
  std::size_t Length(const Char* const text, const std::size_t limit) noexcept
  {
    std::size_t n = 0;
    while (n < limit && text[n] != 0) {
      ++n;
    }
    return n;
  }

  // strncpy_s / wcsncpy_s (count == _TRUNCATE: copy what fits, return STRUNCATE).
  template <class Char>
  errno_t CopyCounted(Char* const destination, const std::size_t size, const Char* const source, const std::size_t count,
                      const char* const name) noexcept
  {
    if (destination == nullptr || size == 0) {
      InvalidParameter(name);
      return EINVAL;
    }
    if (source == nullptr) {
      destination[0] = 0;
      InvalidParameter(name);
      return EINVAL;
    }
    const std::size_t sourceLength = Length(source, count);
    if (count == _TRUNCATE && sourceLength >= size) {
      std::memcpy(destination, source, (size - 1) * sizeof(Char));
      destination[size - 1] = 0;
      return STRUNCATE;
    }
    if (sourceLength >= size) {
      destination[0] = 0;
      InvalidParameter(name);
      return ERANGE;
    }
    std::memcpy(destination, source, sourceLength * sizeof(Char));
    destination[sourceLength] = 0;
    return 0;
  }

  template <class Char>
  errno_t AppendCounted(Char* const destination, const std::size_t size, const Char* const source,
                        const std::size_t count, const char* const name) noexcept
  {
    if (destination == nullptr || size == 0) {
      InvalidParameter(name);
      return EINVAL;
    }
    const std::size_t existing = Length(destination, size);
    if (existing == size) {
      destination[0] = 0; // unterminated destination
      InvalidParameter(name);
      return EINVAL;
    }
    if (source == nullptr) {
      destination[0] = 0;
      InvalidParameter(name);
      return EINVAL;
    }
    const std::size_t sourceLength = Length(source, count);
    const std::size_t room = size - existing - 1;
    if (sourceLength > room) {
      if (count == _TRUNCATE) {
        std::memcpy(destination + existing, source, room * sizeof(Char));
        destination[size - 1] = 0;
        return STRUNCATE;
      }
      destination[0] = 0;
      InvalidParameter(name);
      return ERANGE;
    }
    std::memcpy(destination + existing, source, sourceLength * sizeof(Char));
    destination[existing + sourceLength] = 0;
    return 0;
  }

  template <class Char>
  errno_t CaseCopy(Char* const text, const std::size_t size, const bool upper, const char* const name) noexcept
  {
    if (text == nullptr || size == 0) {
      InvalidParameter(name);
      return EINVAL;
    }
    const std::size_t length = Length(text, size);
    if (length == size) {
      text[0] = 0;
      InvalidParameter(name);
      return EINVAL;
    }
    for (std::size_t i = 0; i < length; ++i) {
      if constexpr (sizeof(Char) == 1) {
        if (upper && text[i] >= 'a' && text[i] <= 'z') {
          text[i] = static_cast<Char>(text[i] - 'a' + 'A');
        } else if (!upper && text[i] >= 'A' && text[i] <= 'Z') {
          text[i] = static_cast<Char>(text[i] - 'A' + 'a');
        }
      } else {
        text[i] = static_cast<Char>(upper ? std::towupper(static_cast<wint_t>(text[i]))
                                          : std::towlower(static_cast<wint_t>(text[i])));
      }
    }
    return 0;
  }

  template <class Unsigned, class Char>
  Char* FormatInteger(Unsigned magnitude, const bool negative, Char* const buffer, const int radix) noexcept
  {
    if (buffer == nullptr) {
      return buffer;
    }
    if (radix < 2 || radix > 36) {
      buffer[0] = 0;
      return buffer;
    }
    Char digits[72];
    std::size_t n = 0;
    do {
      const unsigned digit = static_cast<unsigned>(magnitude % static_cast<Unsigned>(radix));
      digits[n++] = static_cast<Char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
      magnitude /= static_cast<Unsigned>(radix);
    } while (magnitude != 0);
    std::size_t out = 0;
    if (negative) {
      buffer[out++] = '-';
    }
    while (n > 0) {
      buffer[out++] = digits[--n];
    }
    buffer[out] = 0;
    return buffer;
  }

  // Signed conversions: only radix 10 prints a sign; other radixes print the two's
  // complement bit pattern, as the CRT does.
  template <class Signed, class Char>
  Char* FormatSigned(const Signed value, Char* const buffer, const int radix) noexcept
  {
    using Unsigned = std::make_unsigned_t<Signed>;
    if (radix == 10 && value < 0) {
      return FormatInteger(static_cast<Unsigned>(0) - static_cast<Unsigned>(value), true, buffer, radix);
    }
    return FormatInteger(static_cast<Unsigned>(value), false, buffer, radix);
  }

  template <class Signed>
  errno_t FormatSignedChecked(const Signed value, char* const buffer, const std::size_t size, const int radix,
                              const bool isSigned) noexcept
  {
    if (buffer == nullptr || size == 0 || radix < 2 || radix > 36) {
      if (buffer != nullptr && size != 0) {
        buffer[0] = '\0';
      }
      InvalidParameter("_itoa_s");
      return EINVAL;
    }
    char scratch[72];
    if (isSigned) {
      FormatSigned(value, scratch, radix);
    } else {
      FormatInteger(static_cast<std::make_unsigned_t<Signed>>(value), false, scratch, radix);
    }
    const std::size_t length = std::strlen(scratch);
    if (length + 1 > size) {
      buffer[0] = '\0';
      InvalidParameter("_itoa_s");
      return ERANGE;
    }
    std::memcpy(buffer, scratch, length + 1);
    return 0;
  }
} // namespace

// --- Shared conversions (WinCompatInternal.h) ---------------------------------------------------

namespace faf_posix
{
  std::string Utf8FromWide(const wchar_t* const text, const std::size_t length)
  {
    std::string out;
    if (text == nullptr) {
      return out;
    }
    const std::size_t n = length == kNulTerminated ? std::wcslen(text) : length;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      char32_t value = static_cast<char32_t>(text[i]);
      // UTF-16 surrogate pairs (text built for Windows' 16-bit wchar_t) are combined.
      if (value >= 0xD800 && value <= 0xDBFF && i + 1 < n) {
        const char32_t low = static_cast<char32_t>(text[i + 1]);
        if (low >= 0xDC00 && low <= 0xDFFF) {
          value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
          ++i;
        }
      }
      (void)EncodeUtf8(value, out);
    }
    return out;
  }

  std::wstring WideFromUtf8(const char* const text, const std::size_t length)
  {
    std::wstring out;
    if (text == nullptr) {
      return out;
    }
    const std::size_t n = length == kNulTerminated ? std::strlen(text) : length;
    out.reserve(n);
    const auto* const bytes = reinterpret_cast<const unsigned char*>(text);
    std::size_t i = 0;
    while (i < n) {
      char32_t value = 0;
      bool valid = true;
      i += DecodeUtf8(bytes + i, n - i, &value, &valid);
      out.push_back(static_cast<wchar_t>(value));
    }
    return out;
  }

  std::size_t DecodeUtf8Raw(const char* const text, wchar_t* const out, const std::size_t capacity) noexcept
  {
    if (text == nullptr) {
      if (out != nullptr && capacity != 0) {
        out[0] = L'\0';
      }
      return 0;
    }
    const auto* const bytes = reinterpret_cast<const unsigned char*>(text);
    const std::size_t n = std::strlen(text);
    std::size_t i = 0;
    std::size_t written = 0;
    while (i < n) {
      char32_t value = 0;
      bool valid = true;
      i += DecodeUtf8(bytes + i, n - i, &value, &valid);
      if (out != nullptr && written + 1 < capacity) {
        out[written] = static_cast<wchar_t>(value);
      }
      ++written;
    }
    if (out != nullptr && capacity != 0) {
      out[written < capacity ? written : capacity - 1] = L'\0';
    }
    return written;
  }

  DWORD CopyOutString(const std::string& value, char* const buffer, const DWORD capacity) noexcept
  {
    if (buffer == nullptr || value.size() + 1 > capacity) {
      return static_cast<DWORD>(value.size() + 1);
    }
    std::memcpy(buffer, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
  }

  DWORD CopyOutString(const std::wstring& value, wchar_t* const buffer, const DWORD capacity) noexcept
  {
    if (buffer == nullptr || value.size() + 1 > capacity) {
      return static_cast<DWORD>(value.size() + 1);
    }
    std::wmemcpy(buffer, value.c_str(), value.size() + 1);
    return static_cast<DWORD>(value.size());
  }
} // namespace faf_posix

// --- Case and comparison --------------------------------------------------------------------------

int _memicmp(const void* const a, const void* const b, const std::size_t count) noexcept
{
  const auto* const left = static_cast<const unsigned char*>(a);
  const auto* const right = static_cast<const unsigned char*>(b);
  for (std::size_t i = 0; i < count; ++i) {
    int l = left[i];
    int r = right[i];
    if (l >= 'A' && l <= 'Z') {
      l += 'a' - 'A';
    }
    if (r >= 'A' && r <= 'Z') {
      r += 'a' - 'A';
    }
    if (l != r) {
      return l - r;
    }
  }
  return 0;
}

wchar_t* _wcslwr(wchar_t* const s) noexcept
{
  for (wchar_t* p = s; *p != L'\0'; ++p) {
    *p = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(*p)));
  }
  return s;
}

wchar_t* _wcsupr(wchar_t* const s) noexcept
{
  for (wchar_t* p = s; *p != L'\0'; ++p) {
    *p = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(*p)));
  }
  return s;
}

char* _strrev(char* const s) noexcept
{
  std::reverse(s, s + std::strlen(s));
  return s;
}

// --- Code pages -------------------------------------------------------------------------------------

int MultiByteToWideChar(const UINT codePage, const DWORD flags, LPCSTR const source, const int sourceLength,
                        LPWSTR const destination, const int destinationLength) noexcept
{
  const Charset charset = CharsetOf(codePage);
  if (charset == Charset::Invalid || source == nullptr || sourceLength == 0 || destinationLength < 0 ||
      (destination == nullptr && destinationLength != 0)) {
    ::SetLastError(charset == Charset::Invalid ? ERROR_INVALID_PARAMETER : ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    const std::size_t length = sourceLength < 0 ? std::strlen(source) + 1 : static_cast<std::size_t>(sourceLength);
    std::u32string decoded;
    if (!DecodeMultiByte(charset, reinterpret_cast<const unsigned char*>(source), length,
                         (flags & MB_ERR_INVALID_CHARS) != 0, decoded)) {
      ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
      return 0;
    }
    if (destinationLength == 0) {
      return static_cast<int>(decoded.size());
    }
    if (decoded.size() > static_cast<std::size_t>(destinationLength)) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    for (std::size_t i = 0; i < decoded.size(); ++i) {
      destination[i] = static_cast<wchar_t>(decoded[i]);
    }
    return static_cast<int>(decoded.size());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

int WideCharToMultiByte(const UINT codePage, const DWORD flags, LPCWSTR const source, const int sourceLength,
                        LPSTR const destination, const int destinationLength, LPCSTR const defaultChar,
                        LPBOOL const usedDefaultChar) noexcept
{
  const Charset charset = CharsetOf(codePage);
  if (charset == Charset::Invalid || source == nullptr || sourceLength == 0 || destinationLength < 0 ||
      (destination == nullptr && destinationLength != 0) ||
      (charset == Charset::Utf8 && (defaultChar != nullptr || usedDefaultChar != nullptr))) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    const std::size_t length = sourceLength < 0 ? std::wcslen(source) + 1 : static_cast<std::size_t>(sourceLength);
    std::u32string text;
    text.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
      text.push_back(static_cast<char32_t>(source[i]));
    }
    bool usedDefault = false;
    const std::string encoded =
      EncodeMultiByte(charset, text, defaultChar != nullptr ? defaultChar[0] : '?', &usedDefault);
    if (usedDefault && (flags & WC_ERR_INVALID_CHARS) != 0) {
      ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
      return 0;
    }
    if (usedDefaultChar != nullptr) {
      *usedDefaultChar = usedDefault ? TRUE : FALSE;
    }
    if (destinationLength == 0) {
      return static_cast<int>(encoded.size());
    }
    if (encoded.size() > static_cast<std::size_t>(destinationLength)) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }
    std::memcpy(destination, encoded.data(), encoded.size());
    return static_cast<int>(encoded.size());
  } catch (...) {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

UINT GetACP() noexcept
{
  return CP_UTF8;
}

UINT GetOEMCP() noexcept
{
  return CP_UTF8;
}

BOOL GetCPInfo(const UINT codePage, LPCPINFO const info) noexcept
{
  const Charset charset = CharsetOf(codePage);
  if (info == nullptr || charset == Charset::Invalid) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::memset(info, 0, sizeof(*info));
  info->MaxCharSize = charset == Charset::Utf8 ? 4 : 1;
  info->DefaultChar[0] = '?';
  return TRUE;
}

BOOL IsValidCodePage(const UINT codePage) noexcept
{
  return CharsetOf(codePage) != Charset::Invalid && codePage > CP_THREAD_ACP ? TRUE : FALSE;
}

BOOL IsDBCSLeadByte(const BYTE testChar) noexcept
{
  (void)testChar;
  return FALSE;
}

BOOL IsDBCSLeadByteEx(const UINT codePage, const BYTE testChar) noexcept
{
  (void)codePage;
  (void)testChar;
  return FALSE;
}

// --- Secure CRT ------------------------------------------------------------------------------------

_invalid_parameter_handler _set_invalid_parameter_handler(const _invalid_parameter_handler handler) noexcept
{
  return gInvalidParameterHandler.exchange(handler);
}

_invalid_parameter_handler _get_invalid_parameter_handler() noexcept
{
  return gInvalidParameterHandler.load();
}

void _invalid_parameter_noinfo() noexcept
{
  InvalidParameter("_invalid_parameter_noinfo");
}

void _invalid_parameter_noinfo_noreturn() noexcept
{
  InvalidParameter("_invalid_parameter_noinfo_noreturn");
  std::abort();
}

errno_t strcpy_s(char* const destination, const std::size_t size, const char* const source) noexcept
{
  return CopyCounted(destination, size, source, static_cast<std::size_t>(-2), "strcpy_s");
}

errno_t strncpy_s(char* const destination, const std::size_t size, const char* const source,
                  const std::size_t count) noexcept
{
  if (count == 0 && destination != nullptr && size != 0) {
    destination[0] = '\0';
    return 0;
  }
  return CopyCounted(destination, size, source, count, "strncpy_s");
}

errno_t strcat_s(char* const destination, const std::size_t size, const char* const source) noexcept
{
  return AppendCounted(destination, size, source, static_cast<std::size_t>(-2), "strcat_s");
}

errno_t strncat_s(char* const destination, const std::size_t size, const char* const source,
                  const std::size_t count) noexcept
{
  return AppendCounted(destination, size, source, count, "strncat_s");
}

errno_t wcscpy_s(wchar_t* const destination, const std::size_t size, const wchar_t* const source) noexcept
{
  return CopyCounted(destination, size, source, static_cast<std::size_t>(-2), "wcscpy_s");
}

errno_t wcsncpy_s(wchar_t* const destination, const std::size_t size, const wchar_t* const source,
                  const std::size_t count) noexcept
{
  if (count == 0 && destination != nullptr && size != 0) {
    destination[0] = L'\0';
    return 0;
  }
  return CopyCounted(destination, size, source, count, "wcsncpy_s");
}

errno_t wcscat_s(wchar_t* const destination, const std::size_t size, const wchar_t* const source) noexcept
{
  return AppendCounted(destination, size, source, static_cast<std::size_t>(-2), "wcscat_s");
}

errno_t wcsncat_s(wchar_t* const destination, const std::size_t size, const wchar_t* const source,
                  const std::size_t count) noexcept
{
  return AppendCounted(destination, size, source, count, "wcsncat_s");
}

errno_t _strlwr_s(char* const text, const std::size_t size) noexcept
{
  return CaseCopy(text, size, false, "_strlwr_s");
}

errno_t _strupr_s(char* const text, const std::size_t size) noexcept
{
  return CaseCopy(text, size, true, "_strupr_s");
}

errno_t _wcslwr_s(wchar_t* const text, const std::size_t size) noexcept
{
  return CaseCopy(text, size, false, "_wcslwr_s");
}

errno_t _wcsupr_s(wchar_t* const text, const std::size_t size) noexcept
{
  return CaseCopy(text, size, true, "_wcsupr_s");
}

char* strtok_s(char* const text, const char* const delimiters, char** const context) noexcept
{
  return ::strtok_r(text, delimiters, context);
}

wchar_t* wcstok_s(wchar_t* const text, const wchar_t* const delimiters, wchar_t** const context) noexcept
{
  return std::wcstok(text, delimiters, context);
}

errno_t strerror_s(char* const buffer, const std::size_t size, const int error) noexcept
{
  if (buffer == nullptr || size == 0) {
    return EINVAL;
  }
  if (::strerror_r(error, buffer, size) != 0) {
    buffer[size - 1] = '\0';
  }
  return 0;
}

errno_t mbstowcs_s(std::size_t* const converted, wchar_t* const destination, const std::size_t size,
                   const char* const source, const std::size_t count) noexcept
{
  if (source == nullptr) {
    return EINVAL;
  }
  try {
    const std::size_t length = count == _TRUNCATE ? std::strlen(source) : Length(source, count);
    const std::wstring wide = WideFromUtf8(source, length);
    if (destination == nullptr) {
      if (converted != nullptr) {
        *converted = wide.size() + 1;
      }
      return 0;
    }
    std::size_t copy = wide.size();
    errno_t result = 0;
    if (copy + 1 > size) {
      if (count != _TRUNCATE || size == 0) {
        if (size != 0) {
          destination[0] = L'\0';
        }
        return ERANGE;
      }
      copy = size - 1;
      result = STRUNCATE;
    }
    std::wmemcpy(destination, wide.data(), copy);
    destination[copy] = L'\0';
    if (converted != nullptr) {
      *converted = copy + 1;
    }
    return result;
  } catch (...) {
    return ENOMEM;
  }
}

errno_t wcstombs_s(std::size_t* const converted, char* const destination, const std::size_t size,
                   const wchar_t* const source, const std::size_t count) noexcept
{
  if (source == nullptr) {
    return EINVAL;
  }
  try {
    const std::string narrow = Utf8FromWide(source);
    std::size_t copy = std::min(narrow.size(), count == _TRUNCATE ? narrow.size() : count);
    if (destination == nullptr) {
      if (converted != nullptr) {
        *converted = narrow.size() + 1;
      }
      return 0;
    }
    errno_t result = 0;
    if (copy + 1 > size) {
      if (count != _TRUNCATE || size == 0) {
        if (size != 0) {
          destination[0] = '\0';
        }
        return ERANGE;
      }
      copy = size - 1;
      while (copy > 0 && (static_cast<unsigned char>(narrow[copy]) & 0xC0) == 0x80) {
        --copy;
      }
      result = STRUNCATE;
    }
    std::memcpy(destination, narrow.data(), copy);
    destination[copy] = '\0';
    if (converted != nullptr) {
      *converted = copy + 1;
    }
    return result;
  } catch (...) {
    return ENOMEM;
  }
}

// --- printf family ------------------------------------------------------------------------------

int vsprintf_s(char* const buffer, const std::size_t size, const char* const format, va_list arguments) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    if (buffer != nullptr && size != 0) {
      buffer[0] = '\0';
    }
    InvalidParameter("vsprintf_s");
    errno = EINVAL;
    return -1;
  }
  const int length = FormatNarrow(buffer, size, format, arguments);
  if (length < 0 || static_cast<std::size_t>(length) >= size) {
    buffer[0] = '\0';
    InvalidParameter("vsprintf_s: buffer too small");
    errno = ERANGE;
    return -1;
  }
  return length;
}

int sprintf_s(char* const buffer, const std::size_t size, const char* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vsprintf_s(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}

int _vsnprintf_s(char* const buffer, const std::size_t size, const std::size_t count, const char* const format,
                 va_list arguments) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    if (buffer != nullptr && size != 0) {
      buffer[0] = '\0';
    }
    InvalidParameter("_vsnprintf_s");
    errno = EINVAL;
    return -1;
  }
  const std::size_t limit = count == _TRUNCATE ? size : std::min(size, count + 1);
  va_list copy;
  va_copy(copy, arguments);
  const int length = FormatNarrow(buffer, limit, format, copy);
  va_end(copy);
  if (length < 0) {
    buffer[0] = '\0';
    return -1;
  }
  if (static_cast<std::size_t>(length) < limit) {
    return length;
  }
  // Did not fit in `limit - 1` characters.
  if (count == _TRUNCATE) {
    return -1; // truncated, terminated by vsnprintf
  }
  if (count < size) {
    return -1; // count characters written and terminated
  }
  buffer[0] = '\0';
  InvalidParameter("_vsnprintf_s: buffer too small");
  errno = ERANGE;
  return -1;
}

int vsnprintf_s(char* const buffer, const std::size_t size, const std::size_t count, const char* const format,
                va_list arguments) noexcept
{
  return _vsnprintf_s(buffer, size, count, format, arguments);
}

int _snprintf_s(char* const buffer, const std::size_t size, const std::size_t count, const char* const format,
                ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnprintf_s(buffer, size, count, format, arguments);
  va_end(arguments);
  return result;
}

int _vsnprintf(char* const buffer, const std::size_t count, const char* const format, va_list arguments) noexcept
{
  if (format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  va_list copy;
  va_copy(copy, arguments);
  const int length = FormatNarrow(nullptr, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    return -1;
  }
  if (static_cast<std::size_t>(length) < count) {
    return FormatNarrow(buffer, count, format, arguments);
  }
  if (count == 0 || buffer == nullptr) {
    return -1;
  }
  // MSVC: the first `count` characters, no terminator.
  try {
    std::vector<char> scratch(static_cast<std::size_t>(length) + 1);
    (void)FormatNarrow(scratch.data(), scratch.size(), format, arguments);
    std::memcpy(buffer, scratch.data(), count);
  } catch (...) {
    errno = ENOMEM;
  }
  return -1;
}

int _snprintf(char* const buffer, const std::size_t count, const char* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnprintf(buffer, count, format, arguments);
  va_end(arguments);
  return result;
}

int _vscprintf(const char* const format, va_list arguments) noexcept
{
  if (format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  return FormatNarrow(nullptr, 0, format, arguments);
}

int _scprintf(const char* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vscprintf(format, arguments);
  va_end(arguments);
  return result;
}

int vswprintf_s(wchar_t* const buffer, const std::size_t size, const wchar_t* const format,
                va_list arguments) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    if (buffer != nullptr && size != 0) {
      buffer[0] = L'\0';
    }
    InvalidParameter("vswprintf_s");
    errno = EINVAL;
    return -1;
  }
  std::wstring text;
  const int length = FormatWideMeasured(format, arguments, &text);
  if (length < 0 || static_cast<std::size_t>(length) >= size) {
    buffer[0] = L'\0';
    InvalidParameter("vswprintf_s: buffer too small");
    errno = ERANGE;
    return -1;
  }
  std::wmemcpy(buffer, text.c_str(), text.size() + 1);
  return length;
}

int swprintf_s(wchar_t* const buffer, const std::size_t size, const wchar_t* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vswprintf_s(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}

int _vsnwprintf_s(wchar_t* const buffer, const std::size_t size, const std::size_t count, const wchar_t* const format,
                  va_list arguments) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    if (buffer != nullptr && size != 0) {
      buffer[0] = L'\0';
    }
    InvalidParameter("_vsnwprintf_s");
    errno = EINVAL;
    return -1;
  }
  std::wstring text;
  const int length = FormatWideMeasured(format, arguments, &text);
  if (length < 0) {
    buffer[0] = L'\0';
    return -1;
  }
  const std::size_t limit = count == _TRUNCATE ? size - 1 : std::min(size - 1, count);
  if (static_cast<std::size_t>(length) <= limit) {
    std::wmemcpy(buffer, text.c_str(), text.size() + 1);
    return length;
  }
  if (count == _TRUNCATE || count < size) {
    std::wmemcpy(buffer, text.c_str(), limit);
    buffer[limit] = L'\0';
    return -1;
  }
  buffer[0] = L'\0';
  InvalidParameter("_vsnwprintf_s: buffer too small");
  errno = ERANGE;
  return -1;
}

int _snwprintf_s(wchar_t* const buffer, const std::size_t size, const std::size_t count, const wchar_t* const format,
                 ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnwprintf_s(buffer, size, count, format, arguments);
  va_end(arguments);
  return result;
}

int _vsnwprintf(wchar_t* const buffer, const std::size_t count, const wchar_t* const format,
                va_list arguments) noexcept
{
  if (format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  std::wstring text;
  const int length = FormatWideMeasured(format, arguments, &text);
  if (length < 0) {
    return -1;
  }
  if (static_cast<std::size_t>(length) < count) {
    std::wmemcpy(buffer, text.c_str(), text.size() + 1);
    return length;
  }
  if (buffer != nullptr && count != 0) {
    std::wmemcpy(buffer, text.c_str(), count); // MSVC: no terminator when it does not fit
  }
  return -1;
}

int _snwprintf(wchar_t* const buffer, const std::size_t count, const wchar_t* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vsnwprintf(buffer, count, format, arguments);
  va_end(arguments);
  return result;
}

int _vscwprintf(const wchar_t* const format, va_list arguments) noexcept
{
  if (format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  std::wstring text;
  return FormatWideMeasured(format, arguments, &text);
}

int _scwprintf(const wchar_t* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = _vscwprintf(format, arguments);
  va_end(arguments);
  return result;
}

// --- Number conversions ---------------------------------------------------------------------------

char* _itoa(const int value, char* const buffer, const int radix) noexcept
{
  return FormatSigned(value, buffer, radix);
}

char* _ltoa(const long value, char* const buffer, const int radix) noexcept
{
  // MSVC long is 32 bits.
  return FormatSigned(static_cast<std::int32_t>(value), buffer, radix);
}

char* _ultoa(const unsigned long value, char* const buffer, const int radix) noexcept
{
  return FormatInteger(static_cast<std::uint32_t>(value), false, buffer, radix);
}

char* _i64toa(const long long value, char* const buffer, const int radix) noexcept
{
  return FormatSigned(value, buffer, radix);
}

char* _ui64toa(const unsigned long long value, char* const buffer, const int radix) noexcept
{
  return FormatInteger(value, false, buffer, radix);
}

wchar_t* _itow(const int value, wchar_t* const buffer, const int radix) noexcept
{
  return FormatSigned(value, buffer, radix);
}

errno_t _itoa_s(const int value, char* const buffer, const std::size_t size, const int radix) noexcept
{
  return FormatSignedChecked(value, buffer, size, radix, true);
}

errno_t _i64toa_s(const long long value, char* const buffer, const std::size_t size, const int radix) noexcept
{
  return FormatSignedChecked(value, buffer, size, radix, true);
}

errno_t _ui64toa_s(const unsigned long long value, char* const buffer, const std::size_t size, const int radix) noexcept
{
  return FormatSignedChecked(static_cast<long long>(value), buffer, size, radix, false);
}

// --- Secure scanf ---------------------------------------------------------------------------------
// One directive at a time. White space, literals, %%, %n and the counted text conversions
// (%c %s %[ and MSVC's %C %S) are scanned here, because their buffer-size argument and the
// "C" locale narrow <-> wide mapping are MSVC's; each numeric conversion is handed to the C
// library's scanner on the remaining input with a trailing %n (its own size prefix rewritten
// to the POSIX one), which reports how much it consumed.

namespace
{
  template <class Char>
  bool IsScanSpace(const Char c) noexcept
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
  }

  int SystemScanOne(const char* const input, const char* const format, void* const target, int* const consumed) noexcept
  {
    return target != nullptr ? std::sscanf(input, format, target, consumed) : std::sscanf(input, format, consumed);
  }

  int SystemScanOne(const wchar_t* const input, const wchar_t* const format, void* const target,
                    int* const consumed) noexcept
  {
    return target != nullptr ? std::swscanf(input, format, target, consumed) : std::swscanf(input, format, consumed);
  }

  enum class ScanSize
  {
    Default,
    Hh,
    H,
    L,     // MSVC `l`: 32-bit long for integers, double for floating point, wide for text
    Ll,    // ll and I64
    BigL,  // L
    I32,
    Ptr,   // I, z
    T,
    J,
    W      // MSVC `w`: wide text
  };

  // `[` set: `^` negates, a leading `]` is a member, `a-z` is a range (either order).
  template <class Char>
  struct ScanSet
  {
    const Char* begin = nullptr;
    const Char* end = nullptr;
    bool negated = false;

    // Members compare as unsigned code units (bytes >= 0x80 sort high, as in the CRT's table).
    static std::uint32_t Unit(const Char c) noexcept
    {
      return static_cast<std::uint32_t>(static_cast<std::make_unsigned_t<Char>>(c));
    }

    bool Contains(const Char c) const noexcept
    {
      const std::uint32_t value = Unit(c);
      bool found = false;
      for (const Char* p = begin; p < end && !found; ++p) {
        if (p + 2 < end && p[1] == '-') {
          std::uint32_t low = Unit(p[0]);
          std::uint32_t high = Unit(p[2]);
          if (low > high) {
            std::swap(low, high);
          }
          found = value >= low && value <= high;
          p += 2;
        } else {
          found = value == Unit(*p);
        }
      }
      return found != negated;
    }
  };

  template <class Char>
  int ScanSecure(const Char* const input, const Char* const format, va_list arguments, const char* const name) noexcept
  {
    if (input == nullptr || format == nullptr) {
      InvalidParameter(name);
      errno = EINVAL;
      return EOF;
    }
    try {
      std::size_t pos = 0;
      int assigned = 0;
      bool converted = false; // EOF only for an input failure before the first conversion
      const auto inputFailure = [&]() noexcept { return converted ? assigned : EOF; };
      const Char* f = format;
      while (*f != 0) {
        if (IsScanSpace(*f)) {
          while (IsScanSpace(*f)) {
            ++f;
          }
          while (IsScanSpace(input[pos])) {
            ++pos;
          }
          continue;
        }
        if (*f != '%' || f[1] == '%') {
          const Char literal = *f;
          f += (*f == '%') ? 2 : 1;
          if (literal == '%') {
            while (IsScanSpace(input[pos])) {
              ++pos;
            }
          }
          if (input[pos] == 0) {
            return inputFailure();
          }
          if (input[pos] != literal) {
            return assigned;
          }
          ++pos;
          continue;
        }

        ++f;
        bool suppress = false;
        if (*f == '*') {
          suppress = true;
          ++f;
        }
        bool hasWidth = false;
        std::size_t width = 0;
        while (*f >= '0' && *f <= '9') {
          hasWidth = true;
          width = width * 10 + static_cast<std::size_t>(*f - '0');
          ++f;
        }
        ScanSize size = ScanSize::Default;
        if (f[0] == 'h' && f[1] == 'h') {
          size = ScanSize::Hh;
          f += 2;
        } else if (f[0] == 'h') {
          size = ScanSize::H;
          ++f;
        } else if (f[0] == 'l' && f[1] == 'l') {
          size = ScanSize::Ll;
          f += 2;
        } else if (f[0] == 'l') {
          size = ScanSize::L;
          ++f;
        } else if (f[0] == 'L') {
          size = ScanSize::BigL;
          ++f;
        } else if (f[0] == 'I' && f[1] == '6' && f[2] == '4') {
          size = ScanSize::Ll;
          f += 3;
        } else if (f[0] == 'I' && f[1] == '3' && f[2] == '2') {
          size = ScanSize::I32;
          f += 3;
        } else if (f[0] == 'I' || f[0] == 'z') {
          size = ScanSize::Ptr;
          ++f;
        } else if (f[0] == 't') {
          size = ScanSize::T;
          ++f;
        } else if (f[0] == 'j') {
          size = ScanSize::J;
          ++f;
        } else if (f[0] == 'w') {
          size = ScanSize::W;
          ++f;
        }
        const Char conversion = *f;
        if (conversion == 0) {
          return assigned; // the format ends inside a directive
        }
        ++f;

        // --- %n: characters consumed so far; not counted ---
        if (conversion == 'n') {
          if (!suppress) {
            void* const target = va_arg(arguments, void*);
            switch (size) {
              case ScanSize::Hh:
                *static_cast<signed char*>(target) = static_cast<signed char>(pos);
                break;
              case ScanSize::H:
                *static_cast<short*>(target) = static_cast<short>(pos);
                break;
              case ScanSize::Ll:
              case ScanSize::BigL:
              case ScanSize::Ptr:
              case ScanSize::T:
              case ScanSize::J:
                *static_cast<long long*>(target) = static_cast<long long>(pos);
                break;
              default: // int, and MSVC's 32-bit long
                *static_cast<int*>(target) = static_cast<int>(pos);
                break;
            }
          }
          continue;
        }

        // --- counted text: %c %s %[ %C %S ---
        if (conversion == 'c' || conversion == 's' || conversion == '[' || conversion == 'C' ||
            conversion == 'S') {
          const bool upper = conversion == 'C' || conversion == 'S';
          bool wideTarget = false;
          if (size == ScanSize::L || size == ScanSize::W) {
            wideTarget = true;
          } else if (size == ScanSize::H) {
            wideTarget = false;
          } else if constexpr (sizeof(Char) == 1) {
            wideTarget = upper;
          } else {
            wideTarget = !upper;
          }
          ScanSet<Char> set{};
          if (conversion == '[') {
            if (*f == '^') {
              set.negated = true;
              ++f;
            }
            set.begin = f;
            if (*f == ']') {
              ++f;
            }
            while (*f != 0 && *f != ']') {
              ++f;
            }
            if (*f == 0) {
              return assigned; // unterminated set
            }
            set.end = f;
            ++f;
          }
          const bool isChar = conversion == 'c' || conversion == 'C';
          if (conversion == 's' || conversion == 'S') {
            while (IsScanSpace(input[pos])) {
              ++pos;
            }
          }
          if (input[pos] == 0) {
            return inputFailure();
          }
          const std::size_t start = pos;
          if (isChar) {
            const std::size_t count = hasWidth ? width : 1;
            while (pos - start < count && input[pos] != 0) {
              ++pos;
            }
            if (pos - start < count) {
              return inputFailure(); // input ended inside the field
            }
          } else if (conversion == '[') {
            while (input[pos] != 0 && (!hasWidth || pos - start < width) && set.Contains(input[pos])) {
              ++pos;
            }
            if (pos == start) {
              return assigned; // matching failure
            }
          } else {
            while (input[pos] != 0 && (!hasWidth || pos - start < width) && !IsScanSpace(input[pos])) {
              ++pos;
            }
          }
          const std::size_t count = pos - start;
          if (!suppress) {
            void* const target = va_arg(arguments, void*);
            const unsigned int capacity = va_arg(arguments, unsigned int);
            const std::size_t needed = count + (isChar ? 0 : 1);
            if (target == nullptr) {
              InvalidParameter(name);
              errno = EINVAL;
              return assigned;
            }
            if (needed > capacity) {
              // MSVC: the buffer is reset to an empty string and the scan stops.
              if (capacity > 0) {
                if (wideTarget) {
                  *static_cast<wchar_t*>(target) = L'\0';
                } else {
                  *static_cast<char*>(target) = '\0';
                }
              }
              errno = ENOMEM;
              return assigned;
            }
            if (wideTarget) {
              auto* const out = static_cast<wchar_t*>(target);
              for (std::size_t i = 0; i < count; ++i) {
                // "C" locale: a byte is the code unit of the same value.
                out[i] = sizeof(Char) == 1 ? static_cast<wchar_t>(static_cast<unsigned char>(input[start + i]))
                                           : static_cast<wchar_t>(input[start + i]);
              }
              if (!isChar) {
                out[count] = L'\0';
              }
            } else {
              auto* const out = static_cast<char*>(target);
              for (std::size_t i = 0; i < count; ++i) {
                const auto unit = static_cast<std::uint32_t>(input[start + i]);
                if (sizeof(Char) != 1 && unit > 0xFF) {
                  errno = EILSEQ; // "C" locale wctomb: no byte for this character
                  return assigned;
                }
                out[i] = static_cast<char>(unit);
              }
              if (!isChar) {
                out[count] = '\0';
              }
            }
            ++assigned;
          }
          converted = true;
          continue;
        }

        // --- numeric conversions: the C library's scanner, POSIX size prefix ---
        const bool isInteger = conversion == 'd' || conversion == 'i' || conversion == 'o' || conversion == 'u' ||
                               conversion == 'x' || conversion == 'X';
        const bool isFloating = conversion == 'e' || conversion == 'E' || conversion == 'f' || conversion == 'F' ||
                                conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
        if (!isInteger && !isFloating && conversion != 'p') {
          InvalidParameter(name);
          errno = EINVAL;
          return assigned;
        }
        std::basic_string<Char> directive;
        directive.push_back('%');
        if (suppress) {
          directive.push_back('*');
        }
        if (hasWidth) {
          const std::string digits = std::to_string(width);
          directive.append(digits.begin(), digits.end());
        }
        const char* prefix = "";
        switch (size) {
          case ScanSize::Hh:
            prefix = isInteger ? "hh" : "";
            break;
          case ScanSize::H:
            prefix = isInteger ? "h" : "";
            break;
          case ScanSize::L:
            prefix = isFloating ? "l" : ""; // Windows long is 32 bits
            break;
          case ScanSize::Ll:
            prefix = isInteger ? "ll" : "";
            break;
          case ScanSize::BigL:
            prefix = isFloating ? "L" : "ll"; // long double is double on both
            break;
          case ScanSize::Ptr:
            prefix = isInteger ? "z" : "";
            break;
          case ScanSize::T:
            prefix = isInteger ? "t" : "";
            break;
          case ScanSize::J:
            prefix = isInteger ? "j" : "";
            break;
          default:
            break;
        }
        for (const char* p = prefix; *p != 0; ++p) {
          directive.push_back(static_cast<Char>(*p));
        }
        directive.push_back(conversion);
        directive.push_back('%');
        directive.push_back('n');
        void* const target = suppress ? nullptr : va_arg(arguments, void*);
        int consumed = -1;
        const int result = SystemScanOne(input + pos, directive.c_str(), target, &consumed);
        if (result == EOF) {
          return inputFailure();
        }
        if (consumed < 0) {
          return assigned; // matching failure
        }
        pos += static_cast<std::size_t>(consumed);
        if (!suppress) {
          ++assigned;
        }
        converted = true;
      }
      return assigned;
    } catch (...) {
      errno = ENOMEM;
      return EOF;
    }
  }
} // namespace

int vsscanf_s(const char* const buffer, const char* const format, va_list arguments) noexcept
{
  return ScanSecure(buffer, format, arguments, "sscanf_s");
}

int sscanf_s(const char* const buffer, const char* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vsscanf_s(buffer, format, arguments);
  va_end(arguments);
  return result;
}

int vswscanf_s(const wchar_t* const buffer, const wchar_t* const format, va_list arguments) noexcept
{
  return ScanSecure(buffer, format, arguments, "swscanf_s");
}

int swscanf_s(const wchar_t* const buffer, const wchar_t* const format, ...) noexcept
{
  va_list arguments;
  va_start(arguments, format);
  const int result = vswscanf_s(buffer, format, arguments);
  va_end(arguments);
  return result;
}
