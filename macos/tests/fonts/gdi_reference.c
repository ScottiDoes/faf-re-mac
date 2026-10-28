/* gdi_reference: what real GDI reports for the fonts the game creates - the reference for
 * the POSIX GDI font emulation (src/platform/posix/WinCompatGdiFont.cpp).
 *
 * It does exactly what the game does (REN_Init: AddFontMemResourceEx for every font file;
 * CD3DFont::Create: CreateFontA(-size, 0,0,0, 0, 0,0,0, DEFAULT_CHARSET, 0, 0,
 * ANTIALIASED_QUALITY, 0, face)), then prints TEXTMETRIC, the realized face
 * (GetTextFace), the kerning pair count and each ASCII character's advance as CD3DFont
 * measures it (DrawTextW DT_CALCRECT|DT_NOPREFIX|DT_SINGLELINE), in the same columns as
 * fonts_test's metrics.tsv, so the two files diff directly. It also writes sheet_gdi.bmp:
 * sample lines drawn white on black the way CD3DFont draws its glyphs (TA_BASELINE,
 * DrawTextW DT_NOCLIP), to compare with fonts_test's sheet.png.
 *
 * Build (any of):
 *   x86_64-w64-mingw32-gcc -O2 -o gdi_reference.exe gdi_reference.c -lgdi32 -luser32
 *   i686-w64-mingw32-gcc   -O2 -o gdi_reference.exe gdi_reference.c -lgdi32 -luser32
 *   cl /O2 gdi_reference.c gdi32.lib user32.lib
 * Run, with the game's fonts directory (Supreme Commander Forged Alliance\fonts):
 *   wine gdi_reference.exe "C:\...\Supreme Commander Forged Alliance\fonts" > metrics_gdi.tsv
 *   gdi_reference.exe "C:\...\fonts" > metrics_gdi.tsv            (Windows)
 * Compare on the Mac:
 *   python3 -I macos/tests/fonts/compare_tsv.py metrics_gdi.tsv build/m2-fonts-tests/out/metrics.tsv
 * Notes: Wine's GDI is FreeType's, not Windows'; it reproduces Windows' metrics closely but
 * its glyph images are FreeType's. A real Windows run is the authority. Under Wine, the
 * faces the game does not ship (Andale Mono, Courier New, Lucida Console, Times New Roman)
 * resolve to whatever the prefix has installed.
 */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char* const kFaces[] = {
  "Arial", "Arial Bold", "Arial Black", "Arial Narrow", "Zeroes Three", "Butterbelly", "VDub", "Wintermute",
  "Andale Mono", "Courier New", "Arial Gras", "Ariel", "Times New Roman", "Lucida Console",
};
static const int kSizes[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 28, 32, 48};

static HFONT GameFont(int size, const char* face)
{
  return CreateFontA(-size, 0, 0, 0, 0, 0, 0, 0, 1u, 0, 0, 4u, 0, face);
}

static void AddFonts(const char* dir)
{
  char pattern[MAX_PATH];
  WIN32_FIND_DATAA found;
  HANDLE find;
  _snprintf(pattern, sizeof(pattern), "%s\\*.ttf", dir);
  find = FindFirstFileA(pattern, &found);
  if (find == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "no .ttf files in %s\n", dir);
    return;
  }
  do {
    char path[MAX_PATH];
    HANDLE file;
    DWORD size, got = 0, count = 0;
    void* bytes;
    _snprintf(path, sizeof(path), "%s\\%s", dir, found.cFileName);
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
      continue;
    }
    size = GetFileSize(file, NULL);
    bytes = HeapAlloc(GetProcessHeap(), 0, size);
    ReadFile(file, bytes, size, &got, NULL);
    CloseHandle(file);
    if (AddFontMemResourceEx(bytes, got, NULL, &count) == NULL) {
      fprintf(stderr, "AddFontMemResourceEx failed for %s\n", found.cFileName);
    }
    HeapFree(GetProcessHeap(), 0, bytes); /* GDI keeps its own copy */
  } while (FindNextFileA(find, &found));
  FindClose(find);
}

static void PrintMetrics(void)
{
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP bitmap = CreateBitmap(128, 128, 1, 32, NULL);
  int f, s;
  SelectObject(dc, bitmap);
  printf(
    "face\tsize\trealized\ttmHeight\ttmAscent\ttmDescent\ttmInternalLeading\ttmExternalLeading\ttmAveCharWidth\t"
    "tmMaxCharWidth\ttmWeight\ttmOverhang\ttmPitchAndFamily\tkerningPairs\tadvances(0x20..0x7E)\n"
  );
  for (f = 0; f < (int)(sizeof(kFaces) / sizeof(kFaces[0])); ++f) {
    for (s = 0; s < (int)(sizeof(kSizes) / sizeof(kSizes[0])); ++s) {
      HFONT font = GameFont(kSizes[s], kFaces[f]);
      HGDIOBJ old = SelectObject(dc, font);
      TEXTMETRICW tm;
      char face[LF_FACESIZE];
      DWORD pairs;
      WCHAR ch;
      GetTextMetricsW(dc, &tm);
      GetTextFaceA(dc, LF_FACESIZE, face);
      pairs = GetKerningPairsW(dc, 0, NULL);
      printf(
        "%s\t%d\t%s\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t0x%02x\t%lu\t", kFaces[f], kSizes[s], face, (long)tm.tmHeight,
        (long)tm.tmAscent, (long)tm.tmDescent, (long)tm.tmInternalLeading, (long)tm.tmExternalLeading, (long)tm.tmAveCharWidth,
        (long)tm.tmMaxCharWidth, (long)tm.tmWeight, (long)tm.tmOverhang, tm.tmPitchAndFamily, (unsigned long)pairs
      );
      for (ch = 0x20; ch < 0x7F; ++ch) {
        RECT rect = {0, 0, 0, 0};
        DrawTextW(dc, &ch, 1, &rect, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE); /* 0xC20, as CD3DFont */
        printf("%s%ld", ch == 0x20 ? "" : ",", (long)(rect.right - rect.left));
      }
      printf("\n");
      SelectObject(dc, old);
      DeleteObject(font);
    }
  }
  DeleteObject(bitmap);
  DeleteDC(dc);
}

/* sheet_gdi.bmp: the sheet lines of fonts_test (left column labels omitted), each glyph
 * drawn as CD3DFont draws it into its scratch bitmap: TA_BASELINE, white on black,
 * DrawTextW(DT_NOCLIP|DT_NOPREFIX|DT_SINGLELINE) at the pen position. */
static void WriteSheet(void)
{
  static const struct
  {
    const char* face;
    int size;
    const WCHAR* text;
  } lines[] = {
    {"Arial", 10, L"The quick brown fox jumps over the lazy dog. 0123456789"},
    {"Arial", 12, L"The quick brown fox jumps over the lazy dog. 0123456789"},
    {"Arial", 12, L"ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"},
    {"Arial", 14, L"Mass 1250/3000 +25.4  Energy 8000/10000 -120  T3 Strategic Missile Launcher  12:34:56"},
    {"Arial", 16, L"The quick brown fox jumps over the lazy dog. 0123456789"},
    {"Arial Bold", 12, L"Mass 1250/3000 +25.4  Energy 8000/10000 -120  T3 Strategic Missile Launcher  12:34:56"},
    {"Zeroes Three", 14, L"The quick brown fox jumps over the lazy dog. 0123456789"},
    {"Zeroes Three", 20, L"The quick brown fox jumps over the lazy dog. 0123456789"},
    {"Courier New", 12, L"Courier New 12 (net-stats HUD): idx  nick      ping  maxsp  data"},
  };
  const int width = 1200, height = 40 * (int)(sizeof(lines) / sizeof(lines[0]));
  BITMAPINFO info;
  void* bits = NULL;
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP dib;
  int i, top = 4;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
  SelectObject(dc, dib);
  SetTextAlign(dc, TA_BASELINE);
  SetBkColor(dc, RGB(0, 0, 0));
  SetTextColor(dc, RGB(255, 255, 255));
  for (i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
    HFONT font = GameFont(lines[i].size, lines[i].face);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    int x = 150, baseline;
    const WCHAR* p;
    GetTextMetricsW(dc, &tm);
    baseline = top + tm.tmAscent;
    for (p = lines[i].text; *p; ++p) {
      RECT calc = {0, 0, 0, 0}, draw;
      DrawTextW(dc, p, 1, &calc, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
      draw.left = draw.right = x;
      draw.top = draw.bottom = baseline;
      DrawTextW(dc, p, 1, &draw, DT_NOCLIP | DT_NOPREFIX | DT_SINGLELINE);
      x += calc.right - calc.left;
    }
    top += tm.tmHeight + tm.tmExternalLeading + 6;
    SelectObject(dc, old);
    DeleteObject(font);
  }
  GdiFlush();
  {
    BITMAPFILEHEADER file;
    FILE* out = fopen("sheet_gdi.bmp", "wb");
    if (out != NULL) {
      memset(&file, 0, sizeof(file));
      file.bfType = 0x4D42;
      file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
      file.bfSize = file.bfOffBits + (DWORD)(width * height * 4);
      fwrite(&file, sizeof(file), 1, out);
      fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, out);
      fwrite(bits, 4, (size_t)width * height, out);
      fclose(out);
      fprintf(stderr, "wrote sheet_gdi.bmp\n");
    }
  }
  DeleteObject(dib);
  DeleteDC(dc);
}

int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: gdi_reference <Supreme Commander Forged Alliance\\fonts directory>\n");
    return 2;
  }
  AddFonts(argv[1]);
  PrintMetrics();
  WriteSheet();
  return 0;
}
