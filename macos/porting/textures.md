# Package "textures" (milestone 2): texture files on macOS

Owner of: `src/platform/posix/stubs/render/gal/ImageCodecPosix.cpp` (compiled in place of
`gpg/gal/backends/common/ImageCodec.cpp`), `src/platform/posix/gal/Image*.cpp/.h`,
`macos/tests/textures/**`. `ImageCodec.h` needed no change.

## 1. What ImageCodec does on Windows

`gpg/gal/backends/common/ImageCodec.cpp` (FAF addition) runs D3DX11 (`d3dx11_43.dll`) on a
private WARP Direct3D 11 device for the Direct3D 12 and Vulkan backends.

- `Instance()`: null without the DLL or a WARP device; `DeviceVulkan::Setup` then throws "no
  texture decoder". So on POSIX the old stub (null) meant no Vulkan device at all.
- `Decode(data, options)`:
  1. DDS direct path (`ren_DdsDirectLoad`, `TryReadBlockCompressedDds`): DXT1/3/5, 2D or a full
     cube, a full mip chain, power-of-two sides, top level (after `skipMipLevels`) a multiple of
     4, no overrides -> the file's bytes as they are.
  2. Decode cache (`ren_TextureDecodeCache`): XXH64 of the file + options -> `<shader cache
     dir>/textures/<key>.img`; a D3DX decode that took >= 8 ms is written on a background thread.
  3. D3DX11: `D3DX11CreateTextureFromMemory` with `D3DX11_IMAGE_LOAD_INFO` defaults - size from
     the file, **MipLevels DEFAULT = a full chain** (the file's levels kept, the missing ones
     built and, for BC, compressed again on the CPU), Format FROM_FILE, Filter DEFAULT (triangle
     | dither; `ResizeFilter::None` -> D3DX11_FILTER_NONE), FirstMipLevel = `skipMipLevels`
     (retried from 0 when D3DX refuses) - into a STAGING texture, copied into `Image`. A
     block-compressed result has its top level rounded up to a multiple of 4 (stretched with the
     default filter, padded transparent black with NONE). Volumes come back as Texture3D.
- Callers (Vulkan backend): `CreateTexture` (options: skip only); `GetTexture2D`, i.e. every
  UI batch texture (`SBatchTextureDataFactory`): BC3, 1 level, filter NONE + `GetImageSize`;
  `UpdateSurface` with a different shape: `Encode(DDS)` then `Decode` with the destination's
  format/size/levels; `SaveTexture`/`TextureVulkan::SaveToBuffer`/`SaveRenderTarget`:
  `Encode`/`EncodeToFile` as BMP/JPG/PNG/DDS (BMP of BGRA by the engine's own writer).
- D3DX11 itself has no TGA reader (D3DX9 had).

## 2. The POSIX codec

| File | What |
|---|---|
| `stubs/render/gal/ImageCodecPosix.cpp` | The `ImageCodec` API. `TryReadBlockCompressedDds` and the decode cache copied from ImageCodec.cpp (cache through the compat Win32 file calls, key salted `"faf image decode cache posix 1"` so it never mixes with D3DX output), `FAF_IMAGECODEC_VERIFY=1` compares the direct path with the full decode. `DecodeWithD3dx` keeps its name and calls `ImageDecode`. |
| `gal/ImageDecode.cpp/.h` | The decode pipeline (D3DX11's rules, below), `ReadTextureSize`, `EncodeTexture`. |
| `gal/ImageDds.cpp/.h` | DDS reader (legacy pixel formats + DX10 header, 2D/cube/array/volume) and writer (legacy header where one exists, else DX10). |
| `gal/ImageFiles.cpp/.h` | Apple ImageIO: `CGImageSource` decode of PNG/JPEG/BMP/GIF/TIFF/TGA (TGA by type hint: it has no signature) to raw 8-bit RGBA (no colour management, alpha un-premultiplied, 16-bit samples rounded, grey/palette expanded); PNG/JPEG encode through `CGImageDestination`; BMP written by hand (32 bpp top-down, as the engine's `WriteBgraBmp`). |
| `gal/ImagePixels.cpp/.h` | DXGI format <-> 8-bit RGBA for every format the backends use (BGRA/RGBA/X, 565, 5551, 4444, 10:10:10:2, A8, R8, R8G8, R16(G16)(B16A16) UNORM/FLOAT, R32 floats, BC1-BC5). |
| `gal/ImageBc.cpp/.h` | BC1-BC5 block decode (Direct3D 10 rules) and encode (in house). |
| `gal/ImageResample.cpp/.h` | Area-weighted resampling: the 2x2 box for mips, odd sizes, stretching, unscaled placement. |

All are plain C++ plus CoreFoundation/CoreGraphics/ImageIO (frameworks already linked into
faf_replay/faf_game). No new static initializers (the BC tables are function-local statics).

### D3DX11 rules followed, and where this differs

- **Formats out** (`ImageDds.h`): DXT1/3/5 -> BC1/2/3 as is; A8R8G8B8/X8R8G8B8 -> B8G8R8A8/X8
  as is (the D3D9 byte order the engine wrote them in; D3DX11 may give R8G8B8A8 - same sampled
  texels); A8B8G8R8 -> R8G8B8A8; 24-bit and the 16-bit layouts (A1R5G5B5 strategic icons,
  A4R4G4B4, R5G6B5) -> B8G8R8A8 with exact bit replication (Direct3D 11.0, so D3DX11, has no
  16-bit texture formats); L8 -> R8, A8 -> A8, A8L8 -> R8G8, float fourCCs -> their DXGI
  formats. Non-DDS images -> B8G8R8A8 (D3DX11/WIC: R8G8B8A8; same texels), 16-bit PNG rounded to
  8 bits (D3DX11: R16G16B16A16_UNORM).
- **Mip chain**: full chain unless `options.mipLevels`; the file's levels kept byte for byte,
  missing ones built from the last present level (before its encoding) with a box filter
  (`ResizeArea`: exact 2x2 average for halving, area weights for odd sizes - "fant"), channels
  independent, alpha not premultiplied, round to nearest.
- **BC top level**: rounded up to a multiple of 4; default filter stretches the image over it
  with area weights (D3DX: triangle), `ResizeFilter::None` places it unscaled with transparent
  texels around it.
- **BC -> BC without decoding** (an improvement over D3DX11, which decodes and compresses
  again): blocks lying inside the image are copied (same format) or re-packed into BC3 (BC1
  four-colour blocks and BC2 colour losslessly, BC2 alpha re-encoded); only edge blocks are
  encoded. This is the UI path: a DXT5 18x18 button becomes BC3 20x20 with its 16 inner blocks
  the file's own bytes.
- **BC encoder**: principal-axis endpoints + 2 rounds of least squares, optimal single-colour
  endpoints for flat blocks (worst error 1/255 over 4096 flat colours), both BC3 alpha modes
  tried, BC1 punch-through for alpha < 128. Colour fits in BC2/BC3 are **weighted by alpha**:
  FA's UI art and cursors keep white RGB under alpha 0 next to saturated colours, and an
  unweighted fit turned the red attack cursor brown (seen in the first sample sheet, fixed).
  Colour blocks of BC2/BC3 are written with c0 > c1 (or equal with every index 0), so a decoder
  treating them as BC1 reads the same colours.
- **skipMipLevels**: drops that many top levels; ignored when the file has no level that small
  (= the Windows caller's retry from 0).
- **Header leniency**: DDS `dwSize`/`dwFlags` validity bits not checked (FAF's
  `textures/ui/common/hotstats/panel_bmp.dds` says size 24, flags = LINEARSIZE only; D3DX loads
  it); a 2D file cut short keeps its complete levels; a chain longer than the image allows keeps
  the real levels.
- **Not done** (none in the game data): volume textures keep the file's levels, no conversion
  or 3D mip generation; BC6H/BC7/SNORM formats are copied with the file's levels, no conversion.
- **Encode**: DDS of the whole image; BMP/JPG (quality 0.9)/PNG of the first subresource via
  8-bit RGBA (floats clamped to [0,1]). `EncodeToFile` writes through the compat `CreateFileA`
  (Windows-form paths work).

## 3. Tests (macos/tests/textures; build.sh -> build/m2-textures-tests)

`build.sh` compiles the codec TUs, `DxgiFormats.cpp`, the compat `WinCompat*.cpp` (file calls
for the cache/EncodeToFile) and `test_support.cpp` (logging, shader cache dir =
`FAF_TEST_CACHE_DIR`) with the engine's own commands (`engine_cc.py`), links three programs:

- `codec_tests`: BC blocks (flat colours, PSNR on a picture, block ordering, alpha modes,
  punch-through, BC1/BC2 -> BC3 re-packing), every pixel format round trip, resampling, DDS
  write/read of 7 formats + cube, 11 legacy pixel formats one texel each, decode options
  (1-level DXT5 -> 10 levels with level 0 kept, direct == full decode, skip, 18x18 stretched
  and padded + GetImageSize, UpdateSurface-style BGRA 512 -> BC3 256 with 9 levels, PNG/BMP/JPG
  encode+decode, TGA via ImageIO, 1-level cube -> 6 chains), decode cache write + hit.
  **65651 checks, 0 failures.**
- `texture_corpus`: every texture file in the given archives/directories/.scmap files: create
  decode (+ skip 1) validated (layout, full chain, a format `ToVulkanTexture` maps, BC multiple
  of 4), direct path vs full decode byte compare, UI decode (BC3/1 level/None + GetImageSize)
  for every textures/ui file and every non-DDS file; samples dumped to PNG.
- `contact_sheet`: lays decoded images on one PNG (checkerboard under alpha) to look at.
- `run_corpus.sh`: the corpus over FA gamedata/*.scd, FAF gamedata, FA maps, vault maps+mods.
- `survey.py` (python3 -I, stdlib): counts the texture files per container/format/mip state without decoding.

### Corpus results

`run_corpus.sh` (full output: `build/m2-textures-tests/corpus.txt`): **20831 files, 5.97 GB**
(FA gamedata 14338, FAF gamedata 4954, FA maps 312 incl. every .scmap's embedded DDS, vault
maps 717, vault mods 510), **0 failures**, 22 s wall on 8 threads.

| Input (count) | Create decode -> |
|---|---|
| DXT1/3/5 full chain, pow2 (7602) | BC1/2/3, direct path; full decode byte-identical for all 7602 (also with `FAF_IMAGECODEC_VERIFY=1` on units.scd: 3089, no warning) |
| DXT5 without levels (2961 pow2, 6882 npot multiple of 4 - UI, 48 npot) | BC3 full chain (npot: stretched to a multiple of 4) |
| DXT3 without levels (666 + 18), partial chains (58 DXT3, 3 DXT1) | BC2/BC1 full chain, file levels kept |
| DXT1/3/5 full chain, npot (447 + 3 + 280) | BC, file levels kept (D3DX11 re-encodes some) |
| DXT1/3/5 cubes, 1 level (51 + 2 + 14) / full (3 + 1 + 16) | BC cube, 6 full chains |
| A8R8G8B8 (1347, 78 of them cubes), X8R8G8B8 cubes (7) | B8G8R8A8 / B8G8R8X8 |
| R8G8B8 24-bit (36, incl. normal maps), A1R5G5B5 strategic icons (24), A4R4G4B4 (1) | B8G8R8A8 |
| L8 (1), A8 (2) | R8, A8 |
| PNG (291, 12 of them 16-bit), JPEG (3), BMP (4), TGA (1, FAF's reclaim-disabled cursor) | B8G8R8A8 via ImageIO |
| panel_bmp.dds (dwSize 24) | BC1 (failed before the header leniency) |

Every result: a format `vulkan::ToVulkanTexture` maps, layout consistent, a full mip chain, BC top
level a multiple of 4; `skipMipLevels = 1` valid for all; the UI decode (BC3, 1 level, None)
valid for all 11613 textures/ui and non-DDS files with `GetImageSize` matching.

Time (per file, single decode; threads contend): direct 0.04 ms, full decode 0.68 ms average
(slowest 50-85 ms: 2048x2048 DXT without levels, 1024 DXT5 cubes without levels, a big PNG
preview), UI decode 0.15 ms. Single thread: BC1 encode 93 Mpixel/s, BC3 45 Mpixel/s; a
1024x1024 DXT5 without levels (builds and encodes 10 levels) 10 ms - ImageCodec.h quotes
30-40 ms for D3DX11. Decode cache: env.scd + textures.scd, second run 205 cache hits at
0.36 ms instead of ~24 ms (cache files: ~1 MB per 1024x1024 BC3 texture).

### Samples looked at

Terrain strata of SCMP_016 (des_sanddark02 albedo level 0 and 2, des_rock01 normal map from
RGB24), UEL0101 albedo (level 0, 2) and SpecTeam (DXT3), envcube_desert01a faces 0 and 2,
SCMP_016.scmap's preview, terrain normal map (BC3), stratum mask, water map; UI: Aeon close
button (create and UI decode), attack cursor (A8R8G8B8, create and UI), FAF's reclaim-disabled
TGA cursor, A1R5G5B5 strategic icon, DXT3 strategic icons, 16-bit PNG faction icon. All correct
in colour and channel order; mip levels consistent with level 0. One defect found and fixed: the
UI (BC3) decode of the attack cursor came out brown (white RGB under alpha 0 pulling the colour
fit) -> alpha-weighted fits.

## 4. Not verified / open

- No texel-for-texel comparison with D3DX11 (no Windows machine here). Expected differences:
  BC encoder output for levels D3DX builds, stretch filter (area vs triangle), odd-size mip
  filter, B8G8R8A8 vs R8G8B8A8 for 32-bit DDS/images, BC blocks carried over where D3DX
  re-encodes. `FAF_IMAGECODEC_VERIFY` compares only the direct path with this decoder.
- Texture memory: same formats as Windows (BC stays BC).

## 5. Build, integration, requests

- No CMake change needed: the new `src/platform/posix/gal/Image*.cpp` are picked up by the
  `platform/posix/*.cpp` glob (CONFIGURE_DEPENDS: build/macos sees them at its next ninja run);
  ImageIO/CoreGraphics/CoreFoundation are already in `FAF_MACOS_FRAMEWORKS`.
- Own tree `build/m2-textures` (configured from macos/, Release): faf_replay built with this
  package's 7 TUs (an earlier attempt failed to link on other packages' in-progress
  `wxWindowBase::FafNative*` / `faf_posix::shell::IsMainThread`; the retry linked).
  `SPEED=50 OUT=build/m2-textures/replay-out FAF_REPLAY_EXE=build/m2-textures/faf_replay
  macos/run_replay.sh` -> `FAF_REPLAY_RESULT replay=27890640 beats=9716 mismatches=0
  exit=end-of-replay`. Nothing in the headless path calls `ImageCodec::Instance()` (only the
  Direct3D 12/Vulkan backends and the cvar bindings in GpuPathSettings.cpp use the codec), and
  the new TUs have no dynamic static initializers.
- `DeviceNull::UpdateSurface` (render-null) still logs "no image codec" when formats differ;
  it could now convert through `ImageCodec` (headless only, cosmetic).
- With `ImageCodec::Instance()` non-null, `DeviceVulkan::Setup` gets past its "no texture
  decoder" check.
- Requests for others: `macos/exclusions.d/render.cmake` still describes ImageCodecPosix.cpp as
  "no codec: Instance() is null"; `macos/porting/render.md`'s stand-in table and Milestone 2
  checklist row for textures likewise (text in the structured result).
- The cursor path (`WindowsCursor` + `DeviceVulkan`'s `IsRgbaTexture`) reads cursor texels in
  B,G,R,A order unless the texture is R8G8B8A8: cursors decode to B8G8R8A8, so the shell's
  cursor code can take them as D3D9 did.
