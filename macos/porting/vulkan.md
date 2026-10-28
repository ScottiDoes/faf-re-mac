# Porting notes: package "vulkan" (milestone 2)

Scope: `src/gpg/gal/backends/vulkan/**` (FAF_POSIX-guarded edits; the lead's
CreateEffect preprocessing hook untouched), `src/platform/posix/stubs/render/gal/VulkanSurfacePosix.cpp`,
tests in `macos/tests/vulkan/` (build tree `build/m2-vulkan-tests`).

Result: faf-re's Vulkan backend runs on MoltenVK 1.4.2 and on KosmicKrisp (LunarG SDK
1.4.363) on the M2 Max: device up on a real window (the SDL3 test double and the real
shell's frame + viewport-child windows, the game's layout), clears, presents, an
engine-style resize/Reset, a real game effect (primbatcher.fx: HLSL preprocess, d3d11
translation, DXC to SPIR-V) drawn with an engine-written texture and with a game DXT5
DDS decoded by the POSIX codec, and a sampler at descriptor binding 84 - all with
**0 validation messages** (VK_LAYER_KHRONOS_validation) on both drivers, frames checked
with VK_LAYER_LUNARG_screenshot. The Windows view of
every edited file is unchanged (`unifdef -UFAF_POSIX` against HEAD: only wave 1's named
`preprocessedText`).

## What changed (all `#if defined(FAF_POSIX)`)

### VulkanLoader.cpp / .h - loader, driver, layers
- Loader order: `FAF_VULKAN_LIBRARY` (explicit: no fallback, dlerror logged), then
  `FAF_VULKAN_SDK_DIR "/lib/libvulkan.1.dylib"`, then `libvulkan.1.dylib`,
  `libvulkan.dylib`, `libMoltenVK.dylib`, `libvulkan.so.1`.
- Before the loader is opened: unless the user set `VK_DRIVER_FILES`, `VK_ICD_FILENAMES`
  or `VK_ADD_DRIVER_FILES`, `VK_DRIVER_FILES` = the SDK's `MoltenVK_icd.json`, or
  `libkosmickrisp_icd.json` with `FAF_VULKAN_DRIVER=kosmickrisp` (also `kk`; any value
  ending in `.json` is taken as a manifest path; anything else warns and uses MoltenVK).
  Unless `VK_LAYER_PATH`/`VK_ADD_LAYER_PATH` is set, `VK_ADD_LAYER_PATH` = the SDK's
  `explicit_layer.d` (validation, screenshot, api_dump layers).
- Logged once: `Vulkan: loader <dladdr path>, driver <choice> (<manifest>)`.
- `LoaderPath()`, `DriverSelection()`, `EnvironmentFlag()`; `VK_ENABLE_BETA_EXTENSIONS`
  before `<vulkan/vulkan.h>` (VK_KHR_portability_subset lives in vulkan_beta.h).

### DeviceVulkan.cpp / .h
- **Instance**: `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`
  when the loader offers it (MoltenVK is a portability driver; KosmicKrisp is not, and
  works either way).
- **Validation**: as on Windows `/vulkandebug` (GetCommandLineA works on POSIX), plus
  `FAF_VULKAN_VALIDATION=1`. `context.mValidate` (`/novalidate`) is the D3D9 caps check,
  unrelated. Messages go through the debug messenger to the log as `Vulkan validation: ...`
  (deduplicated by message id). `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` also works
  (the layer then prints to stdout).
- **Portability subset**: enabled whenever the device offers it (spec requirement), with
  every subset feature the device has chained into VkDeviceCreateInfo. MoltenVK 1.4.2
  lacks `samplerMipLodBias`, `pointPolygons`, tessellation isolines/point mode; has the
  rest. Uses checked: no triangle fans, point polygons, events, constant-alpha blend
  factors, comparison samplers, reinterpreting views, 2D-on-3D views or tessellation; both
  stencil faces share masks and reference. Relied on and present: view swizzles (A8, X8
  formats), attributes past the stride (the zero-stride binding of missing inputs;
  `minVertexInputBindingStrideAlignment` = 1). Missing LOD bias: `SamplerFor` sets
  `mipLodBias` 0 (logged, 4 times at most) - FAF_POSIX_TODO-level visual difference for
  effects that bias (render only). Elsewhere the bias is clamped to `maxSamplerLodBias`.
- **Descriptor limit (the one real MoltenVK failure)**: set 0 declares 64 sampled images
  and 64 samplers for VERTEX|FRAGMENT; MoltenVK allows 16 samplers per stage in an
  ordinary set (validation: `vkCreatePipelineLayout(): max per-stage sampler bindings count
  (64) exceeds device maxPerStageDescriptorSamplers limit (16)`), and terrain.fx declares
  19 samplers (mesh.fx 15). Where the ordinary limits are short and the update-after-bind
  ones suffice (MoltenVK: 1024), set 0 is created with
  `VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT` + per-binding
  `UPDATE_AFTER_BIND`, `descriptorBindingSampledImageUpdateAfterBind` enabled, descriptor
  pools with `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT`. Nothing is updated after
  binding; only the limits differ (both are Metal argument buffers in MoltenVK). Verified
  with a sampler at register s20 = binding 84 (tests/vulkan/manysamplers.fx). KosmicKrisp
  needs none of it (limits 1M).
- `VK_EXT_memory_budget`: already requested only when offered (checked; both drivers offer it).
- Depth-stencil: neither driver has D24S8; the existing fallback picks D32S8 (logged).
- **Swap chain**:
  - Format B8G8R8A8_UNORM + SRGB_NONLINEAR, as Windows (no sRGB encode; the game writes
    gamma-space values as D3D9 did; MoltenVK tags the layer sRGB so colours match Windows).
  - Present modes: both drivers offer FIFO and IMMEDIATE (no MAILBOX): vsync FIFO, else IMMEDIATE.
  - Images: 3 (MoltenVK 2..3, KosmicKrisp 3..3).
  - Usage masked to `supportedUsageFlags`, composite alpha OPAQUE or the first offered (both offer OPAQUE).
  - **Extent**: the size the engine asks for (head size, else `GetClientRect` of the head
    window; the layer's `currentExtent` only when neither is known - `RequestedExtent`), as
    Direct3D sizes a back buffer. Both drivers accept any extent 1..16384; MoltenVK sets
    the layer's drawableSize to it (checked: a 320x200 swap chain on a 640x400 layer fills
    the window). This keeps a WM_SIZE that reaches the engine before the layer's bounds
    change from producing a back buffer the engine did not lay out for. Logged when it
    differs from the layer.
  - **Resize / Retina scale change**: neither driver ever returns OUT_OF_DATE
    (probe_swapchain): a size change makes acquire (MoltenVK) or present (KosmicKrisp)
    return SUBOPTIMAL, which is presented as is and logged once per swap chain. The swap
    chain is rebuilt by the engine's own path: frame WM_SIZE -> `SyncSupComFrameClientSizeAndViewport`
    -> `CD3DDevice::InitContext` -> `Device::Reset(context)` at the new head size (tested).
    Until then KosmicKrisp scales the old image to the window; MoltenVK draws it unscaled
    into drawables of the new size (SDL's Metal view resizes drawableSize), so it sits in a
    corner for those frames.
  - **Out of date** (Windows drivers do it; the Apple ones may in future): a present
    returning OUT_OF_DATE marks the swap chain; the next acquire (or an acquire returning
    it) rebuilds it at the size the engine knows (`RebuildSwapChain`: device idle, new swap
    chain with oldSwapchain, the engine's back-buffer target keeps its object and gets the
    new images' views) and acquires again.
  - **No image**: a frame whose acquire failed draws into a per-head stand-in image (made
    on first need, `EnsureStandIn`; `AcquiredImage` returns the index after the swap
    chain's images) and is not presented - instead of drawing into an image the
    presentation engine owns (Windows draws into the stale index).
  - **Minimized windows**: both drivers keep acquiring and presenting without stalls
    while the window is minimized/occluded (probe: no acquire over 100 ms, currentExtent
    unchanged), so nothing special is needed; a minimize/restore during the device test
    is validation-clean.
- "Vulkan: ready" line now names the device, the driver and its version
  (`driverName driverInfo`: "MoltenVK 1.4.2", "KosmicKrisp vulkan-sdk-1.4.363 (git-...)") and the loader path.
- Diagnostics (POSIX): `FAF_VULKAN_TEST_OUT_OF_DATE=<frame>` treats that frame's present
  as out of date, `FAF_VULKAN_TEST_NO_IMAGE=<frame>` fails the acquire after it - the only
  way to reach those paths on the Apple drivers.

### VulkanSurfacePosix.cpp
`VK_EXT_metal_surface` from `faf_posix::shell::MetalLayer((HWND)window)` (Shell.h). No
layer (no native window / shell not running): logged, `VK_ERROR_INITIALIZATION_FAILED`,
so Setup throws its usual error.

### GpuDrivenVulkan.cpp - checked, no change needed
GPU-driven set: 1 UBO, 6 storage buffers, 2 sampled images, 1 sampler per stage (MoltenVK
limits 31/31/256/16); storage buffers at offset 0 (alignment 16 on both); b0 from 256-byte
aligned upload memory, at most 4 KiB (KosmicKrisp maxUniformBufferRange 64 KiB, alignment
64); compute and `vkCmdDrawIndirect` (drawCount 1) supported; timestamps
(`timestampComputeAndGraphics`, period 1 ns MoltenVK / 41.67 ns KosmicKrisp, 64 valid bits);
pipeline cache plain. Formats: every texture format the backend maps is sampleable on both
drivers (R8G8B8A8_UINT without linear filtering, as everywhere), every vertex format has
VERTEX_BUFFER. Not exercised yet (needs the game's GPU-driven paths).

## Tests (`macos/tests/vulkan`, own tree `build/m2-vulkan-tests`)

```
cmake -S macos/tests/vulkan -B build/m2-vulkan-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build/m2-vulkan-tests vk_device_test vk_effect_test vk_shell_test probe_swapchain faf_replay_check
macos/tests/vulkan/run_device_test.sh <moltenvk|kosmickrisp> <plain|validate|screenshot> [args]
# effect test (effects/ = primbatcher.fx and d3d10states.compat from effects.nx2):
cd build/m2-vulkan-tests && FAF_SHADER_CACHE_DIR=$PWD/shadercache FAF_VULKAN_DRIVER=moltenvk FAF_VULKAN_VALIDATION=1 \
  python3 -I ../../macos/tests/vulkan/watchdog.py 90 ./vk_effect_test --compat effects/d3d10states.compat \
  --effect effects/primbatcher.fx [--texture effects/btn_up.dds] --frames 40
# real shell: FAF_VULKAN_DRIVER=... python3 -I ../../macos/tests/vulkan/watchdog.py 60 ./vk_shell_test
```
Screenshots: add `VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=<list>
VK_SCREENSHOT_DIR=<dir>` (the engine points the loader at the SDK's layers), then
`python3 -I macos/tests/vulkan/ppm2png.py <dir>/*.ppm`.

The test CMakeLists adds the engine (`macos/`) as a subdirectory and links the tests
from its objects. `ShellDouble.cpp` implements Shell.h on SDL3 directly for
vk_device_test/vk_effect_test (windows whose HWND is a token; the shell's own Shell.h
definitions are hidden in `Shell.cpp.o` with `ld -r -unexported_symbol`, the rest of the
shell stays). `vk_shell_test` uses the real shell with no doubles. `GalOverrides.cpp`
(option `FAF_VK_TEST_FAKE_SERVICES`, now default OFF) hides `ShaderCompiler::Available`
and `ImageCodec::Instance` the same way, for when those packages are not in; the final
runs use the engine's own DXC compiler and POSIX texture codec. Every GUI run is under
`watchdog.py` (kills after 60-90 s); windows are 320x200 points (640x400 px), shown
without activation, a few seconds long. Tests exit with `_Exit` (the engine's statics do
not survive an orderly exit: `mutex lock failed` at teardown, as the runner also avoids).

| Test | MoltenVK 1.4.2 | KosmicKrisp |
|---|---|---|
| probe_swapchain (no engine): resize, minimize, restore, extent/result reporting | SUBOPTIMAL on acquire+present after resize; never OUT_OF_DATE; minimize no stall | SUBOPTIMAL on present; never OUT_OF_DATE; minimize no stall |
| vk_device_test: Device::Create on a 640x400 px (Retina 2x) window, 120 frames Clear+Present, resize to 800x520 + Reset at frame 60 | PASS, 0 validation messages | PASS, 0 |
| same + `FAF_VULKAN_TEST_OUT_OF_DATE=30 FAF_VULKAN_TEST_NO_IMAGE=80 --minimize-at 90` (rebuild, stand-in, minimize/restore) | PASS, 0 | PASS, 0 |
| same, `--reset 0 --vsync 0` (resize without Reset, IMMEDIATE) | PASS, 0 | PASS, 0 |
| screenshots (frames 5, 40, 59, 61, 90, 119): background, moving viewport-cleared box, 16x16 corner marker | exact colours and positions before and after the resize | identical |
| `--head-divisor 2` (320x200 swap chain on a 640x400 layer) | fills the window (MoltenVK sets drawableSize) | - |
| vk_effect_test primbatcher.fx (d3d10states.compat + fx, 14 shaders via DXC): TAlphaBlendLinearSampleNoDepth with a 64x64 engine-written checkerboard (format 2, Lock/Unlock) + TRed, vertex format 6 | PASS, 0 validation messages; checkerboard (clamped uv 0..2), red quad, colours exact | PASS, 0; identical |
| vk_effect_test manysamplers.fx: sampler register s20 (binding 84, WRAP) | PASS, 0; 2x2 tiled checkerboard | PASS, 0 |
| vk_effect_test, + an indexed draw (16-bit indices, firstIndex 1 = byte offset 2, baseVertex 12) | PASS, 0; third quad where expected | PASS, 0 |
| vk_effect_test `--texture btn_up.dds` (textures.nx2 `ui/AEON/BUTTON/large/_btn_up.dds`, DXT5 296x80, POSIX codec) | PASS, 0; the Aeon button alpha-blended, clamped | PASS, 0; identical |
| vk_shell_test: real shell, frame (WS_OVERLAPPEDWINDOW) + viewport child, head.mWindow = child, 640x400 px, resize to 800x520 via SetWindowPos + MoveWindow + Reset | PASS, 0 validation messages; screenshots exact | PASS, 0; identical |
| Validation sanity: set 0 forced to an ordinary layout (temporary edit, reverted) | `max per-stage sampler bindings count (64) exceeds ... (16)` | - |
| milestone 1: `SPEED=50 FAF_REPLAY_EXE=build/m2-vulkan-tests/faf_replay_check OUT=build/m2-vulkan-tests/replay macos/run_replay.sh` | `beats=9718 mismatches=0 exit=end-of-replay` (75.6x); again with the final code and the real shell linked: `beats=9712 mismatches=0` (52x, loaded machine) | |

Screenshots and logs: `build/m2-vulkan-tests/runs/<driver>-<mode>/`, `runs/effect/`, `runs/shell/<driver>/`.

Every edited TU compiles with `macos/tu.sh` (DeviceVulkan, GpuDrivenVulkan,
ResourcesVulkan, VulkanCore, VulkanLoader, VulkanSurfacePosix).

## Open issues / notes for others

- The concurrent shell and wx packages broke this tree's build for a while (half-written
  `shell/*.cpp`, then wx's `FafNative*` members); every result above is from the final
  code once they linked again.
- The sampler LOD bias the subset lacks: terrain/mesh effects that bias mips look slightly
  different on MoltenVK (KosmicKrisp has it).
- Shader cache: `ShaderCacheDirectory()` shows `C:\Users\<u>\Library\Application Support\FAForever\ShaderCache\`
  (the Files area maps it to `~/Library/Application Support/FAForever/ShaderCache`, which
  exists); cosmetic in the log. Tests use `FAF_SHADER_CACHE_DIR`.

## Requests

- Shell package: `MetalLayer(HWND)` for the **viewport child HWND** (`head.mWindow` =
  WD3DViewport) works in the shell as it stands (vk_shell_test, both drivers). Please keep
  delivering the frame's WM_SIZE (in pixels) on a window resize **and** on a Retina scale
  change (display move): that is what makes the engine Reset the device at the new size;
  the backend tolerates the frames in between (SUBOPTIMAL). `MetalLayer` returns null off
  the main thread; Device::Create/Reset run on the main thread in the game.
- Window area: `GetClientRect` of the viewport HWND in drawable pixels (used when a head
  has no size).
- Lead: nothing in macos/CMakeLists.txt is needed for this package. faf_game will want
  `/vulkandebug` or `FAF_VULKAN_VALIDATION=1` for bring-up, and `FAF_VULKAN_DRIVER=kosmickrisp`
  to compare drivers.
