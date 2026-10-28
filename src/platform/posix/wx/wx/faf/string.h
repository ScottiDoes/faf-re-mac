#pragma once

// wx stand-in: wxString and character set conversion (see wx/defs.h).

#include "wx/defs.h"

#include <algorithm>
#include <cstdarg>
#include <cwctype>
#include <string>
#include <vector>

// --- Buffers returned by the conversions ---------------------------------------------------
class wxCharBuffer
{
public:
  wxCharBuffer() = default;
  explicit wxCharBuffer(const char* text) : mData(text != nullptr ? text : ""), mValid(text != nullptr) {}
  explicit wxCharBuffer(std::string text) : mData(std::move(text)), mValid(true) {}

  operator const char*() const { return mValid ? mData.c_str() : nullptr; }
  const char* data() const { return mValid ? mData.c_str() : nullptr; }
  std::size_t length() const { return mData.size(); }

private:
  std::string mData;
  bool mValid = false;
};

class wxWCharBuffer
{
public:
  wxWCharBuffer() = default;
  explicit wxWCharBuffer(const wchar_t* text) : mData(text != nullptr ? text : L""), mValid(text != nullptr) {}
  explicit wxWCharBuffer(std::wstring text) : mData(std::move(text)), mValid(true) {}

  operator const wchar_t*() const { return mValid ? mData.c_str() : nullptr; }
  const wchar_t* data() const { return mValid ? mData.c_str() : nullptr; }
  std::size_t length() const { return mData.size(); }

private:
  std::wstring mData;
  bool mValid = false;
};

// --- Conversions (wx/strconv.h) -----------------------------------------------------------
// MB2WC/WC2MB keep wx's contract: return the converted length (without the terminator), or
// (size_t)-1 when the input cannot be converted; a null output buffer only measures.
class wxMBConv
{
public:
  virtual ~wxMBConv() = default;

  // The library's wxConvLibc runs mbstowcs in the "C" locale, which maps every byte to the
  // code point of the same value; that is what this base class does.
  virtual size_t MB2WC(wchar_t* buf, const char* psz, size_t n) const
  {
    const size_t length = std::strlen(psz);
    if (buf != nullptr) {
      const size_t count = std::min(length, n);
      for (size_t i = 0; i < count; ++i) {
        buf[i] = static_cast<wchar_t>(static_cast<unsigned char>(psz[i]));
      }
      if (count < n) {
        buf[count] = L'\0';
      }
    }
    return length;
  }

  virtual size_t WC2MB(char* buf, const wchar_t* psz, size_t n) const
  {
    const size_t length = std::wcslen(psz);
    for (size_t i = 0; i < length; ++i) {
      if (static_cast<std::uint32_t>(psz[i]) > 0xFFu) {
        return static_cast<size_t>(-1);
      }
    }
    if (buf != nullptr) {
      const size_t count = std::min(length, n);
      for (size_t i = 0; i < count; ++i) {
        buf[i] = static_cast<char>(static_cast<unsigned char>(psz[i]));
      }
      if (count < n) {
        buf[count] = '\0';
      }
    }
    return length;
  }

  const wxWCharBuffer cMB2WC(const char* psz) const
  {
    if (psz == nullptr) {
      return wxWCharBuffer();
    }
    const size_t length = MB2WC(nullptr, psz, 0);
    if (length == static_cast<size_t>(-1)) {
      return wxWCharBuffer();
    }
    std::wstring out(length + 1, L'\0');
    MB2WC(out.data(), psz, length + 1);
    out.resize(length);
    return wxWCharBuffer(std::move(out));
  }

  const wxCharBuffer cWC2MB(const wchar_t* psz) const
  {
    if (psz == nullptr) {
      return wxCharBuffer();
    }
    const size_t length = WC2MB(nullptr, psz, 0);
    if (length == static_cast<size_t>(-1)) {
      return wxCharBuffer();
    }
    std::string out(length + 1, '\0');
    WC2MB(out.data(), psz, length + 1);
    out.resize(length);
    return wxCharBuffer(std::move(out));
  }

  const wxWCharBuffer cWX2WC(const wxChar* psz) const { return wxWCharBuffer(psz); }
  const wxWCharBuffer cWC2WX(const wchar_t* psz) const { return wxWCharBuffer(psz); }
  const wxCharBuffer cWX2MB(const wxChar* psz) const { return cWC2MB(psz); }
  const wxWCharBuffer cMB2WX(const char* psz) const { return cMB2WC(psz); }
};

class wxMBConvLibc : public wxMBConv
{};

// UTF-8 <-> wchar_t (UTF-32 on macOS). Malformed input fails as wx's converter does.
class wxMBConvUTF8 : public wxMBConv
{
public:
  size_t MB2WC(wchar_t* buf, const char* psz, size_t n) const override
  {
    size_t produced = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(psz);
    while (*p != 0) {
      std::uint32_t cp = 0;
      int extra = 0;
      if (*p < 0x80) {
        cp = *p;
      } else if ((*p & 0xE0) == 0xC0) {
        cp = *p & 0x1F;
        extra = 1;
      } else if ((*p & 0xF0) == 0xE0) {
        cp = *p & 0x0F;
        extra = 2;
      } else if ((*p & 0xF8) == 0xF0) {
        cp = *p & 0x07;
        extra = 3;
      } else {
        return static_cast<size_t>(-1);
      }
      ++p;
      for (int i = 0; i < extra; ++i, ++p) {
        if ((*p & 0xC0) != 0x80) {
          return static_cast<size_t>(-1);
        }
        cp = (cp << 6) | (*p & 0x3F);
      }
      if (buf != nullptr && produced < n) {
        buf[produced] = static_cast<wchar_t>(cp);
      }
      ++produced;
    }
    if (buf != nullptr && produced < n) {
      buf[produced] = L'\0';
    }
    return produced;
  }

  size_t WC2MB(char* buf, const wchar_t* psz, size_t n) const override
  {
    size_t produced = 0;
    auto put = [&](unsigned char byte) {
      if (buf != nullptr && produced < n) {
        buf[produced] = static_cast<char>(byte);
      }
      ++produced;
    };
    for (; *psz != L'\0'; ++psz) {
      const auto cp = static_cast<std::uint32_t>(*psz);
      if (cp < 0x80) {
        put(static_cast<unsigned char>(cp));
      } else if (cp < 0x800) {
        put(static_cast<unsigned char>(0xC0 | (cp >> 6)));
        put(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        put(static_cast<unsigned char>(0xE0 | (cp >> 12)));
        put(static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F)));
        put(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x110000) {
        put(static_cast<unsigned char>(0xF0 | (cp >> 18)));
        put(static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F)));
        put(static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F)));
        put(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
      } else {
        return static_cast<size_t>(-1);
      }
    }
    if (buf != nullptr && produced < n) {
      buf[produced] = '\0';
    }
    return produced;
  }
};

inline wxMBConvLibc wxConvLibc;
inline wxMBConvUTF8 wxConvUTF8;
inline wxMBConv& wxConvFile = wxConvLibc;
inline wxMBConv* wxConvCurrent = &wxConvLibc;
#define wxConvLocal wxConvLibc

// --- printf-family formats ----------------------------------------------------------------
namespace faf_wx_detail
{
  // wx hands wide formats to the MSVC CRT, where %s and %c in a wide format take wide
  // arguments and %S / %C / %hs narrow ones. The C library here reads them the other way
  // round, so formats are translated to the explicit spellings first.
  inline std::wstring TranslateWideFormat(const wchar_t* format)
  {
    std::wstring out;
    if (format == nullptr) {
      return out;
    }
    for (const wchar_t* p = format; *p != L'\0';) {
      if (*p != L'%') {
        out.push_back(*p++);
        continue;
      }
      out.push_back(*p++);
      if (*p == L'%') {
        out.push_back(*p++);
        continue;
      }
      while (*p != L'\0' && std::wcschr(L"-+ #0", *p) != nullptr) {
        out.push_back(*p++);
      }
      while (*p != L'\0' && (std::iswdigit(*p) || *p == L'*' || *p == L'.')) {
        out.push_back(*p++);
      }
      bool narrow = false;
      bool wide = false;
      if (p[0] == L'I' && p[1] == L'6' && p[2] == L'4') {
        out += L"ll";
        p += 3;
      } else if (p[0] == L'I' && p[1] == L'3' && p[2] == L'2') {
        p += 3;
      } else if (p[0] == L'I') {
        out += L"z";
        ++p;
      } else if (*p == L'h' && (p[1] == L's' || p[1] == L'c' || p[1] == L'S' || p[1] == L'C')) {
        narrow = true;
        ++p;
      } else if (*p == L'l' && (p[1] == L's' || p[1] == L'c' || p[1] == L'S' || p[1] == L'C')) {
        wide = true;
        ++p;
      } else if (*p == L'w') {
        wide = true;
        ++p;
      } else {
        while (*p != L'\0' && std::wcschr(L"hlLqjzt", *p) != nullptr) {
          out.push_back(*p++);
        }
      }
      if (*p == L'\0') {
        break;
      }
      const wchar_t conversion = *p++;
      switch (conversion) {
        case L's':
          out += narrow ? L"s" : L"ls";
          break;
        case L'c':
          out += narrow ? L"c" : L"lc";
          break;
        case L'S':
          out += wide ? L"ls" : L"s";
          break;
        case L'C':
          out += wide ? L"lc" : L"c";
          break;
        default:
          out.push_back(conversion);
          break;
      }
    }
    return out;
  }

  inline std::wstring VFormatWide(const wchar_t* format, va_list args)
  {
    const std::wstring translated = TranslateWideFormat(format);
    std::vector<wchar_t> buffer(256);
    for (;;) {
      va_list copy;
      va_copy(copy, args);
      const int written = std::vswprintf(buffer.data(), buffer.size(), translated.c_str(), copy);
      va_end(copy);
      if (written >= 0 && static_cast<size_t>(written) < buffer.size()) {
        return std::wstring(buffer.data(), static_cast<size_t>(written));
      }
      if (buffer.size() >= (1u << 24)) {
        return std::wstring();
      }
      buffer.resize(buffer.size() * 2);
    }
  }
} // namespace faf_wx_detail

// --- wxString (Unicode) ----------------------------------------------------------------------
class wxString
{
public:
  typedef wxChar char_type;
  typedef size_t size_type;
  typedef std::wstring::iterator iterator;
  typedef std::wstring::const_iterator const_iterator;
  static const size_t npos = static_cast<size_t>(-1);

  wxString() = default;
  wxString(const wxString&) = default;
  wxString(wxString&&) noexcept = default;
  wxString& operator=(const wxString&) = default;
  wxString& operator=(wxString&&) noexcept = default;

  wxString(wxChar ch, size_t repeat = 1) : m(repeat, ch) {}
  wxString(const wxChar* psz, size_t length = wxSTRING_MAXLEN)
  {
    if (psz != nullptr) {
      m = (length == wxSTRING_MAXLEN) ? std::wstring(psz) : std::wstring(psz, length);
    }
  }
  wxString(const char* psz, const wxMBConv& conv = wxConvLibc, size_t length = wxSTRING_MAXLEN)
  {
    if (psz == nullptr) {
      return;
    }
    if (length != wxSTRING_MAXLEN) {
      const std::string clipped(psz, std::min(length, std::strlen(psz)));
      const wxWCharBuffer converted = conv.cMB2WC(clipped.c_str());
      m = converted.data() != nullptr ? converted.data() : L"";
    } else {
      const wxWCharBuffer converted = conv.cMB2WC(psz);
      m = converted.data() != nullptr ? converted.data() : L"";
    }
  }
  wxString(const unsigned char* psz, const wxMBConv& conv = wxConvLibc, size_t length = wxSTRING_MAXLEN)
    : wxString(reinterpret_cast<const char*>(psz), conv, length)
  {}
  wxString(const wxWCharBuffer& buffer) : wxString(buffer.data()) {}
  wxString(const std::wstring& text) : m(text) {}

  // --- access ---
  const wxChar* c_str() const { return m.c_str(); }
  const wxChar* GetData() const { return m.c_str(); }
  const wxChar* wx_str() const { return m.c_str(); }
  operator const wxChar*() const { return m.c_str(); }
  const wchar_t* wc_str(const wxMBConv& = wxConvLibc) const { return m.c_str(); }
  const wchar_t* fn_str() const { return m.c_str(); }
  const wxCharBuffer mb_str(const wxMBConv& conv = wxConvLibc) const { return conv.cWC2MB(m.c_str()); }
  const wxCharBuffer ToAscii() const
  {
    std::string out;
    out.reserve(m.size());
    for (wchar_t ch : m) {
      out.push_back(static_cast<std::uint32_t>(ch) < 0x80 ? static_cast<char>(ch) : '_');
    }
    return wxCharBuffer(std::move(out));
  }
  static wxString FromAscii(const char* ascii)
  {
    wxString out;
    if (ascii != nullptr) {
      for (; *ascii != '\0'; ++ascii) {
        out.m.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*ascii)));
      }
    }
    return out;
  }
  const std::wstring& ToStdWstring() const { return m; }

  size_t Len() const { return m.size(); }
  size_t Length() const { return m.size(); }
  size_t length() const { return m.size(); }
  size_t size() const { return m.size(); }
  bool IsEmpty() const { return m.empty(); }
  bool empty() const { return m.empty(); }
  bool IsNull() const { return m.empty(); }
  bool operator!() const { return m.empty(); }

  wxChar GetChar(size_t n) const { return m[n]; }
  wxChar& GetWritableChar(size_t n) { return m[n]; }
  void SetChar(size_t n, wxChar ch) { m[n] = ch; }
  wxChar Last() const { return m.empty() ? L'\0' : m.back(); }
  wxChar& Last() { return m.back(); }

  wxChar operator[](int n) const { return m[static_cast<size_t>(n)]; }
  wxChar operator[](unsigned int n) const { return m[n]; }
  wxChar operator[](long n) const { return m[static_cast<size_t>(n)]; }
  wxChar operator[](unsigned long n) const { return m[n]; }
  wxChar operator[](long long n) const { return m[static_cast<size_t>(n)]; }
  wxChar operator[](unsigned long long n) const { return m[static_cast<size_t>(n)]; }
  wxChar& operator[](int n) { return m[static_cast<size_t>(n)]; }
  wxChar& operator[](unsigned int n) { return m[n]; }
  wxChar& operator[](long n) { return m[static_cast<size_t>(n)]; }
  wxChar& operator[](unsigned long n) { return m[n]; }
  wxChar& operator[](long long n) { return m[static_cast<size_t>(n)]; }
  wxChar& operator[](unsigned long long n) { return m[static_cast<size_t>(n)]; }

  iterator begin() { return m.begin(); }
  iterator end() { return m.end(); }
  const_iterator begin() const { return m.begin(); }
  const_iterator end() const { return m.end(); }

  // --- modification ---
  void Empty() { m.clear(); }
  void Clear() { m.clear(); }
  void clear() { m.clear(); }
  bool Alloc(size_t n)
  {
    m.reserve(n);
    return true;
  }
  wxString& Shrink()
  {
    m.shrink_to_fit();
    return *this;
  }
  wxChar* GetWriteBuf(size_t length)
  {
    m.resize(length);
    return m.data();
  }
  void UngetWriteBuf() { m.resize(std::wcslen(m.c_str())); }
  void UngetWriteBuf(size_t length) { m.resize(length); }

  wxString& Append(const wxString& s)
  {
    m += s.m;
    return *this;
  }
  wxString& Append(const wxChar* s)
  {
    if (s != nullptr) {
      m += s;
    }
    return *this;
  }
  wxString& Append(wxChar ch, size_t count = 1)
  {
    m.append(count, ch);
    return *this;
  }
  wxString& Prepend(const wxString& s)
  {
    m.insert(0, s.m);
    return *this;
  }
  wxString& append(const wxString& s) { return Append(s); }
  wxString& append(const wxChar* s) { return Append(s); }
  wxString& append(size_t count, wxChar ch) { return Append(ch, count); }
  wxString& insert(size_t pos, const wxString& s)
  {
    m.insert(pos, s.m);
    return *this;
  }
  wxString& erase(size_t pos = 0, size_t n = npos)
  {
    m.erase(pos, n);
    return *this;
  }
  wxString& Remove(size_t pos)
  {
    m.erase(pos);
    return *this;
  }
  wxString& Remove(size_t pos, size_t len)
  {
    m.erase(pos, len);
    return *this;
  }
  wxString& RemoveLast(size_t n = 1)
  {
    m.erase(m.size() - std::min(n, m.size()));
    return *this;
  }
  wxString& Truncate(size_t len)
  {
    if (len < m.size()) {
      m.resize(len);
    }
    return *this;
  }
  wxString& Pad(size_t count, wxChar ch = L' ', bool fromRight = true)
  {
    if (fromRight) {
      m.append(count, ch);
    } else {
      m.insert(0, count, ch);
    }
    return *this;
  }
  wxString& Trim(bool fromRight = true)
  {
    if (fromRight) {
      while (!m.empty() && std::iswspace(m.back())) {
        m.pop_back();
      }
    } else {
      size_t first = 0;
      while (first < m.size() && std::iswspace(m[first])) {
        ++first;
      }
      m.erase(0, first);
    }
    return *this;
  }
  wxString& MakeLower()
  {
    for (wchar_t& ch : m) {
      ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return *this;
  }
  wxString& MakeUpper()
  {
    for (wchar_t& ch : m) {
      ch = static_cast<wchar_t>(std::towupper(ch));
    }
    return *this;
  }
  wxString Lower() const { return wxString(*this).MakeLower(); }
  wxString Upper() const { return wxString(*this).MakeUpper(); }
  wxString& LowerCase() { return MakeLower(); }
  wxString& UpperCase() { return MakeUpper(); }

  size_t Replace(const wxChar* oldText, const wxChar* newText, bool replaceAll = true)
  {
    const std::wstring from = oldText != nullptr ? oldText : L"";
    const std::wstring to = newText != nullptr ? newText : L"";
    if (from.empty()) {
      return 0;
    }
    size_t count = 0;
    size_t pos = 0;
    while ((pos = m.find(from, pos)) != std::wstring::npos) {
      m.replace(pos, from.size(), to);
      pos += to.size();
      ++count;
      if (!replaceAll) {
        break;
      }
    }
    return count;
  }

  // --- substrings and search ---
  wxString Mid(size_t first, size_t count = wxSTRING_MAXLEN) const
  {
    if (first >= m.size()) {
      return wxString();
    }
    return wxString(m.substr(first, count));
  }
  wxString operator()(size_t start, size_t len) const { return Mid(start, len); }
  wxString SubString(size_t from, size_t to) const { return Mid(from, (to - from) + 1); }
  wxString substr(size_t pos = 0, size_t n = npos) const { return Mid(pos, n); }
  wxString Left(size_t count) const { return wxString(m.substr(0, std::min(count, m.size()))); }
  wxString Right(size_t count) const
  {
    count = std::min(count, m.size());
    return wxString(m.substr(m.size() - count));
  }
  wxString BeforeFirst(wxChar ch) const
  {
    const size_t pos = m.find(ch);
    return pos == std::wstring::npos ? *this : wxString(m.substr(0, pos));
  }
  wxString BeforeLast(wxChar ch) const
  {
    const size_t pos = m.rfind(ch);
    return pos == std::wstring::npos ? wxString() : wxString(m.substr(0, pos));
  }
  wxString AfterFirst(wxChar ch) const
  {
    const size_t pos = m.find(ch);
    return pos == std::wstring::npos ? wxString() : wxString(m.substr(pos + 1));
  }
  wxString AfterLast(wxChar ch) const
  {
    const size_t pos = m.rfind(ch);
    return pos == std::wstring::npos ? *this : wxString(m.substr(pos + 1));
  }
  int Find(wxChar ch, bool fromEnd = false) const
  {
    const size_t pos = fromEnd ? m.rfind(ch) : m.find(ch);
    return pos == std::wstring::npos ? -1 : static_cast<int>(pos);
  }
  int Find(const wxChar* sub) const
  {
    const size_t pos = m.find(sub != nullptr ? sub : L"");
    return pos == std::wstring::npos ? -1 : static_cast<int>(pos);
  }
  size_t find(const wxString& s, size_t pos = 0) const { return m.find(s.m, pos); }
  size_t find(wxChar ch, size_t pos = 0) const { return m.find(ch, pos); }
  size_t rfind(const wxString& s, size_t pos = npos) const { return m.rfind(s.m, pos); }
  size_t rfind(wxChar ch, size_t pos = npos) const { return m.rfind(ch, pos); }
  size_t Freq(wxChar ch) const { return static_cast<size_t>(std::count(m.begin(), m.end(), ch)); }
  bool StartsWith(const wxChar* prefix, wxString* rest = nullptr) const
  {
    const std::wstring p = prefix != nullptr ? prefix : L"";
    if (m.compare(0, p.size(), p) != 0) {
      return false;
    }
    if (rest != nullptr) {
      *rest = wxString(m.substr(p.size()));
    }
    return true;
  }
  bool EndsWith(const wxChar* suffix, wxString* rest = nullptr) const
  {
    const std::wstring s = suffix != nullptr ? suffix : L"";
    if (s.size() > m.size() || m.compare(m.size() - s.size(), s.size(), s) != 0) {
      return false;
    }
    if (rest != nullptr) {
      *rest = wxString(m.substr(0, m.size() - s.size()));
    }
    return true;
  }
  bool Contains(const wxString& sub) const { return m.find(sub.m) != std::wstring::npos; }
  bool Matches(const wxChar* mask) const { return MatchWild(mask, m.c_str()); }

  // --- comparison ---
  int Cmp(const wxChar* psz) const { return std::wcscmp(m.c_str(), psz != nullptr ? psz : L""); }
  int Cmp(const wxString& s) const { return m.compare(s.m); }
  int CmpNoCase(const wxChar* psz) const
  {
    const wchar_t* a = m.c_str();
    const wchar_t* b = psz != nullptr ? psz : L"";
    for (;; ++a, ++b) {
      const wint_t ca = std::towlower(*a);
      const wint_t cb = std::towlower(*b);
      if (ca != cb) {
        return ca < cb ? -1 : 1;
      }
      if (ca == 0) {
        return 0;
      }
    }
  }
  int compare(const wxString& s) const { return m.compare(s.m); }
  bool IsSameAs(const wxChar* psz, bool caseSensitive = true) const
  {
    return caseSensitive ? Cmp(psz) == 0 : CmpNoCase(psz) == 0;
  }
  bool IsSameAs(wxChar ch, bool caseSensitive = true) const
  {
    const wxChar text[2] = {ch, L'\0'};
    return IsSameAs(text, caseSensitive);
  }
  bool IsNumber() const
  {
    if (m.empty()) {
      return false;
    }
    size_t i = (m[0] == L'-' || m[0] == L'+') ? 1 : 0;
    if (i == m.size()) {
      return false;
    }
    for (; i < m.size(); ++i) {
      if (!std::iswdigit(m[i])) {
        return false;
      }
    }
    return true;
  }
  bool IsAscii() const
  {
    return std::all_of(m.begin(), m.end(), [](wchar_t ch) { return static_cast<std::uint32_t>(ch) < 0x80; });
  }

  // --- conversion to numbers (wcstol / wcstod like the library) ---
  bool ToLong(long* value, int base = 10) const
  {
    const wchar_t* start = m.c_str();
    wchar_t* end = nullptr;
    *value = std::wcstol(start, &end, base);
    return !m.empty() && end != start && *end == L'\0';
  }
  bool ToULong(unsigned long* value, int base = 10) const
  {
    const wchar_t* start = m.c_str();
    wchar_t* end = nullptr;
    *value = std::wcstoul(start, &end, base);
    return !m.empty() && end != start && *end == L'\0';
  }
  bool ToDouble(double* value) const
  {
    const wchar_t* start = m.c_str();
    wchar_t* end = nullptr;
    *value = std::wcstod(start, &end);
    return !m.empty() && end != start && *end == L'\0';
  }

  // --- formatting ---
  int PrintfV(const wxChar* format, va_list args)
  {
    m = faf_wx_detail::VFormatWide(format, args);
    return static_cast<int>(m.size());
  }
  int Printf(const wxChar* format, ...)
  {
    va_list args;
    va_start(args, format);
    const int written = PrintfV(format, args);
    va_end(args);
    return written;
  }
  static wxString FormatV(const wxChar* format, va_list args)
  {
    wxString out;
    out.PrintfV(format, args);
    return out;
  }
  static wxString Format(const wxChar* format, ...)
  {
    va_list args;
    va_start(args, format);
    wxString out = FormatV(format, args);
    va_end(args);
    return out;
  }

  // --- operators ---
  wxString& operator=(const wxChar* psz)
  {
    m = psz != nullptr ? psz : L"";
    return *this;
  }
  wxString& operator=(wxChar ch)
  {
    m.assign(1, ch);
    return *this;
  }
  wxString& operator=(const char* psz) { return *this = wxString(psz); }
  wxString& operator+=(const wxString& s) { return Append(s); }
  wxString& operator+=(const wxChar* psz) { return Append(psz); }
  wxString& operator+=(wxChar ch) { return Append(ch); }
  wxString& operator<<(const wxString& s) { return Append(s); }
  wxString& operator<<(const wxChar* psz) { return Append(psz); }
  wxString& operator<<(wxChar ch) { return Append(ch); }
  wxString& operator<<(int value) { return Append(wxString::Format(L"%d", value)); }
  wxString& operator<<(long value) { return Append(wxString::Format(L"%ld", value)); }
  wxString& operator<<(unsigned int value) { return Append(wxString::Format(L"%u", value)); }
  wxString& operator<<(unsigned long value) { return Append(wxString::Format(L"%lu", value)); }
  wxString& operator<<(float value) { return Append(wxString::Format(L"%f", static_cast<double>(value))); }
  wxString& operator<<(double value) { return Append(wxString::Format(L"%f", value)); }

private:
  static bool MatchWild(const wchar_t* pattern, const wchar_t* text)
  {
    if (pattern == nullptr) {
      return false;
    }
    for (;; ++pattern, ++text) {
      if (*pattern == L'*') {
        while (*pattern == L'*') {
          ++pattern;
        }
        if (*pattern == L'\0') {
          return true;
        }
        for (; *text != L'\0'; ++text) {
          if (MatchWild(pattern, text)) {
            return true;
          }
        }
        return false;
      }
      if (*pattern == L'\0') {
        return *text == L'\0';
      }
      if (*text == L'\0' || (*pattern != L'?' && *pattern != *text)) {
        return false;
      }
    }
  }

  std::wstring m;
};

inline wxString operator+(const wxString& a, const wxString& b)
{
  wxString out(a);
  out += b;
  return out;
}
inline wxString operator+(const wxString& a, const wxChar* b)
{
  wxString out(a);
  out += b;
  return out;
}
inline wxString operator+(const wxChar* a, const wxString& b)
{
  wxString out(a);
  out += b;
  return out;
}
inline wxString operator+(const wxString& a, wxChar b)
{
  wxString out(a);
  out += b;
  return out;
}
inline wxString operator+(wxChar a, const wxString& b)
{
  wxString out(a);
  out += b;
  return out;
}

inline bool operator==(const wxString& a, const wxString& b) { return a.Cmp(b) == 0; }
inline bool operator==(const wxString& a, const wxChar* b) { return a.Cmp(b) == 0; }
inline bool operator==(const wxChar* a, const wxString& b) { return b.Cmp(a) == 0; }
inline bool operator!=(const wxString& a, const wxString& b) { return a.Cmp(b) != 0; }
inline bool operator!=(const wxString& a, const wxChar* b) { return a.Cmp(b) != 0; }
inline bool operator!=(const wxChar* a, const wxString& b) { return b.Cmp(a) != 0; }
inline bool operator<(const wxString& a, const wxString& b) { return a.Cmp(b) < 0; }
inline bool operator<(const wxString& a, const wxChar* b) { return a.Cmp(b) < 0; }
inline bool operator<(const wxChar* a, const wxString& b) { return b.Cmp(a) > 0; }
inline bool operator>(const wxString& a, const wxString& b) { return a.Cmp(b) > 0; }
inline bool operator>(const wxString& a, const wxChar* b) { return a.Cmp(b) > 0; }
inline bool operator>(const wxChar* a, const wxString& b) { return b.Cmp(a) < 0; }
inline bool operator<=(const wxString& a, const wxString& b) { return a.Cmp(b) <= 0; }
inline bool operator>=(const wxString& a, const wxString& b) { return a.Cmp(b) >= 0; }

// wxEmptyString and the default window names are `const wxChar*` in wx 2.4.2.
inline const wxChar* const wxEmptyString = L"";
inline const wxChar* const wxFrameNameStr = L"frame";
inline const wxChar* const wxDialogNameStr = L"dialog";
inline const wxChar* const wxPanelNameStr = L"panel";
inline const wxChar* const wxControlNameStr = L"control";
inline const wxChar* const wxButtonNameStr = L"button";
inline const wxChar* const wxCheckBoxNameStr = L"check";
inline const wxChar* const wxComboBoxNameStr = L"comboBox";
inline const wxChar* const wxChoiceNameStr = L"choice";
inline const wxChar* const wxListBoxNameStr = L"listBox";
inline const wxChar* const wxStaticTextNameStr = L"staticText";
inline const wxChar* const wxStaticBoxNameStr = L"groupBox";
inline const wxChar* const wxTextCtrlNameStr = L"text";
inline const wxChar* const wxSliderNameStr = L"slider";
inline const wxChar* const wxListCtrlNameStr = L"listCtrl";
inline const wxChar* const wxTreeCtrlNameStr = L"treeCtrl";
inline const wxChar* const wxNotebookNameStr = L"notebook";
inline const wxChar* const wxToolBarNameStr = L"toolbar";
inline const wxChar* const wxStatusLineNameStr = L"status_line";
inline const wxChar* const wxSplitterNameStr = L"splitter";
inline const wxChar* const wxDirDialogNameStr = L"wxDirCtrl";
inline const wxChar* const wxDirDialogDefaultFolderStr = L"/";
inline const wxChar* const wxDirSelectorPromptStr = L"Select a directory";
inline const wxChar* const wxFileSelectorPromptStr = L"Select a file";
inline const wxChar* const wxFileSelectorDefaultWildcardStr = L"*.*";
inline const wxChar* const wxMessageBoxCaptionStr = L"Message";
inline const wxChar* const wxTreeListCtrlNameStr = L"treelistctrl";

class wxArrayString
{
public:
  wxArrayString() = default;
  size_t GetCount() const { return mItems.size(); }
  size_t Count() const { return mItems.size(); }
  bool IsEmpty() const { return mItems.empty(); }
  wxString& Item(size_t n) { return mItems[n]; }
  const wxString& Item(size_t n) const { return mItems[n]; }
  wxString& operator[](size_t n) { return mItems[n]; }
  const wxString& operator[](size_t n) const { return mItems[n]; }
  wxString& Last() { return mItems.back(); }
  size_t Add(const wxString& s, size_t copies = 1)
  {
    for (size_t i = 0; i < copies; ++i) {
      mItems.push_back(s);
    }
    return mItems.size() - 1;
  }
  void Insert(const wxString& s, size_t index, size_t copies = 1)
  {
    mItems.insert(mItems.begin() + static_cast<std::ptrdiff_t>(index), copies, s);
  }
  void RemoveAt(size_t index, size_t count = 1)
  {
    mItems.erase(
      mItems.begin() + static_cast<std::ptrdiff_t>(index),
      mItems.begin() + static_cast<std::ptrdiff_t>(index + count)
    );
  }
  void Remove(const wxChar* s)
  {
    const int index = Index(s);
    if (index >= 0) {
      RemoveAt(static_cast<size_t>(index));
    }
  }
  int Index(const wxChar* s, bool caseSensitive = true, bool fromEnd = false) const
  {
    if (fromEnd) {
      for (size_t i = mItems.size(); i-- > 0;) {
        if (mItems[i].IsSameAs(s, caseSensitive)) {
          return static_cast<int>(i);
        }
      }
      return -1;
    }
    for (size_t i = 0; i < mItems.size(); ++i) {
      if (mItems[i].IsSameAs(s, caseSensitive)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
  void Clear() { mItems.clear(); }
  void Empty() { mItems.clear(); }
  void Alloc(size_t n) { mItems.reserve(n); }
  void Sort(bool reverse = false)
  {
    std::sort(mItems.begin(), mItems.end());
    if (reverse) {
      std::reverse(mItems.begin(), mItems.end());
    }
  }

private:
  std::vector<wxString> mItems;
};

inline wxString wxGetTranslation(const wxChar* text) { return wxString(text); }
