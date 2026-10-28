// Win32 on POSIX: Gdi fonts (compat/Gdi.h; internals in WinCompatGdiFont.h).
//
// GDI's font table, mapper and TrueType metrics on Core Text:
// - Private fonts (AddFontMemResourceEx) are Core Text descriptors made from the file data
//   (CTFontManagerCreateFontDescriptorsFromData); nothing is registered with the system,
//   so a private "Arial" never collides with the installed one and is preferred to it.
// - The mapper matches the face name, case-insensitively, against the family names (name
//   ID 1), then the full names (name ID 4) of the private fonts, then of the installed
//   fonts, in English and in the user's language (the names a Windows font table holds).
//   Among the faces of a family it takes the one nearest the requested weight and slant.
//   A missing face falls back to Arial (Courier New for fixed pitch or FF_MODERN, Times New
//   Roman for FF_ROMAN), logged once per name.
// - Metrics are the TrueType driver's, in the integer arithmetic Wine uses to reproduce
//   them (FreeType 16.16 scales, 26.6 rounding): the em height in pixels (ppem) from
//   lfHeight (negative: the em height; positive: the cell height), ascent and descent from
//   the VDMX table at that size or else OS/2 usWinAscent/usWinDescent scaled and rounded,
//   advances from the hdmx table at that size or else the hmtx advance scaled and rounded,
//   kerning pairs from the 'kern' table. What the TrueType instructions would change beyond
//   those tables (hinted outlines and the advances of sizes without an hdmx record) is not
//   reproduced: outlines are drawn unhinted.
// - Characters map to glyphs through the face's own cmap (Unicode format 12 or 4, or a
//   symbol font's 0xF0xx range), as GDI maps them (Core Text's own lookup also answers
//   for characters the cmap lacks, e.g. the space glyph for control characters). A
//   missing ASCII or control character is glyph 0, GDI's default character; any other
//   missing character is drawn from Core Text's fallback font for it
//   (CTFontCreateForString), standing in for Windows' font linking and Uniscribe fallback
//   (so Cyrillic or CJK text in a Latin-only face stays legible); failing that, glyph 0.
//   A fallback glyph whose ink overflows the face's cell (ascent + descent) is scaled down
//   to fit and centred in it, as font linking scales linked fonts to the base font; one
//   without advance (a combining mark drawn on its own, e.g. FAF's U+20DD score icon) is
//   drawn as a spacing glyph: shifted to start at the pen, its advance its ink width.
// - Glyphs are drawn with CTFontDrawGlyphs into 8-bit grayscale coverage (CoreGraphics,
//   antialiased, no font smoothing, no subpixel positioning) at integer pen positions.
//   A glyph whose integer advance (hdmx, or the rounded one) differs from its linear width
//   is drawn scaled horizontally to fit it, as the TrueType instructions fit the hinted
//   outline to the hinted advance (Arial's 'x' at 12 px: 5 px for a 6.0 px outline);
//   unscaled, such glyphs run into their neighbours.
//   The coverage then goes through a contrast curve, coverage^(1/gamma) with gamma 1.45
//   (env FAF_GDI_TEXT_GAMMA; 1 = linear): GDI draws the UI's small sizes hinted, mostly
//   bilevel (the fonts' gasp tables), so its stems are whole pixels; unhinted outlines
//   spread a one-pixel stem over two half-covered pixels, which reads faint (Courier New
//   at 12 px) without it.

#include "platform/posix/WinCompatGdiFont.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace faf_posix::gdi
{
  namespace
  {
    // --- Core Foundation ownership -------------------------------------------------------------
    template <class T>
    class CFPtr
    {
    public:
      CFPtr() noexcept = default;
      explicit CFPtr(T ref) noexcept // adopts a Create/Copy result
        : mRef(ref)
      {
      }
      CFPtr(const CFPtr& other) noexcept
        : mRef(other.mRef)
      {
        if (mRef != nullptr) {
          CFRetain(mRef);
        }
      }
      CFPtr(CFPtr&& other) noexcept
        : mRef(std::exchange(other.mRef, nullptr))
      {
      }
      CFPtr& operator=(CFPtr other) noexcept
      {
        std::swap(mRef, other.mRef);
        return *this;
      }
      ~CFPtr()
      {
        if (mRef != nullptr) {
          CFRelease(mRef);
        }
      }
      static CFPtr Retain(T ref) noexcept
      {
        if (ref != nullptr) {
          CFRetain(ref);
        }
        return CFPtr(ref);
      }
      [[nodiscard]] T get() const noexcept
      {
        return mRef;
      }
      explicit operator bool() const noexcept
      {
        return mRef != nullptr;
      }

    private:
      T mRef = nullptr;
    };

    CFPtr<CFStringRef> MakeCFString(const std::wstring& text)
    {
      return CFPtr<CFStringRef>(CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8*>(text.data()), static_cast<CFIndex>(text.size() * sizeof(wchar_t)),
        kCFStringEncodingUTF32LE, false
      ));
    }

    std::wstring FromCFString(const CFStringRef text)
    {
      if (text == nullptr) {
        return {};
      }
      const CFIndex length = CFStringGetLength(text);
      CFIndex bytes = 0;
      CFStringGetBytes(text, CFRangeMake(0, length), kCFStringEncodingUTF32LE, '?', false, nullptr, 0, &bytes);
      std::wstring out(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
      CFStringGetBytes(
        text, CFRangeMake(0, length), kCFStringEncodingUTF32LE, '?', false, reinterpret_cast<UInt8*>(out.data()), bytes, nullptr
      );
      return out;
    }

    // Case-folded form of a face name (the mapper compares names case-insensitively).
    std::wstring Fold(const std::wstring& name)
    {
      CFPtr<CFStringRef> text = MakeCFString(name);
      if (!text) {
        return name;
      }
      CFPtr<CFMutableStringRef> folded(CFStringCreateMutableCopy(nullptr, 0, text.get()));
      if (!folded) {
        return name;
      }
      CFStringLowercase(folded.get(), nullptr);
      return FromCFString(folded.get());
    }

    std::string Utf8(const std::wstring& text)
    {
      CFPtr<CFStringRef> cf = MakeCFString(text);
      if (!cf) {
        return {};
      }
      const CFIndex length = CFStringGetLength(cf.get());
      CFIndex bytes = 0;
      CFStringGetBytes(cf.get(), CFRangeMake(0, length), kCFStringEncodingUTF8, '?', false, nullptr, 0, &bytes);
      std::string out(static_cast<std::size_t>(bytes), '\0');
      CFStringGetBytes(
        cf.get(), CFRangeMake(0, length), kCFStringEncodingUTF8, '?', false, reinterpret_cast<UInt8*>(out.data()), bytes, nullptr
      );
      return out;
    }

    // --- Big-endian table access ---------------------------------------------------------------
    struct Table
    {
      CFPtr<CFDataRef> data;

      [[nodiscard]] const std::uint8_t* Bytes() const noexcept
      {
        return data ? CFDataGetBytePtr(data.get()) : nullptr;
      }
      [[nodiscard]] std::size_t Size() const noexcept
      {
        return data ? static_cast<std::size_t>(CFDataGetLength(data.get())) : 0U;
      }
      [[nodiscard]] bool Has(const std::size_t offset, const std::size_t length) const noexcept
      {
        return offset <= Size() && length <= Size() - offset;
      }
      [[nodiscard]] std::uint8_t U8(const std::size_t offset) const noexcept
      {
        return Has(offset, 1U) ? Bytes()[offset] : 0U;
      }
      [[nodiscard]] std::uint16_t U16(const std::size_t offset) const noexcept
      {
        if (!Has(offset, 2U)) {
          return 0U;
        }
        const std::uint8_t* const p = Bytes() + offset;
        return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
      }
      [[nodiscard]] std::int16_t S16(const std::size_t offset) const noexcept
      {
        return static_cast<std::int16_t>(U16(offset));
      }
      [[nodiscard]] std::uint32_t U32(const std::size_t offset) const noexcept
      {
        return (static_cast<std::uint32_t>(U16(offset)) << 16) | U16(offset + 2U);
      }
    };

    Table CopyTable(const CTFontRef font, const CTFontTableTag tag)
    {
      Table table;
      table.data = CFPtr<CFDataRef>(CTFontCopyTable(font, tag, kCTFontTableOptionNoOptions));
      return table;
    }

    // --- GDI / FreeType arithmetic ---------------------------------------------------------------
    // FT_MulFix: a * b / 65536, rounded half away from zero.
    std::int64_t MulFix(const std::int64_t a, const std::int64_t b) noexcept
    {
      const bool negative = (a < 0) != (b < 0);
      const std::int64_t magnitude = (std::llabs(a) * std::llabs(b) + 0x8000) >> 16;
      return negative ? -magnitude : magnitude;
    }

    // FT_DivFix: a * 65536 / b, rounded half away from zero.
    std::int64_t DivFix(const std::int64_t a, const std::int64_t b) noexcept
    {
      if (b == 0) {
        return 0x7FFFFFFF;
      }
      const bool negative = (a < 0) != (b < 0);
      const std::int64_t magnitude = ((std::llabs(a) << 16) + std::llabs(b) / 2) / std::llabs(b);
      return negative ? -magnitude : magnitude;
    }

    // FT_MulDiv: a * b / c, rounded half away from zero.
    std::int64_t MulDiv(const std::int64_t a, const std::int64_t b, const std::int64_t c) noexcept
    {
      if (c == 0) {
        return 0x7FFFFFFF;
      }
      const bool negative = ((a < 0) != (b < 0)) != (c < 0);
      const std::int64_t magnitude = (std::llabs(a) * std::llabs(b) + std::llabs(c) / 2) / std::llabs(c);
      return negative ? -magnitude : magnitude;
    }

    // A 26.6 value to whole pixels, (v + 32) >> 6.
    LONG Round26Dot6(const std::int64_t value) noexcept
    {
      return static_cast<LONG>((value + 32) >> 6);
    }

    // --- The user's language, as a Windows primary language ID --------------------------------
    std::uint16_t UserPrimaryLanguage()
    {
      static const std::uint16_t language = [] {
        struct Entry
        {
          const char* code;
          std::uint16_t primary;
        };
        static constexpr Entry kTable[] = {
          {"ar", 0x01}, {"bg", 0x02}, {"ca", 0x03}, {"zh", 0x04}, {"cs", 0x05}, {"da", 0x06}, {"de", 0x07},
          {"el", 0x08}, {"en", 0x09}, {"es", 0x0A}, {"fi", 0x0B}, {"fr", 0x0C}, {"he", 0x0D}, {"hu", 0x0E},
          {"is", 0x0F}, {"it", 0x10}, {"ja", 0x11}, {"ko", 0x12}, {"nl", 0x13}, {"nb", 0x14}, {"no", 0x14},
          {"pl", 0x15}, {"pt", 0x16}, {"ro", 0x18}, {"ru", 0x19}, {"hr", 0x1A}, {"sk", 0x1B}, {"sv", 0x1D},
          {"th", 0x1E}, {"tr", 0x1F}, {"uk", 0x22}, {"sl", 0x24}, {"et", 0x25}, {"lv", 0x26}, {"lt", 0x27},
          {"vi", 0x2A}, {"eu", 0x2D},
        };
        CFPtr<CFArrayRef> languages(CFLocaleCopyPreferredLanguages());
        if (!languages || CFArrayGetCount(languages.get()) == 0) {
          return std::uint16_t{0x09};
        }
        const auto first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages.get(), 0));
        char code[64] = {};
        if (!CFStringGetCString(first, code, sizeof(code), kCFStringEncodingUTF8)) {
          return std::uint16_t{0x09};
        }
        code[std::strcspn(code, "-_")] = '\0';
        for (const Entry& entry : kTable) {
          if (std::strcmp(entry.code, code) == 0) {
            return entry.primary;
          }
        }
        return std::uint16_t{0x09};
      }();
      return language;
    }

    // --- Faces -----------------------------------------------------------------------------------
    struct Face
    {
      CFPtr<CTFontDescriptorRef> descriptor;
      CFPtr<CTFontRef> font;     // any size: table access
      std::uint64_t source = 0;  // the AddMemoryFonts id; 0 for an installed font
      std::string postScriptName;

      LONG upem = 2048;
      LONG hheaAscender = 0;
      LONG hheaDescender = 0;
      LONG hheaLineGap = 0;
      LONG winAscent = 0;
      LONG winDescent = 0;
      LONG avgCharWidth = 0;
      LONG weight = FW_NORMAL;
      LONG bboxXMin = 0;
      LONG bboxXMax = 0;
      bool italic = false;
      bool fixedPitch = false;
      bool trueType = false; // 'glyf' outlines
      bool postScript = false; // 'CFF ' outlines
      std::uint16_t os2Version = 0;
      std::uint16_t firstChar = 0x20;
      std::uint16_t lastChar = 0xFFFC;
      std::uint16_t defaultChar = 0;
      std::uint16_t breakChar = 0x20;
      std::uint8_t panoseFamily = 0;
      std::uint8_t panoseSerif = 0;
      std::uint16_t numGlyphs = 0;
      std::uint16_t numHMetrics = 0;
      Table hmtx;
      Table hdmx;
      Table vdmx;
      Table kern;
      Table cmap;
      std::size_t cmapSubtable = 0; // offset of the subtable used; 0: none usable
      std::uint16_t cmapFormat = 0;
      bool symbol = false;          // a (3,0) symbol cmap: characters live at 0xF000 + code

      // The glyph of `ch` in the face's cmap; 0 when it has none.
      [[nodiscard]] std::uint32_t Lookup(std::uint32_t ch) const noexcept
      {
        std::uint32_t glyph = LookupRaw(ch);
        if (glyph == 0U && symbol && ch >= 0x20U && ch <= 0xFFU) {
          glyph = LookupRaw(0xF000U + ch);
        }
        return glyph;
      }

      [[nodiscard]] std::uint32_t LookupRaw(const std::uint32_t ch) const noexcept
      {
        const std::size_t sub = cmapSubtable;
        if (sub == 0U) {
          return 0U;
        }
        if (cmapFormat == 12U) {
          std::uint32_t low = 0;
          std::uint32_t high = cmap.U32(sub + 12U);
          while (low < high) {
            const std::uint32_t middle = (low + high) / 2U;
            const std::size_t group = sub + 16U + 12U * static_cast<std::size_t>(middle);
            const std::uint32_t first = cmap.U32(group);
            const std::uint32_t last = cmap.U32(group + 4U);
            if (ch < first) {
              high = middle;
            } else if (ch > last) {
              low = middle + 1U;
            } else {
              return cmap.U32(group + 8U) + (ch - first);
            }
          }
          return 0U;
        }
        // Format 4.
        if (ch > 0xFFFFU) {
          return 0U;
        }
        const std::size_t segments = cmap.U16(sub + 6U) / 2U;
        const std::size_t ends = sub + 14U;
        const std::size_t starts = ends + 2U * segments + 2U;
        const std::size_t deltas = starts + 2U * segments;
        const std::size_t ranges = deltas + 2U * segments;
        std::size_t low = 0;
        std::size_t high = segments;
        while (low < high) {
          const std::size_t middle = (low + high) / 2U;
          if (cmap.U16(ends + 2U * middle) < ch) {
            low = middle + 1U;
          } else {
            high = middle;
          }
        }
        if (low >= segments) {
          return 0U;
        }
        const std::uint16_t start = cmap.U16(starts + 2U * low);
        if (ch < start) {
          return 0U;
        }
        const std::uint16_t delta = cmap.U16(deltas + 2U * low);
        const std::uint16_t rangeOffset = cmap.U16(ranges + 2U * low);
        if (rangeOffset == 0U) {
          return (ch + delta) & 0xFFFFU;
        }
        const std::size_t at = ranges + 2U * low + rangeOffset + 2U * (ch - start);
        const std::uint16_t glyph = cmap.U16(at);
        return (glyph == 0U) ? 0U : ((glyph + delta) & 0xFFFFU);
      }

      std::wstring family;                 // the English family name (GetTextFace)
      std::vector<std::wstring> familyKeys; // folded family names the mapper matches
      std::vector<std::wstring> fullKeys;   // folded full names

      std::once_flag glyphCharsOnce;
      std::vector<std::uint16_t> glyphChars; // glyph -> lowest character mapping to it (kerning pairs)

      [[nodiscard]] LONG Advance(const std::uint32_t glyph) const noexcept
      {
        if (numHMetrics == 0U) {
          return 0;
        }
        const std::uint32_t index = std::min<std::uint32_t>(glyph, numHMetrics - 1U);
        return hmtx.U16(static_cast<std::size_t>(index) * 4U);
      }
    };

    bool Contains(const std::vector<std::wstring>& keys, const std::wstring& key)
    {
      return std::find(keys.begin(), keys.end(), key) != keys.end();
    }

    // Family (ID 1) and full (ID 4) names in English and the user's language.
    void ReadNames(Face& face, const Table& name)
    {
      const std::uint16_t count = name.U16(2);
      const std::size_t strings = name.U16(4);
      const std::uint16_t user = UserPrimaryLanguage();
      std::wstring englishWindowsFamily;
      for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t record = 6U + 12U * i;
        const std::uint16_t platform = name.U16(record);
        const std::uint16_t encoding = name.U16(record + 2U);
        const std::uint16_t language = name.U16(record + 4U);
        const std::uint16_t id = name.U16(record + 6U);
        const std::uint16_t length = name.U16(record + 8U);
        const std::size_t offset = strings + name.U16(record + 10U);
        if ((id != 1U && id != 4U) || !name.Has(offset, length)) {
          continue;
        }
        bool wanted = false;
        CFStringEncoding cfEncoding = kCFStringEncodingUTF16BE;
        if (platform == 3U) {
          const std::uint16_t primary = language & 0x3FFU;
          wanted = primary == 0x09U || primary == user;
        } else if (platform == 0U) {
          wanted = true;
        } else if (platform == 1U && encoding == 0U) {
          wanted = language == 0U; // English
          cfEncoding = kCFStringEncodingMacRoman;
        }
        if (!wanted) {
          continue;
        }
        CFPtr<CFStringRef> text(CFStringCreateWithBytes(nullptr, name.Bytes() + offset, length, cfEncoding, false));
        const std::wstring value = FromCFString(text.get());
        if (value.empty()) {
          continue;
        }
        const std::wstring key = Fold(value);
        std::vector<std::wstring>& keys = (id == 1U) ? face.familyKeys : face.fullKeys;
        if (!Contains(keys, key)) {
          keys.push_back(key);
        }
        if (id == 1U) {
          if (platform == 3U && language == 0x0409U) {
            englishWindowsFamily = value;
          } else if (face.family.empty() && (platform != 3U || (language & 0x3FFU) == 0x09U)) {
            face.family = value;
          }
        }
      }
      if (!englishWindowsFamily.empty()) {
        face.family = englishWindowsFamily;
      }
      if (face.family.empty()) {
        CFPtr<CFStringRef> family(CTFontCopyFamilyName(face.font.get()));
        face.family = FromCFString(family.get());
        if (!face.family.empty() && !Contains(face.familyKeys, Fold(face.family))) {
          face.familyKeys.push_back(Fold(face.family));
        }
      }
    }

    // The Unicode subtable GDI reads: (3,10) format 12, then (3,1) format 4, then a Unicode
    // platform one, then (3,0) symbol.
    void SelectCmap(Face& face)
    {
      const Table& cmap = face.cmap;
      const std::uint16_t count = cmap.U16(2);
      int bestRank = 0;
      for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t record = 4U + 8U * i;
        const std::uint16_t platform = cmap.U16(record);
        const std::uint16_t encoding = cmap.U16(record + 2U);
        const std::size_t offset = cmap.U32(record + 4U);
        const std::uint16_t format = cmap.U16(offset);
        int rank = 0;
        if (format == 12U && cmap.Has(offset, 16)) {
          rank = (platform == 3U && encoding == 10U) ? 6 : (platform == 0U) ? 5 : 0;
        } else if (format == 4U && cmap.Has(offset, 14)) {
          rank = (platform == 3U && encoding == 1U) ? 4 : (platform == 0U) ? 3 : (platform == 3U && encoding == 0U) ? 2 : 0;
        }
        if (rank > bestRank) {
          bestRank = rank;
          face.cmapSubtable = offset;
          face.cmapFormat = format;
          face.symbol = rank == 2;
        }
      }
      if (face.cmapFormat == 12U) {
        const std::uint64_t groups = cmap.U32(face.cmapSubtable + 12U);
        if (!cmap.Has(face.cmapSubtable + 16U, static_cast<std::size_t>(groups * 12U))) {
          face.cmapSubtable = 0U;
        }
      } else if (face.cmapFormat == 4U) {
        const std::size_t segments = cmap.U16(face.cmapSubtable + 6U) / 2U;
        if (!cmap.Has(face.cmapSubtable, 16U + 8U * segments)) {
          face.cmapSubtable = 0U;
        }
      }
    }

    std::shared_ptr<Face> LoadFace(const CTFontDescriptorRef descriptor, const std::uint64_t source)
    {
      CFPtr<CTFontRef> font(CTFontCreateWithFontDescriptor(descriptor, 16.0, nullptr));
      if (!font) {
        return nullptr;
      }
      auto face = std::make_shared<Face>();
      face->descriptor = CFPtr<CTFontDescriptorRef>::Retain(descriptor);
      face->font = font;
      face->source = source;
      {
        CFPtr<CFStringRef> postScript(CTFontCopyPostScriptName(font.get()));
        face->postScriptName = Utf8(FromCFString(postScript.get()));
      }

      const Table head = CopyTable(font.get(), kCTFontTableHead);
      const Table hhea = CopyTable(font.get(), kCTFontTableHhea);
      const Table os2 = CopyTable(font.get(), kCTFontTableOS2);
      const Table maxp = CopyTable(font.get(), kCTFontTableMaxp);
      const Table post = CopyTable(font.get(), kCTFontTablePost);
      face->hmtx = CopyTable(font.get(), kCTFontTableHmtx);
      face->hdmx = CopyTable(font.get(), kCTFontTableHdmx);
      face->vdmx = CopyTable(font.get(), kCTFontTableVDMX);
      face->kern = CopyTable(font.get(), kCTFontTableKern);
      face->cmap = CopyTable(font.get(), kCTFontTableCmap);
      SelectCmap(*face);

      face->upem = head.Has(18, 2) ? head.U16(18) : static_cast<LONG>(CTFontGetUnitsPerEm(font.get()));
      if (face->upem <= 0) {
        face->upem = 1000;
      }
      face->bboxXMin = head.S16(36);
      face->bboxXMax = head.S16(40);
      const std::uint16_t macStyle = head.U16(44);
      face->hheaAscender = hhea.S16(4);
      face->hheaDescender = hhea.S16(6);
      face->hheaLineGap = hhea.S16(8);
      face->numHMetrics = hhea.U16(34);
      face->numGlyphs = maxp.U16(4);
      if (face->numGlyphs == 0U) {
        face->numGlyphs = static_cast<std::uint16_t>(std::min<CFIndex>(CTFontGetGlyphCount(font.get()), 0xFFFF));
      }
      if (static_cast<std::size_t>(face->numHMetrics) * 4U > face->hmtx.Size()) {
        face->numHMetrics = static_cast<std::uint16_t>(face->hmtx.Size() / 4U);
      }
      face->fixedPitch = post.U32(12) != 0U;

      if (os2.Has(0, 78)) {
        face->os2Version = os2.U16(0);
        face->avgCharWidth = os2.S16(2);
        face->weight = os2.U16(4);
        if (face->weight > 0 && face->weight < 10) {
          face->weight *= 100; // some fonts use the 1..9 scale
        }
        face->panoseFamily = os2.U8(32);
        face->panoseSerif = os2.U8(33);
        face->italic = (os2.U16(62) & 0x01U) != 0U;
        face->firstChar = os2.U16(64);
        face->lastChar = os2.U16(66);
        face->winAscent = os2.U16(74);
        face->winDescent = os2.U16(76);
        if (face->os2Version >= 2U && os2.Has(90, 4)) {
          face->defaultChar = os2.U16(90);
          face->breakChar = os2.U16(92);
        }
      } else {
        // No OS/2 table (some Mac fonts): what GDI's driver derives from the other tables.
        face->weight = (macStyle & 0x01U) != 0U ? FW_BOLD : FW_NORMAL;
        face->italic = (macStyle & 0x02U) != 0U;
        LONG sum = 0;
        LONG used = 0;
        for (std::uint16_t i = 0; i < face->numHMetrics; ++i) {
          const LONG advance = face->hmtx.U16(static_cast<std::size_t>(i) * 4U);
          if (advance > 0) {
            sum += advance;
            ++used;
          }
        }
        face->avgCharWidth = (used > 0) ? sum / used : face->upem / 2;
      }
      if (face->weight == 0) {
        face->weight = FW_NORMAL;
      }
      if (face->winAscent == 0 && face->winDescent == 0) {
        face->winAscent = face->hheaAscender;
        face->winDescent = -face->hheaDescender;
      }

      CFPtr<CFArrayRef> tables(CTFontCopyAvailableTables(font.get(), kCTFontTableOptionNoOptions));
      if (tables) {
        for (CFIndex i = 0; i < CFArrayGetCount(tables.get()); ++i) {
          // The array holds the tags themselves (not CFNumbers).
          const auto tag = static_cast<CTFontTableTag>(reinterpret_cast<std::uintptr_t>(CFArrayGetValueAtIndex(tables.get(), i)));
          face->trueType = face->trueType || tag == kCTFontTableGlyf;
          face->postScript = face->postScript || tag == kCTFontTableCFF;
        }
      }

      ReadNames(*face, CopyTable(font.get(), kCTFontTableName));
      return face;
    }

    // --- Realized fonts --------------------------------------------------------------------------
    struct Glyph
    {
      CGGlyph glyph = 0;
      std::uint16_t font = 0; // index into Font::mFonts (0: the face itself)
      LONG advance = 0;
      LONG offset = 0;        // drawn this far right of the pen (spacing fallback marks)
      CGFloat scale = 1.0;    // fallback glyphs fitted to the face's cell
      CGFloat rise = 0.0;     // ... and moved up this far (pixels)
    };
  } // namespace

  class Font
  {
  public:
    std::shared_ptr<Face> mFace;
    CFPtr<CTFontRef> mCtFont; // at ppem, horizontally scaled by xppem / ppem
    LONG mPpem = 0;
    LONG mXPpem = 0;
    std::int64_t mXScale = 0; // 16.16, font units -> 26.6 pixels
    std::int64_t mYScale = 0;
    bool mAntialias = true;
    TEXTMETRICW mMetrics{};
    const std::uint8_t* mHdmxWidths = nullptr; // the face's hdmx widths at ppem

    std::mutex mMutex;
    std::unordered_map<char32_t, Glyph> mGlyphs;
    std::vector<CFPtr<CTFontRef>> mFonts; // [0] = mCtFont, then fallback fonts
    bool mKerningReady = false;
    std::vector<KERNINGPAIR> mKerning;
  };

  namespace
  {
    struct RealizeKey
    {
      const Face* face;
      LONG ppem;
      LONG xppem;
      bool antialias;
      BYTE underline;
      BYTE strikeOut;
      BYTE charSet;

      bool operator<(const RealizeKey& other) const noexcept
      {
        return std::tie(face, ppem, xppem, antialias, underline, strikeOut, charSet) <
          std::tie(other.face, other.ppem, other.xppem, other.antialias, other.underline, other.strikeOut, other.charSet);
      }
    };

    struct InstalledMatch
    {
      std::vector<std::shared_ptr<Face>> family; // faces of the family with that name
      std::shared_ptr<Face> full;                // the face with that full name
    };

    struct Registry
    {
      std::mutex mutex;
      std::vector<std::shared_ptr<Face>> memory; // private fonts, in registration order
      std::uint64_t nextSource = 1;
      std::map<std::string, std::shared_ptr<Face>> installedByPostScriptName;
      std::map<std::wstring, InstalledMatch> installedByName; // folded name -> match (empty: none)
      std::map<RealizeKey, std::shared_ptr<Font>> realized;
      std::set<std::wstring> reportedMissing;
    };

    Registry& Fonts()
    {
      static Registry* const registry = new Registry(); // never destroyed: GDI may run during exit
      return *registry;
    }

    std::shared_ptr<Face> LoadInstalledLocked(Registry& registry, const CTFontDescriptorRef descriptor)
    {
      CFPtr<CFStringRef> name(static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute)));
      const std::string postScriptName = Utf8(FromCFString(name.get()));
      if (!postScriptName.empty()) {
        const auto found = registry.installedByPostScriptName.find(postScriptName);
        if (found != registry.installedByPostScriptName.end()) {
          return found->second;
        }
      }
      std::shared_ptr<Face> face = LoadFace(descriptor, 0U);
      if (face != nullptr && !postScriptName.empty()) {
        registry.installedByPostScriptName.emplace(postScriptName, face);
      }
      return face;
    }

    const InstalledMatch& FindInstalledLocked(Registry& registry, const std::wstring& name, const std::wstring& key)
    {
      const auto cached = registry.installedByName.find(key);
      if (cached != registry.installedByName.end()) {
        return cached->second;
      }
      InstalledMatch match;
      CFPtr<CFStringRef> cfName = MakeCFString(name);
      if (cfName) {
        const void* keys[] = {kCTFontFamilyNameAttribute};
        const void* values[] = {cfName.get()};
        CFPtr<CFDictionaryRef> attributes(
          CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
        );
        CFPtr<CTFontDescriptorRef> wanted(CTFontDescriptorCreateWithAttributes(attributes.get()));
        CFPtr<CFSetRef> mandatory(CFSetCreate(nullptr, keys, 1, &kCFTypeSetCallBacks));
        CFPtr<CFArrayRef> matches(CTFontDescriptorCreateMatchingFontDescriptors(wanted.get(), mandatory.get()));
        if (matches) {
          for (CFIndex i = 0; i < CFArrayGetCount(matches.get()); ++i) {
            const auto descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches.get(), i));
            std::shared_ptr<Face> face = LoadInstalledLocked(registry, descriptor);
            if (face != nullptr && Contains(face->familyKeys, key)) {
              match.family.push_back(std::move(face));
            }
          }
        }
        if (match.family.empty()) {
          // A full (or PostScript) name: Core Text answers with some font for any name, so
          // only a face that carries the name counts.
          CFPtr<CTFontRef> font(CTFontCreateWithName(cfName.get(), 16.0, nullptr));
          if (font) {
            CFPtr<CTFontDescriptorRef> descriptor(CTFontCopyFontDescriptor(font.get()));
            std::shared_ptr<Face> face = descriptor ? LoadInstalledLocked(registry, descriptor.get()) : nullptr;
            if (face != nullptr && Contains(face->fullKeys, key)) {
              match.full = face;
            } else if (face != nullptr && Contains(face->familyKeys, key)) {
              match.family.push_back(face);
            }
          }
        }
      }
      return registry.installedByName.emplace(key, std::move(match)).first->second;
    }

    // The face of a family nearest the requested weight and slant (GDI's mapper penalties,
    // reduced to the two that tell faces of one family apart).
    std::shared_ptr<Face> BestOfFamily(const std::vector<std::shared_ptr<Face>>& faces, const LOGFONTW& logFont)
    {
      const LONG weight = (logFont.lfWeight == FW_DONTCARE) ? FW_NORMAL : logFont.lfWeight;
      const bool italic = logFont.lfItalic != 0U;
      std::shared_ptr<Face> best;
      LONG bestScore = 0;
      for (const std::shared_ptr<Face>& face : faces) {
        const LONG score = ((face->italic != italic) ? 100000 : 0) + std::abs(face->weight - weight);
        if (best == nullptr || score < bestScore) {
          best = face;
          bestScore = score;
        }
      }
      return best;
    }

    std::shared_ptr<Face> FindFaceLocked(Registry& registry, const std::wstring& name, const LOGFONTW& logFont)
    {
      const std::wstring key = Fold(name);
      if (key.empty()) {
        return nullptr;
      }
      std::vector<std::shared_ptr<Face>> family;
      for (const std::shared_ptr<Face>& face : registry.memory) {
        if (Contains(face->familyKeys, key)) {
          family.push_back(face);
        }
      }
      if (!family.empty()) {
        return BestOfFamily(family, logFont);
      }
      for (const std::shared_ptr<Face>& face : registry.memory) {
        if (Contains(face->fullKeys, key)) {
          return face;
        }
      }
      const InstalledMatch& installed = FindInstalledLocked(registry, name, key);
      if (!installed.family.empty()) {
        return BestOfFamily(installed.family, logFont);
      }
      return installed.full;
    }

    std::wstring RequestedName(const LOGFONTW& logFont)
    {
      std::wstring name;
      for (std::size_t i = 0; i < LF_FACESIZE - 1U && logFont.lfFaceName[i] != L'\0'; ++i) {
        name.push_back(logFont.lfFaceName[i]);
      }
      if (!name.empty() && name.front() == L'@') {
        name.erase(0, 1); // vertical-writing variant: drawn horizontally here
      }
      return name;
    }

    std::shared_ptr<Face> MapFaceLocked(Registry& registry, const LOGFONTW& logFont)
    {
      const std::wstring requested = RequestedName(logFont);
      if (std::shared_ptr<Face> face = FindFaceLocked(registry, requested, logFont)) {
        return face;
      }
      const BYTE pitch = logFont.lfPitchAndFamily & 0x03U;
      const BYTE family = logFont.lfPitchAndFamily & 0xF0U;
      const wchar_t* const fallback = (pitch == FIXED_PITCH || family == FF_MODERN) ? L"Courier New"
        : (family == FF_ROMAN)                                                      ? L"Times New Roman"
                                                                                    : L"Arial";
      std::shared_ptr<Face> face = FindFaceLocked(registry, fallback, logFont);
      if (face == nullptr) {
        face = FindFaceLocked(registry, L"Helvetica", logFont);
      }
      if (registry.reportedMissing.insert(Fold(requested)).second) {
        std::fprintf(
          stderr, "[faf-posix] gdi: no font named \"%s\"; using \"%s\"\n", Utf8(requested).c_str(),
          face != nullptr ? Utf8(face->family).c_str() : "?"
        );
      }
      return face;
    }

    // Wine's load_VDMX: the ppem and exact ascent/descent for a height, from the VDMX group
    // of the 1:1 aspect ratio. False when the table has no answer (linear scaling then).
    bool LookupVdmx(const Face& face, const LONG height, LONG& ppem, LONG& yMax, LONG& yMin)
    {
      const Table& vdmx = face.vdmx;
      if (!vdmx.Has(0, 6) || height == 0) {
        return false;
      }
      const std::uint16_t ratios = vdmx.U16(4);
      std::size_t group = 0;
      for (std::uint16_t i = 0; i < ratios; ++i) {
        const std::size_t ratio = 6U + 4U * i;
        const std::uint8_t xRatio = vdmx.U8(ratio + 1U);
        const std::uint8_t yStart = vdmx.U8(ratio + 2U);
        const std::uint8_t yEnd = vdmx.U8(ratio + 3U);
        // The screen is 1:1.
        if ((xRatio == 0U && yStart == 0U && yEnd == 0U) || (xRatio == 1U && yStart <= 1U && yEnd >= 1U)) {
          group = vdmx.U16(6U + 4U * ratios + 2U * i);
          break;
        }
      }
      if (group == 0U || !vdmx.Has(group, 4)) {
        return false;
      }
      const std::uint16_t records = vdmx.U16(group);
      const std::uint8_t startSize = vdmx.U8(group + 2U);
      const std::uint8_t endSize = vdmx.U8(group + 3U);
      const auto entry = [&](const std::uint16_t i, LONG& pel, LONG& top, LONG& bottom) {
        const std::size_t at = group + 4U + 6U * i;
        pel = vdmx.U16(at);
        top = vdmx.S16(at + 2U);
        bottom = vdmx.S16(at + 4U);
      };
      if (height > 0) {
        LONG previousPel = 0;
        LONG previousTop = 0;
        LONG previousBottom = 0;
        for (std::uint16_t i = 0; i < records && vdmx.Has(group + 4U + 6U * i, 6); ++i) {
          LONG pel = 0;
          LONG top = 0;
          LONG bottom = 0;
          entry(i, pel, top, bottom);
          if (top - bottom == height) {
            ppem = pel;
            yMax = top;
            yMin = bottom;
            return true;
          }
          if (top - bottom > height) {
            if (i == 0U) {
              return false;
            }
            ppem = previousPel;
            yMax = previousTop;
            yMin = previousBottom;
            return true;
          }
          previousPel = pel;
          previousTop = top;
          previousBottom = bottom;
        }
        return false;
      }
      const LONG wanted = -height;
      if (wanted < startSize || wanted > endSize) {
        return false;
      }
      for (std::uint16_t i = 0; i < records && vdmx.Has(group + 4U + 6U * i, 6); ++i) {
        LONG pel = 0;
        LONG top = 0;
        LONG bottom = 0;
        entry(i, pel, top, bottom);
        if (pel > wanted) {
          return false;
        }
        if (pel == wanted) {
          ppem = pel;
          yMax = top;
          yMin = bottom;
          return true;
        }
      }
      return false;
    }

    // Wine's calc_ppem_for_height.
    LONG PpemForHeight(const Face& face, LONG height)
    {
      if (height == 0) {
        height = 16;
      }
      LONG ppem = 0;
      if (height > 0) {
        LONG units = face.winAscent + face.winDescent;
        if (units == 0) {
          units = face.hheaAscender - face.hheaDescender;
        }
        if (units <= 0) {
          units = face.upem;
        }
        ppem = static_cast<LONG>(MulDiv(face.upem, height, units));
        if (ppem > 1 && MulDiv(units, ppem, face.upem) > height) {
          --ppem;
        }
      } else if (height >= -1000000) {
        ppem = -height;
      } else {
        ppem = 1;
      }
      return std::max<LONG>(ppem, 1);
    }

    BYTE FamilyBits(const Face& face)
    {
      switch (face.panoseFamily) {
      case 3:
        return FF_SCRIPT;
      case 4:
        return FF_DECORATIVE;
      default:
        break;
      }
      if (face.fixedPitch) {
        return FF_MODERN;
      }
      switch (face.panoseSerif) {
      case 0:
      case 1:
        return FF_DONTCARE;
      case 11:
      case 12:
      case 13:
        return FF_SWISS;
      default:
        return FF_ROMAN;
      }
    }

    std::shared_ptr<Font> RealizeLocked(Registry& registry, const std::shared_ptr<Face>& face, const LOGFONTW& logFont)
    {
      LONG ppem = 0;
      LONG yMax = 0;
      LONG yMin = 0;
      const LONG height = (logFont.lfHeight == 0) ? 16 : logFont.lfHeight;
      const bool exactHeights = LookupVdmx(*face, height, ppem, yMax, yMin);
      if (!exactHeights) {
        ppem = PpemForHeight(*face, height);
      }
      const std::int64_t yScale = DivFix(static_cast<std::int64_t>(ppem) << 6, face->upem);
      LONG xppem = ppem;
      if (logFont.lfWidth != 0) {
        // lfWidth is the average character width wanted: scale horizontally to it.
        const LONG average = std::max<LONG>(Round26Dot6(MulFix(face->avgCharWidth, yScale)), 1);
        xppem = std::max<LONG>(static_cast<LONG>(MulDiv(ppem, std::abs(logFont.lfWidth), average)), 1);
      }
      const bool antialias = logFont.lfQuality != NONANTIALIASED_QUALITY;

      const RealizeKey key{face.get(), ppem, xppem, antialias, logFont.lfUnderline, logFont.lfStrikeOut, logFont.lfCharSet};
      const auto found = registry.realized.find(key);
      if (found != registry.realized.end()) {
        return found->second;
      }

      auto font = std::make_shared<Font>();
      font->mFace = face;
      font->mPpem = ppem;
      font->mXPpem = xppem;
      font->mYScale = yScale;
      font->mXScale = DivFix(static_cast<std::int64_t>(xppem) << 6, face->upem);
      font->mAntialias = antialias;
      const CGAffineTransform stretch = CGAffineTransformMakeScale(static_cast<CGFloat>(xppem) / static_cast<CGFloat>(ppem), 1.0);
      font->mCtFont = CFPtr<CTFontRef>(
        CTFontCreateWithFontDescriptor(face->descriptor.get(), static_cast<CGFloat>(ppem), (xppem != ppem) ? &stretch : nullptr)
      );
      if (!font->mCtFont) {
        font->mCtFont = face->font;
      }
      font->mFonts.push_back(font->mCtFont);

      // The hdmx widths at this size.
      const Table& hdmx = face->hdmx;
      if (xppem == ppem && hdmx.Has(0, 8)) {
        const auto records = static_cast<std::int16_t>(hdmx.U16(2));
        const std::uint32_t recordSize = hdmx.U32(4);
        for (std::int16_t i = 0; i < records; ++i) {
          const std::size_t at = 8U + static_cast<std::size_t>(recordSize) * static_cast<std::size_t>(i);
          if (!hdmx.Has(at, 2U + face->numGlyphs)) {
            break;
          }
          if (hdmx.U8(at) == ppem) {
            font->mHdmxWidths = hdmx.Bytes() + at + 2U;
            break;
          }
        }
      }

      // TEXTMETRIC as the TrueType driver fills it (Wine's get_text_metrics).
      TEXTMETRICW& tm = font->mMetrics;
      const LONG ascent = face->winAscent;
      const LONG descent = face->winDescent;
      if (exactHeights) {
        tm.tmAscent = yMax;
        tm.tmDescent = -yMin;
        tm.tmInternalLeading = (tm.tmAscent + tm.tmDescent) - ppem;
      } else {
        tm.tmAscent = Round26Dot6(MulFix(ascent, yScale));
        tm.tmDescent = Round26Dot6(MulFix(descent, yScale));
        tm.tmInternalLeading = Round26Dot6(MulFix(ascent + descent - face->upem, yScale));
      }
      tm.tmHeight = tm.tmAscent + tm.tmDescent;
      tm.tmExternalLeading = std::max<LONG>(
        0, Round26Dot6(MulFix(face->hheaLineGap - ((ascent + descent) - (face->hheaAscender - face->hheaDescender)), yScale))
      );
      tm.tmAveCharWidth = std::max<LONG>(Round26Dot6(MulFix(face->avgCharWidth, font->mXScale)), 1);
      tm.tmMaxCharWidth = Round26Dot6(MulFix(face->bboxXMax - face->bboxXMin, font->mXScale));
      tm.tmWeight = face->weight;
      tm.tmOverhang = 0;
      tm.tmDigitizedAspectX = 96;
      tm.tmDigitizedAspectY = 96;
      tm.tmFirstChar = face->firstChar;
      tm.tmLastChar = face->lastChar;
      tm.tmDefaultChar = face->defaultChar;
      tm.tmBreakChar = (face->breakChar != 0U) ? face->breakChar : 0x20U;
      tm.tmItalic = face->italic ? 0xFFU : 0U;
      tm.tmUnderlined = logFont.lfUnderline;
      tm.tmStruckOut = logFont.lfStrikeOut;
      tm.tmPitchAndFamily = static_cast<BYTE>(
        (face->fixedPitch ? 0U : TMPF_FIXED_PITCH) | TMPF_VECTOR | (face->trueType ? TMPF_TRUETYPE : 0U) |
        (face->postScript ? TMPF_DEVICE : 0U) | FamilyBits(*face)
      );
      tm.tmCharSet = (logFont.lfCharSet == DEFAULT_CHARSET) ? ANSI_CHARSET : logFont.lfCharSet;

      registry.realized.emplace(key, font);
      return font;
    }

    LONG FaceAdvance(const Font& font, const CGGlyph glyph)
    {
      const Face& face = *font.mFace;
      if (font.mHdmxWidths != nullptr && glyph < face.numGlyphs) {
        return font.mHdmxWidths[glyph];
      }
      if (face.numHMetrics != 0U) {
        return Round26Dot6(MulFix(face.Advance(glyph), font.mXScale));
      }
      CGGlyph copy = glyph;
      const double advance = CTFontGetAdvancesForGlyphs(font.mCtFont.get(), kCTFontOrientationHorizontal, &copy, nullptr, 1);
      return static_cast<LONG>(std::floor(advance + 0.5));
    }

    bool IsControl(const char32_t ch) noexcept
    {
      return ch < 0x20U || (ch >= 0x7FU && ch < 0xA0U);
    }

    CFIndex Utf16(const char32_t ch, UniChar (&units)[2]) noexcept
    {
      if (ch >= 0x10000U && ch <= 0x10FFFFU) {
        const char32_t value = ch - 0x10000U;
        units[0] = static_cast<UniChar>(0xD800U + (value >> 10));
        units[1] = static_cast<UniChar>(0xDC00U + (value & 0x3FFU));
        return 2;
      }
      units[0] = static_cast<UniChar>(ch);
      units[1] = 0;
      return 1;
    }

    // Under font.mMutex.
    const Glyph& GlyphFor(Font& font, const char32_t ch)
    {
      const auto cached = font.mGlyphs.find(ch);
      if (cached != font.mGlyphs.end()) {
        return cached->second;
      }
      Glyph result;
      UniChar units[2];
      const CFIndex length = Utf16(ch, units);
      const Face& face = *font.mFace;
      CGGlyph glyph = 0;
      if (face.cmapSubtable != 0U) {
        const std::uint32_t mapped = face.Lookup(static_cast<std::uint32_t>(ch));
        glyph = (mapped < face.numGlyphs) ? static_cast<CGGlyph>(mapped) : 0U;
      } else {
        // No cmap GDI could read (a Mac-only font): Core Text's mapping.
        CGGlyph glyphs[2] = {0, 0};
        if (CTFontGetGlyphsForCharacters(font.mCtFont.get(), units, glyphs, length)) {
          glyph = glyphs[0];
        }
      }
      if (glyph != 0U) {
        result.glyph = glyph;
        result.advance = FaceAdvance(font, glyph);
      } else {
        bool found = false;
        if (ch >= 0x80U && !IsControl(ch)) {
          CFPtr<CFStringRef> text(CFStringCreateWithCharacters(nullptr, units, length));
          CFPtr<CTFontRef> fallback(text ? CTFontCreateForString(font.mCtFont.get(), text.get(), CFRangeMake(0, length)) : nullptr);
          CGGlyph fallbackGlyphs[2] = {0, 0};
          if (fallback && CTFontGetGlyphsForCharacters(fallback.get(), units, fallbackGlyphs, length) && fallbackGlyphs[0] != 0U) {
            std::size_t index = 0;
            while (index < font.mFonts.size() && !CFEqual(font.mFonts[index].get(), fallback.get())) {
              ++index;
            }
            if (index == font.mFonts.size()) {
              font.mFonts.push_back(fallback);
            }
            result.glyph = fallbackGlyphs[0];
            result.font = static_cast<std::uint16_t>(index);
            double advance =
              CTFontGetAdvancesForGlyphs(fallback.get(), kCTFontOrientationHorizontal, &fallbackGlyphs[0], nullptr, 1);
            const CGRect ink =
              CTFontGetBoundingRectsForGlyphs(fallback.get(), kCTFontOrientationHorizontal, &fallbackGlyphs[0], nullptr, 1);
            const double top = static_cast<double>(font.mMetrics.tmAscent);
            const double bottom = -static_cast<double>(font.mMetrics.tmDescent);
            if (ink.size.height > 0.0 && (ink.origin.y + ink.size.height > top || ink.origin.y < bottom)) {
              const double scale = std::min(1.0, (top - bottom) / ink.size.height);
              result.scale = static_cast<CGFloat>(scale);
              result.rise = static_cast<CGFloat>((top + bottom) / 2.0 - scale * (ink.origin.y + ink.size.height / 2.0));
              advance *= scale;
            }
            result.advance = static_cast<LONG>(std::floor(advance + 0.5));
            if (result.advance <= 0 && ink.size.width > 0.0) {
              const double left = std::floor(result.scale * ink.origin.x);
              result.offset = static_cast<LONG>(-left);
              result.advance = static_cast<LONG>(std::ceil(result.scale * (ink.origin.x + ink.size.width) - left)) + 1;
            }
            found = true;
          }
        }
        if (!found) {
          result.glyph = 0; // the face's missing-glyph shape
          result.advance = FaceAdvance(font, 0);
        }
      }
      return font.mGlyphs.emplace(ch, result).first->second;
    }

    CGColorSpaceRef DeviceGray()
    {
      static const CGColorSpaceRef space = CGColorSpaceCreateDeviceGray(); // process lifetime
      return space;
    }

    // The contrast curve applied to antialiased coverage (see the top of this file).
    const std::uint8_t* CoverageCurve()
    {
      static const auto* const curve = [] {
        double gamma = 1.45;
        if (const char* const text = std::getenv("FAF_GDI_TEXT_GAMMA")) {
          const double value = std::strtod(text, nullptr);
          if (value >= 0.25 && value <= 4.0) {
            gamma = value;
          }
        }
        auto* const table = new std::uint8_t[256];
        for (int i = 0; i < 256; ++i) {
          table[i] = static_cast<std::uint8_t>(std::lround(255.0 * std::pow(i / 255.0, 1.0 / gamma)));
        }
        return table;
      }();
      return curve;
    }
  } // namespace

  HANDLE AddMemoryFonts(const void* const data, const std::size_t size, DWORD* const count)
  {
    if (count != nullptr) {
      *count = 0U;
    }
    if (data == nullptr || size == 0U) {
      return nullptr;
    }
    CFPtr<CFDataRef> bytes(CFDataCreate(nullptr, static_cast<const UInt8*>(data), static_cast<CFIndex>(size)));
    CFPtr<CFArrayRef> descriptors(bytes ? CTFontManagerCreateFontDescriptorsFromData(bytes.get()) : nullptr);
    if (!descriptors || CFArrayGetCount(descriptors.get()) == 0) {
      return nullptr;
    }
    Registry& registry = Fonts();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    const std::uint64_t source = registry.nextSource++;
    DWORD added = 0U;
    for (CFIndex i = 0; i < CFArrayGetCount(descriptors.get()); ++i) {
      const auto descriptor = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descriptors.get(), i));
      if (std::shared_ptr<Face> face = LoadFace(descriptor, source)) {
        registry.memory.push_back(std::move(face));
        ++added;
      }
    }
    if (added == 0U) {
      return nullptr;
    }
    if (count != nullptr) {
      *count = added;
    }
    // Opaque, non-null, distinct per call (not a kernel handle; only RemoveFontMemResourceEx takes it).
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x46460000U + source * 4U));
  }

  bool RemoveMemoryFonts(const HANDLE handle)
  {
    const auto value = reinterpret_cast<std::uintptr_t>(handle);
    if (value < 0x46460000U || (value & 3U) != 0U) {
      return false;
    }
    const std::uint64_t source = (value - 0x46460000U) / 4U;
    Registry& registry = Fonts();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    const auto removed = std::remove_if(registry.memory.begin(), registry.memory.end(), [source](const std::shared_ptr<Face>& face) {
      return face->source == source;
    });
    if (removed == registry.memory.end()) {
      return false;
    }
    registry.memory.erase(removed, registry.memory.end());
    return true; // fonts already realized from them stay usable, as GDI keeps them while selected
  }

  std::shared_ptr<Font> RealizeFont(const LOGFONTW& logFont)
  {
    Registry& registry = Fonts();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    std::shared_ptr<Face> face = MapFaceLocked(registry, logFont);
    if (face == nullptr) {
      // Not even Helvetica: the system font, so that text still has metrics.
      CFPtr<CTFontRef> system(CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 16.0, nullptr));
      CFPtr<CTFontDescriptorRef> descriptor(system ? CTFontCopyFontDescriptor(system.get()) : nullptr);
      face = descriptor ? LoadInstalledLocked(registry, descriptor.get()) : nullptr;
      if (face == nullptr) {
        return nullptr;
      }
    }
    if (logFont.lfEscapement != 0 || logFont.lfOrientation != 0) {
      FAF_POSIX_STUB("CreateFont: escapement/orientation are not supported on POSIX; text is drawn horizontally");
    }
    if (logFont.lfUnderline != 0U || logFont.lfStrikeOut != 0U) {
      FAF_POSIX_STUB("CreateFont: underline/strikeout are not drawn on POSIX");
    }
    if ((logFont.lfWeight >= 600 && face->weight < 550) || (logFont.lfItalic != 0U && !face->italic)) {
      FAF_POSIX_STUB("CreateFont: no simulated bold/italic on POSIX; the nearest face is used as is");
    }
    return RealizeLocked(registry, face, logFont);
  }

  const TEXTMETRICW& Metrics(const Font& font) noexcept
  {
    return font.mMetrics;
  }

  std::wstring FaceName(const Font& font)
  {
    return font.mFace->family;
  }

  LONG Advance(Font& font, const char32_t ch)
  {
    const std::lock_guard<std::mutex> lock(font.mMutex);
    return GlyphFor(font, ch).advance;
  }

  LONG TextWidth(Font& font, const std::u32string& text)
  {
    const std::lock_guard<std::mutex> lock(font.mMutex);
    LONG width = 0;
    for (const char32_t ch : text) {
      width += GlyphFor(font, ch).advance;
    }
    return width;
  }

  const std::vector<KERNINGPAIR>& KerningPairs(Font& font)
  {
    const std::lock_guard<std::mutex> lock(font.mMutex);
    if (font.mKerningReady) {
      return font.mKerning;
    }
    font.mKerningReady = true;
    Face& face = *font.mFace;
    const Table& kern = face.kern;
    if (kern.U16(0) != 0U) {
      return font.mKerning; // only the Windows 'kern' layout (version 0); Apple's is not GDI's
    }

    // Glyph -> character: the lowest character of the cmap mapping to each glyph (Wine's
    // glyph_to_char).
    std::call_once(face.glyphCharsOnce, [&face] {
      face.glyphChars.assign(face.numGlyphs, 0U);
      for (std::uint32_t ch = 0U; ch <= 0xFFFFU; ++ch) {
        const std::uint32_t glyph = face.Lookup(ch);
        if (glyph != 0U && glyph < face.glyphChars.size() && face.glyphChars[glyph] == 0U) {
          face.glyphChars[glyph] = static_cast<std::uint16_t>(ch);
        }
      }
    });

    const std::uint16_t tables = kern.U16(2);
    std::size_t at = 4U;
    for (std::uint16_t t = 0; t < tables && kern.Has(at, 6); ++t) {
      const std::uint16_t length = kern.U16(at + 2U);
      const std::uint16_t coverage = kern.U16(at + 4U);
      if ((coverage >> 8) == 0U && (coverage & 0x01U) != 0U) {
        const std::uint16_t pairs = kern.U16(at + 6U);
        for (std::uint16_t i = 0; i < pairs; ++i) {
          const std::size_t pair = at + 14U + 6U * i;
          if (!kern.Has(pair, 6)) {
            break;
          }
          const std::uint16_t left = kern.U16(pair);
          const std::uint16_t right = kern.U16(pair + 2U);
          // Wine's rounding ("appears to better match what Windows does").
          LONG amount = static_cast<LONG>(kern.S16(pair + 4U)) * font.mPpem;
          if (amount < 0) {
            amount -= face.upem / 2 + font.mPpem;
          } else if (amount > 0) {
            amount += face.upem / 2 + font.mPpem;
          }
          amount /= face.upem;
          KERNINGPAIR out{};
          out.wFirst = (left < face.glyphChars.size()) ? face.glyphChars[left] : 0U;
          out.wSecond = (right < face.glyphChars.size()) ? face.glyphChars[right] : 0U;
          out.iKernAmount = amount;
          font.mKerning.push_back(out);
        }
      }
      if (length < 6U) {
        break;
      }
      at += length;
    }
    return font.mKerning;
  }

  void Rasterize(
    Font& font, const std::u32string& text, const LONG penX, const LONG baselineY, std::uint8_t* const coverage, const LONG width,
    const LONG height, const LONG stride
  )
  {
    if (text.empty() || coverage == nullptr || width <= 0 || height <= 0 || stride < width) {
      return;
    }
    const std::lock_guard<std::mutex> lock(font.mMutex);
    struct Placed
    {
      std::uint16_t font;
      CGGlyph glyph;
      LONG x;
      CGFloat stretch; // horizontal scale fitting the outline to the integer advance
      CGFloat scale;   // uniform scale and rise of a fitted fallback glyph
      CGFloat rise;
    };
    const Face& face = *font.mFace;
    std::vector<Placed> placed;
    placed.reserve(text.size());
    LONG x = penX;
    for (const char32_t ch : text) {
      const Glyph& glyph = GlyphFor(font, ch);
      CGFloat stretch = 1.0;
      if (glyph.font == 0U && face.numHMetrics != 0U && glyph.advance > 0) {
        const double linear = static_cast<double>(face.Advance(glyph.glyph)) * font.mXPpem / face.upem;
        if (linear > 0.0 && std::fabs(linear - glyph.advance) > 0.01) {
          stretch = static_cast<CGFloat>(std::clamp(glyph.advance / linear, 0.75, 1.33));
        }
      }
      placed.push_back(Placed{glyph.font, glyph.glyph, x + glyph.offset, stretch, glyph.scale, glyph.rise});
      x += glyph.advance;
    }

    CFPtr<CGContextRef> context(CGBitmapContextCreate(
      coverage, static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8U, static_cast<std::size_t>(stride), DeviceGray(),
      kCGImageAlphaNone
    ));
    if (!context) {
      return;
    }
    CGContextSetShouldAntialias(context.get(), font.mAntialias);
    CGContextSetAllowsFontSmoothing(context.get(), false);
    CGContextSetShouldSmoothFonts(context.get(), false);
    CGContextSetAllowsFontSubpixelPositioning(context.get(), false);
    CGContextSetShouldSubpixelPositionFonts(context.get(), false);
    CGContextSetAllowsFontSubpixelQuantization(context.get(), false);
    CGContextSetShouldSubpixelQuantizeFonts(context.get(), false);
    CGContextSetGrayFillColor(context.get(), 1.0, 1.0);
    CGContextSetTextMatrix(context.get(), CGAffineTransformIdentity);

    // Core Graphics counts rows from the bottom.
    const CGFloat baseline = static_cast<CGFloat>(height - baselineY);
    std::vector<CGGlyph> glyphs;
    std::vector<CGPoint> positions;
    for (std::size_t index = 0; index < font.mFonts.size(); ++index) {
      glyphs.clear();
      positions.clear();
      for (const Placed& item : placed) {
        if (item.font != index) {
          continue;
        }
        if (item.stretch != 1.0 || item.scale != 1.0 || item.rise != 0.0) {
          CGContextSaveGState(context.get());
          CGContextTranslateCTM(context.get(), static_cast<CGFloat>(item.x), baseline + item.rise);
          CGContextScaleCTM(context.get(), item.stretch * item.scale, item.scale);
          const CGPoint origin = CGPointMake(0.0, 0.0);
          CTFontDrawGlyphs(font.mFonts[index].get(), &item.glyph, &origin, 1, context.get());
          CGContextRestoreGState(context.get());
          continue;
        }
        glyphs.push_back(item.glyph);
        positions.push_back(CGPointMake(static_cast<CGFloat>(item.x), baseline));
      }
      if (!glyphs.empty()) {
        CTFontDrawGlyphs(font.mFonts[index].get(), glyphs.data(), positions.data(), glyphs.size(), context.get());
      }
    }
    CGContextFlush(context.get());
    if (font.mAntialias) {
      const std::uint8_t* const curve = CoverageCurve();
      for (LONG row = 0; row < height; ++row) {
        std::uint8_t* const line = coverage + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
        for (LONG column = 0; column < width; ++column) {
          line[column] = curve[line[column]];
        }
      }
    }
  }

  std::u32string DecodeWide(const WCHAR* const text, const int count)
  {
    std::u32string out;
    if (text == nullptr) {
      return out;
    }
    std::size_t length = 0;
    if (count < 0) {
      while (text[length] != 0) {
        ++length;
      }
    } else {
      length = static_cast<std::size_t>(count);
    }
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
      auto value = static_cast<std::uint32_t>(text[i]);
      if (value >= 0xD800U && value < 0xDC00U && i + 1U < length) {
        const auto low = static_cast<std::uint32_t>(text[i + 1U]);
        if (low >= 0xDC00U && low < 0xE000U) {
          out.push_back(static_cast<char32_t>(0x10000U + ((value - 0xD800U) << 10) + (low - 0xDC00U)));
          ++i;
          continue;
        }
      }
      if (value > 0x10FFFFU) {
        value &= 0xFFFFU;
      }
      out.push_back(static_cast<char32_t>(value));
    }
    return out;
  }
} // namespace faf_posix::gdi
