// fonts_test: the POSIX GDI fonts (compat/Gdi.h) and the engine's own CD3DFont on top of them.
//
//   fonts_test --fonts <dir with the game's .ttf files> --out <dir> [--quiet]
//
// 1. Registers every *.ttf in --fonts with AddFontMemResourceEx, as REN_Init does.
// 2. metrics.tsv: for the faces and sizes the UI uses, CD3DFont::Create(size, face) (the
//    game's call) and the TEXTMETRIC / face / kerning / per-character advances GDI reports for
//    the same CreateFontA. Same columns as gdi_reference.c prints on Windows or Wine, so the
//    two files diff directly.
// 3. sheet.png: text drawn the way the game draws it - CD3DFont::Render2D, whose glyph quads
//    and DXT5 textures the test doubles record - decoded and composited at the quads'
//    positions. Left margin marks: red = baseline, blue = cell top (baseline - mAscent) and
//    bottom (baseline + mDescent). sheet_zoom.png: the top left of it at 4x.
// 4. Checks (exit status 1 on failure): every font resolves; CD3DFont's members equal GDI's
//    TEXTMETRIC; heights are positive; no CD3DFont warnings (edge not black, clamping, the
//    GetBitmapBits/GetPixel cross-check); every glyph quad is its texture's size.

#include <dirent.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "macos/tests/fonts/test_doubles.h"
#include "moho/render/d3d/CD3DFont.h"

namespace
{
  int gFailures = 0;

  void Fail(const char* const fmt, const std::string& detail)
  {
    std::fprintf(stderr, "FAIL: ");
    std::fprintf(stderr, fmt, detail.c_str());
    std::fprintf(stderr, "\n");
    ++gFailures;
  }

  std::vector<char> ReadFile(const std::string& path)
  {
    std::vector<char> bytes;
    FILE* const file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
      return bytes;
    }
    char buffer[65536];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
      bytes.insert(bytes.end(), buffer, buffer + got);
    }
    std::fclose(file);
    return bytes;
  }

  // --- PNG (8-bit RGB) ---------------------------------------------------------------------------
  void PutChunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data)
  {
    const auto be32 = [&out](const std::uint32_t v) {
      out.push_back(static_cast<unsigned char>(v >> 24));
      out.push_back(static_cast<unsigned char>(v >> 16));
      out.push_back(static_cast<unsigned char>(v >> 8));
      out.push_back(static_cast<unsigned char>(v));
    };
    be32(static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(static_cast<std::uint32_t>(crc32(0, out.data() + start, static_cast<uInt>(out.size() - start))));
  }

  bool WritePng(const std::string& path, const int width, const int height, const std::vector<unsigned char>& rgb)
  {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
      raw.push_back(0);
      raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(y) * width * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y + 1) * width * 3);
    }
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> packed(packedSize);
    compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), 9);
    packed.resize(packedSize);
    std::vector<unsigned char> header = {0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
      header[i] = static_cast<unsigned char>(width >> (24 - 8 * i));
      header[4 + i] = static_cast<unsigned char>(height >> (24 - 8 * i));
    }
    std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    PutChunk(out, "IHDR", header);
    PutChunk(out, "IDAT", packed);
    PutChunk(out, "IEND", {});
    FILE* const file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
      return false;
    }
    std::fwrite(out.data(), 1, out.size(), file);
    std::fclose(file);
    return true;
  }

  // --- DXT5 alpha --------------------------------------------------------------------------------
  void DecodeAlphaBlock(const std::uint64_t block, unsigned char (&alpha)[16])
  {
    const unsigned a0 = static_cast<unsigned>(block & 0xFFU);
    const unsigned a1 = static_cast<unsigned>((block >> 8) & 0xFFU);
    unsigned values[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) {
      for (unsigned i = 2; i < 8; ++i) {
        values[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
      }
    } else {
      for (unsigned i = 2; i < 6; ++i) {
        values[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
      }
      values[6] = 0;
      values[7] = 255;
    }
    for (int i = 0; i < 16; ++i) {
      alpha[i] = static_cast<unsigned char>(values[(block >> (16 + 3 * i)) & 7U]);
    }
  }

  struct Canvas
  {
    int width;
    int height;
    std::vector<unsigned char> rgb;

    Canvas(const int w, const int h, const unsigned char r, const unsigned char g, const unsigned char b)
      : width(w)
      , height(h)
      , rgb(static_cast<std::size_t>(w) * h * 3)
    {
      for (std::size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = r;
        rgb[i + 1] = g;
        rgb[i + 2] = b;
      }
    }

    void Blend(const int x, const int y, const unsigned r, const unsigned g, const unsigned b, const unsigned a)
    {
      if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
      }
      unsigned char* const p = rgb.data() + (static_cast<std::size_t>(y) * width + x) * 3;
      p[0] = static_cast<unsigned char>((p[0] * (255 - a) + r * a + 127) / 255);
      p[1] = static_cast<unsigned char>((p[1] * (255 - a) + g * a + 127) / 255);
      p[2] = static_cast<unsigned char>((p[2] * (255 - a) + b * a + 127) / 255);
    }
  };

  // Composites the recorded quads (and clears the record): each glyph texture decoded and
  // blended in `color` at its quad, 1:1 (the quads are texture-sized at integer positions).
  void CompositeQuads(Canvas& canvas, const unsigned r, const unsigned g, const unsigned b)
  {
    for (const fonts_test::Quad& quad : fonts_test::Quads()) {
      const auto found = fonts_test::Textures().find(quad.texture);
      if (found == fonts_test::Textures().end()) {
        Fail("quad without a recorded texture%s", "");
        continue;
      }
      const fonts_test::Dxt5Texture& texture = found->second;
      const float quadWidth = quad.right - quad.left;
      const float quadHeight = quad.bottom - quad.top;
      if (std::fabs(quadWidth - static_cast<float>(texture.width)) > 0.001f ||
          std::fabs(quadHeight - static_cast<float>(texture.height)) > 0.001f ||
          quad.left != std::floor(quad.left) || quad.top != std::floor(quad.top)) {
        Fail("glyph quad is not its texture's size at an integer position%s", "");
      }
      const int left = static_cast<int>(std::lround(quad.left));
      const int top = static_cast<int>(std::lround(quad.top));
      const std::uint32_t blocksAcross = texture.width / 4U;
      for (std::uint32_t by = 0; by < texture.height / 4U; ++by) {
        for (std::uint32_t bx = 0; bx < blocksAcross; ++bx) {
          unsigned char alpha[16];
          DecodeAlphaBlock(texture.blocks[(static_cast<std::size_t>(by) * blocksAcross + bx) * 2U], alpha);
          for (int i = 0; i < 16; ++i) {
            if (alpha[i] != 0) {
              canvas.Blend(left + static_cast<int>(bx * 4U) + (i & 3), top + static_cast<int>(by * 4U) + (i >> 2), r, g, b, alpha[i]);
            }
          }
        }
      }
    }
    fonts_test::Quads().clear();
  }

  struct Request
  {
    const char* face;
    int size;
  };

  // The faces the FA/FAF UI asks for (lua/skins/skins.lua, lua/ui/**), at its common sizes.
  const char* const kFaces[] = {
    "Arial", "Arial Bold", "Arial Black", "Arial Narrow", "Zeroes Three", "Butterbelly", "VDub", "Wintermute",
    "Andale Mono", "Courier New", "Arial Gras", "Ariel", "Times New Roman", "Lucida Console",
  };
  const int kSizes[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 28, 32, 48};

  std::string Narrow(const std::wstring& text)
  {
    std::string out;
    for (const wchar_t ch : text) {
      out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
    }
    return out;
  }

  // GDI's view of CD3DFont::Create(size, face): the same CreateFontA, selected into a DC.
  struct GdiView
  {
    TEXTMETRICW metrics{};
    std::string face;
    DWORD kerningPairs = 0;
  };

  GdiView QueryGdi(const int size, const char* const face)
  {
    GdiView view;
    const HFONT font = CreateFontA(-size, 0, 0, 0, 0, 0, 0, 0, 1u, 0, 0, 4u, 0, face);
    const HDC dc = CreateCompatibleDC(nullptr);
    SelectObject(dc, font);
    GetTextMetricsW(dc, &view.metrics);
    WCHAR name[LF_FULLFACESIZE] = {};
    GetTextFaceW(dc, LF_FULLFACESIZE, name);
    view.face = Narrow(name);
    view.kerningPairs = GetKerningPairsW(dc, 0, nullptr);
    DeleteDC(dc);
    DeleteObject(font);
    return view;
  }

  void WriteMetrics(const std::string& path)
  {
    FILE* const out = std::fopen(path.c_str(), "w");
    if (out == nullptr) {
      Fail("cannot write %s", path);
      return;
    }
    std::fprintf(
      out,
      "face\tsize\trealized\ttmHeight\ttmAscent\ttmDescent\ttmInternalLeading\ttmExternalLeading\ttmAveCharWidth\t"
      "tmMaxCharWidth\ttmWeight\ttmOverhang\ttmPitchAndFamily\tkerningPairs\tadvances(0x20..0x7E)\n"
    );
    for (const char* const face : kFaces) {
      for (const int size : kSizes) {
        fonts_test::Context() = std::string(face) + " " + std::to_string(size);
        boost::SharedPtrRaw<moho::CD3DFont> handle = moho::CD3DFont::Create(size, face);
        moho::CD3DFont* const font = handle.px;
        if (font == nullptr) {
          Fail("CD3DFont::Create failed for %s", std::string(face) + " " + std::to_string(size));
          continue;
        }
        const GdiView gdi = QueryGdi(size, face);
        const TEXTMETRICW& tm = gdi.metrics;
        // CD3DFont copies GetTextMetricsW: its members must equal GDI's.
        if (font->mHeight != static_cast<float>(tm.tmHeight) || font->mAscent != static_cast<float>(tm.tmAscent) ||
            font->mDescent != static_cast<float>(tm.tmDescent) ||
            font->mInternalLeading != static_cast<float>(tm.tmInternalLeading) ||
            font->mExternalLeading != static_cast<float>(tm.tmExternalLeading) ||
            font->mAveCharWidth != static_cast<float>(tm.tmAveCharWidth) || font->mKerningPairs.size() != gdi.kerningPairs) {
          Fail("CD3DFont members differ from GDI's TEXTMETRIC for %s", std::string(face) + " " + std::to_string(size));
        }
        if (!(font->mHeight > 0.0f) || !(font->mAscent > 0.0f)) {
          Fail("non-positive height for %s", std::string(face) + " " + std::to_string(size));
        }
        std::string advances;
        for (wchar_t ch = 0x20; ch < 0x7F; ++ch) {
          const float advance = font->GetCharInfo(ch).mAdvance;
          advances += (advances.empty() ? "" : ",") + std::to_string(static_cast<int>(advance));
        }
        std::fprintf(
          out, "%s\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t0x%02x\t%u\t%s\n", face, size, gdi.face.c_str(), tm.tmHeight,
          tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading, tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmWeight,
          tm.tmOverhang, tm.tmPitchAndFamily, gdi.kerningPairs, advances.c_str()
        );
        handle.release();
      }
    }
    std::fclose(out);
  }

  // One sheet line: a label in Arial 12, then `text` in (face, size).
  struct SheetLine
  {
    const char* face;
    int size;
    const char* text;
  };

  const char* const kPangram = "The quick brown fox jumps over the lazy dog. 0123456789";
  const char* const kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  const char* const kLatin = "\xC3\x80\xC3\x81\xC3\x82\xC3\x83\xC3\x84\xC3\x85 \xC3\x87\xC3\xA7 \xC3\x89\xC3\xA9 \xC3\x91\xC3\xB1 "
                             "\xC3\x96\xC3\xB6 \xC3\x9C\xC3\xBC \xC3\x9F \xC3\xBF \xC5\x92\xC5\x93 \xE2\x82\xAC\xC2\xA3\xC2\xA5"
                             "\xC2\xA9\xC2\xAE\xE2\x84\xA2\xE2\x80\xA6\xE2\x80\x93\xE2\x80\x94 \xC2\xB0\xC2\xB1\xC2\xBD";
  const char* const kWorld = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, \xD0\xBC\xD0\xB8\xD1\x80! "
                             "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC "
                             "\xE4\xB8\xAD\xE6\x96\x87 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 "
                             "\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A";
  const char* const kHud = "Mass 1250/3000 +25.4  Energy 8000/10000 -120  T3 Strategic Missile Launcher  12:34:56";

  const SheetLine kSheet[] = {
    {"Arial", 10, kPangram},
    {"Arial", 12, kPangram},
    {"Arial", 12, kAlphabet},
    {"Arial", 12, kLatin},
    {"Arial", 12, kWorld},
    {"Arial", 14, kPangram},
    {"Arial", 14, kHud},
    {"Arial", 16, kPangram},
    {"Arial", 18, kLatin},
    {"Arial", 20, kPangram},
    {"Arial", 24, kPangram},
    {"Arial Bold", 12, kHud},
    {"Arial Bold", 14, kPangram},
    {"Arial Black", 16, kPangram},
    {"Arial Narrow", 14, kPangram},
    {"Zeroes Three", 12, "ZEROES THREE 12: Supreme Commander Forged Alliance 0123456789"},
    {"Zeroes Three", 14, kPangram},
    {"Zeroes Three", 16, kAlphabet},
    {"Zeroes Three", 20, kPangram},
    {"Zeroes Three", 28, "Supreme Commander: Forged Alliance"},
    {"Butterbelly", 16, kPangram},
    {"VDub", 16, kPangram},
    {"Andale Mono", 12, "fixedFont (Andale Mono 12): console> sim_Gravity 4.9  [0123456789]"},
    {"Courier New", 12, "Courier New 12 (net-stats HUD): idx  nick      ping  maxsp  data"},
    {"Arial Gras", 14, "\"Arial Gras\" 14 (lobby names; French full name of Arial Bold)"},
    {"Ariel", 12, "\"Ariel\" 12 (sim debug text; no such face)"},
    {"Calibri", 13, "score.lua resource-mode icon: \xE2\x83\x9D  (U+20DD in \"Calibri\")"},
  };

  // DrawChar is the test double, which never touches the batcher itself.
  alignas(16) unsigned char gBatcherStorage[256];
  moho::CD3DPrimBatcher* const gBatcher = reinterpret_cast<moho::CD3DPrimBatcher*>(gBatcherStorage);

  bool WriteSheet(const std::string& directory)
  {
    // Layout pass: the line heights from the fonts themselves.
    const int width = 1200;
    int height = 8;
    for (const SheetLine& line : kSheet) {
      boost::SharedPtrRaw<moho::CD3DFont> handle = moho::CD3DFont::Create(line.size, line.face);
      height += static_cast<int>(handle.px->mHeight + handle.px->mExternalLeading) + 6;
      handle.release();
    }
    Canvas canvas(width, height, 24, 28, 36);
    boost::SharedPtrRaw<moho::CD3DFont> labelHandle = moho::CD3DFont::Create(12, "Arial");
    moho::CD3DFont* const labelFont = labelHandle.px;

    int top = 4;
    for (const SheetLine& line : kSheet) {
      fonts_test::Context() = std::string("sheet: ") + line.face + " " + std::to_string(line.size);
      boost::SharedPtrRaw<moho::CD3DFont> handle = moho::CD3DFont::Create(line.size, line.face);
      moho::CD3DFont* const font = handle.px;
      const int baseline = top + static_cast<int>(font->mAscent); // as CMauiText: top + mAscent
      for (int x = 0; x < 6; ++x) {
        canvas.Blend(x, baseline, 255, 60, 60, 255);
        canvas.Blend(x, baseline - static_cast<int>(font->mAscent), 80, 120, 255, 255);
        canvas.Blend(x, baseline + static_cast<int>(font->mDescent), 80, 120, 255, 255);
      }
      char label[64];
      std::snprintf(label, sizeof(label), "%s %d", line.face, line.size);
      labelFont->Render2D(label, gBatcher, Wm3::Vector2f(10.0f, static_cast<float>(baseline)), 0xFFFFFFFFu, 0.0f, 0.0f);
      CompositeQuads(canvas, 150, 160, 175);
      font->Render2D(line.text, gBatcher, Wm3::Vector2f(150.0f, static_cast<float>(baseline)), 0xFFFFFFFFu, 0.0f, 0.0f);
      CompositeQuads(canvas, 255, 255, 255);
      top += static_cast<int>(font->mHeight + font->mExternalLeading) + 6;
      handle.release();
    }
    labelHandle.release();

    bool ok = WritePng(directory + "/sheet.png", canvas.width, canvas.height, canvas.rgb);
    // 4x zoom of the top left (Arial 10..14 and the accents).
    const int zoom = 4;
    const int cropWidth = 300;
    const int cropHeight = std::min(150, canvas.height);
    Canvas zoomed(cropWidth * zoom, cropHeight * zoom, 0, 0, 0);
    for (int y = 0; y < cropHeight * zoom; ++y) {
      for (int x = 0; x < cropWidth * zoom; ++x) {
        const unsigned char* const src = canvas.rgb.data() + (static_cast<std::size_t>(y / zoom) * canvas.width + 140 + x / zoom) * 3;
        unsigned char* const dst = zoomed.rgb.data() + (static_cast<std::size_t>(y) * zoomed.width + x) * 3;
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
      }
    }
    ok = WritePng(directory + "/sheet_zoom.png", zoomed.width, zoomed.height, zoomed.rgb) && ok;
    return ok;
  }
} // namespace

int main(int argc, char** argv)
{
  std::string fontsDir;
  std::string outDir = ".";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--fonts") == 0 && i + 1 < argc) {
      fontsDir = argv[++i];
    } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      outDir = argv[++i];
    }
  }

  // 1. REN_Init: every /fonts/*.ttf through AddFontMemResourceEx.
  if (!fontsDir.empty()) {
    DIR* const dir = opendir(fontsDir.c_str());
    if (dir == nullptr) {
      Fail("cannot open %s", fontsDir);
    } else {
      std::vector<std::string> names;
      while (const dirent* const entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".ttf") == 0) {
          names.push_back(name);
        }
      }
      closedir(dir);
      std::sort(names.begin(), names.end());
      for (const std::string& name : names) {
        std::vector<char> bytes = ReadFile(fontsDir + "/" + name);
        DWORD count = 0;
        const HANDLE handle = AddFontMemResourceEx(bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &count);
        std::printf("adding font file %s: %s, %u face(s)\n", name.c_str(), handle != nullptr ? "ok" : "FAILED", count);
        if (handle == nullptr) {
          Fail("AddFontMemResourceEx failed for %s", name);
        }
      }
    }
  }

  // Win32 return conventions.
  {
    DWORD count = 7;
    char junk[64] = "not a font";
    if (AddFontMemResourceEx(junk, sizeof(junk), nullptr, &count) != nullptr || count != 0) {
      Fail("AddFontMemResourceEx accepted junk%s", "");
    }
    if (AddFontMemResourceEx(nullptr, 0, nullptr, &count) != nullptr || GetLastError() != ERROR_INVALID_PARAMETER) {
      Fail("AddFontMemResourceEx(null) is not ERROR_INVALID_PARAMETER%s", "");
    }
    if (RemoveFontMemResourceEx(reinterpret_cast<HANDLE>(0x1234)) != FALSE) {
      Fail("RemoveFontMemResourceEx accepted a foreign handle%s", "");
    }
  }

  WriteMetrics(outDir + "/metrics.tsv");
  if (!WriteSheet(outDir)) {
    Fail("cannot write the sheet into %s", outDir);
  }

  // "Clamping character ... to MAX_CHAR_WIDTH/HEIGHT" follows from the metrics alone (the
  // scratch cell is mAveCharWidth * 2 + advance wide), so Windows warns for the same glyphs;
  // anything else (an edge not black, the GetBitmapBits/GetPixel cross-check) is a failure.
  // Butterbelly has no '{' or '}': they are its glyph 0, which is 800 units tall against a
  // usWinAscent of 791, so at some sizes its top reaches the scratch cell's first row (font
  // data: Windows' hinted outline lands there too).
  const auto expected = [](const std::string& warning) {
    return warning.find("Clamping character") != std::string::npos ||
      (warning.find("Butterbelly") != std::string::npos && warning.find("Top edge") != std::string::npos &&
       (warning.find("u+007b") != std::string::npos || warning.find("u+007d") != std::string::npos));
  };
  int failing = 0;
  for (const std::string& warning : fonts_test::Warnings()) {
    const bool known = expected(warning);
    std::fprintf(stderr, "CD3DFont warning%s: %s\n", known ? " (expected, as on Windows)" : "", warning.c_str());
    failing += known ? 0 : 1;
  }
  if (failing != 0) {
    Fail("CD3DFont warned %s time(s)", std::to_string(failing));
  }
  std::printf("%s: %d failure(s); wrote %s/metrics.tsv, sheet.png, sheet_zoom.png\n", gFailures == 0 ? "PASS" : "FAIL", gFailures, outDir.c_str());
  return gFailures == 0 ? 0 : 1;
}
