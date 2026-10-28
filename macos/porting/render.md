# Porting notes: package "render"

Scope: `src/gpg/gal/**` (the Direct3D 9/10/11/12 and DXGI backends are excluded by
`macos/exclusions.cmake`), `src/moho/render/**`, `src/moho/mesh/**`, `src/moho/particles/**`,
the D3D/DXGI stand-in headers in `src/platform/posix/include/`, `macos/exclusions.d/render.cmake`
and `src/platform/posix/stubs/render/`.

Rendering itself is milestone 2 (Vulkan on MoltenVK). This wave only makes the layer
compile, keeps every symbol other packages use defined, and maps what a headless replay
run touches.

## Status

113 of the 127 package TUs that `src/sources.cmake` lists and the macOS build keeps
(133 minus the 6 excluded below), and all 11 new files in `stubs/render/`, compile with
`macos/tu.sh`. The other 14 are blocked outside this package:

| TU | Blocked on |
|---|---|
| `moho/render/d3d/CD3DDevice.cpp`, `moho/mesh/MeshThumbnailRenderer.cpp` | wx headers, through `moho/render/WRenViewport.h` -> `moho/render/d3d/WD3DViewport.h` (`WD3DViewport` derives from `wxWindow`) |
| `moho/render/MapImager.cpp`, `moho/render/RangeRenderer.cpp` | wx, through `moho/app/WxRuntimeTypes.h` |
| `moho/render/SelectionBracketParams.cpp`, `moho/render/SelectionBracketRenderer.cpp`, `moho/render/d3d/CD3DPrimBatcher.cpp` | wx, through `moho/ui/UiRuntimeTypes.h` |
| `gpg/gal/Device.cpp` | compat Window: `GetForegroundWindow` |
| `gpg/gal/RenderProfiler.cpp`, `moho/particles/ParticleRenderBuckets.cpp` | CRT: `getenv_s` |
| `gpg/gal/backends/common/LockShadow.cpp` | compat Modules/Misc: `RtlCaptureStackBackTrace`, `GetModuleHandleExA` + `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS`/`_UNCHANGED_REFCOUNT`, `GetModuleFileNameA`, `IsDebuggerPresent`, `DebugBreak` |
| `gpg/gal/backends/vulkan/DeviceVulkan.cpp` | compat Misc: `GetCommandLineA`; Window: `GetClientRect`, `BITMAPFILEHEADER`, `BITMAPINFOHEADER`, `BI_RGB` (diagnostic BMP writer) |
| `gpg/gal/effects/EffectRuntime.cpp` | shell (`shlobj.h`): `SHGetFolderPathA` + `CSIDL_LOCAL_APPDATA` + `SHGFP_TYPE_CURRENT` (and `SHCreateDirectoryExA` behind it) - the shader cache directory |
| `moho/render/RenderInfo.cpp` | Memory (`psapi.h`): `PROCESS_MEMORY_COUNTERS(_EX)`, `GetProcessMemoryInfo` |

All of these are generic Win32/CRT calls (most used by other packages too); they belong to
the compat areas (README rule 5), so they are lead requests, not local workarounds. The
compat areas were filling in while this ran (Time and Files landed and cleared
`CD3DEffectTechnique.cpp`, `CD3DDeviceResources.cpp` and half of `EffectRuntime.cpp` /
`RenderInfo.cpp`), so re-run the census before acting on this table.

## Stand-in headers (`src/platform/posix/include/`)

Types and constants only, SDK values, no COM interfaces, no functions.

- `d3d9types.h` (existing): added `_D3DSAMPLERSTATETYPE` and `_D3DTEXTURESTAGESTATETYPE`
  (the key types of `gal::StateCache`, which `StateCache.cpp` instantiates).
- `dxgiformat.h` (new): the full `DXGI_FORMAT` enumeration. The values matter: a DDS file's
  DX10 header stores them.
- `d3d11.h` (new): `D3D11_INPUT_ELEMENT_DESC`, `D3D11_INPUT_CLASSIFICATION`,
  `D3D11_APPEND_ALIGNED_ELEMENT` - the Vulkan backend reads the gal vertex layouts in that
  form (`backends/d3d11/D3D11VertexLayouts`). Code needing the real D3D11 API stays behind
  `!defined(FAF_POSIX)`.
- `d3dcommon.h` (new): `D3D_SHADER_MACRO`.

## Excluded TUs (`macos/exclusions.d/render.cmake`) and what replaces them

| Excluded | Replacement in `stubs/render/` | Behaviour |
|---|---|---|
| `gpg/gal/effects/ShaderCompiler.cpp` (DXC via WRL, `ID3D12ShaderReflection`) | `gal/ShaderCompilerPosix.cpp` | Milestone 2: the same front end on libdxcompiler.dylib through the DXC bridge; SPIR-V only, DXIL reported unavailable (shaders.md) |
| `gpg/gal/backends/common/ImageCodec.cpp` (D3DX11 on a WARP D3D11 device) | `gal/ImageCodecPosix.cpp` | Milestone 2: the POSIX codec (DDS, ImageIO, BC1-BC5, the DDS direct path and decode cache; textures.md) |
| `gpg/gal/backends/common/WindowsCursor.cpp` (GDI DIB + user32 cursor) | `gal/WindowsCursorPosix.cpp` | Milestone 2: real colour cursors through the shell (shell.md) |
| `gpg/gal/backends/vulkan/VulkanSurface.cpp` (`VK_KHR_win32_surface`) | `gal/VulkanSurfacePosix.cpp` | Milestone 2: `VK_EXT_metal_surface` on the window's CAMetalLayer (vulkan.md) |
| `gpg/gal/DxgiAdapterInfo.cpp` (DXGI 1.4 adapter/memory query) | none | only the excluded D3D9/11/12 backends call it |
| `moho/render/d3d/CD3DFont.cpp` (GDI glyph rasterizer) | `moho/CD3DFontPosix.cpp` | Milestone 2: includes the engine's own CD3DFont.cpp unchanged, on the Gdi area's Core Text fonts (fonts.md) |

Every stub logs once with `FAF_POSIX_STUB` (compat/Errors.h).

### Other new files in `stubs/render/gal/`

- `DxgiFormats.cpp`, `D3D11VertexLayouts.cpp`, `EffectSourceD3D11.cpp`: one-line wrappers
  that `#include` the renderer-neutral `.cpp` of the same name from the excluded
  `backends/dxgi` and `backends/d3d11` directories (pure tables and text translation, no D3D
  API; they compile unchanged on POSIX against the stand-ins). The Vulkan backend needs them.
  **If the lead narrows the base exclusion so they build in place, delete these wrappers.**
- `HardwareVertexFormattersD3D10Posix.cpp`: the Direct3D 10 hardware vertex formatters
  (`HardwareVertexFormatterD3D10`, `Float16HardwareVertexFormatterD3D10`), which
  `gal/MeshVertex.cpp` hands to every API but D3D9 (Vulkan included). Same bodies as in
  `backends/d3d10/D3D10Interfaces.cpp`; `D3DXFloat32To16Array` replaced by a portable
  round-to-nearest-even float->half (`FAF_POSIX_TODO(milestone 2)`: d3dx9's overflow/NaN
  behaviour unverified; render only).
- `D3DXMathPosix.cpp`: the extern "C" D3DX 9 matrix functions `gal/Matrix.cpp` and
  `moho/render/Shadow.cpp` declare (`D3DXMatrixMultiply/Inverse/Translation/Scaling/
  RotationX/Y/Z/RotationAxis/RotationQuaternion/LookAtRH`), from the documented formulas.
  Callers are render/effects only (`gal::Math` via `VMatrix4::Load*`/`VEC_Mul`, Shadow,
  CEfxEmitter, CWldTerrainDecal); a grep finds no sim caller of those VMatrix4 helpers, so
  bit-exactness with d3dx9 is not required (`FAF_POSIX_TODO(determinism)` in the file in
  case one appears).
- `GalBackendSharedPosix.cpp`: renderer-neutral definitions that live in excluded backend
  TUs on Windows: the base ctors/dtors of `EffectTechnique`, `IndexBuffer`, `RenderTarget`,
  `CubeRenderTarget`, `PipelineState` (in `D3D9Interfaces.cpp`), and `gPlacedResources` /
  `gPipelineDiskCache` (in the D3D12 backend; bound by console variables in
  `moho/render/GpuPathSettings.cpp`).

## Guarded edits to engine files (`#if defined(FAF_POSIX)`; Windows code unchanged)

- `gpg/gal/Device.cpp`: POSIX includes only the Vulkan backend; the D3D9/10/11/12 cases of
  `Device::Create` and `SupportsVertexTextureFormat` are compiled out, so asking for them
  throws the existing "unknown API requested" (`Device.cpp` line 135).
- `gpg/gal/MeshVertex.cpp`: no D3D9 formatter objects or D3D9 case on POSIX.
- `gpg/gal/backends/vulkan/VulkanLoader.cpp`: `dlopen` of `$FAF_VULKAN_LIBRARY`, else
  `libvulkan.1.dylib`, `libvulkan.dylib`, `libMoltenVK.dylib`, `libvulkan.so.1`;
  `dlsym`/`dlclose`.
- `gpg/gal/backends/vulkan/DeviceVulkan.cpp`: no `<d3dcompiler.h>`/`<wrl/client.h>`;
  `CreateEffect` has no HLSL preprocessor (`D3DPreprocess`) and throws its usual
  "unable to preprocess effect" error with a stub log. The Windows path only gained a named
  `std::string_view` for the preprocessed text.
- `moho/mesh/HardwareMeshBatch.cpp`: D3D9 backend includes out; the
  `EffectTechniqueD3D9::CommitChanges` fast path is compiled out (only a D3D9 device leaves a
  pass open, and there is none on POSIX).
- `moho/render/Shadow.cpp`: the `FAF_TOGGLE_DIR` shadow-map dump (D3DXSaveSurfaceToFile
  through `RenderTargetD3D9`) is compiled out.
- Include-only guards (D3D9 backend headers / `<d3d9.h>` replaced by the gal interface
  headers they were used for): `moho/mesh/Mesh.cpp`, `moho/render/CRenFrame.cpp`,
  `moho/render/Cartographic.cpp`, `moho/render/d3d/CD3DDevice.cpp`,
  `CD3DDeviceResources.cpp`, `CD3DIndexSheet.cpp`, `CD3DVertexStream.cpp`,
  `RD3DTextureResource.cpp`, `ShaderVar.cpp`, `moho/render/textures/CD3DDynamicTextureSheet.cpp`.

Nothing here touches sim math. No Win32 type sizes changed in render code.

## Headless replay: what the game calls into gal/render, and a null device

### How the device comes up today

`CScApp::AppInit` (moho/app/CScApp.cpp) runs `REN_Init` (fonts via `AddFontMemResourceEx`),
`UI_Init`, then `CScApp::CreateDevice`, which builds a `gal::DeviceContext` and tries the
renderers in order through `CreateAppFrame`: wx frame + `REN_CreateGameViewport`
(`WD3DViewport`, a wxWindow), `D3D_GetDevice()->Clear2`, `gal::Device::Create(&context)`,
`CD3DDevice::SetRenViewport` (-> `InitContext`: device resources, effect loading),
then `ConfigureStartupGraphicsOptionGroups`, `SetupBasicMovieManager`, `OPTIONS_Apply`.
If no renderer comes up, `AppInit` returns false and the app exits. Per frame,
`CScApp::Main` calls `WLD_Frame`, `CAM_GetManager()->Frame`, `sUserStage->UserFrame`,
`REN_Frame` and `D3D_GetDevice()->Refresh()` (present).

`moho::D3D_GetDevice()` is a function-local static `CD3DDevice`: never null, with or
without a gal device. `gal::Device::GetInstance()` is null until `Device::Create` succeeds.

### Calls that are safe with no gal device

- Texture lookups by path (`D3D_GetDevice()->GetResources()->GetTexture`, `GetTextureSheet`):
  `RD3DTextureResource::LoadTexture` checks `GetGalDevice()` and only warns
  "Unable to load texture" (one warning per texture). Sim-thread callers: the beam
  texture in `moho/unit/core/UnitWeapon.cpp` (Lua), `CAnimTexture`, `CParticleTexture`;
  user side: `CWldTerrainRes::Load` (utility/stratum/water-map sheets), `ScriptedDecal`,
  `CWldSession` order-line textures. None feeds sim state (texture dimensions are only read
  by UI/movie/terrain-render code).
- `CWorldParticles::AdvancementBeat`/`AddParticles`, `CDecalManager::AddDecals/RemoveDecals`,
  camera shake/follow, `MeshRenderer::UpdateMapSize`: CPU only. GPU buffers are made at
  render time (`ParticleBuffer::Initialize` returns false without a device).
- `CD3DFont::Create` callers in sim/UI code check for an empty handle (except SimDriver's
  net-stats panel, which only draws with a renderer).

### Calls that dereference the gal device unconditionally

- `gal::Texture::Create`, `RenderTarget::Create`, `DepthStencilTarget::Create`,
  `IndexBuffer::Create`, `VertexBuffer::Create`, `VertexFormat::Create`
  (gal/ContextInterfaces.cpp: `Device::GetInstance()->Create...`).
- `CD3DDevice::UpdateSurface` and most of `CD3DDevice` (`GetInstance()` without
  `IsReady()`), `CD3DDynamicTextureSheet::CreateTexture`.
- **`CWldTerrainRes::Finalize`** (moho/sim/CWldMap.cpp): three dynamic texture sheets
  (`NewDynamicTextureSheet` -> `gal::Texture::Create`), `UpdateSurface`, `InitNormalMap`.
  It runs from `CWldTerrainRes::NotifyMapChange`, which **`CWldSession::DoBeat` calls for
  every playable-rect update the sim syncs** - i.e. on the user side of every replay, in
  the first beats. `WLD_Teardown` also drains the remaining sync packets through `DoBeat`.
- Everything under `REN_Frame`, `CRenFrame`, terrain/water/mesh/particle rendering,
  `UICommandGraph` (lazily created by render code), `CWldTerrainRes::UpdateWaveSystem`
  (only when a "WorldCamera" exists).

### Session flow of a replay (`/replay`): `CScApp::InitializeSessionFromCommandLine` ->
`VCR_SetupReplaySession`, then each frame `WLD_Frame`:
`WLD_DoLoading` (`CWldSessionLoaderImpl::LoadGameData` -> `CWldMap::MapLoad` ->
`CWldTerrainRes::Load` [null-safe texture loads], `WLD_CreateSession` -> `CWldSession` ctor
[CPU only; `MeshRenderer::UpdateMapSize`], `SIM_CreateDriver`), `WLD_DoInitializing`
(`DoBeat` on the first sync packet, `UI_StartGameUI`, `CreateGameInterface`), then
`WLD_DoPlayingAction`: `simDriver->Dispatch()`, `SetGeomCams(CAM_GetAllCameras())`,
`CWldSession::SessionFrame` -> `DoBeat` per sync packet -> `NotifyMapChange` ->
`CWldTerrainRes::Finalize` (device needed). The sim itself (SimDriver threads, the
checksums) never needs the gal device; only the user-side session consuming its sync
packets does.

### Recommendation

A **null `gal::Device`** (`gpg::gal::DeviceNull`, render package, POSIX build first) is
the least invasive way to run the unchanged user-side flow headless, the way the Windows
parity runner runs with a hidden window:

- `Device::Create` accepts it (a POSIX-only `DeviceApi` value or a `/nullrenderer`
  switch the headless driver sets); `GetDeviceContext()` returns the context with one
  windowless head.
- Resources are CPU-backed: `Texture`/`RenderTarget`/`DepthStencilTarget` with a context
  and a byte buffer sized from format x size for `Lock`/`Unlock` (terrain stratum masks,
  water map, normal map are written through locks), `VertexBuffer`/`IndexBuffer` returning
  CPU memory from `Lock`, a dummy `VertexFormat`, `CreateEffect` returning an effect whose
  techniques/variables accept every call (CD3DDeviceResources loads every `.fx` at device
  init), `SaveTexture`/`GetRenderTargetData` empty.
- Draw/Clear/Present/state calls are no-ops; caps queries return the D3D10-dialect
  defaults the Vulkan backend reports.

The alternative - a headless driver that skips `CScApp` and guards
`CWldTerrainRes::Finalize`/`NotifyMapChange` and the rendering entry points with
`Device::IsReady()` - touches sim-game/app files in many places and diverges from what the
Windows runner executes. Either way the headless driver must not call `CScApp::CreateDevice`
(wx frames) and should skip `REN_Init`, `REN_Frame`, `CD3DDevice::Refresh` and
`UI_Init`'s rendering parts. Lead decision; the render package can implement `DeviceNull`
next.

## Milestone 2 (Vulkan on MoltenVK) checklist

Done (macos/porting/bringup.md: the game runs in a window on MoltenVK and KosmicKrisp,
replay 27890640 drawn with its UI, 0 checksum mismatches). The list below is the wave-1 plan.

- Window/surface: SDL3 window + `CAMetalLayer`; `VulkanSurfacePosix.cpp` creates the
  `VK_EXT_metal_surface` surface (or `SDL_Vulkan_CreateSurface`). `WD3DViewport`/`WRenViewport`
  are wxWindows today (wx package decision).
- Instance/device on MoltenVK: `VK_KHR_portability_enumeration` +
  `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`, device extension
  `VK_KHR_portability_subset`; `DeviceVulkan::CreateInstance` requires a 1.3 loader and uses
  dynamic rendering - check MoltenVK's level.
- Shaders: `ShaderCompilerPosix.cpp` -> load `libdxcompiler.dylib` (Vulkan SDK) with
  `dlopen` and DXC's portable headers (`dxcapi.h` + `WinAdapter.h`); `DeviceVulkan::CreateEffect`
  needs a preprocessor in place of `D3DPreprocess` (DXC `-P`); vertex-input reflection from
  SPIR-V (SpirvReflect) instead of `ID3D12ShaderReflection`. `EffectRuntime.cpp` shader cache
  needs the compat Files/shell calls above.
- Textures: `ImageCodecPosix.cpp` -> a decoder producing D3DX11's texels: reuse
  `ImageCodec.cpp`'s DDS direct path (BC1-3 with full mips, pure C++), plus DDS without mips /
  other formats, TGA/PNG/BMP/JPG, mip generation + BC re-compression (DirectXTex portable
  parts, stb_image), verified against D3DX11 output; `Encode` for screenshots/cartographic.
- Fonts: `CD3DFontPosix.cpp` -> Core Text (or FreeType) rasterizer feeding the same glyph
  page cache; `REN_Init`'s `AddFontMemResourceEx` -> `CTFontManagerRegisterGraphicsFont`.
- Cursor: `WindowsCursorPosix.cpp` -> SDL3 colour cursor.
- `DeviceVulkan.cpp` diagnostics: `WriteBmp` (BITMAP* structs), `GetClientRect` for head size.

## Lead requests

1. `macos/tu.sh`: a file not yet in `compile_commands.json` fails to compile because the
   borrowed command's file path is replaced by `${1:A}` (symlinks resolved:
   `/Volumes/SSD Programs/...`) unquoted, so the space splits it. Substitute inside the
   `shlex.split` argument list instead of the command string.
2. Narrow the base exclusion `^gpg/gal/backends/(d3d9|d3d10|d3d11|d3d12|dxgi)/` so
   `gpg/gal/backends/dxgi/DxgiFormats.cpp`, `gpg/gal/backends/d3d11/D3D11VertexLayouts.cpp`
   and `gpg/gal/backends/d3d11/EffectSourceD3D11.cpp` build in place (renderer-neutral; the
   Vulkan backend needs them), then delete the three wrappers in `stubs/render/gal/`.
3. Compat, by area (callers in the Status table; Files and Time already landed):
   CRT (Strings or Misc) `getenv_s`; Misc `GetCommandLineA`; Window `GetForegroundWindow`,
   `GetClientRect`, `BITMAPFILEHEADER`/`BITMAPINFOHEADER`/`BI_RGB`, and
   `AddFontMemResourceEx` (needed by `REN_Init` in CD3DDevice.cpp once wx unblocks it);
   shell (`shlobj.h`) `SHGetFolderPathA` + `CSIDL_LOCAL_APPDATA` + `SHGFP_TYPE_CURRENT`
   (suggest `~/Library/Application Support`); Memory (`psapi.h`)
   `PROCESS_MEMORY_COUNTERS(_EX)`, `GetProcessMemoryInfo`; Modules/Misc (diagnostics)
   `RtlCaptureStackBackTrace`, `GetModuleHandleExA` + `GET_MODULE_HANDLE_EX_FLAG_*`,
   `GetModuleFileNameA`, `IsDebuggerPresent`, `DebugBreak`.
4. wx: 7 render TUs are blocked on wx headers (Status table); `WD3DViewport` derives from
   `wxWindow`, so the wx strategy decides how the viewport classes compile.
5. Other packages' TUs include excluded backend headers and must guard them
   (`#if !defined(FAF_POSIX)`): `gpg/core/utils/BoostWrappers.cpp` (D3D9/D3D10 effect,
   technique, variable, pipeline-state headers; `sp_counted_impl_p<...D3D9>` deleters),
   `moho/app/WxRuntimeTypes.cpp` (`DeviceD3D9.hpp`), `moho/misc/StartupHelpers.cpp`
   (`DeviceD3D10.hpp`, `DeviceD3D9.hpp`). No D3D backend class is defined on POSIX.
6. Headless driver: decide null `gal::Device` vs device-less session (see above).
