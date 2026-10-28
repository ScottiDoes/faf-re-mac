# Package "fonts": in-game text on macOS (milestone 2)

GDI's role on Windows: every in-game font is a `moho::CD3DFont` (moho/render/d3d/CD3DFont.cpp),
which creates a GDI font, measures it with GDI and rasterizes each glyph with GDI into a
128x128 32 bpp scratch bitmap, then uploads it as a DXT5 texture. Before this package the POSIX
stand-in returned no font at all, so every UI text control was 0 pixels high and the game UI
hung in FAF's fill-the-panel loops (link-run.md, level 4).

## Approach

The engine's **own CD3DFont.cpp now runs unchanged** on macOS: the stand-in
`src/platform/posix/stubs/render/moho/CD3DFontPosix.cpp` is a single
`#include "moho/render/d3d/CD3DFont.cpp"`, and the GDI calls it makes are implemented in the
Gdi compat area on Core Text + Core Graphics. Everything after GDI (glyph trimming, the DXT5
alpha blocks, the glyph-page cache, `mAdvance`, `ren_GlyphBitmapRead` and its
GetBitmapBits/GetPixel cross-check) is the Windows code path itself. No engine file changed:
`REN_Init`'s font registration (CD3DDevice.cpp) already calls `AddFontMemResourceEx`, which
now works, so its lines need no edit; CD3DFont.h needs none either.

### What CD3DFont asks of GDI (read from CD3DFont.cpp)

- `CreateFontA(-pointSize, 0,0,0, 0 /*FW_DONTCARE*/, 0,0,0, 1 /*DEFAULT_CHARSET*/, 0, 0,
  4 /*ANTIALIASED_QUALITY*/, 0, face)`: negative height = the em height in pixels (the
  "point size" Lua passes is pixels), any weight, upright.
- `CreateCompatibleDC`, `CreateBitmap(128,128,1,32)`, `SelectObject` x2.
- `GetTextMetricsW`: copies tmHeight, tmAscent, tmDescent, tmInternalLeading,
  tmExternalLeading, tmAveCharWidth, tmOverhang, TMPF_TRUETYPE into the members the UI reads
  (row height = mHeight + mExternalLeading, baseline = top + mAscent, carets, SimDriver's
  HUD, TimeBar, CWldSession debug text).
- `GetKerningPairsW` (stored, sorted; never used for drawing).
- Per new character: `SetTextAlign(TA_BASELINE)`; `DrawTextW(&ch, 1, DT_CALCRECT|DT_NOPREFIX|
  DT_SINGLELINE)` for the advance (`mAdvance`, an integer); `FillRect` black; white on black
  `DrawTextW(DT_NOCLIP|DT_NOPREFIX|DT_SINGLELINE)` with the pen at x = (int)mAveCharWidth and
  the baseline at ceil(mAscent + 1); `GetBitmapBits` (or `GetPixel` per pixel); then the
  coverage (red channel) goes into DXT5 alpha blocks. No GetCharABCWidths, no GetGlyphOutline.

### Gdi area (compat/Gdi.h, WinCompatGdi.cpp, WinCompatGdiFont.{h,cpp})

- New declarations: LOGFONTA/W, TEXTMETRICA/W, KERNINGPAIR, FW_/charset/OUT_/CLIP_/quality/
  pitch/FF_/TMPF_/TA_/DT_ constants; CreateFont{A,W}, CreateFontIndirect{A,W},
  GetTextMetrics{A,W}, GetTextFace{A,W}, GetKerningPairs{A,W}, SetTextAlign/GetTextAlign,
  GetTextExtentPoint32{A,W}, TextOut{A,W}, DrawText{A,W} (user32 on Windows), GetPixel,
  GetBitmapBits; GetObject{A,W} of a font gives its LOGFONT. Stock fonts carry Windows'
  sizes on the fallback faces.
- **Fonts** (WinCompatGdiFont.cpp). `AddFontMemResourceEx` -> Core Text descriptors from the
  data (`CTFontManagerCreateFontDescriptorsFromData`, collections too); private, nothing
  registered system-wide, so the game's Arial 3.00 never collides with macOS' Arial 5.01 and
  is preferred to it. `RemoveFontMemResourceEx` removes them. Win32 returns: null +
  ERROR_INVALID_PARAMETER for a null view/size/count, null for data without a font.
- **Mapper**: face name (cut to 31 chars, '@' dropped), case-insensitive, against family names
  (name ID 1), then full names (name ID 4) of the private fonts, then of the installed fonts
  (Core Text family match, then `CTFontCreateWithName`, accepted only when the face carries
  the name), in English and the user's macOS language (the names a Windows font table
  holds: system language + English). Within a family: nearest weight, matching slant.
  Missing -> Arial (Courier New for FIXED_PITCH/FF_MODERN, Times New Roman for FF_ROMAN),
  logged once per name on stderr: `[faf-posix] gdi: no font named "X"; using "Arial"`.
- **Metrics** (Wine's reproduction of the TrueType driver, FreeType 16.16/26.6 arithmetic):
  ppem = -lfHeight (positive heights: Wine's calc_ppem_for_height); ascent/descent = the
  VDMX entry at that ppem (the 1:1 ratio group) when present, else round(usWinAscent/Descent
  x scale); internal leading likewise; external leading = max(0, round((lineGap -
  ((winAsc+winDesc) - (hheaAsc-hheaDesc))) x scale)); tmAveCharWidth = round(xAvgCharWidth x
  scale); advances = the hdmx record at that ppem, else round(hmtx advance x scale);
  kerning = every 'kern' format-0 pair, glyphs mapped to their lowest character, Wine's
  rounding. TMPF_TRUETYPE for 'glyf' fonts.
- **Glyphs**: the face's own cmap ((3,10) fmt 12, (3,1) fmt 4, Unicode, (3,0) symbol at
  0xF0xx), not Core Text's lookup (which answers the space glyph for control characters).
  Missing ASCII/control character -> glyph 0 (GDI's default character); any other missing
  character -> Core Text's fallback font for it (`CTFontCreateForString`), standing in for
  Windows' font linking / Uniscribe fallback, so Cyrillic/Greek/CJK/Korean in a Latin-only
  face (Zeroes Three) render; fallback advances are the rounded linear ones. A fallback
  glyph whose ink overflows the face's cell is scaled down and centred in it (font linking
  scales linked fonts to the base font); a zero-advance one (a combining mark on its own:
  FAF's score.lua draws U+20DD in "Calibri" as an icon) becomes a spacing glyph, shifted to
  start at the pen. Before this, that icon was the only source of CD3DFont edge warnings in
  a whole UI run.
- **Rasterizing**: `CTFontDrawGlyphs` into an 8-bit DeviceGray bitmap context, antialiased,
  font smoothing and subpixel positioning off, integer pen positions, integer baseline.
  Two corrections for the missing TrueType hinting:
  1. each glyph is scaled horizontally by (integer advance / linear width), clamped to
     0.75..1.33, as the hinter fits outlines to the hdmx advance (Arial 'x' at 12 px: 5 px
     advance for a 6.0 px outline; unscaled, "fox jumps" ran together);
  2. coverage goes through coverage^(1/1.45) (env `FAF_GDI_TEXT_GAMMA`, 1 = linear): GDI
     draws the UI sizes hinted and mostly bilevel (the fonts' gasp tables: Arial 9..16 px is
     gridfit-only), so its stems are whole pixels; unhinted one-pixel stems split over two
     half-covered pixels read faint (Courier New 12).
  The coverage is blended from the background to the text colour into the DC's bitmap
  (fast path for 32 bpp BGRX, writing 0 into the unused byte as GDI does; generic path via
  the existing pixel writer); OPAQUE background mode fills the character cell; TA_BASELINE /
  TA_BOTTOM / TA_CENTER / TA_RIGHT honoured; DT_NOCLIP or clipping to the rectangle.
- **Lifetime**: the GDI lock, object/DC registries and stock objects are now never destroyed
  (heap singletons): CD3DFont's font cache is torn down by an atexit handler registered
  before the first GDI call, so function-local statics were already destroyed when it ran
  (fonts_test aborted with "mutex lock failed" at exit). The font registry is likewise
  immortal.

## Faces FA/FAF use

Game files in `<FA>/fonts` (mounted at /fonts, registered by REN_Init): ARIAL, ARIALBD, ARIALBI,
ARIALI (family Arial, v3.00), ARIALN/NB/NBI/NI (Arial Narrow), ARIBLK (Arial Black), ARLRDBD
(Arial Rounded MT Bold), BUTTERBE (Butterbelly), vdub (VDub), wintermu (Wintermute),
zeroes_3 (Zeroes Three). FAF Lua asks for: "Arial" (bodyFont), "Zeroes Three" (titleFont,
buttonFont, factionFont), "Andale Mono" (fixedFont: console, debug panels), "Arial Bold",
"Arial Black", "Arial Gras" (lobby names, 19 calls), "Courier New" (SimDriver net HUD,
UnitsTooltip), "Butterbelly", "Times New Roman"/"Lucida Console" (MauiTest only), "Ariel"
(Sim.cpp debug text, econoverlayparams.lua: a typo, falls back to Arial).

- "Arial Bold" is a full name (name ID 4) -> the game's Arial Bold.
- "Arial Gras" is Arial Bold's **French** full name. Windows matches it only on a French
  system (GDI registers system-language + English names), so on English Windows it falls back
  to plain Arial; here the same rule applies with the macOS language. Wine's matching is the
  same rule.
- "Andale Mono" is not a stock Windows font (Windows falls back to Arial); macOS has it
  installed, so the mapper uses it, as GDI would on a machine where it is installed.
- "Lucida Console" is not installed on macOS -> Arial (logged).

## Tests (macos/tests/fonts)

`macos/tests/fonts/run.sh` builds (no cmake: the engine's own compile command per file, into
build/m2-fonts-tests) and runs:

1. **fonts_test** links WinCompatGdi.cpp, WinCompatGdiFont.cpp, WinCompatStrings.cpp, the
   CD3DFont stand-in (the real CD3DFont.cpp), DXTCodec.cpp, CountedObject.cpp, plus test
   doubles (test_doubles.cpp) that record `CD3DBatchTexture::FromDXT5` (the DXT5 blocks) and
   `CD3DPrimBatcher::DrawChar` (the quads, with DrawChar's own corner math).
   - Registers the 14 game fonts like REN_Init (all OK, 1 face each).
   - metrics.tsv: 14 faces x 17 sizes (8..48), CD3DFont::Create + the same CreateFontA through
     GDI: TEXTMETRIC, realized face, kerning count, the 95 ASCII advances via
     CD3DFont::GetCharInfo. Checks CD3DFont's members equal GDI's TEXTMETRIC, heights > 0.
   - sheet.png / sheet_zoom.png: 26 lines (Arial 10..24, Bold, Black, Narrow, Zeroes Three
     12..28, Butterbelly, VDub, Andale Mono, Courier New, the "Arial Gras"/"Ariel"
     fallbacks; ASCII, Latin-1 accents, symbols, Cyrillic, Greek, CJK, Korean, Arabic) drawn
     with CD3DFont::Render2D, the recorded DXT5 textures decoded and composited at the
     recorded quads. Margin marks: red = baseline, blue = cell top/bottom. Checks every quad
     is its texture's size at an integer position.
   - Win32 conventions of AddFontMemResourceEx/RemoveFontMemResourceEx.
   - CD3DFont warnings: "Clamping ... MAX_CHAR_WIDTH" (VDub 48: M W w |) is metric-only
     (scratch cell = 2 x mAveCharWidth + advance > 128), so Windows warns identically;
     Butterbelly '{' '}' "Top edge not black": the font lacks them, its glyph 0 is 800 units
     tall against usWinAscent 791 (font data; Windows' hinted outline reaches the top row
     too). Both expected; any other warning fails.
   Result: PASS, 0 failures. Looked at: baselines and cell marks line up for every face,
   no clipping, accents and descenders inside the cell, Zeroes Three/VDub/Butterbelly
   correct, fallback scripts legible (Arabic unshaped, as expected without Uniscribe).
2. **reference.py** recomputes TEXTMETRIC, the ASCII advances and the kerning count for
   every game-font row of metrics.tsv straight from the font files' tables (own cmap/hdmx/
   VDMX/hmtx/kern parsing, Wine's arithmetic) - an independent implementation of the same
   rules. Result: PASS, 136 rows, 0 differences.
3. Sample values (all from the fonts' VDMX/hdmx, i.e. Windows' own precomputed data): Arial
   12 -> height 15, ascent 12, descent 3, internal leading 3, external 0, avg width 5 (the
   values commonly reported for Arial 9 pt at 96 dpi); Arial 16 -> 18/15/3/2/1/7; Zeroes
   Three 14 -> 19/13/6/5/0/8.

### GDI reference on Windows / Wine (the user's Linux PC)

`gdi_reference.c` does what the game does (AddFontMemResourceEx of every font file,
CreateFontA as CD3DFont, GetTextMetricsW, GetTextFaceA, GetKerningPairsW, DrawTextW
DT_CALCRECT per ASCII character) and prints metrics.tsv's columns; it also writes
sheet_gdi.bmp (sample lines drawn per glyph exactly as CD3DFont draws into its scratch
bitmap). On Linux:

```
sudo apt install mingw-w64            # or the distro's equivalent
x86_64-w64-mingw32-gcc -O2 -o gdi_reference.exe gdi_reference.c -lgdi32 -luser32
wine gdi_reference.exe 'C:\path\to\Supreme Commander Forged Alliance\fonts' > metrics_gdi.tsv
```

(the fonts folder of the FAF install, e.g. under ~/.wine/drive_c/... or Steam's prefix).
Bring metrics_gdi.tsv and sheet_gdi.bmp back and run
`python3 -I macos/tests/fonts/compare_tsv.py metrics_gdi.tsv build/m2-fonts-tests/out/metrics.tsv`.
Wine's GDI is FreeType's (it mirrors Windows' metrics closely, glyph images differ); a run on
real Windows is the authority. No Windows cross toolchain exists on this Mac, so it has not
been compiled here.

## End to end: the replay with the game UI (link-run.md level 4's hang)

Own tree build/m2-fonts (full engine + faf_replay, configured from macos/; the wx package's
in-progress native-window declarations briefly broke the link, retried until they landed):

| Run | Result |
|---|---|
| `SPEED=50 macos/run_replay.sh` (/noui), FAF_REPLAY_EXE=build/m2-fonts/faf_replay | `FAF_REPLAY_RESULT replay=27890640 beats=9720 mismatches=0 exit=end-of-replay`, 66.8x (77.9x on an earlier run; load average 33-39 from other agents' builds) |
| same with `UI=1` (was: hang, Lua heap doubling) | `beats=9716 mismatches=0 exit=end-of-replay`, 16.0 min of game time in 15.0 s = 64x; the benchmark hook's BENCH lines are back; 0 CD3DFont warnings in the engine log |

Fonts reported missing in the UI run: "Zeroes Three" (the headless runner does not call
REN_Init, so the game's fonts are not registered: see Requests) and "Calibri" (not on macOS;
Windows 7+ has it); both fall back to Arial. Logs: build/m2-fonts-run/{noui,ui}/.

## Known differences from Windows

- No TrueType hinting: glyph shapes are unhinted outlines (fitted horizontally to the
  advance, contrast-boosted), not GDI's hinted/bilevel bitmaps. Layout (every advance,
  height, ascent) is Windows'; pixels inside a glyph cell are not. Small text is softer
  than on Windows.
- Advances for sizes without an hdmx record are the rounded linear ones; the hinter can
  differ by a pixel for glyphs the LTSH table marks as not yet linear at that size. The
  game's hdmx records cover the standard point sizes at 96 dpi: Arial/Arial Bold/Arial Black
  11, 12, 13, 15, 16, 17, 19, 21, 24, 27, 29, 32, 33, 37, 42, 46, 50... px; Zeroes Three and
  VDub also 9 and 10. Not covered and partly non-linear: Arial 8-10, 14, 18, 20, 22, 28 px
  (Arial 14: 45 ASCII glyphs), Zeroes Three 14, 18, 20 px... (its LTSH marks every glyph
  non-linear). Exact advances there need a TrueType interpreter (FreeType's v35 one, or
  Windows); the reference comparison will show which glyphs differ.
- Wine is not an authority for advances: modern FreeType (v40 interpreter, backward
  compatibility mode) ignores hdmx, so Wine reports rounded linear advances even where
  Windows uses hdmx (Arial 'x' at 12 px: Wine 6, Windows/hdmx 5). Its TEXTMETRIC (VDMX,
  OS/2) follows the same rules as here.
- Missing non-ASCII characters use Core Text's fallback where Windows uses font linking /
  Uniscribe; the fallback fonts and their advances differ. Complex scripts are not shaped.
- No simulated bold/italic, underline/strikeout, escapement, word wrap, tabs, ellipses,
  TA_UPDATECP (stub-logged once each; CD3DFont uses none of them).
- The glyph cache key is the low 16 bits of the character (CD3DFont's page/slot); on POSIX
  STR_DecodeUtf8Char yields full code points for 4-byte UTF-8, so two characters that share
  their low 16 bits (an emoji and a BMP character) share a slot (first one wins). Windows
  truncates the character itself.

## Stubs left (FAF_POSIX_STUB, logged once)

- CreateFont: escapement/orientation; underline/strikeout; no simulated bold/italic.
- SetTextAlign: TA_UPDATECP. DrawText: DT_WORDBREAK/DT_EXPANDTABS/DT_TABSTOP/ellipses.
- GetObject of a palette (unchanged from before).

## Files

- `src/platform/posix/compat/Gdi.h` (declarations, area comment)
- `src/platform/posix/WinCompatGdi.cpp` (font objects, text API, GetPixel/GetBitmapBits,
  immortal registries)
- `src/platform/posix/WinCompatGdiFont.h`, `WinCompatGdiFont.cpp` (new: font table, mapper,
  metrics, Core Text rasterizer)
- `src/platform/posix/stubs/render/moho/CD3DFontPosix.cpp` (now includes the original)
- `macos/tests/fonts/`: fonts_test.cpp, test_doubles.{h,cpp}, build.sh, run.sh, reference.py,
  fontinfo.py (table dump), gdi_reference.c, compare_tsv.py

## Requests

- render package (macos/exclusions.d/render.cmake) and render.md: the comment on the
  CD3DFont replacement is stale ("Create fails as when CreateFontA fails"). Either update it
  to "the engine's own CD3DFont.cpp, compiled through the stand-in on the Gdi area's Core Text
  fonts", or drop the replacement line and delete CD3DFontPosix.cpp so CD3DFont.cpp builds in
  place (identical object; the stand-in only includes it).
- runner package (HeadlessApp.cpp): call `moho::REN_Init()` (font registration only, no
  device; logs "adding font file ..." per font) when the UI runs, so UI=1 replays measure
  text with the game's fonts (Zeroes Three) rather than the installed fallbacks. faf_game
  gets it from CScApp::AppInit already. Also run_replay.sh's UI=1 comment ("hangs until
  fonts exist") and link-run.md open issue 1 are resolved by this package.
- lead: README.md layout table: the Gdi area is now compat/Gdi.h -> WinCompatGdi.cpp +
  WinCompatGdiFont.cpp (+ internal WinCompatGdiFont.h). run_replay.sh's UI=1 comment ("hangs
  until fonts exist") is stale once this lands (see the UI=1 result below).
