#pragma once

// Win32 on POSIX: Gdi. Included only by ../WinCompat.h (see its rules), after Window.h.
// Implementations: ../WinCompatGdi.cpp (objects, device contexts, bitmaps, text drawing)
// and ../WinCompatGdiFont.cpp (fonts: the font table, the mapper, Core Text rasterizing).
//
// gdi32: device contexts, DIB sections and bitmap headers, stock objects, fonts and font
// resources, text drawing into memory bitmaps. The bitmap structures keep their Windows
// layouts (they are written to files as-is); WinCompatGdi.cpp asserts every size and the
// offsets that matter.
//
// The process has no display, so this is gdi32 for memory drawing only:
// - GetDC(NULL) hands out a screen DC without pixels (drawing into it succeeds and shows
//   nothing); GetDC of a window fails as for an invalid HWND, since no window exists
//   (compat/Window.h). CreateCompatibleDC makes a memory DC with the 1x1 monochrome default
//   bitmap selected, as on Windows.
// - CreateBitmap, CreateCompatibleBitmap and CreateDIBSection own real, zero-initialised
//   pixel memory (DIB rows DWORD-aligned, device-dependent bitmap rows WORD-aligned);
//   CreateDIBSection returns the bits pointer. GetObject describes them as BITMAP /
//   DIBSECTION, brushes as LOGBRUSH, pens as LOGPEN, fonts as LOGFONT.
// - FillRect fills the bitmap selected into a memory DC with a solid or stock brush colour
//   (1/4/8 bpp through the DIB colour table, 16/24/32 bpp through the colour masks; GDI
//   writes 0 into the unused top byte of a 32 bpp pixel). GetPixel and GetBitmapBits read
//   it back.
// - Handles are validated: a stale or foreign handle fails with the documented result
//   instead of crashing (ERROR_INVALID_HANDLE where Windows sets an error).
// - Fonts (WinCompatGdiFont.cpp). AddFontMemResourceEx adds private fonts (Core Text
//   descriptors from the data; nothing is registered system-wide). CreateFont* records a
//   LOGFONT; the font is realized on first use as GDI's mapper would: the face name (case
//   insensitive) against the family names, then the full names (English and the user's
//   language) of the private fonts, then of the installed fonts; a missing face falls back
//   to Arial (Courier New for fixed pitch / FF_MODERN, Times New Roman for FF_ROMAN), logged
//   once per name. Sizes follow GDI: lfHeight < 0 is the em height in pixels, > 0 the cell
//   height (ascent + descent). TEXTMETRIC follows the TrueType driver: ascent and descent
//   from the VDMX table at that size, else from OS/2 usWinAscent/usWinDescent scaled and
//   rounded; advances from the hdmx table at that size, else the scaled and rounded hmtx
//   advance (GDI's integer advances); kerning pairs from the 'kern' table.
// - Text (DrawTextW/A, TextOutW/A, GetTextExtentPoint32W/A) is drawn with Core Text into the
//   bitmap selected into a memory DC: glyph outlines without TrueType hinting (each fitted
//   horizontally to its integer advance), antialiased grayscale coverage with a contrast
//   curve (NONANTIALIASED_QUALITY draws bilevel), blended from the background to the text
//   colour, at integer pen positions and the integer advances above, honouring
//   SetTextAlign (TA_BASELINE, TA_BOTTOM, TA_CENTER, TA_RIGHT), SetBkMode/SetBkColor and
//   DT_CALCRECT/DT_SINGLELINE/DT_NOCLIP/DT_NOPREFIX/DT_LEFT/DT_CENTER/DT_RIGHT/DT_TOP/
//   DT_VCENTER/DT_BOTTOM/DT_EXTERNALLEADING. Characters map through the face's cmap; a
//   missing ASCII or control character is glyph 0 (GDI's default character), any other
//   missing one comes from a Core Text fallback font (standing in for font linking). Wide
//   strings are UTF-16 (surrogate pairs combined) or UTF-32 code points.
// Not provided: synthesized bold/italic, underline/strikeout drawing, escapement and
// orientation, word wrapping (DT_WORDBREAK), tab expansion, ellipses, TA_UPDATECP (each
// logged once), blits, regions, palettes. The engine's only GDI text user is
// moho/render/d3d/CD3DFont.cpp (compiled unchanged through stubs/render/moho/CD3DFontPosix.cpp);
// WindowsCursor.cpp is replaced on POSIX (macos/exclusions.d/render.cmake).

// --- Bitmap structures (wingdi.h) --------------------------------------------------------------
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPCOREHEADER {
  DWORD bcSize;
  WORD bcWidth;
  WORD bcHeight;
  WORD bcPlanes;
  WORD bcBitCount;
} BITMAPCOREHEADER, *LPBITMAPCOREHEADER, *PBITMAPCOREHEADER;

typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef LONG FXPT2DOT30;

typedef struct tagCIEXYZ {
  FXPT2DOT30 ciexyzX;
  FXPT2DOT30 ciexyzY;
  FXPT2DOT30 ciexyzZ;
} CIEXYZ, *LPCIEXYZ;

typedef struct tagCIEXYZTRIPLE {
  CIEXYZ ciexyzRed;
  CIEXYZ ciexyzGreen;
  CIEXYZ ciexyzBlue;
} CIEXYZTRIPLE, *LPCIEXYZTRIPLE;

typedef struct {
  DWORD bV4Size;
  LONG bV4Width;
  LONG bV4Height;
  WORD bV4Planes;
  WORD bV4BitCount;
  DWORD bV4V4Compression;
  DWORD bV4SizeImage;
  LONG bV4XPelsPerMeter;
  LONG bV4YPelsPerMeter;
  DWORD bV4ClrUsed;
  DWORD bV4ClrImportant;
  DWORD bV4RedMask;
  DWORD bV4GreenMask;
  DWORD bV4BlueMask;
  DWORD bV4AlphaMask;
  DWORD bV4CSType;
  CIEXYZTRIPLE bV4Endpoints;
  DWORD bV4GammaRed;
  DWORD bV4GammaGreen;
  DWORD bV4GammaBlue;
} BITMAPV4HEADER, *LPBITMAPV4HEADER, *PBITMAPV4HEADER;

typedef struct {
  DWORD bV5Size;
  LONG bV5Width;
  LONG bV5Height;
  WORD bV5Planes;
  WORD bV5BitCount;
  DWORD bV5Compression;
  DWORD bV5SizeImage;
  LONG bV5XPelsPerMeter;
  LONG bV5YPelsPerMeter;
  DWORD bV5ClrUsed;
  DWORD bV5ClrImportant;
  DWORD bV5RedMask;
  DWORD bV5GreenMask;
  DWORD bV5BlueMask;
  DWORD bV5AlphaMask;
  DWORD bV5CSType;
  CIEXYZTRIPLE bV5Endpoints;
  DWORD bV5GammaRed;
  DWORD bV5GammaGreen;
  DWORD bV5GammaBlue;
  DWORD bV5Intent;
  DWORD bV5ProfileData;
  DWORD bV5ProfileSize;
  DWORD bV5Reserved;
} BITMAPV5HEADER, *LPBITMAPV5HEADER, *PBITMAPV5HEADER;

typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD, *LPRGBQUAD;

typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO, *PBITMAPINFO;

// GetObject results. Not file formats, so they have the 64-bit Windows layouts (a pointer
// or handle member is 8 bytes): BITMAP is 32 bytes, DIBSECTION 104.
typedef struct tagBITMAP {
  LONG bmType;
  LONG bmWidth;
  LONG bmHeight;
  LONG bmWidthBytes;
  WORD bmPlanes;
  WORD bmBitsPixel;
  LPVOID bmBits;
} BITMAP, *PBITMAP, *NPBITMAP, *LPBITMAP;

typedef struct tagDIBSECTION {
  BITMAP dsBm;
  BITMAPINFOHEADER dsBmih;
  DWORD dsBitfields[3];
  HANDLE dshSection;
  DWORD dsOffset;
} DIBSECTION, *LPDIBSECTION, *PDIBSECTION;

typedef struct tagLOGBRUSH {
  UINT lbStyle;
  COLORREF lbColor;
  ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *LPLOGBRUSH;

typedef struct tagLOGPEN {
  UINT lopnStyle;
  POINT lopnWidth;
  COLORREF lopnColor;
} LOGPEN, *PLOGPEN, *LPLOGPEN;

// biCompression
#define BI_RGB 0L
#define BI_RLE8 1L
#define BI_RLE4 2L
#define BI_BITFIELDS 3L
#define BI_JPEG 4L
#define BI_PNG 5L

// CreateDIBSection / SetDIBits colour-table usage
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1

// bV4CSType / bV5CSType, bV5Intent
#define LCS_CALIBRATED_RGB 0x00000000L
#define LCS_sRGB 0x73524742L
#define LCS_WINDOWS_COLOR_SPACE 0x57696E20L
#define PROFILE_LINKED 0x4C494E4BL
#define PROFILE_EMBEDDED 0x4D424544L
#define LCS_GM_BUSINESS 0x00000001L
#define LCS_GM_GRAPHICS 0x00000002L
#define LCS_GM_IMAGES 0x00000004L
#define LCS_GM_ABS_COLORIMETRIC 0x00000008L

// GetObjectType
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_METADC 4
#define OBJ_PAL 5
#define OBJ_FONT 6
#define OBJ_BITMAP 7
#define OBJ_REGION 8
#define OBJ_METAFILE 9
#define OBJ_MEMDC 10
#define OBJ_EXTPEN 11
#define OBJ_ENHMETADC 12
#define OBJ_ENHMETAFILE 13
#define OBJ_COLORSPACE 14

// GetStockObject
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define OEM_FIXED_FONT 10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT 12
#define SYSTEM_FONT 13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE 15
#define SYSTEM_FIXED_FONT 16
#define DEFAULT_GUI_FONT 17
#define DC_BRUSH 18
#define DC_PEN 19

// Brush and pen styles
#define BS_SOLID 0
#define BS_NULL 1
#define BS_HOLLOW BS_NULL
#define PS_SOLID 0
#define PS_NULL 5

// Background modes
#define TRANSPARENT 1
#define OPAQUE 2

#define CLR_INVALID 0xFFFFFFFF
#define GDI_ERROR 0xFFFFFFFFL
#define HGDI_ERROR (reinterpret_cast<HGDIOBJ>(static_cast<LONG_PTR>(-1)))

#define GetRValue(rgb) (static_cast<BYTE>(rgb))
#define GetGValue(rgb) (static_cast<BYTE>((static_cast<WORD>(rgb)) >> 8))
#define GetBValue(rgb) (static_cast<BYTE>((rgb) >> 16))

// --- Font and text structures (wingdi.h) -------------------------------------------------------
// In-memory structures only (never written to files): WCHAR members are the platform's
// wchar_t, as everywhere in this layer.
#define LF_FACESIZE 32
#define LF_FULLFACESIZE 64

typedef struct tagLOGFONTA {
  LONG lfHeight;
  LONG lfWidth;
  LONG lfEscapement;
  LONG lfOrientation;
  LONG lfWeight;
  BYTE lfItalic;
  BYTE lfUnderline;
  BYTE lfStrikeOut;
  BYTE lfCharSet;
  BYTE lfOutPrecision;
  BYTE lfClipPrecision;
  BYTE lfQuality;
  BYTE lfPitchAndFamily;
  CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *NPLOGFONTA, *LPLOGFONTA;

typedef struct tagLOGFONTW {
  LONG lfHeight;
  LONG lfWidth;
  LONG lfEscapement;
  LONG lfOrientation;
  LONG lfWeight;
  BYTE lfItalic;
  BYTE lfUnderline;
  BYTE lfStrikeOut;
  BYTE lfCharSet;
  BYTE lfOutPrecision;
  BYTE lfClipPrecision;
  BYTE lfQuality;
  BYTE lfPitchAndFamily;
  WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *NPLOGFONTW, *LPLOGFONTW;

typedef struct tagTEXTMETRICA {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  BYTE tmFirstChar;
  BYTE tmLastChar;
  BYTE tmDefaultChar;
  BYTE tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *NPTEXTMETRICA, *LPTEXTMETRICA;

typedef struct tagTEXTMETRICW {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  WCHAR tmFirstChar;
  WCHAR tmLastChar;
  WCHAR tmDefaultChar;
  WCHAR tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRICW, *PTEXTMETRICW, *NPTEXTMETRICW, *LPTEXTMETRICW;

typedef struct tagKERNINGPAIR {
  WORD wFirst;
  WORD wSecond;
  int iKernAmount;
} KERNINGPAIR, *LPKERNINGPAIR;

// lfWeight / tmWeight
#define FW_DONTCARE 0
#define FW_THIN 100
#define FW_EXTRALIGHT 200
#define FW_ULTRALIGHT 200
#define FW_LIGHT 300
#define FW_NORMAL 400
#define FW_REGULAR 400
#define FW_MEDIUM 500
#define FW_SEMIBOLD 600
#define FW_DEMIBOLD 600
#define FW_BOLD 700
#define FW_EXTRABOLD 800
#define FW_ULTRABOLD 800
#define FW_HEAVY 900
#define FW_BLACK 900

// lfCharSet / tmCharSet
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define SYMBOL_CHARSET 2
#define MAC_CHARSET 77
#define SHIFTJIS_CHARSET 128
#define HANGEUL_CHARSET 129
#define HANGUL_CHARSET 129
#define JOHAB_CHARSET 130
#define GB2312_CHARSET 134
#define CHINESEBIG5_CHARSET 136
#define GREEK_CHARSET 161
#define TURKISH_CHARSET 162
#define VIETNAMESE_CHARSET 163
#define HEBREW_CHARSET 177
#define ARABIC_CHARSET 178
#define BALTIC_CHARSET 186
#define RUSSIAN_CHARSET 204
#define THAI_CHARSET 222
#define EASTEUROPE_CHARSET 238
#define OEM_CHARSET 255

// lfOutPrecision, lfClipPrecision, lfQuality
#define OUT_DEFAULT_PRECIS 0
#define OUT_STRING_PRECIS 1
#define OUT_CHARACTER_PRECIS 2
#define OUT_STROKE_PRECIS 3
#define OUT_TT_PRECIS 4
#define OUT_DEVICE_PRECIS 5
#define OUT_RASTER_PRECIS 6
#define OUT_TT_ONLY_PRECIS 7
#define OUT_OUTLINE_PRECIS 8
#define OUT_SCREEN_OUTLINE_PRECIS 9
#define OUT_PS_ONLY_PRECIS 10
#define CLIP_DEFAULT_PRECIS 0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS 2
#define CLIP_MASK 0xf
#define CLIP_LH_ANGLES (1 << 4)
#define CLIP_TT_ALWAYS (2 << 4)
#define CLIP_EMBEDDED (8 << 4)
#define DEFAULT_QUALITY 0
#define DRAFT_QUALITY 1
#define PROOF_QUALITY 2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define CLEARTYPE_NATURAL_QUALITY 6

// lfPitchAndFamily
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define MONO_FONT 8
#define FF_DONTCARE (0 << 4)
#define FF_ROMAN (1 << 4)
#define FF_SWISS (2 << 4)
#define FF_MODERN (3 << 4)
#define FF_SCRIPT (4 << 4)
#define FF_DECORATIVE (5 << 4)

// tmPitchAndFamily (TMPF_FIXED_PITCH set means a variable-pitch font, as on Windows)
#define TMPF_FIXED_PITCH 0x01
#define TMPF_VECTOR 0x02
#define TMPF_TRUETYPE 0x04
#define TMPF_DEVICE 0x08

// SetTextAlign
#define TA_NOUPDATECP 0
#define TA_UPDATECP 1
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define TA_RTLREADING 256
#define TA_MASK (TA_BASELINE + TA_CENTER + TA_UPDATECP + TA_RTLREADING)

// DrawText format
#define DT_TOP 0x00000000
#define DT_LEFT 0x00000000
#define DT_CENTER 0x00000001
#define DT_RIGHT 0x00000002
#define DT_VCENTER 0x00000004
#define DT_BOTTOM 0x00000008
#define DT_WORDBREAK 0x00000010
#define DT_SINGLELINE 0x00000020
#define DT_EXPANDTABS 0x00000040
#define DT_TABSTOP 0x00000080
#define DT_NOCLIP 0x00000100
#define DT_EXTERNALLEADING 0x00000200
#define DT_CALCRECT 0x00000400
#define DT_NOPREFIX 0x00000800
#define DT_INTERNAL 0x00001000
#define DT_EDITCONTROL 0x00002000
#define DT_PATH_ELLIPSIS 0x00004000
#define DT_END_ELLIPSIS 0x00008000
#define DT_MODIFYSTRING 0x00010000
#define DT_RTLREADING 0x00020000
#define DT_WORD_ELLIPSIS 0x00040000
#define DT_NOFULLWIDTHCHARBREAK 0x00080000
#define DT_HIDEPREFIX 0x00100000
#define DT_PREFIXONLY 0x00200000

// --- Functions ------------------------------------------------------------------------------------
#if defined(__cplusplus)
// Device contexts.
HDC GetDC(HWND hWnd) noexcept;
int ReleaseDC(HWND hWnd, HDC hDC) noexcept;
HDC CreateCompatibleDC(HDC hdc) noexcept;
BOOL DeleteDC(HDC hdc) noexcept;
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) noexcept;
HGDIOBJ GetCurrentObject(HDC hdc, UINT type) noexcept;
COLORREF SetTextColor(HDC hdc, COLORREF color) noexcept;
COLORREF SetBkColor(HDC hdc, COLORREF color) noexcept;
int SetBkMode(HDC hdc, int mode) noexcept;
BOOL GdiFlush() noexcept;

// Objects. GetObject has no neutral name here (it would rename engine GetObject methods;
// see compat/Errors.h): call GetObjectW or GetObjectA.
HGDIOBJ GetStockObject(int i) noexcept;
BOOL DeleteObject(HGDIOBJ ho) noexcept;
int GetObjectW(HANDLE h, int c, LPVOID pv) noexcept;
int GetObjectA(HANDLE h, int c, LPVOID pv) noexcept;
DWORD GetObjectType(HGDIOBJ h) noexcept;

// Bitmaps and brushes.
HBITMAP CreateBitmap(int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, const void* lpBits) noexcept;
HBITMAP CreateCompatibleBitmap(HDC hdc, int cx, int cy) noexcept;
HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset) noexcept;
HBRUSH CreateSolidBrush(COLORREF color) noexcept;
int FillRect(HDC hDC, const RECT* lprc, HBRUSH hbr) noexcept;

// Pixels (memory DCs) and bitmap bits.
COLORREF GetPixel(HDC hdc, int x, int y) noexcept;
LONG GetBitmapBits(HBITMAP hbit, LONG cb, LPVOID lpvBits) noexcept;

// Font resources: private to the process (WinCompatGdiFont.cpp).
HANDLE AddFontMemResourceEx(PVOID pFileView, DWORD cjSize, PVOID pvResrved, DWORD* pNumFonts) noexcept;
BOOL RemoveFontMemResourceEx(HANDLE h) noexcept;

// Fonts.
HFONT CreateFontIndirectA(const LOGFONTA* lplf) noexcept;
HFONT CreateFontIndirectW(const LOGFONTW* lplf) noexcept;
HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline,
                  DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
                  DWORD iPitchAndFamily, LPCSTR pszFaceName) noexcept;
HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline,
                  DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
                  DWORD iPitchAndFamily, LPCWSTR pszFaceName) noexcept;
BOOL GetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm) noexcept;
BOOL GetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm) noexcept;
int GetTextFaceA(HDC hdc, int c, LPSTR lpName) noexcept;
int GetTextFaceW(HDC hdc, int c, LPWSTR lpName) noexcept;
DWORD GetKerningPairsA(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKernPair) noexcept;
DWORD GetKerningPairsW(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKernPair) noexcept;

// Text.
UINT SetTextAlign(HDC hdc, UINT align) noexcept;
UINT GetTextAlign(HDC hdc) noexcept;
BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl) noexcept;
BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl) noexcept;
BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c) noexcept;
BOOL TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c) noexcept;
// user32 on Windows; here with the rest of text drawing.
int DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format) noexcept;
int DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) noexcept;
#endif // __cplusplus
