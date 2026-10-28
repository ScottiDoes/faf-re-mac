# Wave 3 report: macOS native port, the game in a window (milestone 2)

This verification pass ran after every wave-3 agent (shell, vulkan, shaders, textures, fonts,
bring-up) had finished. I:
- rebuilt both executables from clean;
- re-ran the headless replay checks;
- watched the windowed replay through its own frame captures;
- audited the working tree against `src/platform/posix/README.md`.

I changed no source file and committed nothing. My notes are in `macos/porting/verify3.md`. My
tools, logs and frames are in `build/verify3-tests/`, which is untracked.

## 1. Headline

**Milestone 2 is reached for replay 27890640.** The replay plays in a normal macOS window. It
is drawn by faf-re's own Vulkan renderer on MoltenVK 1.4.2, with the in-game UI, at 60 fps.
Milestone 1 still holds from a clean build: the headless replay matches every checksum.

**What the user can see now:**
- **The world:** Canis River with textured, team-coloured units, shadows, decals, props and
  sand-ripple normal maps. Explosions, smoke, the Cybran shield dome and fog of war also draw.
- **The UI:** the score panel, chat, the replay controls and the economy panel, all with
  readable text.

### How to run it

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
REPLAY=/Users/scottharris/Gaming/faf/bench/replay_27890640.scfareplay PREFS=bringup-high.prefs macos/run_game.sh
#   SPEED=10                       the replay at +10 (about 100 s to the end)
#   FAF_VULKAN_DRIVER=kosmickrisp  the other Vulkan driver
#   TIME=<s>                       stop after s seconds
# Quit with the in-game menu, the window's close box, or Ctrl-C in the terminal.
```

- **The window:** 1280x800 points (2560x1600 pixels).
- **The camera:** starts on the whole-map overview. Zoom with the mouse wheel; pan with the
  arrow keys or a middle-button drag.

### One caveat

- **The bare main menu does not keep drawing** (`macos/run_game.sh` with no replay).
  - **What you see:** the FA logo on black.
  - **What it takes:** moving the mouse over the window or pressing a key brings the menu up.
  - **Details:** finding F1 in section 4.
- **Replays are not affected.**

## 2. Build census

### Command

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:/Users/scottharris/Gaming/rosettax87/.venv/bin:$PATH
ninja -C build/macos -t clean faf_replay faf_game                   # 1,382 files removed
ninja -C build/macos -k 0 -j 10 faf_replay faf_game > build/macos/census-wave3.log 2>&1
```

Before the clean I copied the bring-up's binaries and maps to `build/verify3-tests/*.pre-wave3*`.

### Results

| | Wave 2 (census-wave2) | Wave 3 (this pass) |
|---|---|---|
| ninja steps | 1,365 | **1,383**, exit 0, 1 min 59 s wall |
| `faf_engine` TUs | 960 | **975** |
| Other TUs | 3 runner, 386 Wild Magic, 12 zlib | 3 runner, **1 DXC bridge, 1 GameMain**, 386 Wild Magic, 12 zlib |
| `FAILED:` lines / errors | 0 / 0 | **0 / 0** |
| Warnings | 34 | **34, the same set** (`diff` of the warning lines is empty) |
| Executables | `faf_replay` | `faf_replay` 15.8 MB, **`faf_game` 15.8 MB** |

**The 15 new engine TUs** are all under `platform/posix`:
- `WinCompatGdiFont.cpp`;
- `gal/HlslPreprocess.cpp` and the six `gal/Image*.cpp`;
- the seven `shell/*.cpp` files (`Shell`, `Windows`, `Queue`, `Input`, `System`, `MetalView`,
  `TestInput`).

The 934 `sources.cmake` TUs and the stand-ins are unchanged from wave 2.

**The warnings** are still the 30 `-Wundefined-bool-conversion`, 2 `-Winvalid-noreturn` and
2 `-Wself-assign-field`, all assessed in wave 2. No new code adds a warning.

### Reproducibility

The clean `faf_game` and `faf_replay` match the bring-up's binaries byte for byte, except:
- the LC_UUID;
- one `__TIME__` string;
- the two ad-hoc code-signature hashes.

That is 82 bytes in all.

### Post-link checks (all pass)

- **`check_map_order.py`, both binaries:**
  - `StaticInitPreRegister.cpp.o` comes first;
  - then the 934 `sources.cmake` objects in order;
  - then the 40 platform/posix objects;
  - then the DXC bridge and `GameMain.cpp.o` (faf_game) or the 3 runner objects (faf_replay).
  - The script's "runner objects after engine" line prints False for faf_game only because
    faf_game has no runner objects.
- **`check_symbols.sh`:** faf_replay has 291 libSystem imports and faf_game 289. All 33
  faf_crtmath functions, `rand` and `srand` are defined in each executable.
- **`check_fma.sh`:** the engine, runner, DXC bridge and GameMain objects, plus Wm3 and zlib,
  contain no fused multiply-add outside the whitelist.
- **`otool -tv`:** 0 `fmadd`/`fmsub`/`fnmadd`/`fnmsub`/`fmla`/`fmls` in either binary.
- **Linked libraries:** faf_game links SDL3 from `/opt/homebrew/opt/sdl3` and the system
  frameworks. The Vulkan loader, MoltenVK and libdxcompiler are opened at run time from the
  SDK path.

## 3. Regression

### 3.1 Headless (the milestone-1 check)

| Run | Result | Sim speed |
|---|---|---|
| `SPEED=50 macos/run_replay.sh` (serial sim) | `beats=9718 mismatches=0 exit=end-of-replay` | ticks 1..9610 in 12.4 s: **77.7x real time** |
| Same, with `/simworkers 8` | `beats=9716 mismatches=0 exit=end-of-replay` | 11.5 s: **83.5x** |
| **Negative control:** beat 5000 digest flipped for both players (`corrupt_replay.py`) | `mismatches=2`, exit 1 | — |
| `UI=1` (the game UI Lua runs headless; the fonts package's fix) | `beats=9709 mismatches=0 exit=end-of-replay` | **76.6x** |

- **Serial run:** 27.1 s wall, peak RSS 652 MB.
- **Self-tests:** 75 of 75 pass at start-up in every run.
- **The negative control** printed the native sim's digest for beat 5000,
  `fecbe63e2c7637a3c10db12024946f6d`. That is the same value as in wave 2: the recorded digest
  with the flipped byte restored. So the checksum check is live.
- **Speed:** the serial run is 5% slower than wave 2 (77.7x against 82.4x). The run-to-run
  spread on this machine is of that size: the bring-up measured 76.5x to 76.9x. The executable
  now also contains the shell and the wx stand-in's native paths, but a headless run never
  calls `shell::Init`, and the sim result is unchanged.

### 3.2 The windowed replay

Every run used `macos/run_game.sh` under its `TIME` watchdog. No `faf_game` or watchdog process
was left running.

| Run | Command (beyond the documented how-to-run) | Result |
|---|---|---|
| **A** | Exactly bring-up's how_to_run, plus `TIME=100 SHOTS=1500,3600,5400 FAF_GPU_PROFILE=600` | "Vulkan: ready (... MoltenVK 1.4.2 ...)", high-fidelity terrain and water. The replay plays in real time. 0 checksum mismatches, 0 validation messages |
| **C** | `SPEED=10`, plus the bring-up's camera hook (`init_shots.lua`, `/shots <dir>`) | Reached `BENCH end tick=9610` (the replay's last tick) with **0 mismatches**. 11 engine frame dumps |
| **B** | Same as C with `FAF_GPU_PROFILE=600` | Reached tick 9610, **0 mismatches**. Its frame dumps were lost to tooling note T1 |
| **KK** | `FAF_VULKAN_DRIVER=kosmickrisp VALIDATE=1 SPEED=10`, 70 s | "driver KosmicKrisp vulkan-sdk-1.4.363". 1 validation message: the known unused-vertex-output performance warning. 0 mismatches to tick 6642 |

### 3.3 Frame rate

MoltenVK, 2560x1600, high fidelity, vsync on, M2 Max, 60 Hz display:

| Run | Wall ms/frame | GPU ms/frame | Draws/frame |
|---|---|---|---|
| A (real time, overview camera) | 16.64-16.67 (**60 fps**) | 2.8-3.6 | ~1,080 |
| B (+10, the camera moving over bases and battles) | 16.6-16.9 (**59-60 fps**) | 2.8-5.6 | 260-1,180 |
| KK with validation | 60.5 (16.5 fps) | 7.8 | ~660 |

- The first 600-frame window of each run (17.2 to 17.5 ms) includes the load.
- The KK row is dominated by the validation layer. It is not a measure of KosmicKrisp itself.
- These numbers agree with the bring-up's: the GPU has 3-6x headroom at 60 Hz.

### 3.4 What the frames show

I looked at each frame myself.

**Run A**, from the screenshot layer (UI included):
- `build/verify3-tests/gameA/shots/1500.png` (0:23), `3600.png` (0:58), `5400.png` (1:28).
- These show the default whole-map overview: terrain relief and shading, the river channel,
  strategic icons in team colours, and the map-border band beyond the map edge.
- The UI frame (top-left buttons, replay controls, score panel) is in place.
- Close up (`gameA/crop_1500_score.png`), the score panel text is crisp: "00:00:23 (+0 / +0)",
  "TheGoodGeek [1000]", "whiplash [1000]", "Observer", "Full Share - 5x5 - Canis River".

**Run C**, engine frame dumps converted to PNG, in `build/verify3-tests/gameC/`:

| Shot | Game time | What it shows |
|---|---|---|
| `acu.png` | 0:13 | The Cybran ACU, factory, mass extractors and power generators, close up: textures, red team colour, shadows, build decals, props, sand ripples |
| `mid.png` | 8:05 | The river bed from above, rocks, vegetation clusters, units, chat lines |
| `mid2.png` | 9:26 | A yellow air wing over the dunes, team colour and health bars, chat in two colours |
| `water.png` | 9:37 | A low view across the river towards a Cybran base: terrain relief, units, the map edge |
| `low.png` | 10:46 | The Cybran base at a low angle: the T2 shield dome, Mantis bots, smoke and an explosion |
| `fogclose.png` | 11:56 | Fog of war (`SetFocusArmy(1)`): bright vision circles over a darkened map, and the economy panel |

**Judgement:** nothing is black, pink, upside down, mirrored or wrongly coloured, and the
lighting is consistent. Three things I cannot confirm without a reference frame from the
Windows or Linux game:
1. **The river's water surface is nearly invisible**, even at the low angle of `water.png`
   (pitch 0.5), where Fresnel reflection should be strongest. The bed reads as dark wet mud.
   The bring-up measured that high-fidelity water changes 2.7% of a frame, exactly inside the
   shoreline, so the water pass does run. Whether it should look stronger is open.
2. **The Cybran shield dome** (`low.png`, `fogclose.png`) is a flat, evenly translucent purple
   with no visible pattern.
3. **Mip detail on MoltenVK:** it cannot apply a sampler mip LOD bias (vulkan package, risk 1).
   Terrain and mesh textures there may be slightly softer or sharper than on Windows.

## 4. Findings

| # | Severity | Where | Finding |
|---|---|---|---|
| **F1** | **Medium** (main-menu path; replays unaffected) | shell / app loop (`platform/posix/shell`, `WinAppPosix.cpp` `WIN_AppExecute`) | **The main menu stops drawing when idle.** See below |
| F2 | Low | `macos/tests/bringup/shots/.../gamemain.lua` | `ConExecute('dump_Frame ' .. dir)` splits a directory that contains a space. The dump silently writes nothing, and the repo's real path has one (`/Volumes/SSD Programs/...`). Pass the `~/Gaming/faf-re` path, which has no space, or quote the argument. (Tooling note T1) |
| F3 | Low | `macos/run_replay.sh` | A relative `REPLAY=` path ends in `exit=load-failed`, because the script changes into the run folder. `run_game.sh` already makes the path absolute (`${REPLAY:A}`); `run_replay.sh` should too |
| F4 | Info | `macos/run_game.sh:113` | With `SHOTS` frames that were never reached, zsh prints "no matches found". Cosmetic |

### F1 in detail

**Observed:** `macos/run_game.sh` without `REPLAY`, with `Game.prefs` or `bringup-high.prefs`
(both now hold a profile), behaves like this:
- frames 1-8 present: black, then the FA logo, with no menu buttons yet (`gameMenu7/shots/5.png`, `8.png`);
- then **no frame for the rest of the run**:
  - `FAF_GPU_PROFILE=5`: the first 5 frames spanned 19 s;
  - `gameMenu3`: no frame in 55 s.
- The main thread waits in `MsgWaitForMultipleObjectsEx` → `WaitHandles`, polling
  `shell::InputAvailable()`, which stays false (`gameMenu4/sample.txt`). No window is invalid,
  so no WM_PAINT is due.
- When the watchdog's SIGTERM arrives, SDL turns it into a quit event. The game opens "Are you
  sure you'd like to quit?" and **from then on presents at 60 fps** (`gameMenu2/shots/120.png`).

**Why the bring-up did not see it:** its main-menu test used a fresh prefs file, which opens
the Profile Manager dialog, and it then drove the menu with synthetic input. Each of those
keeps the loop awake. An idle main menu with an existing profile was not tried.

**Likely area:** in the front end, `CScApp::Main` → `CD3DDevice::Refresh` →
`viewport->Refresh` does not leave a window that `shell::detail::FindInvalid` finds. Possible
causes:
- the viewport or frame is not marked visible in the shell's records (FindInvalid only walks
  visible windows);
- or `IsIconic` reports the frame minimized, which skips `Refresh`, and with no session no
  wakeup timer is set.

Logging `IsWindowVisible`/`IsIconic` of `head.mWindow` and the frame at the main menu would
settle it.

**Impact:** a user who starts at the main menu sees a logo on black until they move the mouse
or press a key. Mouse movement then drives frames, so menu animations run only while input
arrives.

## 5. Rule audit (wave 3)

### 5.1 What passed

- **The Windows view is unchanged** (`unifdef -UFAF_POSIX` of HEAD against the working copy,
  all 64 modified tracked files, in `build/verify3-tests/unifdef/`).
  - 41 files have an identical Windows view.
  - The 23 that differ show only the forms accepted in wave 2:
    - widened guards (`|| defined(FAF_POSIX)`, and Sync.h's `&& !defined(FAF_POSIX)`);
    - comments and blank lines;
    - the wave-1 `preprocessedText` local (W2-5).
  - Wave 3 touched 7 tracked files: `DeviceVulkan.cpp/.h`, `VulkanLoader.cpp/.h`,
    `EffectRuntime.cpp`, `CDiskWatch.cpp`, `CScApp.cpp`. Their Windows-view differences are a
    blank line at `VulkanLoader.cpp:7` and `CScApp.cpp:1743`, and W2-5. Everything else is
    inside `#if defined(FAF_POSIX) ... #else <original> #endif`.
- **No sim-path edits.** Wave 3's engine edits are:
  - the renderer choice and the default-window retry (`CScApp.cpp`);
  - the disk-watch mutex lifetime (`CDiskWatch.cpp`);
  - the Vulkan loader, device and swap chain;
  - the effect dump path.

  The compat additions are user-side: the Window area, Gdi, and the main-thread-only shell
  branch of `MsgWaitForMultipleObjectsEx`.
  - That shell branch requires `shell::IsActive()`. The headless runner never calls
    `shell::Init`, so it takes the old code.
  - The sim still matches every checksum: headless serial, threaded and UI runs, plus three
    windowed runs to the end of the replay.
- **Lead-owned files are untouched by the agents.**
  - `Shell.h`, `HlslPreprocess.h`, `README.md`, `macos/CMakeLists.txt` and `dxc.cmake` date
    from the lead's set-up (21:20-21:21).
  - `WinCompat.h`, `LayoutAsserts.h`, `immintrin.h`, `deps.cmake`, `exclusions.cmake` and
    `tu.sh` date from waves 1-2.
  - `startup/**`, `headless/**`, `math/**` and `crt/**` were last changed at 19:54 or earlier.
  - `src/sources.cmake` is unmodified.
- **Changes after the lead's set-up are all in owned areas:**
  - shell: `shell/*`, `WinCompatWindow.cpp`, `compat/Window.h`, `WinCompatSync.cpp`'s two
    MsgWait functions, `WindowsCursorPosix.cpp`;
  - wx: the wx stand-in headers and `WinAppPosix.cpp`;
  - vulkan: the Vulkan backend files and `VulkanSurfacePosix.cpp`;
  - shaders: `dxc/*`, `HlslPreprocess.cpp`, `ShaderCompilerPosix.cpp`, `EffectRuntime.cpp`;
  - textures: `gal/Image*`, `ImageCodecPosix.cpp`;
  - fonts: `WinCompatGdi*`, `compat/Gdi.h`, `CD3DFontPosix.cpp`;
  - bring-up: `game/GameMain.cpp`, `CScApp.cpp`, `CDiskWatch.cpp`, `TestInput.cpp`, the shell's
    paint fix, `run_game.sh`, `macos/tests/bringup/*`, and comment-only edits to
    `exclusions.d/render.cmake`, `run_replay.sh`, `render.md` and `wx.md`;
  - each package's tests and notes.

  The non-comment content of `render.cmake` (5 replacements and 1 exclusion) matches the
  engine TUs built.
- **No downloads or installs by agents.**
  - The newest Homebrew keg is `sdl3` (21:15) and the newest items in `~/Gaming` are
    `VulkanSDK` (21:17) and `downloads/vulkansdk-1.4.363.0` (21:16). Both belong to the lead's
    approved set-up.
  - Nothing is new in the rosettax87 venv, `~/Library/Python`, `~/.local` or the Homebrew
    download cache.
  - `~/.faforever` has nothing newer than the set-up.
- **Dependency trees still match their patches:**
  - **boost:** `git -C dependencies/boost_1_34_1 diff` is byte-identical to
    `boost_1_34_1_faf_macos.patch`.
  - **Wild Magic:** `git archive HEAD` + `wildmagic3p8_faf_required.patch` +
    `wildmagic3p8_faf_macos.patch` equals the working tree (`diff -r` empty).
  - **zlib:** the hunks equal `zlib_1_2_3_faf_required.patch`. Only the diff headers differ:
    git's against the patch's `diff -ruN`.
- **Stub logging.**
  - Every file in `stubs/` that stubs behaviour logs `FAF_POSIX_STUB`.
  - The new Window-area and shell refusals log too:
    - off-main-thread window changes;
    - `SetParent` of a top-level window;
    - non-text clipboard formats.
  - `GameMain.cpp` logs each Windows-only switch: `/crashtest`, `/waitfordebugger`, `/alloclog`,
    `/mediacenter`.
  - Several stubs files have no `FAF_POSIX_STUB` because they are no longer stubs:
    `ImageCodecPosix`, `ShaderCompilerPosix`, `VulkanSurfacePosix` and `CD3DFontPosix` are real
    implementations; `D3DXMathPosix`, `GalBackendSharedPosix` and
    `HardwareVertexFormattersD3D10Posix` are unchanged since wave 2.
- **Win32 sizes for the new areas.**
  - **The probe** (`build/verify3-tests/probe/SizeProbe3.cpp`, with a failing negative
    control) gives the Windows x64 sizes for MSG 48, WNDCLASSEXW 80, MINMAXINFO 40,
    TEXTMETRICA 56, LOGFONTA 60, KERNINGPAIR 8, BITMAP 32, SIZE 8, BITMAPINFOHEADER 40 and
    RGBQUAD 4.
  - **Wide structs:** TEXTMETRICW is 68 bytes (Windows 60) and LOGFONTW 156 (Windows 92),
    because WCHAR is the 4-byte `wchar_t`. That is allowed by rule 3: they live in memory only.
- **No stray process or file.**
  - Nothing is left running.
  - `faf-re-bin/run-native` has no new files.
  - The tree's diff, status and untracked-file hashes were identical at the start and the end
    of this pass.

### 5.2 Violations and notes

No high or medium rule violation.

| # | Severity | Where | Rule | Problem |
|---|---|---|---|---|
| W3-1 | Low | `src/platform/posix/WinCompatWindow.cpp:1058` `SetWindowsHookExW` | 6 (stub logging) | Returns a non-null handle but never calls the hook, and does not log `FAF_POSIX_STUB`. Its only use is suppressing the Windows key in full screen, which has no macOS counterpart, so the behaviour is harmless. It should still log once |
| W3-2 | Low (hygiene) | `macos/build/m2-textures-tests/obj/*.o`, `macos/build/m2-shaders-tests/` (168 KB, untracked) | Build trees go under `build/` | Early runs of the textures and shaders test scripts wrote objects inside the source tree. They are safe to delete. The scripts now write to `build/m2-*-tests` |
| W3-3 | Info | `ShaderCompilerPosix.cpp:35` | 6 | A DXIL compile fails with a message returned to the caller instead of a `FAF_POSIX_STUB` log. Acceptable: the Vulkan path never asks for DXIL, and the message is Windows' behaviour when the DLL is missing |
| W3-4 | Info | The engine log | — | The shader cache is logged as `C:\Users\...\FAForever\ShaderCache\`, and the effects as `\volumes\ssd programs\...`. The Files area maps both correctly; only the text is Windows-form (vulkan and shaders requests) |
| W2-1, W2-2, W2-4, W2-5 | Low/Info | (wave 2) | — | Still open: `HIDWORD` text, `static_assert` neutralised by `LayoutAsserts.h`, the `StaticInitPhase.h:47` comment path, the `preprocessedText` restructure |

## 6. Per-package results

| Package | What it delivered | Verified here |
|---|---|---|
| **shell** | SDL3 window system behind the Window area and the wx stand-in:<br>• native top-level windows and child-window rectangles<br>• a CAMetalLayer per viewport<br>• Windows-order message queue, keyboard, mouse, wheel<br>• cursor from the cursor texture<br>• clipboard, monitors<br>• the `MsgWait` input wake | Builds clean. Windows, input routing and the viewport layer work in every windowed run. **F1** is in this area. Pointer warping, clipboard, borderless full screen and multiple monitors remain untested on screen (shell's own risks) |
| **vulkan** | Loader and ICD selection from the SDK; portability enumeration and subset; the update-after-bind texture set for MoltenVK's 16-sampler limit; swap-chain format, present modes and extents; SUBOPTIMAL and OUT_OF_DATE handling; `VK_EXT_metal_surface` | "Vulkan: ready" on both drivers. terrain.fx (19 samplers) draws: the risk the package left open is closed. KosmicKrisp with validation gives 1 known warning. The Windows view is unchanged |
| **shaders** | DXC bridge (dlopen of libdxcompiler); the POSIX shader front end; the HLSL preprocessor (DXC `-P`); 344/344 game entry points to SPIR-V; shader cache | Every run logs "N of N shaders read from the cache ahead of use" for all 11 effects. Shaders never fail to compile in the game |
| **textures** | The POSIX ImageCodec: DDS, ImageIO, in-house BC1-BC5, mips, the decode cache, the DDS direct path | The world, unit, UI and cursor textures in the frames are correct. Nothing is pink or missing |
| **fonts** | The Gdi area on Core Text (private fonts, GDI face mapping, TrueType metrics, drawing), with the engine's own CD3DFont.cpp | UI text is readable in every frame I looked at. Headless `UI=1` now finishes with 0 mismatches at 76.6x (the WAVE2 "UI=1 hangs" item is resolved) |
| **bring-up** | `faf_game` (`GameMain.cpp`); the Vulkan-only renderer order; the CDiskWatch exit fix; the shell paint fix that made frames appear; `TestInput.cpp`; `run_game.sh`; the capture tools | Reproduced from a clean build:<br>• levels 1-4 and 7, independently (windowed to tick 9610, 0 mismatches; frames looked at; 60 fps; KosmicKrisp)<br>• level 6: the main menu draws only with input (F1)<br>• level 5 (camera input): not re-run |

## 7. Open requests

**Lead-owned:**

| From | Request | Status |
|---|---|---|
| fonts, bring-up | README layout table: `game/`, `shell/TestInput.cpp`, the Gdi area as `WinCompatGdi.cpp` + `WinCompatGdiFont.{h,cpp}`. Worth adding too: `gal/Image*.cpp` (textures) and `dxc/` (already listed) | Open |
| shaders | Packaging: ship `libdxcompiler.dylib` next to `faf_game`, or set `FAF_DXC_DIR`. Today the `FAF_VULKAN_SDK_DIR` fallback works only on this machine. The same applies to the Vulkan loader and MoltenVK, and to SDL3 from Homebrew | Open (milestone 4 packaging) |
| verify (this pass) | Mark link-run open issue 2 (`DISK_ResetWatch`) and WAVE2's "UI=1 hangs" as resolved | Done by bring-up and fonts; confirmed here |
| wave 2 list | `tu.sh` argv quoting; `FAF_FORMAT_ASSERT`; `format(printf)` attributes; patch application in the build; a post-link check script; per-worker FPCR check; hardware oracle items | Still open (WAVE2_REPORT.md section 7) |

**Other packages:**

| From → to | Request | Status |
|---|---|---|
| verify → shell | **F1:** the idle main menu stops presenting | **Open, first priority** |
| fonts → runner | `HeadlessApp.cpp` should call `moho::REN_Init()` so that headless `UI=1` measures text with the game's fonts (today it falls back to Arial: one `[faf-posix] gdi: no font named "Zeroes Three"` line) | Open. Headless only, no effect on the sim |
| textures → render-null | `DeviceNull::UpdateSurface` could convert through the codec. Its log text "(no image codec)" is now inaccurate | Open (optional, headless only) |
| shaders → effects owner | Vanilla `water2.fx` local-shadows-global (`waterLerp`) fails under DXC on every platform. FAF's file is fixed | Open (optional) |
| vulkan, shaders → Files/EffectRuntime | Windows-form shader-cache path in the log | Open (cosmetic, W3-4) |
| bring-up open issues | 1. A window bigger than the screen is shrunk without WM_SIZE (the image is scaled slightly).<br>2. `/prefs` with an absolute POSIX path is joined to the prefs folder.<br>3. One unexplained unit selection during a middle drag | Open |
| shell (own risks) | On-screen tests of pointer warping (middle-drag camera), the clipboard, borderless full screen and multiple monitors. No `WM_TIMER`, no IME | Open |
| verify → bring-up tooling | F2 (`dump_Frame` with a space in the path), F3 (`run_replay.sh` relative `REPLAY`) | Open (low) |

## 8. Recommended next steps (towards a playable game)

1. **Fix F1, then play-test the front end by hand.**
   - Start at the main menu; go to Skirmish; start a game against the AI; quit.
   - With a real mouse, this also covers what synthetic input cannot:
     - pointer warping for the middle-drag camera;
     - the clipboard;
     - bring-up open issue 3.
   - Then `/windowed` against borderless full screen (`SIZE=prefs`).
2. **Get reference frames from the Linux PC.** The user has FAF installed there.
   - Play replay 27890640 there and take screenshots at the same game times and camera
     positions as `build/verify3-tests/gameC/*.png` (the `init_shots.lua` hook works in
     FAF's game too if the run folder is set up the same way).
   - Compare: the river water (does Canis River show a visible water surface?), the Cybran
     shield dome, the map-border band, and texture sharpness (MoltenVK's missing mip LOD bias).
   - The same PC can build the fonts package's `gdi_reference.c` with mingw-w64 and run it
     under Wine, as a first font comparison. Only a real Windows run is authoritative for
     advances.
3. **Milestone 3: audio and movies.** These are stubs today (`/nosound /nomovie`):
   - **Sound:** XACT banks (`.xwb`/`.xsb`) through the AudioEngine on a macOS backend (Core
     Audio or SDL3 audio), plus X3DAudio's positioning.
   - **Movies:** the SFD decoder on ffmpeg (`FfmpegSfdDecoderPosix.cpp` is a stub), which the
     main-menu background movie and the intros need.
   - **Like rendering, user-side only:** audio must never feed back into the sim (wave-1
     audio-movie notes).
4. **Lock in determinism before multiplayer.**
   - Run the 8-replay core set from WAVE2_REPORT.md section 8, with `/simworkers 0` and 8.
   - Add `tick=` and `checked=` to the `FAF_REPLAY_RESULT` line.
   - Make the post-link checks and the negative control one script.
   - A desync in online play is the biggest risk for a native client, and replays are the
     cheapest way to find one.
5. **Milestone 4: the FAF client launches `faf_game`.**
   - The FAF client starts the game with `/init init_faf.lua /gpgnet <host:port>` and the
     lobby and replay-server arguments.
   - Needed:
     - a macOS game-path setting or wrapper in the client;
     - the GPGNet TCP link and the lobby's UDP networking (the net package's sockets, tested
       against a Windows or Linux peer);
     - replay saving;
     - packaging: an `.app` bundle with SDL3, the Vulkan loader, MoltenVK and libdxcompiler,
       code signing, and the run folder's init files.
   - First target: a LAN skirmish between this Mac and the Linux PC.
6. **Performance.** It is already 60 fps with 3-6x GPU headroom at 2560x1600, and about 119
   fps with vsync off (the 8 ms frame cap).
   - Next: measure the render-thread CPU (2.4-7.1 ms per frame in the bring-up) in a large
     late-game battle.
   - Next: the cold shader compile (about 8 s on first launch; the cache makes later
     launches fast).
7. **A native Metal backend is optional, not needed now.**
   - **The case for it:** MoltenVK gives 60 fps on Vulkan, and KosmicKrisp gives a second
     driver to compare against. A Metal gal backend would remove the SPIR-V → MSL step and
     MoltenVK's limits (mip LOD bias, primitive restart, 16 samplers per set).
   - **The cost:** it is a fourth backend to maintain. Revisit it only if profiling shows
     MoltenVK overhead or a feature gap that matters.

## Files produced by this pass

- `build/macos/census-wave3.log`: the full rebuild.
- `build/verify3-tests/` (untracked):
  - `faf_{game,replay}.pre-wave3{,.map}`: the bring-up's binaries;
  - `run50/`, `run50-w8/`, `negctl/`, `run50-ui/`: the headless runs;
  - `gameA/`, `gameB/`, `gameC/`, `gameKK/`, `gameMenu*/`: the windowed runs, with logs, PNG
    frames and the main-menu sample;
  - `unifdef/`: the Windows views;
  - `probe/`: the size probes;
  - `tools/pngcrop.py`;
  - `wave3-start.*`: the tree as I found it.
- `macos/porting/verify3.md`: the verifier's notes.
