# Porting notes: package "render-null" (wave 2)

Scope: the headless renderer (null gal device + `REN_InitHeadless`), gdi32 (compat area
Gdi), audio-movie stub cleanup. Owns `src/gpg/gal/**`, `src/moho/render/**`,
`src/moho/mesh/**`, `src/moho/particles/**`, `src/moho/audio/**`, `src/moho/movie/**`,
`src/platform/posix/stubs/render/**`, `src/platform/posix/stubs/audio-movie/**`,
`macos/exclusions.d/render.cmake` and `audio-movie.cmake`, the `include/d3d*.h` stand-ins,
`compat/Gdi.h` and `WinCompatGdi.cpp`.

Nothing here touches sim math or sim state. The null device only serves the user-side
session code (terrain finalisation, texture sheets, effect loading); every engine edit
is `FAF_POSIX`-guarded and leaves the Windows preprocessed text unchanged.

## Status

- **All 172 TUs of the package compile** with `macos/tu.sh`: every
  `compile_commands.json` entry under the owned directories, plus the two new files
  (list: `build/render-null-tests/tus.txt`, results: `tu-results.txt`, logs: `tu-logs/`).
  This includes the two wave-1 failures:
  - `gpg/gal/backends/vulkan/DeviceVulkan.cpp` (needed `BITMAPFILEHEADER`, `BITMAPINFOHEADER`,
    `BI_RGB`);
  - `moho/render/d3d/CD3DDevice.cpp` (needed `AddFontMemResourceEx`).
- No change to `macos/exclusions.d/*.cmake`: the new files sit under
  `src/platform/posix/stubs/`, which `macos/CMakeLists.txt` globs into `faf_engine`.
  **The build must be reconfigured** (`cmake -S macos -B build/macos`) to pick them up.

## 1. gdi32: compat area Gdi

`src/platform/posix/compat/Gdi.h` and `src/platform/posix/WinCompatGdi.cpp`.

### Structures

The structures are the wingdi.h layouts:

- `BITMAPFILEHEADER`: pack 2, 14 bytes.
- `BITMAPCOREHEADER`: 12 bytes.
- `BITMAPINFOHEADER`: 40 bytes.
- `BITMAPV4HEADER`: 108 bytes.
- `BITMAPV5HEADER`: 124 bytes.
- `CIEXYZ` and `CIEXYZTRIPLE`.
- `RGBQUAD`.
- `BITMAPINFO`: 44 bytes.
- `BITMAP`: 32 bytes, the Windows x64 layout (bmBits at 24).
- `DIBSECTION`: 104 bytes.
- `LOGBRUSH`: 16 bytes.
- `LOGPEN`: 16 bytes.

`WinCompatGdi.cpp` checks every size and the key offsets with real `static_assert`s. It does
this under `#pragma push_macro("static_assert")` / `#undef`, because `LayoutAsserts.h` disables
the keyword.

### Constants

`BI_*`, `DIB_*`, `LCS_*`, `OBJ_*`, the stock object indices, `BS_*`/`PS_*`, `TRANSPARENT`/`OPAQUE`,
`CLR_INVALID`, `GDI_ERROR`, `HGDI_ERROR` and `Get[RGB]Value`.

The handle types (`HDC`, `HBITMAP`, `HBRUSH`, `HFONT`, `HPEN`, `HGDIOBJ`) were already in
`WinCompat.h`.

### Functions

Functions: `GetDC`, `ReleaseDC`, `CreateCompatibleDC`, `DeleteDC`, `SelectObject`,
`GetCurrentObject`, `SetTextColor`, `SetBkColor`, `SetBkMode`, `GdiFlush`, `GetStockObject`,
`DeleteObject`, `GetObjectW`/`GetObjectA`, `GetObjectType`, `CreateBitmap`,
`CreateCompatibleBitmap`, `CreateDIBSection`, `CreateSolidBrush`, `FillRect`,
`AddFontMemResourceEx` and `RemoveFontMemResourceEx`.

There is no neutral `GetObject` macro, as the Errors area rule requires: it would rename the
engine's `GetObject` methods.

**Behaviour** (real memory drawing, no display):

- **Handles.** Handles are heap objects held in registries, so a stale or foreign handle fails
  as gdi32 fails (`ERROR_INVALID_HANDLE` / `ERROR_INVALID_PARAMETER`) instead of crashing.
- **Device contexts.**
  - `GetDC(NULL)` returns a screen DC with no pixels.
  - `GetDC(hwnd)` fails with `ERROR_INVALID_WINDOW_HANDLE`, because no window exists (compat Window).
  - A memory DC starts with the 1x1 monochrome default bitmap, `WHITE_BRUSH`, `BLACK_PEN`
    and `SYSTEM_FONT`.
- **Bitmaps.**
  - DIB sections own zero-initialised memory with DWORD-aligned rows. `ppvBits` points to it.
  - The masks come from `BI_BITFIELDS` (after the header, or inside a V4/V5 header). For
    `BI_RGB` they are the GDI defaults: 5-5-5 at 16 bpp, 8-8-8 at 32 bpp.
  - DIB sections at 8 bpp or less keep their colour table.
  - Device-dependent bitmaps have WORD-aligned rows.
  - `CreateBitmap(0, 0, ...)` returns the stock 1x1 monochrome bitmap.
  - `hSection` (file mapping) is not supported: logged, `ERROR_NOT_SUPPORTED`.
- **GetObject.**
  - Bitmaps: `BITMAP`, or `DIBSECTION` when the buffer is big enough. With a null buffer it
    returns `sizeof(BITMAP)`. `bmBits` is set only for DIBs.
  - Brushes and pens: `LOGBRUSH` / `LOGPEN`, copying `min(count, size)` bytes.
  - Fonts and palettes: 0, with a stub log.
- **FillRect.**
  - Fills the bitmap selected into a memory DC, clipped, honouring bottom-up and top-down DIBs.
  - 1/4/8 bpp: the nearest colour-table entry. A monochrome DDB takes the nearer of black and white.
  - 16/32 bpp: packed through the masks. 32 bpp writes 0 to the top byte.
  - 24 bpp: BGR.
  - The null brush and the screen DC succeed without painting.
  - The system-colour brush form `(HBRUSH)(COLOR_x+1)` fails, because the area has no system colours.
- **DeleteObject.** On an object still selected into a DC it fails, as on Windows. On a stock
  object it succeeds and does nothing.
- **AddFontMemResourceEx.** A logged failure in Windows convention: it returns 0, sets
  `*pNumFonts = 0` and gives no extended error. `RemoveFontMemResourceEx` returns FALSE. No
  rasterizer exists until milestone 2 (Core Text: `CTFontManagerRegisterGraphicsFont`).

**Not provided:**

- text and fonts: `CreateFont*`, `DrawText`/`TextOut`, `GetTextMetrics`, `GetKerningPairs`;
- blits, regions and palettes.

Their only engine callers are `moho/render/d3d/CD3DFont.cpp` and
`gpg/gal/backends/common/WindowsCursor.cpp`, both replaced on POSIX (render.md). Milestone 2
should add them to this area if those TUs come back.

## 2. Null gal device and headless render start-up

### Files

| File | What |
|---|---|
| `src/platform/posix/stubs/render/gal/DeviceNull.h`, `DeviceNull.cpp` | `gpg::gal::DeviceNull` and its resources, plus `gpg::gal::null_device::{ReadImageInfo, ComputeLevelLayout, ScanEffectTechniques}` |
| `src/platform/posix/stubs/render/moho/HeadlessRenderPosix.cpp` | `moho::REN_InitHeadless` and `moho::REN_ShutdownHeadless`, the contract in `headless/HeadlessRender.h` |

### Guarded engine edits (Windows text unchanged)

| File | Change |
|---|---|
| `gpg/gal/DeviceContext.hpp` | `DeviceApi::Null = 6` under `FAF_POSIX`. `UsesD3D10EffectDialect` also answers true for `Null` (the `#else` keeps the original). |
| `gpg/gal/Device.cpp` | Includes `DeviceNull.h` in the existing POSIX include branch. `Device::Create` gets `case DeviceApi::Null` as the `#else` of the existing `#if !defined(FAF_POSIX)` around Direct3D 12; it uses the same install-then-Setup-with-rollback pattern as Vulkan. `SupportsVertexTextureFormat` gets a guarded `Null` case. |
| `gpg/gal/MeshVertex.cpp` | `GetHardwareVertexFormatter`: a guarded `case DeviceApi::Null`, which gets the D3D10 formatters (as Vulkan). |

A separate enumerator was needed. Reporting `Vulkan` would make
`ResourcesVulkan.cpp:30` and `GpuDrivenVulkan.cpp:47` `static_cast` the null device to
`DeviceVulkan`.

### DeviceNull (D3D10 effect dialect, as the Vulkan backend it stands in for)

**Caps.** They are `DeviceVulkan::CheckAvailableFormats`':

- `mMaxPrimitiveCount` 0x10000;
- `mMaxVertexCount` 0xFFFF;
- hardware instancing on;
- VS/PS profile 4/8;
- `validFormats1` = render target formats 1..7;
- `validFormats2` = every texture format with a DXGI mapping (2..18).

`SetRenViewport` therefore sets `graphics_FidelitySupported = 2` and
`shadow_FidelitySupported = 3`. A head of size 0 becomes 1024x768. There are no adapter modes.

**Head targets.** Each head gets a back buffer (`RenderTargetNull`, gal format 2, head size)
and a depth-stencil target (format 3). The first is bound and the viewport is set to it.
`Reset` rebuilds them at the requested sizes. `TestCooperativeLevel` is 0 (never lost).

**Textures** (`TextureNull`) are CPU-backed.

- **Layout.** Per mip level of the first face or slice: the DXGI row pitch from format and
  size, as the D3D11 and Vulkan backends use it. BC formats count 4x4 blocks. Formats with no
  DXGI mapping get 4 bytes per texel; gal 1, `D3DFMT_R8G8B8`, gets 3.
- **Contents.** Levels are allocated as zeros on first lock and persist.
- **Locking.**
  - `Lock` with a rectangle offsets to its corner (in blocks for BC formats), as the D3D11
    and Vulkan backends do.
  - These throw the backends' gal errors: double lock, unlock mismatch, level out of
    range, and locking a volume texture.
- **Engine textures** (`source_ == 2`) take format, size and mips from the context
  (mips 0 means 1, as Vulkan).
- **File textures** (`source_ == 1`)
  - They take their shape from the file's header, with no pixel decode:
    - **DDS**: legacy fourCC (DXT1-5, ATI1/2, BC4/5, the D3DFMT float codes), RGB,
      luminance, alpha and bump masks, DX10 headers, cube and volume.
    - **PNG**: IHDR.
    - **JPEG**: SOFn.
    - **BMP**: core and info headers.
    - **TGA**.
  - Non-DDS files come back as B8G8R8A8 with a full mip chain, as D3DX loads them.
  - The resource's skip-mip setting (`reserved0x44_`) drops top levels, like
    `D3DX_SKIP_DDS_MIP_LEVELS`.
  - **Context update.** The context's width, height, mips, format and type are updated as
    `TextureVulkan` updates them, and the file bytes are released.
  - **Failure.** An unrecognised header throws "unable to load texture ..."; the caller
    (`RD3DTextureResource::LoadTexture`) warns "Unable to load texture", as it does for a
    failed decode on Windows.
- `UpdateSurface` copies the top level's CPU texels between same-format textures (rectangles
  honoured, BC by blocks). Across formats it is a logged no-op, since there is no codec.
- `SaveTexture`, `SaveToBuffer`, `SaveRenderTarget` and `SaveCubeRenderTarget` produce
  nothing (an empty buffer or no file, logged once). `GetRenderTargetData` and `StretchRect`
  leave the destination alone, because nothing was rendered.
- `GetTexture2D` (batch textures) returns the file's size and zeroed BC3 blocks padded to
  whole blocks, as D3D9 and Vulkan do. An unknown header gives a warning, size 0 and an empty
  buffer, so `SBatchTextureDataFactory` returns null.

**Buffers.** `VertexBufferNull` and `IndexBufferNull` lock the same `backend::LockShadow`
CPU copy the D3D11, D3D12 and Vulkan backends hand out, with their lock rules: mismatch and
offset-out-of-range throw, and lock-past-end is reported by LockShadow. Contents persist
under every flag.

**Vertex formats and pipeline.** `VertexFormatNull` takes per-stream strides from the D3D11
layouts, as `VertexFormatVulkan` does. Invalid codes throw. `PipelineStateNull` is empty.

**Effects.** `EffectNull` accepts every technique and variable.

- **Technique scan.** `GetTechniques` lists the techniques the source declares, with their
  annotations and pass counts. These come from `null_device::ScanEffectTechniques`:
  - comments are stripped;
  - `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif` are honoured, with a C constant evaluator
    and `defined`;
  - object-like `#define`s are honoured; function-like ones are recorded as defined and never
    expanded;
  - techniques are taken at brace depth 0, including the `#define technique technique10`
    alias;
  - annotations are read as string, int, float or bool, with values macro-expanded and
    evaluated (for example `int fidelity = FIDELITY_HIGH`, `int renderStage = STAGE_A + STAGE_B`).
- **Other names.** `GetTechnique` of an undeclared name returns a one-pass technique with no
  annotations, and `GetVariable` returns a variable whose setters do nothing. Real backends
  throw "invalid effect technique/variable requested" here.
- **Context.** The effect drops its source buffer, as `ProgramEffect` does.

So `CD3DEffect::InitEffectFromFile` builds the same technique / fidelity tables it builds on
a real device. `CD3DDeviceResources::DevResInitResources` never dies for lack of a compiler.
Nothing is written to the shader cache.

**Everything else is a no-op.** This covers draws, clears, presents, scene begin/end, state
setters, cursor and fog. `ShowCursor` returns the previous state. `GetRuntimeInfo` reports
api "Null".

### REN_InitHeadless / REN_ShutdownHeadless

`REN_InitHeadless` does what `CScApp::CreateAppFrame` does to the device:

```
D3D_GetDevice()->Clear2(true);
Device::Create(context);
D3D_GetDevice()->SetRenViewport(ren_Viewport);
D3D_GetDevice()->Clear2(false);
```

The context has one windowed head of `wnd_DefaultCreateWidth` x `Height` (1024x768),
`mValidate` false and vsync off.

**SetRenViewport.** This call leaves `D3D_GetDevice()` in the windowed start-up state:

- the head writer locks;
- the fidelity support levels;
- `InitResources(true)`, which loads every `/effects/*.fx` as a null effect (a missing
  effect file `gpg::Die`s there, as on Windows);
- the device-init broadcast.

**Return values.**

- If a device already exists it returns false.
- Gal and standard exceptions are caught, logged and rolled back, and it returns false.
- A second call after success returns true.

**The viewport.** If `moho::ren_Viewport` is already set when it runs, that viewport is bound
and gets `D3DWindowOnDeviceInit(true)`, as in `CreateAppFrame`, which creates the viewport
before the device. Otherwise none is bound, so `CD3DDevice::Paint` never renders.

**REN_ShutdownHeadless** is `CScApp::Destroy`'s `D3D_Exit()`: the index-sheet and particle
singletons, then `CD3DDevice::Destroy`. It is a no-op unless init succeeded.

### Replay session flow: device calls covered

Every call from the user side reaches the device through `gal::Device` virtuals, which
`DeviceNull` implements in full, or through resource virtuals. A grep found no other downcast
in compiled POSIX code: `HardwareMeshBatch`'s D3D9 cast and `Shadow`'s D3D9 dump are already
compiled out.

| Code | What it does on the null device |
|---|---|
| `CWldSession::DoBeat` -> `CWldTerrainRes::NotifyMapChange` -> `Finalize` | Three `NewDynamicTextureSheet` calls (formats 2, 2, 12), then `CD3DDevice::UpdateSurface` (a real CPU copy). Then `InitNormalMap` (format 12 sheets), `EnterEditMode`/`ExitEditMode` (`Lock`/`LockRect` and writes on dynamic sheets). |
| `CWldTerrainRes::Load` and `SetBackground`/`SetSkycube`/`AddEnvLookup` | `GetTexture` / `GetTextureSheet` -> `RD3DTextureResource::LoadTexture` -> `Texture::Create` from file data. The header gives the dimensions, which UI and terrain code read. |
| `CD3DDevice::SetRenViewport` | `GetHeadOutputContext`, `CD3DDeviceResources::InitResources(true)` (`Effect::Create` x 13, `GetTechniques`, annotations). |
| `CD3DDeviceResources` sheets, render targets, depth-stencils | Created through `Texture`/`RenderTarget`/`DepthStencilTarget::Create`; recreated on device events. |
| `ParticleBuffer`, index/vertex sheets | Buffer creation and `Lock`/`Unlock`. |
| `SBatchTextureDataFactory` (batch_textures prefetch) | `GetTexture2D`. |
| `RWldMapPreviewChunk::Save` | `SaveTexture`: 0 bytes. |
| `MeshVertex::GetHardwareVertexFormatter` | `CreateVertexFormat` (D3D10 formats). |

Resource creation keeps no shared mutable state, so `Texture::Create` from the sim thread
(beam and particle textures through Lua) is safe.

## 3. audio-movie: FAF_POSIX_STUB

- `stubs/audio-movie/AudioMovieStub.h` is deleted. The five stubs (`X3DAudioPosix`,
  `XactClockPosix`, `FfmpegSfdDecoderPosix`, `DirectSoundMovieSinkPosix`, `DirectSoundPosix`)
  now use the shared `FAF_POSIX_STUB` from compat/Errors.h.
- `moho/audio/AudioEngine.cpp`'s POSIX `/nosound` notice changed from
  `gpg::Logf("FAF_POSIX_STUB: ...")` to `FAF_POSIX_STUB(...)`; it stays inside its existing
  `FAF_POSIX` block.
- The stub lines now go to stderr (`[faf-posix] stub: ...`) instead of the engine log.

## Tests

- **Compile.** All 172 package TUs with `macos/tu.sh`, 172 OK (section Status).
- **Standalone test.** `build/render-null-tests/null_device_test.cpp`, built and run by
  `python3 -I build/render-null-tests/build.py`.
  - **Build.** It recompiles `DeviceNull.cpp`, `Device.cpp`, `MeshVertex.cpp`,
    `WinCompatGdi.cpp`, the test and `test_stubs.cpp` (log functions) with the engine's
    command lines. It links them with `-dead_strip` against 14 current engine objects from
    `build/macos` (read only). The two unreached Vulkan cases of `Device::Create` /
    `SupportsVertexTextureFormat` are bound to an abort with `-alias`.
  - **Inputs.** It extracts the game's effect sources read-only from
    `~/.faforever/gamedata/effects.nx2` and writes sample images with `sips`.
  - **What it checks.**
    - **Image headers:** synthetic DDS (DXT5/DXT1/DXT3 cube/DX10 cube/RGB32), real BMP, PNG,
      JPEG and TGA files from sips at 37x21 and 1000x600, and garbage input.
    - **The effect scanner** on a synthetic source (conditionals, comments, aliases,
      annotation kinds, pass counts), and on all 11 shipped `.fx` files merged with
      `d3d10states.compat`. The mesh fidelity annotations resolve through `FIDELITY_*`, and
      `TTerrain` has `usage = "composite"`.
    - **The device through `gal::Device::Create(Null)`:**
      - caps and head targets;
      - texture lock pitch (BGRA and BC3), persistence, rect offsets and lock-error throws;
      - `UpdateSurface`;
      - file textures with skip-mip;
      - `GetTexture2D`, `SaveTexture`;
      - vertex and index buffer locking and lock rules;
      - every vertex format code, and `GetHardwareVertexFormatter`;
      - effects and annotations;
      - `Reset`, render-target viewport, `DestroyInstance`.
    - **gdi32:**
      - 32 bpp top-down and 24 bpp bottom-up DIBs, 16 bpp 565 bitfields and a 1 bpp colour
        table, through `FillRect`;
      - `GetObject` `DIBSECTION`/`BITMAP`/`LOGBRUSH`;
      - delete-while-selected;
      - DDBs and the stock 1x1 monochrome bitmap;
      - stock objects;
      - DC state;
      - font resources;
      - handle validation.
  - **Result: 170 checks, 0 failures.**
    - **Shipped effects.** The scan finds exactly the technique and pass lines of every
      shipped effect:

      | Effect | Techniques | Passes |
      |---|---|---|
      | frame | 18 | 18 |
      | particle | 68 | 68 |
      | mesh | 180 | 234 |
      | vision | 3 | 5 |
      | range | 1 | 1 |
      | water2 | 5 | 7 |
      | sky | 4 | 5 |
      | primbatcher | 14 | 15 |
      | ui | 6 | 6 |
      | terrain | 68 | 68 |
      | cartographic | 4 | 4 |

      For mesh, 140 techniques carry a `fidelity` annotation, and water2 has 3 such
      techniques.
    - **Vertex formats.** 23 of the 24 vertex format codes have a layout.
    - The output is in `build/render-null-tests/build-out.txt`.
- **Other packages' TUs.** These still compile with the new Gdi declarations, which reach
  every TU through `WinCompat.h`: `BitArray2D`, `COGrid`, `Cluster` (`FillRect` members),
  `Reflection` (`GetObject`), `WxRuntimeTypes`, `CScApp`, `CWldMap`, `UiRuntimeTypes`,
  `CrashReport`, `WinAppPosix`, `WinCompatWindow`.
- **Name collisions.** A search of `src/` for the new macro and type names finds only
  Windows-only GDI users.

## Open issues / notes for others

- **runner (HeadlessApp.cpp).**
  - `CreateHeadlessFrame` creates the game viewport *after* `REN_InitHeadless`, so the
    device never binds it. `D3DWindowOnDeviceInit(true)` therefore never runs for it: no
    batchers, range/vision/boundary renderer init, viewport dynamic sheet or head targets.
    More importantly, `uiManager->OnResize(head, w, h)` is never called for the UI heads.
  - Either create the viewport first and then call `REN_InitHeadless`, which binds
    `ren_Viewport` (the `CreateAppFrame` order), or call
    `moho::D3D_GetDevice()->SetRenViewport(viewport)` after creating it. The second option
    reruns `InitResources(true)`: about 13 effect scans, cheap.
  - The UI's world views dereference `ren_Viewport` unconditionally:
    `UiRuntimeTypes.cpp:18619, 18708, 20824/20827` and `MeshThumbnailRenderer.cpp:252`. A
    `/noui` run must not create them.
- **Lead.** The new stub files are picked up by the `CONFIGURE_DEPENDS` glob of
  `src/platform/posix/*.cpp`: the next ninja run re-globs, or run
  `cmake -S macos -B build/macos`. No exclusion changes are needed.
- **Removed file.** `stubs/audio-movie/AudioMovieStub.h` is gone. Nothing else included it.
- **Milestone 2.**
  - gdi32 text/fonts if `CD3DFont.cpp` returns (Core Text).
  - A real image decoder: then `DeviceNull` could decode file textures, though nothing in
    milestone 1 reads them.
- The test lives in `build/render-null-tests/`, which is not tracked. If the lead wants it
  kept, move `null_device_test.cpp`, `test_stubs.cpp` and `build.py` to a tracked test
  location.
