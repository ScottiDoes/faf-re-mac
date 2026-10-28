// Win32 on POSIX: Gdi (declarations in compat/Gdi.h).
//
// Memory drawing for a process without a display: device contexts and GDI objects are
// heap objects whose addresses are the handles, tracked in two registries so that a stale
// or foreign handle fails the way gdi32 fails instead of being dereferenced. Bitmaps own
// real pixel memory; FillRect fills them and the text calls draw into them (see
// compat/Gdi.h). Fonts are LOGFONTs realized by WinCompatGdiFont.cpp on first use.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

#include "platform/posix/WinCompatGdiFont.h"

// The structure layouts, checked for real: LayoutAsserts.h turns `static_assert` off for
// the x86 layout pins of the engine, and these are the Windows x64 sizes instead.
#pragma push_macro("static_assert")
#undef static_assert
static_assert(sizeof(BITMAPFILEHEADER) == 14, "BITMAPFILEHEADER is 14 bytes (pack 2)");
static_assert(offsetof(BITMAPFILEHEADER, bfSize) == 2, "BITMAPFILEHEADER::bfSize at 2");
static_assert(offsetof(BITMAPFILEHEADER, bfReserved1) == 6, "BITMAPFILEHEADER::bfReserved1 at 6");
static_assert(offsetof(BITMAPFILEHEADER, bfOffBits) == 10, "BITMAPFILEHEADER::bfOffBits at 10");
static_assert(sizeof(BITMAPCOREHEADER) == 12, "BITMAPCOREHEADER is 12 bytes");
static_assert(sizeof(BITMAPINFOHEADER) == 40, "BITMAPINFOHEADER is 40 bytes");
static_assert(offsetof(BITMAPINFOHEADER, biPlanes) == 12, "BITMAPINFOHEADER::biPlanes at 12");
static_assert(offsetof(BITMAPINFOHEADER, biCompression) == 16, "BITMAPINFOHEADER::biCompression at 16");
static_assert(offsetof(BITMAPINFOHEADER, biClrImportant) == 36, "BITMAPINFOHEADER::biClrImportant at 36");
static_assert(sizeof(CIEXYZTRIPLE) == 36, "CIEXYZTRIPLE is 36 bytes");
static_assert(sizeof(BITMAPV4HEADER) == 108, "BITMAPV4HEADER is 108 bytes");
static_assert(sizeof(BITMAPV5HEADER) == 124, "BITMAPV5HEADER is 124 bytes");
static_assert(offsetof(BITMAPV5HEADER, bV5RedMask) == 40, "BITMAPV5HEADER::bV5RedMask at 40");
static_assert(offsetof(BITMAPV5HEADER, bV5AlphaMask) == 52, "BITMAPV5HEADER::bV5AlphaMask at 52");
static_assert(offsetof(BITMAPV5HEADER, bV5CSType) == 56, "BITMAPV5HEADER::bV5CSType at 56");
static_assert(offsetof(BITMAPV5HEADER, bV5Endpoints) == 60, "BITMAPV5HEADER::bV5Endpoints at 60");
static_assert(offsetof(BITMAPV5HEADER, bV5GammaRed) == 96, "BITMAPV5HEADER::bV5GammaRed at 96");
static_assert(offsetof(BITMAPV5HEADER, bV5Intent) == 108, "BITMAPV5HEADER::bV5Intent at 108");
static_assert(offsetof(BITMAPV5HEADER, bV5Reserved) == 120, "BITMAPV5HEADER::bV5Reserved at 120");
static_assert(sizeof(RGBQUAD) == 4, "RGBQUAD is 4 bytes");
static_assert(sizeof(BITMAPINFO) == 44, "BITMAPINFO is 44 bytes");
static_assert(offsetof(BITMAPINFO, bmiColors) == 40, "BITMAPINFO::bmiColors at 40");
static_assert(sizeof(BITMAP) == 32, "BITMAP is 32 bytes on Windows x64");
static_assert(offsetof(BITMAP, bmBits) == 24, "BITMAP::bmBits at 24 on Windows x64");
static_assert(sizeof(DIBSECTION) == 104, "DIBSECTION is 104 bytes on Windows x64");
static_assert(offsetof(DIBSECTION, dsBmih) == 32, "DIBSECTION::dsBmih at 32");
static_assert(offsetof(DIBSECTION, dsBitfields) == 72, "DIBSECTION::dsBitfields at 72");
static_assert(offsetof(DIBSECTION, dshSection) == 88, "DIBSECTION::dshSection at 88");
static_assert(offsetof(DIBSECTION, dsOffset) == 96, "DIBSECTION::dsOffset at 96");
static_assert(sizeof(LOGBRUSH) == 16, "LOGBRUSH is 16 bytes on Windows x64");
static_assert(sizeof(LOGPEN) == 16, "LOGPEN is 16 bytes");
#pragma pop_macro("static_assert")

namespace
{
  constexpr WORD kScreenBitsPerPixel = 32; // the screen DC (compat/Window.h: one 1920x1080 monitor)

  struct GdiObject
  {
    DWORD type = 0;          // OBJ_BITMAP, OBJ_BRUSH, OBJ_PEN, OBJ_FONT, OBJ_PAL
    bool stock = false;
    int selections = 0;      // device contexts it is selected into

    // OBJ_BITMAP
    LONG width = 0;
    LONG height = 0;         // DIB sections: as the BITMAPINFOHEADER gave it (negative: top-down)
    WORD planes = 1;
    WORD bitsPerPixel = 1;
    LONG stride = 0;         // DIB sections: DWORD-aligned rows; device-dependent bitmaps: WORD-aligned
    bool dib = false;
    std::unique_ptr<std::uint8_t[]> pixels;
    BITMAPINFOHEADER header{}; // DIB sections
    DWORD masks[3]{};          // 16/32 bpp DIB sections
    std::vector<RGBQUAD> colors; // <= 8 bpp DIB sections

    // OBJ_BRUSH / OBJ_PEN
    UINT style = 0;
    COLORREF color = 0;
    LONG penWidth = 0;

    // OBJ_FONT: what CreateFont was given, and the font it realizes to (on first use)
    LOGFONTW logFont{};
    std::shared_ptr<faf_posix::gdi::Font> realized;
  };

  struct GdiDc
  {
    bool memory = false;     // CreateCompatibleDC, else GetDC(NULL)
    GdiObject* bitmap = nullptr;
    GdiObject* brush = nullptr;
    GdiObject* pen = nullptr;
    GdiObject* font = nullptr;
    GdiObject* palette = nullptr;
    COLORREF textColor = RGB(0, 0, 0);
    COLORREF backgroundColor = RGB(255, 255, 255);
    int backgroundMode = OPAQUE;
    UINT textAlign = TA_TOP | TA_LEFT | TA_NOUPDATECP;
  };

  // One lock over both registries and every object's selection state; GDI calls are rare.
  // The lock, the registries and the stock objects are never destroyed: objects are released
  // from atexit handlers (CD3DFont's font cache) after function-local statics constructed
  // later than their registration would already be gone.
  std::mutex& Lock()
  {
    static std::mutex* const mutex = new std::mutex();
    return *mutex;
  }

  std::unordered_set<const GdiObject*>& Objects()
  {
    static auto* const objects = new std::unordered_set<const GdiObject*>();
    return *objects;
  }

  std::unordered_set<const GdiDc*>& Dcs()
  {
    static auto* const dcs = new std::unordered_set<const GdiDc*>();
    return *dcs;
  }

  GdiObject* FindObject(const void* const handle)
  {
    auto* const object = static_cast<const GdiObject*>(handle);
    return Objects().count(object) != 0U ? const_cast<GdiObject*>(object) : nullptr;
  }

  GdiDc* FindDc(const HDC handle)
  {
    auto* const dc = reinterpret_cast<const GdiDc*>(handle);
    return Dcs().count(dc) != 0U ? const_cast<GdiDc*>(dc) : nullptr;
  }

  GdiObject* Register(std::unique_ptr<GdiObject> object)
  {
    GdiObject* const raw = object.release();
    Objects().insert(raw);
    return raw;
  }

  GdiObject* MakeStock(const DWORD type, const UINT style, const COLORREF color)
  {
    auto object = std::make_unique<GdiObject>();
    object->type = type;
    object->stock = true;
    object->style = style;
    object->color = color;
    return Register(std::move(object));
  }

  // GetStockObject indices 0..19 plus the memory DC's default 1x1 monochrome bitmap
  // (stock, never deleted). Built once, under Lock().
  struct StockObjects
  {
    GdiObject* objects[20]{};
    GdiObject* defaultBitmap = nullptr;
  };

  StockObjects& Stock()
  {
    static StockObjects& stock = *new StockObjects([] {
      StockObjects made;
      made.objects[WHITE_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(255, 255, 255));
      made.objects[LTGRAY_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(192, 192, 192));
      made.objects[GRAY_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(128, 128, 128));
      made.objects[DKGRAY_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(64, 64, 64));
      made.objects[BLACK_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(0, 0, 0));
      made.objects[NULL_BRUSH] = MakeStock(OBJ_BRUSH, BS_NULL, RGB(0, 0, 0));
      made.objects[WHITE_PEN] = MakeStock(OBJ_PEN, PS_SOLID, RGB(255, 255, 255));
      made.objects[BLACK_PEN] = MakeStock(OBJ_PEN, PS_SOLID, RGB(0, 0, 0));
      made.objects[NULL_PEN] = MakeStock(OBJ_PEN, PS_NULL, RGB(0, 0, 0));
      // Stock fonts: Windows' sizes, on the faces the mapper falls back to (the raster
      // System/Terminal/Fixedsys/MS Sans Serif fonts do not exist here).
      struct StockFont
      {
        int index;
        LONG height;
        LONG weight;
        BYTE charSet;
        BYTE pitchAndFamily;
        const wchar_t* face;
      };
      static constexpr StockFont kStockFonts[] = {
        {OEM_FIXED_FONT, 12, FW_NORMAL, OEM_CHARSET, FIXED_PITCH | FF_MODERN, L"Courier New"},
        {ANSI_FIXED_FONT, 12, FW_NORMAL, ANSI_CHARSET, FIXED_PITCH | FF_MODERN, L"Courier New"},
        {ANSI_VAR_FONT, 12, FW_NORMAL, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS, L"Arial"},
        {SYSTEM_FONT, 16, FW_BOLD, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS, L"Arial"},
        {DEVICE_DEFAULT_FONT, 16, FW_BOLD, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS, L"Arial"},
        {SYSTEM_FIXED_FONT, 15, FW_NORMAL, ANSI_CHARSET, FIXED_PITCH | FF_MODERN, L"Courier New"},
        {DEFAULT_GUI_FONT, -11, FW_NORMAL, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS, L"Arial"},
      };
      for (const StockFont& font : kStockFonts) {
        GdiObject* const object = MakeStock(OBJ_FONT, 0U, 0U);
        object->logFont.lfHeight = font.height;
        object->logFont.lfWeight = font.weight;
        object->logFont.lfCharSet = font.charSet;
        object->logFont.lfPitchAndFamily = font.pitchAndFamily;
        std::wcsncpy(object->logFont.lfFaceName, font.face, LF_FACESIZE - 1);
        made.objects[font.index] = object;
      }
      made.objects[DEFAULT_PALETTE] = MakeStock(OBJ_PAL, 0U, 0U);
      made.objects[DC_BRUSH] = MakeStock(OBJ_BRUSH, BS_SOLID, RGB(255, 255, 255));
      made.objects[DC_PEN] = MakeStock(OBJ_PEN, PS_SOLID, RGB(0, 0, 0));

      auto bitmap = std::make_unique<GdiObject>();
      bitmap->type = OBJ_BITMAP;
      bitmap->stock = true;
      bitmap->width = 1;
      bitmap->height = 1;
      bitmap->bitsPerPixel = 1;
      bitmap->stride = 2;
      bitmap->pixels.reset(new std::uint8_t[2]());
      made.defaultBitmap = Register(std::move(bitmap));
      return made;
    }());
    return stock;
  }

  [[nodiscard]] bool ValidBitCount(const WORD bits) noexcept
  {
    return bits == 1U || bits == 4U || bits == 8U || bits == 16U || bits == 24U || bits == 32U;
  }

  // Row bytes for `width` pixels, rounded up to `alignBits` (32 for DIBs, 16 for DDBs).
  // Zero when the row does not fit a LONG.
  [[nodiscard]] LONG RowBytes(const LONG width, const WORD bitsPerPixel, const std::uint64_t alignBits) noexcept
  {
    const std::uint64_t bits = static_cast<std::uint64_t>(width) * bitsPerPixel;
    const std::uint64_t bytes = ((bits + alignBits - 1U) / alignBits) * (alignBits / 8U);
    return bytes > static_cast<std::uint64_t>(std::numeric_limits<LONG>::max()) ? 0 : static_cast<LONG>(bytes);
  }

  // A bitmap of `width` x |height| with zeroed pixels; null when the size cannot be allocated.
  std::unique_ptr<GdiObject> NewBitmap(const LONG width, const LONG height, const WORD bitsPerPixel, const bool dib)
  {
    const LONG rows = (height < 0) ? -height : height;
    const LONG stride = RowBytes(width, bitsPerPixel, dib ? 32U : 16U);
    const std::uint64_t bytes = static_cast<std::uint64_t>(stride) * static_cast<std::uint64_t>(rows);
    if (stride == 0 || bytes > (std::uint64_t{1} << 31U)) {
      return nullptr;
    }
    auto object = std::make_unique<GdiObject>();
    object->type = OBJ_BITMAP;
    object->width = width;
    object->height = height;
    object->bitsPerPixel = bitsPerPixel;
    object->stride = stride;
    object->dib = dib;
    object->pixels.reset(new (std::nothrow) std::uint8_t[static_cast<std::size_t>(bytes)]());
    if (object->pixels == nullptr) {
      return nullptr;
    }
    return object;
  }

  HBITMAP AsBitmap(GdiObject* const object) noexcept
  {
    return reinterpret_cast<HBITMAP>(object);
  }

  GdiObject* MonochromeOneByOne()
  {
    return Stock().defaultBitmap;
  }

  // --- FillRect ------------------------------------------------------------------------------

  [[nodiscard]] unsigned MaskShift(const DWORD mask) noexcept
  {
    unsigned shift = 0U;
    while (shift < 32U && ((mask >> shift) & 1U) == 0U) {
      ++shift;
    }
    return shift;
  }

  [[nodiscard]] unsigned MaskBits(const DWORD mask) noexcept
  {
    unsigned bits = 0U;
    for (DWORD rest = mask >> MaskShift(mask); (rest & 1U) != 0U; rest >>= 1U) {
      ++bits;
    }
    return bits;
  }

  // One 8-bit channel into a mask, keeping its top bits (as GDI does for 5- and 6-bit fields).
  [[nodiscard]] DWORD PackChannel(const BYTE value, const DWORD mask) noexcept
  {
    const unsigned bits = MaskBits(mask);
    if (bits == 0U) {
      return 0U;
    }
    const DWORD field = (bits >= 8U) ? (static_cast<DWORD>(value) << (bits - 8U)) : (static_cast<DWORD>(value) >> (8U - bits));
    return (field << MaskShift(mask)) & mask;
  }

  [[nodiscard]] std::uint32_t NearestColorIndex(const std::vector<RGBQUAD>& table, const COLORREF color) noexcept
  {
    std::uint32_t best = 0U;
    std::uint32_t bestDistance = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0U; index < table.size(); ++index) {
      const int dr = static_cast<int>(table[index].rgbRed) - GetRValue(color);
      const int dg = static_cast<int>(table[index].rgbGreen) - GetGValue(color);
      const int db = static_cast<int>(table[index].rgbBlue) - GetBValue(color);
      const auto distance = static_cast<std::uint32_t>(dr * dr + dg * dg + db * db);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
      }
    }
    return best;
  }

  // Writes `color` into pixels [left, right) of every row in [top, bottom) of `bitmap`.
  // False for a depth this cannot represent (a 4/8 bpp bitmap without a colour table).
  bool FillBitmap(GdiObject& bitmap, const LONG left, const LONG top, const LONG right, const LONG bottom, const COLORREF color)
  {
    const WORD bpp = bitmap.bitsPerPixel;
    std::uint32_t value = 0U; // the pixel, little-endian, low bpp bits
    if (bpp == 1U && bitmap.colors.empty()) {
      // Monochrome device-dependent bitmap: the nearer of black and white.
      value = (static_cast<unsigned>(GetRValue(color)) + GetGValue(color) + GetBValue(color) >= 384U) ? 1U : 0U;
    } else if (bpp <= 8U) {
      if (bitmap.colors.empty()) {
        return false;
      }
      value = NearestColorIndex(bitmap.colors, color);
    } else if (bpp == 24U) {
      value = static_cast<std::uint32_t>(GetBValue(color)) | (static_cast<std::uint32_t>(GetGValue(color)) << 8U) |
        (static_cast<std::uint32_t>(GetRValue(color)) << 16U);
    } else {
      const DWORD red = bitmap.masks[0];
      const DWORD green = bitmap.masks[1];
      const DWORD blue = bitmap.masks[2];
      value = PackChannel(GetRValue(color), red) | PackChannel(GetGValue(color), green) | PackChannel(GetBValue(color), blue);
    }

    const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
    const bool topDown = bitmap.dib ? (bitmap.height < 0) : true;
    for (LONG y = top; y < bottom; ++y) {
      const LONG row = topDown ? y : (rows - 1 - y);
      std::uint8_t* const line = bitmap.pixels.get() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.stride);
      for (LONG x = left; x < right; ++x) {
        if (bpp >= 8U) {
          const std::size_t bytes = bpp / 8U;
          std::uint8_t* const pixel = line + static_cast<std::size_t>(x) * bytes;
          for (std::size_t b = 0U; b < bytes; ++b) {
            pixel[b] = static_cast<std::uint8_t>(value >> (8U * b));
          }
        } else {
          // MSB-first packing within each byte.
          const std::size_t bit = static_cast<std::size_t>(x) * bpp;
          std::uint8_t& byte = line[bit / 8U];
          const unsigned shift = 8U - bpp - static_cast<unsigned>(bit % 8U);
          const auto mask = static_cast<std::uint8_t>(((1U << bpp) - 1U) << shift);
          byte = static_cast<std::uint8_t>((byte & ~mask) | ((value << shift) & mask));
        }
      }
    }
    return true;
  }
} // namespace

// --- Device contexts ---------------------------------------------------------------------------

HDC GetDC(const HWND hWnd) noexcept
{
  if (hWnd != nullptr && IsWindow(hWnd) == FALSE) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return nullptr;
  }
  const std::lock_guard<std::mutex> lock(Lock());
  StockObjects& stock = Stock();
  auto* const dc = new (std::nothrow) GdiDc();
  if (dc == nullptr) {
    return nullptr;
  }
  dc->brush = stock.objects[WHITE_BRUSH];
  dc->pen = stock.objects[BLACK_PEN];
  dc->font = stock.objects[SYSTEM_FONT];
  dc->palette = stock.objects[DEFAULT_PALETTE];
  Dcs().insert(dc);
  return reinterpret_cast<HDC>(dc);
}

namespace
{
  void ReleaseSelections(GdiDc& dc) noexcept
  {
    for (GdiObject* const object : {dc.bitmap, dc.brush, dc.pen, dc.font, dc.palette}) {
      if (object != nullptr && object->selections > 0) {
        --object->selections;
      }
    }
  }
} // namespace

int ReleaseDC(const HWND hWnd, const HDC hDC) noexcept
{
  static_cast<void>(hWnd);
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hDC);
  if (dc == nullptr || dc->memory) {
    return 0; // not a DC that GetDC handed out
  }
  ReleaseSelections(*dc);
  Dcs().erase(dc);
  delete dc;
  return 1;
}

HDC CreateCompatibleDC(const HDC hdc) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  if (hdc != nullptr && FindDc(hdc) == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  StockObjects& stock = Stock();
  auto* const dc = new (std::nothrow) GdiDc();
  if (dc == nullptr) {
    return nullptr;
  }
  dc->memory = true;
  dc->bitmap = stock.defaultBitmap;
  dc->brush = stock.objects[WHITE_BRUSH];
  dc->pen = stock.objects[BLACK_PEN];
  dc->font = stock.objects[SYSTEM_FONT];
  dc->palette = stock.objects[DEFAULT_PALETTE];
  Dcs().insert(dc);
  return reinterpret_cast<HDC>(dc);
}

BOOL DeleteDC(const HDC hdc) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  ReleaseSelections(*dc);
  Dcs().erase(dc);
  delete dc;
  return TRUE;
}

HGDIOBJ SelectObject(const HDC hdc, const HGDIOBJ h) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  GdiObject* const object = FindObject(h);
  if (dc == nullptr || object == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  GdiObject** slot = nullptr;
  switch (object->type) {
  case OBJ_BITMAP:
    // A bitmap goes into one memory DC at a time; a screen DC takes none.
    if (!dc->memory || (object->selections > 0 && dc->bitmap != object && !object->stock)) {
      return nullptr;
    }
    slot = &dc->bitmap;
    break;
  case OBJ_BRUSH:
    slot = &dc->brush;
    break;
  case OBJ_PEN:
    slot = &dc->pen;
    break;
  case OBJ_FONT:
    slot = &dc->font;
    break;
  default:
    // Palettes go through SelectPalette; regions are not provided.
    return HGDI_ERROR;
  }
  GdiObject* const previous = *slot;
  if (previous != object) {
    if (previous != nullptr && previous->selections > 0) {
      --previous->selections;
    }
    ++object->selections;
    *slot = object;
  }
  return previous;
}

HGDIOBJ GetCurrentObject(const HDC hdc, const UINT type) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    return nullptr;
  }
  switch (type) {
  case OBJ_BITMAP:
    return dc->memory ? dc->bitmap : nullptr;
  case OBJ_BRUSH:
    return dc->brush;
  case OBJ_PEN:
    return dc->pen;
  case OBJ_FONT:
    return dc->font;
  case OBJ_PAL:
    return dc->palette;
  default:
    return nullptr;
  }
}

COLORREF SetTextColor(const HDC hdc, const COLORREF color) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    return CLR_INVALID;
  }
  return std::exchange(dc->textColor, color);
}

COLORREF SetBkColor(const HDC hdc, const COLORREF color) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    return CLR_INVALID;
  }
  return std::exchange(dc->backgroundColor, color);
}

int SetBkMode(const HDC hdc, const int mode) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr || (mode != TRANSPARENT && mode != OPAQUE)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return std::exchange(dc->backgroundMode, mode);
}

BOOL GdiFlush() noexcept
{
  return TRUE; // nothing is batched
}

// --- Objects -----------------------------------------------------------------------------------

HGDIOBJ GetStockObject(const int i) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  if (i < 0 || i >= static_cast<int>(std::size(Stock().objects))) {
    return nullptr;
  }
  return Stock().objects[i]; // index 9 is unused on Windows too: null
}

BOOL DeleteObject(const HGDIOBJ ho) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiObject* const object = FindObject(ho);
  if (object == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (object->stock) {
    return TRUE; // deleting a stock object is allowed and does nothing
  }
  if (object->selections > 0) {
    return FALSE; // still selected into a DC
  }
  Objects().erase(object);
  delete object;
  return TRUE;
}

namespace
{
  int DescribeObject(const HANDLE h, const int c, const LPVOID pv, const bool wide) noexcept
  {
    const std::lock_guard<std::mutex> lock(Lock());
    GdiObject* const object = FindObject(h);
    if (object == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return 0;
    }
    const std::size_t room = (c > 0) ? static_cast<std::size_t>(c) : 0U;
    switch (object->type) {
    case OBJ_BITMAP: {
      if (pv == nullptr) {
        return static_cast<int>(sizeof(BITMAP));
      }
      if (room < sizeof(BITMAP)) {
        return 0;
      }
      BITMAP bitmap{};
      bitmap.bmType = 0;
      bitmap.bmWidth = object->width;
      bitmap.bmHeight = (object->height < 0) ? -object->height : object->height;
      bitmap.bmWidthBytes = object->stride;
      bitmap.bmPlanes = object->planes;
      bitmap.bmBitsPixel = object->bitsPerPixel;
      bitmap.bmBits = object->dib ? object->pixels.get() : nullptr;
      if (object->dib && room >= sizeof(DIBSECTION)) {
        DIBSECTION section{};
        section.dsBm = bitmap;
        section.dsBmih = object->header;
        std::memcpy(section.dsBitfields, object->masks, sizeof(section.dsBitfields));
        section.dshSection = nullptr;
        section.dsOffset = 0U;
        std::memcpy(pv, &section, sizeof(section));
        return static_cast<int>(sizeof(DIBSECTION));
      }
      std::memcpy(pv, &bitmap, sizeof(bitmap));
      return static_cast<int>(sizeof(BITMAP));
    }
    case OBJ_BRUSH: {
      LOGBRUSH brush{};
      brush.lbStyle = object->style;
      brush.lbColor = object->color;
      brush.lbHatch = 0U;
      if (pv == nullptr) {
        return static_cast<int>(sizeof(LOGBRUSH));
      }
      const std::size_t copied = std::min(room, sizeof(LOGBRUSH));
      std::memcpy(pv, &brush, copied);
      return static_cast<int>(copied);
    }
    case OBJ_PEN: {
      LOGPEN pen{};
      pen.lopnStyle = object->style;
      pen.lopnWidth.x = object->penWidth;
      pen.lopnWidth.y = 0;
      pen.lopnColor = object->color;
      if (pv == nullptr) {
        return static_cast<int>(sizeof(LOGPEN));
      }
      const std::size_t copied = std::min(room, sizeof(LOGPEN));
      std::memcpy(pv, &pen, copied);
      return static_cast<int>(copied);
    }
    case OBJ_FONT: {
      if (wide) {
        if (pv == nullptr) {
          return static_cast<int>(sizeof(LOGFONTW));
        }
        const std::size_t copied = std::min(room, sizeof(LOGFONTW));
        std::memcpy(pv, &object->logFont, copied);
        return static_cast<int>(copied);
      }
      LOGFONTA font{};
      std::memcpy(&font, &object->logFont, offsetof(LOGFONTA, lfFaceName));
      WideCharToMultiByte(CP_ACP, 0, object->logFont.lfFaceName, -1, font.lfFaceName, LF_FACESIZE, nullptr, nullptr);
      font.lfFaceName[LF_FACESIZE - 1] = '\0';
      if (pv == nullptr) {
        return static_cast<int>(sizeof(LOGFONTA));
      }
      const std::size_t copied = std::min(room, sizeof(LOGFONTA));
      std::memcpy(pv, &font, copied);
      return static_cast<int>(copied);
    }
    default:
      // The default palette carries no palette data here.
      FAF_POSIX_STUB("GetObject: no palette data on POSIX");
      return 0;
    }
  }
} // namespace

int GetObjectW(const HANDLE h, const int c, const LPVOID pv) noexcept
{
  return DescribeObject(h, c, pv, true);
}

int GetObjectA(const HANDLE h, const int c, const LPVOID pv) noexcept
{
  return DescribeObject(h, c, pv, false);
}

DWORD GetObjectType(const HGDIOBJ h) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  if (GdiObject* const object = FindObject(h)) {
    return object->type;
  }
  if (GdiDc* const dc = FindDc(static_cast<HDC>(h))) {
    return dc->memory ? OBJ_MEMDC : OBJ_DC;
  }
  SetLastError(ERROR_INVALID_HANDLE);
  return 0U;
}

// --- Bitmaps and brushes -----------------------------------------------------------------------

HBITMAP CreateBitmap(const int nWidth, const int nHeight, const UINT nPlanes, const UINT nBitCount, const void* const lpBits) noexcept
{
  if (nWidth < 0 || nHeight < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  const std::lock_guard<std::mutex> lock(Lock());
  if (nWidth == 0 || nHeight == 0) {
    return AsBitmap(MonochromeOneByOne()); // as documented: the stock 1x1 monochrome bitmap
  }
  // GDI folds planes into the depth (planes x bits per plane); only one plane is real.
  const UINT bits = nPlanes * nBitCount;
  if (nPlanes != 1U || !ValidBitCount(static_cast<WORD>(bits)) || bits > 32U) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  std::unique_ptr<GdiObject> bitmap = NewBitmap(nWidth, nHeight, static_cast<WORD>(bits), false);
  if (bitmap == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  if (bits > 1U) {
    // Device-dependent colour bitmaps read as the screen's 32 bpp BGR layout.
    bitmap->masks[0] = 0x00FF0000U;
    bitmap->masks[1] = 0x0000FF00U;
    bitmap->masks[2] = 0x000000FFU;
    if (bits == 16U) {
      bitmap->masks[0] = 0x7C00U;
      bitmap->masks[1] = 0x03E0U;
      bitmap->masks[2] = 0x001FU;
    }
  }
  if (lpBits != nullptr) {
    std::memcpy(bitmap->pixels.get(), lpBits, static_cast<std::size_t>(bitmap->stride) * static_cast<std::size_t>(nHeight));
  }
  return AsBitmap(Register(std::move(bitmap)));
}

HBITMAP CreateCompatibleBitmap(const HDC hdc, const int cx, const int cy) noexcept
{
  WORD bits = kScreenBitsPerPixel;
  {
    const std::lock_guard<std::mutex> lock(Lock());
    GdiDc* const dc = FindDc(hdc);
    if (dc == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return nullptr;
    }
    if (dc->memory && dc->bitmap != nullptr) {
      bits = dc->bitmap->bitsPerPixel; // a fresh memory DC: monochrome, as on Windows
    }
  }
  return CreateBitmap(cx, cy, 1U, bits, nullptr);
}

HBITMAP CreateDIBSection(
  const HDC hdc, const BITMAPINFO* const pbmi, const UINT usage, void** const ppvBits, const HANDLE hSection, const DWORD offset
) noexcept
{
  if (ppvBits != nullptr) {
    *ppvBits = nullptr;
  }
  if (pbmi == nullptr || (usage != DIB_RGB_COLORS && usage != DIB_PAL_COLORS)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  if (hSection != nullptr) {
    static_cast<void>(offset);
    FAF_POSIX_STUB("CreateDIBSection: file-mapping sections are not supported on POSIX");
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
  }
  const BITMAPINFOHEADER& header = pbmi->bmiHeader;
  const bool bitfields = header.biCompression == BI_BITFIELDS;
  if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biWidth <= 0 || header.biHeight == 0 ||
      header.biHeight == std::numeric_limits<LONG>::min() || !ValidBitCount(header.biBitCount) || header.biPlanes != 1U ||
      (header.biCompression != BI_RGB && !bitfields) || (bitfields && header.biBitCount != 16U && header.biBitCount != 32U)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }

  const std::lock_guard<std::mutex> lock(Lock());
  if (hdc != nullptr && FindDc(hdc) == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  std::unique_ptr<GdiObject> bitmap = NewBitmap(header.biWidth, header.biHeight, header.biBitCount, true);
  if (bitmap == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }

  // The masks: after a BITMAPINFOHEADER in bmiColors, inside a V4/V5 header, or the
  // BI_RGB defaults GDI reports (5-5-5 for 16 bpp, 8-8-8 for 32 bpp).
  if (bitfields) {
    const auto* const raw = reinterpret_cast<const std::uint8_t*>(pbmi);
    const std::size_t at = (header.biSize >= sizeof(BITMAPV4HEADER)) ? offsetof(BITMAPV5HEADER, bV5RedMask) : sizeof(BITMAPINFOHEADER);
    std::memcpy(bitmap->masks, raw + at, sizeof(bitmap->masks));
  } else if (header.biBitCount == 16U) {
    bitmap->masks[0] = 0x7C00U;
    bitmap->masks[1] = 0x03E0U;
    bitmap->masks[2] = 0x001FU;
  } else if (header.biBitCount == 32U) {
    bitmap->masks[0] = 0x00FF0000U;
    bitmap->masks[1] = 0x0000FF00U;
    bitmap->masks[2] = 0x000000FFU;
  }

  // The colour table of a palettized DIB (DIB_RGB_COLORS: RGBQUADs after the header).
  if (header.biBitCount <= 8U) {
    const DWORD entries = (header.biClrUsed != 0U) ? std::min<DWORD>(header.biClrUsed, 1U << header.biBitCount) : (1U << header.biBitCount);
    if (usage == DIB_RGB_COLORS) {
      const auto* const table =
        reinterpret_cast<const RGBQUAD*>(reinterpret_cast<const std::uint8_t*>(pbmi) + header.biSize);
      bitmap->colors.assign(table, table + entries);
    } else {
      // Palette indices into the DC's (default) palette: black and white for 1 bpp, a grey ramp otherwise.
      bitmap->colors.resize(entries);
      for (DWORD index = 0U; index < entries; ++index) {
        const auto level = static_cast<BYTE>((entries > 1U) ? (index * 255U) / (entries - 1U) : 0U);
        bitmap->colors[index] = RGBQUAD{level, level, level, 0U};
      }
    }
  }

  bitmap->header = header;
  bitmap->header.biSize = sizeof(BITMAPINFOHEADER);
  bitmap->header.biSizeImage =
    static_cast<DWORD>(static_cast<std::uint64_t>(bitmap->stride) * static_cast<std::uint64_t>((header.biHeight < 0) ? -header.biHeight : header.biHeight));
  if (ppvBits != nullptr) {
    *ppvBits = bitmap->pixels.get();
  }
  return AsBitmap(Register(std::move(bitmap)));
}

HBRUSH CreateSolidBrush(const COLORREF color) noexcept
{
  auto brush = std::make_unique<GdiObject>();
  brush->type = OBJ_BRUSH;
  brush->style = BS_SOLID;
  brush->color = color & 0x00FFFFFFU;
  const std::lock_guard<std::mutex> lock(Lock());
  return reinterpret_cast<HBRUSH>(Register(std::move(brush)));
}

int FillRect(const HDC hDC, const RECT* const lprc, const HBRUSH hbr) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hDC);
  GdiObject* const brush = FindObject(hbr);
  if (dc == nullptr || lprc == nullptr || brush == nullptr || brush->type != OBJ_BRUSH) {
    // Includes the system-colour form (HBRUSH)(COLOR_xxx + 1): no system colours here.
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (brush->style == BS_NULL || !dc->memory || dc->bitmap == nullptr) {
    return 1; // nothing to paint, or the screen (which has no pixels)
  }
  GdiObject& bitmap = *dc->bitmap;
  const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
  const LONG left = std::max<LONG>(lprc->left, 0);
  const LONG top = std::max<LONG>(lprc->top, 0);
  const LONG right = std::min<LONG>(lprc->right, bitmap.width);
  const LONG bottom = std::min<LONG>(lprc->bottom, rows);
  if (left >= right || top >= bottom) {
    return 1;
  }
  if (!FillBitmap(bitmap, left, top, right, bottom, brush->color)) {
    FAF_POSIX_STUB("FillRect: a 4/8 bpp device-dependent bitmap has no colour table on POSIX");
    return 0;
  }
  return 1;
}

// --- Pixels -----------------------------------------------------------------------------------

namespace
{
  [[nodiscard]] BYTE UnpackChannel(const DWORD value, const DWORD mask) noexcept
  {
    const unsigned bits = MaskBits(mask);
    if (bits == 0U) {
      return 0U;
    }
    const DWORD field = (value & mask) >> MaskShift(mask);
    if (bits >= 8U) {
      return static_cast<BYTE>(field >> (bits - 8U));
    }
    // Widen by repeating the top bits, as GDI reads 5- and 6-bit fields.
    DWORD wide = field << (8U - bits);
    for (unsigned filled = bits; filled < 8U; filled += bits) {
      wide |= field << (8U - bits) >> filled;
    }
    return static_cast<BYTE>(wide);
  }

  // The colour of pixel (x, y), which must be inside `bitmap`.
  [[nodiscard]] COLORREF ReadPixel(const GdiObject& bitmap, const LONG x, const LONG y) noexcept
  {
    const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
    const bool topDown = bitmap.dib ? (bitmap.height < 0) : true;
    const LONG row = topDown ? y : (rows - 1 - y);
    const std::uint8_t* const line = bitmap.pixels.get() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.stride);
    const WORD bpp = bitmap.bitsPerPixel;
    std::uint32_t value = 0U;
    if (bpp >= 8U) {
      const std::size_t bytes = bpp / 8U;
      const std::uint8_t* const pixel = line + static_cast<std::size_t>(x) * bytes;
      for (std::size_t b = 0U; b < bytes; ++b) {
        value |= static_cast<std::uint32_t>(pixel[b]) << (8U * b);
      }
    } else {
      const std::size_t bit = static_cast<std::size_t>(x) * bpp;
      const unsigned shift = 8U - bpp - static_cast<unsigned>(bit % 8U);
      value = (line[bit / 8U] >> shift) & ((1U << bpp) - 1U);
    }
    if (bpp == 1U && bitmap.colors.empty()) {
      return (value != 0U) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    }
    if (bpp <= 8U) {
      if (value >= bitmap.colors.size()) {
        return RGB(0, 0, 0);
      }
      const RGBQUAD& entry = bitmap.colors[value];
      return RGB(entry.rgbRed, entry.rgbGreen, entry.rgbBlue);
    }
    if (bpp == 24U) {
      return RGB((value >> 16U) & 0xFFU, (value >> 8U) & 0xFFU, value & 0xFFU);
    }
    return RGB(UnpackChannel(value, bitmap.masks[0]), UnpackChannel(value, bitmap.masks[1]), UnpackChannel(value, bitmap.masks[2]));
  }
} // namespace

COLORREF GetPixel(const HDC hdc, const int x, const int y) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr || !dc->memory || dc->bitmap == nullptr) {
    return CLR_INVALID;
  }
  const GdiObject& bitmap = *dc->bitmap;
  const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
  if (x < 0 || y < 0 || x >= bitmap.width || y >= rows) {
    return CLR_INVALID; // outside the bitmap (the clipping region)
  }
  return ReadPixel(bitmap, x, y);
}

LONG GetBitmapBits(const HBITMAP hbit, const LONG cb, const LPVOID lpvBits) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiObject* const bitmap = FindObject(hbit);
  if (bitmap == nullptr || bitmap->type != OBJ_BITMAP) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  const LONG rows = (bitmap->height < 0) ? -bitmap->height : bitmap->height;
  const std::uint64_t total = static_cast<std::uint64_t>(bitmap->stride) * static_cast<std::uint64_t>(rows);
  if (lpvBits == nullptr) {
    return static_cast<LONG>(total); // the size of the bits
  }
  if (cb <= 0) {
    return 0;
  }
  const std::size_t copied = static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(cb), total));
  std::memcpy(lpvBits, bitmap->pixels.get(), copied);
  return static_cast<LONG>(copied);
}

// --- Font resources ---------------------------------------------------------------------------

HANDLE AddFontMemResourceEx(const PVOID pFileView, const DWORD cjSize, const PVOID pvResrved, DWORD* const pNumFonts) noexcept
{
  static_cast<void>(pvResrved);
  if (pFileView == nullptr || cjSize == 0U || pNumFonts == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  try {
    // Fails as gdi32 does for data that holds no font: a null handle, no extended error.
    return faf_posix::gdi::AddMemoryFonts(pFileView, cjSize, pNumFonts);
  } catch (...) {
    *pNumFonts = 0U;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

BOOL RemoveFontMemResourceEx(const HANDLE h) noexcept
{
  try {
    if (faf_posix::gdi::RemoveMemoryFonts(h)) {
      return TRUE;
    }
  } catch (...) {
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

// --- Fonts -------------------------------------------------------------------------------------

HFONT CreateFontIndirectW(const LOGFONTW* const lplf) noexcept
{
  if (lplf == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  try {
    auto font = std::make_unique<GdiObject>();
    font->type = OBJ_FONT;
    font->logFont = *lplf;
    font->logFont.lfFaceName[LF_FACESIZE - 1] = L'\0';
    const std::lock_guard<std::mutex> lock(Lock());
    return reinterpret_cast<HFONT>(Register(std::move(font)));
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
}

HFONT CreateFontIndirectA(const LOGFONTA* const lplf) noexcept
{
  if (lplf == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  LOGFONTW font{};
  std::memcpy(&font, lplf, offsetof(LOGFONTA, lfFaceName));
  char face[LF_FACESIZE + 1] = {};
  std::memcpy(face, lplf->lfFaceName, LF_FACESIZE);
  MultiByteToWideChar(CP_ACP, 0, face, -1, font.lfFaceName, LF_FACESIZE);
  font.lfFaceName[LF_FACESIZE - 1] = L'\0';
  return CreateFontIndirectW(&font);
}

namespace
{
  LOGFONTW MakeLogFont(
    const int cHeight, const int cWidth, const int cEscapement, const int cOrientation, const int cWeight, const DWORD bItalic,
    const DWORD bUnderline, const DWORD bStrikeOut, const DWORD iCharSet, const DWORD iOutPrecision, const DWORD iClipPrecision,
    const DWORD iQuality, const DWORD iPitchAndFamily
  ) noexcept
  {
    LOGFONTW font{};
    font.lfHeight = cHeight;
    font.lfWidth = cWidth;
    font.lfEscapement = cEscapement;
    font.lfOrientation = cOrientation;
    font.lfWeight = cWeight;
    font.lfItalic = static_cast<BYTE>(bItalic);
    font.lfUnderline = static_cast<BYTE>(bUnderline);
    font.lfStrikeOut = static_cast<BYTE>(bStrikeOut);
    font.lfCharSet = static_cast<BYTE>(iCharSet);
    font.lfOutPrecision = static_cast<BYTE>(iOutPrecision);
    font.lfClipPrecision = static_cast<BYTE>(iClipPrecision);
    font.lfQuality = static_cast<BYTE>(iQuality);
    font.lfPitchAndFamily = static_cast<BYTE>(iPitchAndFamily);
    return font;
  }
} // namespace

HFONT CreateFontW(
  const int cHeight, const int cWidth, const int cEscapement, const int cOrientation, const int cWeight, const DWORD bItalic,
  const DWORD bUnderline, const DWORD bStrikeOut, const DWORD iCharSet, const DWORD iOutPrecision, const DWORD iClipPrecision,
  const DWORD iQuality, const DWORD iPitchAndFamily, const LPCWSTR pszFaceName
) noexcept
{
  LOGFONTW font = MakeLogFont(
    cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision,
    iQuality, iPitchAndFamily
  );
  if (pszFaceName != nullptr) {
    std::wcsncpy(font.lfFaceName, pszFaceName, LF_FACESIZE - 1);
  }
  return CreateFontIndirectW(&font);
}

HFONT CreateFontA(
  const int cHeight, const int cWidth, const int cEscapement, const int cOrientation, const int cWeight, const DWORD bItalic,
  const DWORD bUnderline, const DWORD bStrikeOut, const DWORD iCharSet, const DWORD iOutPrecision, const DWORD iClipPrecision,
  const DWORD iQuality, const DWORD iPitchAndFamily, const LPCSTR pszFaceName
) noexcept
{
  LOGFONTW font = MakeLogFont(
    cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision,
    iQuality, iPitchAndFamily
  );
  if (pszFaceName != nullptr) {
    // The face name is converted from the ANSI code page (UTF-8 here) and cut to LF_FACESIZE - 1.
    WCHAR face[256] = {};
    MultiByteToWideChar(CP_ACP, 0, pszFaceName, -1, face, 255);
    std::wcsncpy(font.lfFaceName, face, LF_FACESIZE - 1);
  }
  return CreateFontIndirectW(&font);
}

namespace
{
  // The realized font selected into a DC (under Lock()): the DC's font object realized on
  // first use. Null for an invalid DC.
  std::shared_ptr<faf_posix::gdi::Font> SelectedFont(const HDC hdc)
  {
    GdiDc* const dc = FindDc(hdc);
    if (dc == nullptr || dc->font == nullptr) {
      return nullptr;
    }
    GdiObject& font = *dc->font;
    if (font.realized == nullptr) {
      font.realized = faf_posix::gdi::RealizeFont(font.logFont);
    }
    return font.realized;
  }

  std::u32string DecodeAnsi(const LPCSTR text, const int count)
  {
    if (text == nullptr) {
      return {};
    }
    const int length = (count < 0) ? static_cast<int>(std::strlen(text)) : count;
    if (length == 0) {
      return {};
    }
    const int units = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
    std::vector<WCHAR> wide(static_cast<std::size_t>(std::max(units, 0)) + 1U, 0);
    MultiByteToWideChar(CP_ACP, 0, text, length, wide.data(), units);
    return faf_posix::gdi::DecodeWide(wide.data(), units);
  }
} // namespace

BOOL GetTextMetricsW(const HDC hdc, const LPTEXTMETRICW lptm) noexcept
{
  try {
    const std::lock_guard<std::mutex> lock(Lock());
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (font == nullptr || lptm == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    *lptm = faf_posix::gdi::Metrics(*font);
    return TRUE;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetTextMetricsA(const HDC hdc, const LPTEXTMETRICA lptm) noexcept
{
  TEXTMETRICW wide{};
  if (lptm == nullptr || GetTextMetricsW(hdc, &wide) == FALSE) {
    if (lptm == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
    }
    return FALSE;
  }
  std::memcpy(lptm, &wide, offsetof(TEXTMETRICA, tmFirstChar));
  // The ANSI forms of the characters: 8-bit codes, as the A call reports them.
  lptm->tmFirstChar = static_cast<BYTE>(std::min<WCHAR>(wide.tmFirstChar, 0xFF));
  lptm->tmLastChar = static_cast<BYTE>(std::min<WCHAR>(wide.tmLastChar, 0xFF));
  lptm->tmDefaultChar = static_cast<BYTE>(std::min<WCHAR>(wide.tmDefaultChar, 0xFF));
  lptm->tmBreakChar = static_cast<BYTE>(std::min<WCHAR>(wide.tmBreakChar, 0xFF));
  lptm->tmItalic = wide.tmItalic;
  lptm->tmUnderlined = wide.tmUnderlined;
  lptm->tmStruckOut = wide.tmStruckOut;
  lptm->tmPitchAndFamily = wide.tmPitchAndFamily;
  lptm->tmCharSet = wide.tmCharSet;
  return TRUE;
}

int GetTextFaceW(const HDC hdc, const int c, const LPWSTR lpName) noexcept
{
  try {
    const std::lock_guard<std::mutex> lock(Lock());
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (font == nullptr || (lpName != nullptr && c <= 0)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    const std::wstring name = faf_posix::gdi::FaceName(*font);
    if (lpName == nullptr) {
      return static_cast<int>(name.size()) + 1; // with the terminating null
    }
    const std::size_t copied = std::min(name.size(), static_cast<std::size_t>(c) - 1U);
    std::wmemcpy(lpName, name.data(), copied);
    lpName[copied] = L'\0';
    return static_cast<int>(copied) + 1;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

int GetTextFaceA(const HDC hdc, const int c, const LPSTR lpName) noexcept
{
  WCHAR wide[LF_FULLFACESIZE] = {};
  if (GetTextFaceW(hdc, LF_FULLFACESIZE, wide) == 0) {
    return 0;
  }
  const int needed = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (lpName == nullptr) {
    return needed;
  }
  if (c <= 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  std::vector<char> narrow(static_cast<std::size_t>(std::max(needed, 1)), '\0');
  WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow.data(), needed, nullptr, nullptr);
  const std::size_t copied = std::min(std::strlen(narrow.data()), static_cast<std::size_t>(c) - 1U);
  std::memcpy(lpName, narrow.data(), copied);
  lpName[copied] = '\0';
  return static_cast<int>(copied) + 1;
}

DWORD GetKerningPairsW(const HDC hdc, const DWORD nPairs, const LPKERNINGPAIR lpKernPair) noexcept
{
  try {
    const std::lock_guard<std::mutex> lock(Lock());
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (font == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0U;
    }
    const std::vector<KERNINGPAIR>& pairs = faf_posix::gdi::KerningPairs(*font);
    if (lpKernPair == nullptr || nPairs == 0U) {
      return static_cast<DWORD>(pairs.size()); // the number of pairs
    }
    const std::size_t copied = std::min<std::size_t>(nPairs, pairs.size());
    std::copy_n(pairs.begin(), copied, lpKernPair);
    return static_cast<DWORD>(copied);
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0U;
  }
}

DWORD GetKerningPairsA(const HDC hdc, const DWORD nPairs, const LPKERNINGPAIR lpKernPair) noexcept
{
  // The pairs whose characters both have a single-byte ANSI form (ASCII: the ANSI code page is UTF-8 here).
  try {
    std::vector<KERNINGPAIR> pairs;
    {
      const std::lock_guard<std::mutex> lock(Lock());
      const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
      if (font == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0U;
      }
      for (const KERNINGPAIR& pair : faf_posix::gdi::KerningPairs(*font)) {
        if (pair.wFirst < 0x80U && pair.wSecond < 0x80U) {
          pairs.push_back(pair);
        }
      }
    }
    if (lpKernPair == nullptr || nPairs == 0U) {
      return static_cast<DWORD>(pairs.size());
    }
    const std::size_t copied = std::min<std::size_t>(nPairs, pairs.size());
    std::copy_n(pairs.begin(), copied, lpKernPair);
    return static_cast<DWORD>(copied);
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0U;
  }
}

// --- Text ----------------------------------------------------------------------------------------

UINT SetTextAlign(const HDC hdc, const UINT align) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return GDI_ERROR;
  }
  if ((align & TA_UPDATECP) != 0U) {
    FAF_POSIX_STUB("SetTextAlign: TA_UPDATECP is not supported on POSIX (text is drawn at the given position)");
  }
  return std::exchange(dc->textAlign, align & TA_MASK);
}

UINT GetTextAlign(const HDC hdc) noexcept
{
  const std::lock_guard<std::mutex> lock(Lock());
  GdiDc* const dc = FindDc(hdc);
  if (dc == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return GDI_ERROR;
  }
  return dc->textAlign;
}

namespace
{
  // Blends `color` over the pixel at (x, y) by `coverage` / 255 (inside the bitmap).
  void BlendPixel(GdiObject& bitmap, const LONG x, const LONG y, const COLORREF color, const unsigned coverage)
  {
    const bool standard32 = bitmap.bitsPerPixel == 32U && bitmap.masks[0] == 0x00FF0000U && bitmap.masks[1] == 0x0000FF00U &&
      bitmap.masks[2] == 0x000000FFU;
    const auto mix = [coverage](const unsigned under, const unsigned over) {
      return static_cast<BYTE>((under * (255U - coverage) + over * coverage + 127U) / 255U);
    };
    if (standard32) {
      const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
      const bool topDown = bitmap.dib ? (bitmap.height < 0) : true;
      const LONG row = topDown ? y : (rows - 1 - y);
      std::uint8_t* const pixel =
        bitmap.pixels.get() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.stride) + static_cast<std::size_t>(x) * 4U;
      pixel[0] = mix(pixel[0], GetBValue(color));
      pixel[1] = mix(pixel[1], GetGValue(color));
      pixel[2] = mix(pixel[2], GetRValue(color));
      pixel[3] = 0U; // GDI leaves the unused byte 0
      return;
    }
    const COLORREF under = ReadPixel(bitmap, x, y);
    const COLORREF blended =
      RGB(mix(GetRValue(under), GetRValue(color)), mix(GetGValue(under), GetGValue(color)), mix(GetBValue(under), GetBValue(color)));
    static_cast<void>(FillBitmap(bitmap, x, y, x + 1, y + 1, blended));
  }

  // ExtTextOut without options: draws `text` at (x, y) as the DC's text alignment places it,
  // inside `clip` when given. Under Lock().
  bool DrawString(GdiDc& dc, faf_posix::gdi::Font& font, LONG x, const LONG y, const std::u32string& text, const RECT* const clip)
  {
    const TEXTMETRICW& metrics = faf_posix::gdi::Metrics(font);
    const LONG width = faf_posix::gdi::TextWidth(font, text);
    LONG baseline = y + metrics.tmAscent; // TA_TOP
    if ((dc.textAlign & TA_BASELINE) == TA_BASELINE) {
      baseline = y;
    } else if ((dc.textAlign & TA_BOTTOM) != 0U) {
      baseline = y - metrics.tmDescent;
    }
    if ((dc.textAlign & TA_CENTER) == TA_CENTER) {
      x -= width / 2;
    } else if ((dc.textAlign & TA_RIGHT) != 0U) {
      x -= width;
    }
    if (!dc.memory || dc.bitmap == nullptr || text.empty()) {
      return true; // the screen DC has no pixels
    }
    GdiObject& bitmap = *dc.bitmap;
    const LONG rows = (bitmap.height < 0) ? -bitmap.height : bitmap.height;
    RECT bounds{0, 0, bitmap.width, rows};
    if (clip != nullptr) {
      bounds.left = std::max(bounds.left, clip->left);
      bounds.top = std::max(bounds.top, clip->top);
      bounds.right = std::min(bounds.right, clip->right);
      bounds.bottom = std::min(bounds.bottom, clip->bottom);
    }

    // The character cell, filled with the background colour in OPAQUE mode.
    if (dc.backgroundMode == OPAQUE) {
      const LONG left = std::max(bounds.left, x);
      const LONG top = std::max(bounds.top, baseline - metrics.tmAscent);
      const LONG right = std::min(bounds.right, x + width);
      const LONG bottom = std::min(bounds.bottom, baseline + metrics.tmDescent);
      if (left < right && top < bottom) {
        static_cast<void>(FillBitmap(bitmap, left, top, right, bottom, dc.backgroundColor));
      }
    }

    // Ink can reach past the cell (overhangs, tall accents): rasterize a margin around it.
    const LONG margin = metrics.tmHeight + metrics.tmMaxCharWidth;
    const LONG left = std::max(bounds.left, x - margin);
    const LONG top = std::max(bounds.top, baseline - metrics.tmAscent - margin);
    const LONG right = std::min(bounds.right, x + width + margin);
    const LONG bottom = std::min(bounds.bottom, baseline + metrics.tmDescent + margin);
    if (left >= right || top >= bottom) {
      return true;
    }
    const LONG areaWidth = right - left;
    const LONG areaHeight = bottom - top;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(areaWidth) * static_cast<std::size_t>(areaHeight), 0U);
    faf_posix::gdi::Rasterize(font, text, x - left, baseline - top, coverage.data(), areaWidth, areaHeight, areaWidth);
    const COLORREF color = dc.textColor & 0x00FFFFFFU;
    for (LONG row = 0; row < areaHeight; ++row) {
      const std::uint8_t* const line = coverage.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(areaWidth);
      for (LONG column = 0; column < areaWidth; ++column) {
        if (line[column] != 0U) {
          BlendPixel(bitmap, left + column, top + row, color, line[column]);
        }
      }
    }
    return true;
  }

  // DrawText's prefix processing: '&' marks the next character (dropped here; no underline),
  // "&&" is one '&'.
  std::u32string StripPrefixes(const std::u32string& text)
  {
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == U'&' && i + 1U < text.size()) {
        ++i;
      } else if (text[i] == U'&') {
        continue;
      }
      out.push_back(text[i]);
    }
    return out;
  }

  int DrawTextImpl(const HDC hdc, std::u32string text, const LPRECT lprc, const UINT format)
  {
    if (lprc == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    const std::lock_guard<std::mutex> lock(Lock());
    GdiDc* const dc = FindDc(hdc);
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (dc == nullptr || font == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return 0;
    }
    if ((format & (DT_WORDBREAK | DT_EXPANDTABS | DT_TABSTOP | DT_PATH_ELLIPSIS | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS)) != 0U) {
      FAF_POSIX_STUB("DrawText: word breaks, tabs and ellipses are not supported on POSIX; text is drawn as given");
    }
    if ((format & DT_NOPREFIX) == 0U) {
      text = StripPrefixes(text);
    }

    // Lines: one with DT_SINGLELINE, else split at CR, LF and CR LF.
    std::vector<std::u32string> lines;
    if ((format & DT_SINGLELINE) != 0U) {
      lines.push_back(std::move(text));
    } else {
      std::u32string line;
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == U'\r' || text[i] == U'\n') {
          lines.push_back(line);
          line.clear();
          if (text[i] == U'\r' && i + 1U < text.size() && text[i + 1U] == U'\n') {
            ++i;
          }
        } else {
          line.push_back(text[i]);
        }
      }
      lines.push_back(line);
    }

    const TEXTMETRICW& metrics = faf_posix::gdi::Metrics(*font);
    const LONG lineHeight = metrics.tmHeight + (((format & DT_EXTERNALLEADING) != 0U) ? metrics.tmExternalLeading : 0);
    LONG widest = 0;
    std::vector<LONG> widths;
    widths.reserve(lines.size());
    for (const std::u32string& line : lines) {
      widths.push_back(faf_posix::gdi::TextWidth(*font, line));
      widest = std::max(widest, widths.back());
    }
    const LONG textHeight = lineHeight * static_cast<LONG>(lines.size());

    if ((format & DT_CALCRECT) != 0U) {
      lprc->right = lprc->left + widest;
      lprc->bottom = lprc->top + textHeight;
      return textHeight;
    }

    LONG y = lprc->top;
    if ((format & DT_SINGLELINE) != 0U) {
      if ((format & DT_BOTTOM) != 0U) {
        y = lprc->bottom - textHeight;
      } else if ((format & DT_VCENTER) != 0U) {
        y = lprc->top + (lprc->bottom - lprc->top - textHeight) / 2;
      }
    }
    const RECT clip = *lprc;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      LONG x = lprc->left;
      if ((format & DT_RIGHT) != 0U) {
        x = lprc->right - widths[i];
      } else if ((format & DT_CENTER) != 0U) {
        x = lprc->left + (lprc->right - lprc->left - widths[i]) / 2;
      }
      DrawString(*dc, *font, x, y + lineHeight * static_cast<LONG>(i), lines[i], ((format & DT_NOCLIP) != 0U) ? nullptr : &clip);
    }
    return (y - lprc->top) + textHeight; // from the top of the rectangle to the bottom of the text
  }
} // namespace

BOOL GetTextExtentPoint32W(const HDC hdc, const LPCWSTR lpString, const int c, const LPSIZE psizl) noexcept
{
  if (psizl == nullptr || c < 0 || (lpString == nullptr && c > 0)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::u32string text = faf_posix::gdi::DecodeWide(lpString, c);
    const std::lock_guard<std::mutex> lock(Lock());
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (font == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    const TEXTMETRICW& metrics = faf_posix::gdi::Metrics(*font);
    psizl->cx = faf_posix::gdi::TextWidth(*font, text) + (text.empty() ? 0 : metrics.tmOverhang);
    psizl->cy = metrics.tmHeight;
    return TRUE;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL GetTextExtentPoint32A(const HDC hdc, const LPCSTR lpString, const int c, const LPSIZE psizl) noexcept
{
  if (psizl == nullptr || c < 0 || (lpString == nullptr && c > 0)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    std::u32string text = DecodeAnsi(lpString, c);
    std::vector<WCHAR> wide(text.begin(), text.end());
    return GetTextExtentPoint32W(hdc, wide.data(), static_cast<int>(wide.size()), psizl);
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL TextOutW(const HDC hdc, const int x, const int y, const LPCWSTR lpString, const int c) noexcept
{
  if (c < 0 || (lpString == nullptr && c > 0)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    const std::u32string text = faf_posix::gdi::DecodeWide(lpString, c);
    const std::lock_guard<std::mutex> lock(Lock());
    GdiDc* const dc = FindDc(hdc);
    const std::shared_ptr<faf_posix::gdi::Font> font = SelectedFont(hdc);
    if (dc == nullptr || font == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    return DrawString(*dc, *font, x, y, text, nullptr) ? TRUE : FALSE;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

BOOL TextOutA(const HDC hdc, const int x, const int y, const LPCSTR lpString, const int c) noexcept
{
  if (c < 0 || (lpString == nullptr && c > 0)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  try {
    std::u32string text = DecodeAnsi(lpString, c);
    std::vector<WCHAR> wide(text.begin(), text.end());
    return TextOutW(hdc, x, y, wide.data(), static_cast<int>(wide.size()));
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
}

int DrawTextW(const HDC hdc, const LPCWSTR lpchText, const int cchText, const LPRECT lprc, const UINT format) noexcept
{
  if (lpchText == nullptr && cchText != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    return DrawTextImpl(hdc, faf_posix::gdi::DecodeWide(lpchText, cchText), lprc, format);
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}

int DrawTextA(const HDC hdc, const LPCSTR lpchText, const int cchText, const LPRECT lprc, const UINT format) noexcept
{
  if (lpchText == nullptr && cchText != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  try {
    return DrawTextImpl(hdc, DecodeAnsi(lpchText, cchText), lprc, format);
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
}
