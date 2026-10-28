// POSIX stand-in for moho/render/d3d/CD3DFont.cpp (replaced in
// macos/exclusions.d/render.cmake): the engine's own CD3DFont.cpp, compiled unchanged.
//
// Its GDI calls are the Gdi compat area's (compat/Gdi.h): CreateFontA realizes the face
// the way GDI's mapper does (the game's /fonts/*.ttf, registered by REN_Init through
// AddFontMemResourceEx, then installed fonts; WinCompatGdiFont.cpp), GetTextMetricsW and
// GetKerningPairsW give the TrueType driver's integer metrics (VDMX/OS/2 ascent and
// descent, hdmx/hmtx advances, 'kern' pairs), and DrawTextW draws each glyph with Core Text
// into the 32 bpp memory bitmap that GetBitmapBits/GetPixel read back. Everything after
// that (the glyph trimming, the DXT5 alpha blocks, the glyph-page cache, the advances and
// extents the UI lays out with, ren_GlyphBitmapRead) is the Windows code path itself.
//
// Notes and tests: macos/porting/fonts.md, macos/tests/fonts/.

#include "moho/render/d3d/CD3DFont.cpp"
