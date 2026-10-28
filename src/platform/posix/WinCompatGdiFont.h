#pragma once

// Internals of the Gdi area's fonts (compat/Gdi.h), shared by WinCompatGdi.cpp (the GDI
// objects and text drawing) and WinCompatGdiFont.cpp (the font table, the mapper and the
// Core Text rasterizer). Not part of the force-included surface: engine code never
// includes this.
//
// A Font is what a LOGFONT realizes to: one face at one pixel size, with GDI's integer
// metrics. Realized fonts are shared and live for the process (as GDI caches realizations).
// Every function is thread-safe.

#if !defined(FAF_POSIX)
#error "POSIX build only"
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace faf_posix::gdi
{
  class Font;

  // AddFontMemResourceEx / RemoveFontMemResourceEx: the faces of one font file (a TrueType
  // or OpenType font, or a collection) become private fonts. `data` is copied. Null when
  // the data holds no font (`*count` = 0).
  HANDLE AddMemoryFonts(const void* data, std::size_t size, DWORD* count);
  bool RemoveMemoryFonts(HANDLE handle);

  // The font GDI's mapper would choose for `logFont` (never null: a missing face falls back,
  // logged once per name).
  std::shared_ptr<Font> RealizeFont(const LOGFONTW& logFont);

  // GetTextMetrics.
  const TEXTMETRICW& Metrics(const Font& font) noexcept;

  // GetTextFace: the family name of the realized face.
  std::wstring FaceName(const Font& font);

  // The integer advance of each character, in pixels (GetTextExtentPoint32 is their sum).
  LONG Advance(Font& font, char32_t ch);
  LONG TextWidth(Font& font, const std::u32string& text);

  // GetKerningPairs: every pair of the font's 'kern' table, by character, in pixels.
  const std::vector<KERNINGPAIR>& KerningPairs(Font& font);

  // Draws `text` as coverage (0 = none, 255 = full) into `coverage` (`width` x `height`
  // bytes, rows top-down, `stride` bytes apart), the first glyph's origin at (`penX`,
  // `baselineY`) in the buffer's pixels, each next one an Advance() further. Coverage is
  // written where glyphs have ink; the buffer is expected to start at 0.
  void Rasterize(
    Font& font, const std::u32string& text, LONG penX, LONG baselineY, std::uint8_t* coverage, LONG width, LONG height,
    LONG stride
  );

  // A wide string as code points: UTF-16 surrogate pairs combined, other values taken as
  // UTF-32 (wchar_t is 32-bit here); values beyond U+10FFFF keep their low 16 bits (the
  // value a Windows wchar_t holds). `count` < 0: up to the terminating null.
  std::u32string DecodeWide(const WCHAR* text, int count);
} // namespace faf_posix::gdi
