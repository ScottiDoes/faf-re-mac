# Porting notes: bring-up (milestone 2: the game in a window)

Running log, resumable: newest entries at the bottom of each level; resume from the last
"NEXT" line.

## Commands

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:/Users/scottharris/Gaming/rosettax87/.venv/bin:$PATH
cmake -S macos -B build/macos
ninja -C build/macos -k 0 -j 10 faf_replay faf_game
SPEED=50 macos/run_replay.sh            # headless regression (must print mismatches=0)
macos/run_game.sh                       # the game (see the script header)
```

## Log

- Start: reconfigured build/macos (934 of 958 TUs, 10 stand-ins), full rebuild of
  faf_replay after the packages' changes (980 steps, 0 errors).
- Baseline headless regression: `SPEED=50 macos/run_replay.sh` ->
  `FAF_REPLAY_RESULT replay=27890640 beats=9719 mismatches=0 exit=end-of-replay`
  (76.5x real time).

### Level 1: faf_game links (reached)

- New `src/platform/posix/game/GameMain.cpp`: `main()` doing what WinMain does (thread name,
  CRT diagnostic hooks, /w /p probe arming, /aqtime, die handler, /singleproc, /purgecache,
  `CScApp` through `WIN_AppExecute`, run-time log line, gal/std exception -> Die), with
  `faf_posix::shell::Init()` first and `Shutdown()` at the end, the platform self-tests
  (`faf_posixRunSelfTests`; /skipselftest overrides a failure), and an exit through
  `std::_Exit` after flushing (`FAF_GAME_STATIC_EXIT=1` runs `exit()` instead).
  Windows-only switches (/crashtest, /alloclog, /waitfordebugger, /mediacenter) log a
  FAF_POSIX_STUB; the accessibility-key changes are not made (no counterpart, and the game
  must not change system settings).
- `src/moho/misc/CDiskWatch.cpp` (FAF_POSIX): `gDiskWatchInitMutex` is a never-destroyed,
  constant-initialized mutex (union storage), so `DISK_ResetWatch` (atexit, registered
  during static init) no longer locks a destroyed mutex on `exit()` (link-run open issue 2).
- `src/moho/app/CScApp.cpp` (FAF_POSIX): `RendererAttemptOrder` returns {Vulkan} only
  (Direct3D switches and the graphics_api value are logged as ignored); the all-failed
  default-window retry uses Vulkan (and the Windows cursor) instead of D3D9/D3D10.
- `ninja -C build/macos faf_replay faf_game`: both link, no undefined or duplicate symbols.
  faf_game 15.8 MB. Map order (`build/link-run-tests/check_map_order.py src/sources.cmake
  macos build/macos/faf_game.map`): StaticInitPreRegister first, then the 934
  sources.cmake objects in order, then 39 platform/posix objects, the DXC bridge and
  GameMain.cpp.o. PASS.

### Level 2: window, Vulkan device, effects, frames (reached)

- `macos/run_game.sh` (new): run folder, /init init_faf.lua /nobugreport /nosound /nomovie
  /log, `SIZE` (/windowed w h, default 2560x1600 px), `REPLAY`/`REPLAYID`/`SPEED`
  (init_bench.lua + /benchspeed), `TIME` watchdog (`macos/tests/bringup/watchdog.py`:
  process group SIGTERM then SIGKILL), `VALIDATE=1`, `FAF_VULKAN_DRIVER`, `SHOTS=<frames>`
  (VK_LAYER_LUNARG_screenshot, PNGs via `macos/tests/bringup/ppm2png.py`).
- First run: "Vulkan: ready (1 head(s) on Apple M2 Max, driver MoltenVK 1.4.2 ...)",
  swap chain 1280x800 FIFO, all 11 effects of effects.nx2 loaded (344 entry points queued
  to the background compiler; cache ~/Library/Application Support/FAForever/ShaderCache
  filled: 345 files, 3.1 MB). But the window stayed black after the first frame and the
  main thread slept in MsgWaitEx.
- Cause: the game's frame loop runs on WM_PAINT. `CD3DDevice::Refresh` invalidates the
  viewport, `WIN_AppExecute` waits in `MsgWaitForMultipleObjectsEx(QS_ALLINPUT)`, which on
  Windows returns at once for a pending WM_PAINT (QS_PAINT), and `WD3DViewport::OnPaint`
  renders and presents. The shell's wait only woke on queued messages and SDL events.
  Fix (shell): `detail::PaintPending()` (Queue.cpp, a visible window waits for WM_PAINT)
  now ends `InputAvailable()` and `WaitForEvent()` too (Shell.cpp, ShellState.h,
  ShellInternal.h comment).
- After the fix: frames present at 60 Hz (vsync). VK_LAYER_LUNARG_screenshot frames show
  the first-run Profile Manager dialog (no profile in the native Game.prefs yet), drawn
  with readable text (build/macos/game-run4/profile_manager.png). 0 validation messages
  (validation not enabled in that run; see level 7).

### Level 3: replay 27890640 in the window (reached)

- `REPLAY=~/Gaming/faf/bench/replay_27890640.scfareplay SPEED=10 SIZE="1280 800"`: the
  replay loads (SCMP_016 Canis River, 256x256) and plays in the window; the default camera
  shows the whole map with strategic icons, the score panel, chat and the speed slider.
- Capture tooling: `macos/tests/bringup/shots/init_shots.lua` (init_bench.lua + a
  `shotshook` mount) and its `lua/ui/game/gamemain.lua` hook: with `/shots <dir>` it snaps
  the WorldCamera (Camera:SnapTo) to the start positions (maputil GetStartPositions), the
  map centre and low angles at set game ticks, and runs the engine's own `dump_Frame <dir>`
  console command (render-target readback -> DeviceVulkan::SaveTexture -> POSIX codec
  BMP), logging `SHOT <name> tick=...`. `bmp2png.py` converts; `pngdiff.py` diffs.
  The engine frame dump works on Vulkan.
- Looked at (build/macos/game-shots2): ACU and first buildings at 0:12 (Cybran textures,
  team colour, shadows, decals under buildings, props, sand ripples from the normal map);
  bases at 1:34 and 4:05; the middle at 8:06 and 9:26 (units, smoke, the river bed); a low
  angle at 10:43 (terrain relief, sun shading, the map border texture beyond the edge);
  fog of war with SetFocusArmy(1) at 11:46 (visible circles, the rest darkened, the
  economy panel). Nothing upside down, no black/pink textures, colours plausible.
- Water: SCMP_016 has water at elevation 17.5 over a river bed at 16.4 (about 1 unit
  deep). With `ren_Water false` vs on (same view): 0.49% of pixels differ, all in the river
  (diff build/macos/game-shots2/diff_water.png). FAF's terrain.fx already darkens and tints
  underwater terrain (ApplyWaterColor); the water surface adds ~10% water colour and a
  Fresnel reflection that is ~8% top-down, so a subtle surface is what the shaders
  compute. Not compared against a Windows frame (none here).
- High fidelity (later runs): the replay runs above used Game.prefs without a profile, i.e.
  FAF's option defaults: medium terrain, LOW water ("creating low fidelity water": the
  MediumFidelityPS overlay, alpha <= 0.3), low shadows, mip skip 1. With
  `PREFS=bringup-high.prefs` (fidelity 2, shadow_quality 3, texture_level 0,
  level_of_detail 2, shadow_resolution 4096): "creating high fidelity terrain/water"; the
  high-fidelity water (refraction + Fresnel sky/unit reflection + sun, masked by
  TWaterLayAlphaMask/TShoreline alpha) now changes 2.70% of the frame, exactly inside the
  river's shoreline (build/macos/game-shots-high/diff_water_high.png), so the alpha-mask
  chain (terrain alpha 0.01, mask pass, refraction copy with alpha, alpha test emulation)
  works on Vulkan. Explosions, fire and smoke visible at 10:44.

### Level 4: UI text, windowed replay to the end with 0 mismatches (reached)

- UI: score panel, chat, speed slider, unit info panel, tooltips, menus, dialogs: all text
  readable (fonts package's Core Text GDI; Zeroes Three and Arial from the game's fonts).
- Windowed replay 27890640 reached `BENCH end tick=9610` (the replay's last tick, as
  headless) in six runs: MoltenVK medium (game-run5, run6, run8), MoltenVK + validation
  (run14), KosmicKrisp + validation (run15), MoltenVK high fidelity + validation (run16).
  `grep -c 'Checksum for beat'` = 0 in every log (Sim.cpp:7975 logs each mismatch).
  ~110 s wall at speed +10 (about 8.7x real time) while rendering at 60 Hz.

### Level 5: camera input (reached)

- New `src/platform/posix/shell/TestInput.cpp` (+ declarations in ShellState.h, hooks in
  Shell.cpp Init, Queue.cpp, Input.cpp): `FAF_SHELL_TEST_INPUT="<frame>:<action> ...;..."`
  (debug only, off unless the variable is set) posts SDL events (move, down/up, wheel, key,
  text) with SDL_PushEvent at given WM_PAINT counts, so they take the shell's normal
  Input.cpp path. While a script runs the cursor is virtual (GetCursorPos/SetCursorPos and
  the other pointer reads use it; the middle-drag warp never moves the real pointer) and
  real mouse/keyboard/text events are dropped. Steps are logged as `[shell-test] frame N`.
- Replay, 1280x800 window (build/macos/game-run9, run17; VK_LAYER_LUNARG_screenshot):
  - wheel +6 notches at the window centre: overview -> close-up of the river (88% of pixels
    change; macos/porting/screenshots/replay-27890640-mouse-wheel-zoom.png);
  - Up arrow held 60 frames: the camera pans north to the map edge (90% change);
  - middle-button drag (640,400)->(760,440): the terrain follows the drag (rock moved
    (470,395)->(650,560)), and a middle click without a drag changes nothing (0.13%, unit
    motion only). In run9 a T1 Land Factory became selected around a middle drag that
    followed the Up key; not reproduced in run17 (open question, see below).
  - left clicks work on Maui buttons (level 6).

### Level 6: main menu (reached)

- Without REPLAY and with a fresh prefs file (`/prefs bringup-test.prefs`, a file name in
  the native prefs folder; an absolute POSIX path is joined to that folder, see open
  issues): first-run Profile Manager -> synthetic click "Create" -> "Enter your Name"
  dialog -> `text Mac` -> OK -> OK -> the main menu (Campaign, Skirmish, Multiplayer LAN,
  Replay, Mods Manager, Extras, Options, Exit) with the profile name
  (macos/porting/screenshots/main-menu.png). Exit -> "Are you sure you'd like to exit?"
  (with the button tooltip) -> Yes: the game quits with status 0 and saves the prefs file.
  That run used `FAF_GAME_STATIC_EXIT=1` (exit() with every static destructor): no abort,
  so the CDiskWatch fix holds on that path. The main-menu background movie is off
  (/nomovie).

### Level 7: validation, KosmicKrisp, frame rate (reached)

- Validation (`VALIDATE=1`, the engine's debug messenger, deduplicated by message id), the
  whole replay at medium and at high fidelity: 2 distinct messages, no errors in the
  engine's use of Vulkan:
  - `vkCreateGraphicsPipelines(): ... Output value declared at Location 0 ... no
    corresponding Input in [FRAGMENT]`: a performance warning (an unused vertex output),
    allowed by the spec.
  - `VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling primitive restart.`:
    MoltenVK's own report for strip topologies (Metal always restarts strips at 0xFFFF).
    It matters only for an indexed strip draw whose 16-bit indices contain 0xFFFF as a
    real vertex; not seen. KosmicKrisp does not report it.
- KosmicKrisp (`FAF_VULKAN_DRIVER=kosmickrisp`, + validation): "Vulkan: ready ... driver
  KosmicKrisp vulkan-sdk-1.4.363", replay to tick 9610, 0 mismatches, 1 validation
  message (the unused-output warning). Same shots as MoltenVK: the 8:06 frame differs in
  0.48% of pixels, all moving units/props (ticks 4849 vs 4857); terrain and water match
  (build/macos/game-shots-kk/diff_mid_mvk_vs_kk.png).
- Frame rate (`FAF_GPU_PROFILE=600`: wall, GPU and render-CPU ms per frame over 600
  frames; replay at speed +10 with the camera hook, high fidelity, MoltenVK):

  | Window | Present | Wall ms/frame | GPU ms/frame | Render CPU ms/frame |
  |---|---|---|---|---|
  | 1024x768 borderless (the game's own default, `SIZE=prefs`) | FIFO | 16.6-16.7 (60 fps) | 2.4-3.1 | - |
  | 2560x1600 (run_game.sh default, 1280x800 pt) | FIFO | 16.6-16.7 (60 fps) | 2.8-5.5 | 2.4-7.1 |
  | 2560x1600, vsync off | IMMEDIATE | 8.4-8.5 (~119 fps, the 8 ms frame-time cap) | 4.6-5.0 | - |
  | 6016x3328 (Retina full screen, 20 Mpix) | FIFO | 16.6-17.0 (59-60 fps) | 10.2-12.5 | 2.5-6.5 |

  The display is 60 Hz, so vsync holds 60 fps everywhere; the GPU has 1.3-6x headroom.

## Final state

- Headless regression after all changes (last build, CScApp `#else` wrap included):
  `SPEED=50 macos/run_replay.sh` ->
  `FAF_REPLAY_RESULT replay=27890640 beats=9716 mismatches=0 exit=end-of-replay`
  (76.9x real time). A last 30 s windowed replay check on the same build: Vulkan ready,
  0 mismatches, frames present. `unifdef -UFAF_POSIX` of CDiskWatch.cpp and CScApp.cpp
  matches HEAD's Windows view (CScApp: only an older trailing blank line from wave 2).
- No faf_game, faf_replay or watchdog process left running.
- Screenshots: macos/porting/screenshots/ (replay-27890640-acu-start.png,
  replay-27890640-mid-battle-9min.png, replay-27890640-cybran-base-low-angle-high-fidelity.png,
  replay-27890640-fog-of-war-overview.png, replay-27890640-mouse-wheel-zoom.png,
  main-menu.png).

## How to run

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:$PATH
ninja -C build/macos faf_game
# watch replay 27890640 in a 1280x800-point window (close it with the window's close box /
# the in-game menu, or Ctrl-C in the terminal):
REPLAY=/Users/scottharris/Gaming/faf/bench/replay_27890640.scfareplay macos/run_game.sh
#   + PREFS=bringup-high.prefs   high fidelity;  SPEED=10  replay at +10
#   + FAF_VULKAN_DRIVER=kosmickrisp;  VALIDATE=1;  SIZE="w h";  TIME=<s> for unattended runs
macos/run_game.sh                      # the main menu (first run: create a profile)
```

Capture tooling (unattended): `INIT=$PWD/macos/tests/bringup/shots/init_shots.lua` +
`/shots <existing dir>` (engine dump_Frame BMPs at set ticks; `macos/tests/bringup/bmp2png.py`),
or `SHOTS=<frame list>` (screenshot layer), `FAF_SHELL_TEST_INPUT=...` (synthetic input).

## Package requests applied

- shell: Init() before WIN_AppExecute, Shutdown() after (GameMain.cpp); /windowed for a
  normal window (run_game.sh SIZE).
- shaders: the exit path (std::_Exit, and the CDiskWatch atexit mutex fixed).
- vulkan: /vulkandebug or FAF_VULKAN_VALIDATION (run_game.sh VALIDATE), FAF_VULKAN_DRIVER.
- fonts: run_replay.sh's stale UI=1 comment replaced.
- shaders/textures/fonts (render.cmake comments) and render.md status rows: updated
  (comments only); wx.md's "Nothing is displayed" paragraph now says headless only.
- Not applied: fonts' HeadlessApp REN_Init request (headless UI=1 text metrics only; the
  game calls REN_Init itself), textures' DeviceNull UpdateSurface conversion (headless
  only), the README layout table (lead-owned), the shader-cache log text (cosmetic).

## Open issues

1. A window macOS cannot fit (e.g. /windowed 6016 3328 on a 3008x1692-point display) is
   shrunk by AppKit at creation (layer 6016x3080) without a WM_SIZE reaching the engine:
   the swap chain stays at the requested 6016x3328 and MoltenVK scales it into the
   smaller layer (slightly squashed). The shell could read the real size back after
   creation and send WM_SIZE (the frame then re-inits the device), or clamp the request.
2. /prefs with an absolute POSIX path is joined to the prefs folder
   (`...Forged Alliance\/Users/...`): the engine's absolute-path test knows drive letters
   only. A file name works.
3. The T1 Land Factory selection seen once after an Up-key pan plus middle drag (run9);
   a middle click alone does not select (run17). Worth one manual check with a mouse.
4. MoltenVK cannot disable primitive restart for strips (validation note above).
5. The frame dump (`dump_Frame`) writes `<dir>\SCFrame_...bmp`; the codec maps the
   backslash, but the directory must exist.
6. Sound and movies are off (/nosound /nomovie): milestone 3.
