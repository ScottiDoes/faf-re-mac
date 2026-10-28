# Porting notes: package "runner" (headless replay runner `faf_replay`)

## Result

The milestone-1 entry point is written and every file of it compiles with `macos/tu.sh`:

| File | What |
|---|---|
| `src/platform/posix/runner/Runner.h` | Shared declarations: result reporting, switches, `HeadlessApp` |
| `src/platform/posix/runner/RunnerMain.cpp` | `main()`: signal handlers, self-tests, die handler, `WIN_AppExecute`, result |
| `src/platform/posix/runner/HeadlessApp.cpp` | The `moho::IWinApp` that `WIN_AppExecute` runs |
| `src/platform/posix/runner/ReplayReport.cpp` | Counts checksum mismatches, prints `FAF_REPLAY_RESULT` |
| `src/moho/app/CScApp.h`, `CScApp.cpp` | Guarded addition (`#if defined(FAF_POSIX)`), see below |
| `macos/run_replay.sh` | Launcher with the benchmark's arguments |
| `/Users/scottharris/Gaming/faf-re-bin/run-native/` | Run folder (FAF init files) |
| `/Users/scottharris/Gaming/faf-re-bin/fa_path.lua` | Copy of `~/.faforever/replaydata/fa_path.lua` |
| `/Users/scottharris/Gaming/faf-re-bin/gamedata` | Symlink to `~/.faforever/replaydata/gamedata` (see "Run folder") |

Linking is the next phase. A link probe (the 944 existing engine objects plus the runner and a
fresh `CScApp.o`, in `build/runner-tests`, nothing written to `build/macos`) leaves exactly these
runner references unresolved, all owned elsewhere:

- `faf_posixRunSelfTests(bool)`: crt package (`SelfTest.h` exists, its `.cpp` not yet).
- `moho::REN_InitHeadless()`, `moho::REN_ShutdownHeadless()`: render-null.
- `APP_InitializeIdentity`, `CFG_GetArgOption`, `USER_LoadPreferences`, `USER_SavePreferences`,
  `USER_EnsureDocumentDirectories`, `USER_SetCurrentProfile`, `USER_DebugFacilitiesEnabled`,
  `DestroyMovieManagerSingleton`, `wnd_DefaultCreateWidth/Height`: `StartupHelpers.cpp`
  (still failing, sim-game).
- `moho::sc_FrameTimeClamp`: `CConCommand.cpp` (still failing, sim-game).

`_main` is no longer undefined, and no duplicate symbols appear.

## Command line

```
macos/run_replay.sh
```

The launcher `cd`s into the run folder and runs:

```
build/macos/faf_replay \
  /init /Users/scottharris/Gaming/faf-re-bin/run-native/init_bench.lua /nobugreport \
  /log build/macos/replay-27890640.log \
  /replay /Users/scottharris/Gaming/faf/bench/replay_27890640.scfareplay /replayid 27890640 \
  /benchspeed 10 /nosound /nomovie /timeout 3600
```

The arguments are the benchmark's (`~/Gaming/faf/bench/bench_replay.sh`, mode `fafre`) plus
`/nosound /nomovie /timeout`. `/init` is absolute: the engine resolves a relative one against the
launch directory, which would be `build/macos` once `DISK_GetLaunchDir` works on POSIX (see the
requests below).

Environment knobs:

- `REPLAY`, `REPLAYID`.
- `SPEED`: `/benchspeed`, default 10.
- `MAXMIN`: `/timeout` in minutes, default 60.
- `OUT`: where the log, result and checksum files go, default `build/macos`.
- `LOG`.
- `FAF_REPLAY_EXE`.
- `EXITONGAMEOVER=1`: adds `/exitongameover`.
- `SYNCLOG=1`: see "Per-beat checksums".

Extra arguments go to `faf_replay` unchanged.

### Expected output

Stdout carries exactly one line from the runner:

```
FAF_REPLAY_RESULT replay=27890640 beats=<last beat> mismatches=0 exit=end-of-replay
```

The launcher saves that line as `replay-27890640.result` and then adds two summaries:

- the sim rate, from the benchmark hook's `BENCH` lines (the same awk as `bench_replay.sh`);
- `checksum mismatches in the log: N`, counted with the same grep as the benchmark.

Each mismatch is also echoed live to stderr as `faf_replay: Checksum for beat N mismatched: ...`.

The log ends with:

```
FAF_REPLAY_RESULT ...
faf_replay: last beat B, game tick T, first mismatch at beat M
Run time: 0h04m12s
```

`M` is -1 when nothing mismatched.

**Pass criterion for milestone 1:** `mismatches=0` and `exit=end-of-replay` (or `exit=gameover`
with `/exitongameover`), with `beats` matching the x64 run of the same replay. The x64 benchmark
log's `BENCH end tick=N` is a tick, not a beat; compare it with `game tick T` in the log.

### Fields

- `replay`: `/replayid`, else the replay file's stem. Whitespace becomes `_`.
- `beats`: the number of the last sim beat the user side applied. Beats are numbered from 0. The
  value comes from `moho::gUserSyncBeats` (`CWldSession.cpp`) minus 1, read after
  `WLD_Teardown`, which applies the sync packets still queued.
- `mismatches`: the number of `Checksum for beat %d mismatched: %s (sim) != %s (%s).` warnings
  the sim logged. `Sim::VerifyChecksum` (`moho/sim/Sim.cpp:7969`) logs them on every platform.
  A gpg log target in `ReplayReport.cpp` counts them on whichever thread logs. This is the
  same count as `grep -c 'Checksum for beat'` in the benchmark.
- `exit`: why the run ended.

### Exit reasons and status

| `exit=` | When | Status |
|---|---|---|
| `end-of-replay` | No new beat for `/stalltimeout` s after game over. A replay stream ends with an end-game command (`CReplayClient::Start`), so a finished replay always reaches game over. | 0 (1 with mismatches) |
| `gameover` | `/exitongameover` (the engine's own, `CWldSession::DoBeat`) | 0 / 1 |
| `stalled` | No new beat for `/stalltimeout` s and no game over: a hang | 2 |
| `session-ended` | The world session went away (Exit / teardown) | 2 |
| `exit-requested` | The loop ended for another reason (`ExitApplication`, ...) | 2 |
| `timeout` | `/timeout` s of wall time | 2 |
| `interrupted` | SIGINT / SIGTERM (a second Ctrl-C kills) | 2 |
| `load-failed` | `/replay` did not start a session (the reason is on stderr: `WIN_OkBox` text) | 2 |
| `init-failed` | `AppInitCommonServices` or `REN_InitHeadless` failed | 2 |
| `selftest-failed` | `faf_posixRunSelfTests(true)` failed (no `/skipselftest`) | 2 |
| `usage` | No `/replay` | 2 |
| `die` | `gpg::Die` (message on stderr) | 3 |
| `crash-SIGSEGV` etc. | Fatal signal. Written async-signal-safe from atomics, then the signal is re-raised. | 128+n |

Mismatches always give status 1, whatever the reason. After printing the result, the runner
leaves with `_Exit` (see "Design").

## Switches

### Engine switches the runner relies on

These are handled where Windows handles them:

- `/init`, `/log`: `IWinApp::AppInitCommonServices`.
- `/replay`: `CScApp`'s `InitializeSessionFromCommandLine`.
- `/replayid`: Lua.
- `/nobugreport`, `/nosound`, `/nomovie`: the engine.
- `/exitongameover`: `CWldSession::DoBeat`.
- `/framerate N`: through `CScApp`'s `TryReadFixedFrameDeltaSeconds`, so the session clock gets
  a fixed `1/N`.
- `/sse2`: `SetSse2Mode`.
- `/prefs`, `/profile`, `/con`, `/synclog`, `/debug`, and so on: as on Windows.

`/benchspeed N` is read by the benchmark's Lua hook. `init_bench.lua` mounts
`~/Gaming/faf/bench/mount/benchhook`, whose `gamemain.lua` `CreateUI` hook sets
`WLD_GameSpeed N` and logs `BENCH` lines. The UI Lua therefore runs.

### Runner switches

| Switch | Default | Effect |
|---|---|---|
| `/stalltimeout <s>` | 10 | Seconds without a new beat (after the first) that end the run (0: never) |
| `/timeout <s>` | 0 (none) | Wall-clock limit |
| `/skipselftest` | off | Run even when the platform self-tests fail (says so on stderr) |
| `/echolog` | off | Mirror every log line to stderr |
| `/noui` | off | Fallback; see "UI" below. The runner sets the `/benchspeed` game speed itself. |

## Design

### Start-up

`main()` (`RunnerMain.cpp`) does what `WinMain` does that matters here:

1. Names the thread "Main".
2. Installs the die handler (in place of `FatalErrorDieHandler`).
3. Runs `WIN_AppExecute(&app)`, catching `std::exception` into `gpg::Die` as `WinMain` does.
4. Logs `Run time:`.

Before step 3 it installs signal handlers, starts the mismatch count, and calls
`faf_posixRunSelfTests(true)`. It refuses to start unless that returns true or `/skipselftest`
is given. The command line itself was already published by
`startup/StaticInitPreRegister.cpp`.

### `HeadlessApp` (`HeadlessApp.cpp`)

`HeadlessApp` follows `CScApp` step by step.

**`Init`** is `CScApp::Init`'s order:

1. `APP_InitializeIdentity`.
2. `SetSse2Mode(/sse2 ? 1 : 0)`. Without `/sse2` the SSE2 lane is off; `global_mode_sse2`
   starts at 1.
3. `AppInitCommonServices`.
4. `USER_LoadPreferences`.
5. `/lua/userInit.lua`.
6. `USER_EnsureDocumentDirectories`.
7. `/profile`.
8. `USER_DebugFacilitiesEnabled`.
9. `UI_Init`.
10. `REN_InitHeadless()` in place of `CreateDevice`.
11. What `CreateAppFrame` does without the device:
    - an unshown `WSupComFrame` as the main window;
    - `WINX_PrecreateLogWindow`;
    - `REN_CreateGameViewport` → `ren_Viewport`;
    - `UI_GetManager()->AddFrame(viewport, frame)`.

The head size is the null device's head 0, else `wnd_DefaultCreateWidth` × `Height` (1024×768).

Left out:

- `/splash`;
- `REN_Init` (fonts);
- the renderer attempts and `Device::Create`;
- `ConfigureStartupGraphicsOptionGroups` and `SetupBasicMovieManager`;
- `OPTIONS_Apply`;
- the screensaver and the cursor clip.

**`Main`** is `CScApp::Main`'s frame:

1. Heap stats.
2. The frame-time clamp (`sc_FrameTimeClamp`, an early return with a wake-up timer).
3. The rolling frame-rate ring and `wld_FrameClock` smoothing.
4. `UI_SetRenderWorldFrameSeconds`, `SND_Frame`, `DISK_UpdateWatcher`.
5. `WLD_Frame`.
6. `CAM_GetManager()->Frame`.
7. `sUserStage->UserFrame()`.
8. `ADXM_ExecMain`.
9. User sound `Frame`.
10. UI `UpdateFrameRate`.
11. On the first frame, `InitializeSessionFromCommandLine`.

There is no `REN_Frame` and no `Refresh`. Instead of presenting, the runner always takes
`CScApp::Main`'s minimized branch:

- It sleeps until the next beat is due (`GetDelayToNextBeat`).
- If a beat is already due, it waits on the driver's sync-data event.
- Sleeps are capped at 100 ms, so the run-end checks and the user stage's Lua threads keep
  running.
- Outside a playing session, frames run at the `sc_FrameTimeClamp` rate.

The runner removes the sync event it added at the start of the next frame and in `Destroy`. It
re-reads the driver after `WLD_Frame`, so it never touches a driver destroyed during the frame.

**`Destroy`** is `CScApp::Destroy`'s order:

1. `WLD_Teardown`.
2. `GPGNET_Shutdown`.
3. `UI_Exit`.
4. Session loader `Finalize`.
5. `DestroyMovieManagerSingleton`.
6. `REN_ShutdownHeadless` in place of `D3D_Exit`.
7. Clear the main window and `ren_Viewport`.
8. `SND_Shutdown`.
9. `SCR_DestroyDebugWindow`.
10. `USER_SavePreferences`.
11. Destroy the frame.

### `CScApp` exposure

`CScApp.h` and `CScApp.cpp` gain one `#if defined(FAF_POSIX)` block each, in namespace
`moho::scapp`. The functions forward to `CScApp.cpp`'s file-local pieces unchanged, so the runner
runs the same session start and frame clock rather than a copy:

- `InitializeSessionFromCommandLine()`
- `TryReadFixedFrameDeltaSeconds()`
- `GetFrameClockMode()` (`wld_FrameClock`)
- `PublishFrameHeapStats()`

The Windows text is unchanged.

### The wx app

`WIN_AppExecute` (`stubs/wx/WinAppPosix.cpp`) builds `MohoApp` through `wxEntry` before
`Init`, so `wxTheApp` exists for `SimDriver`, `cfunc_ExitApplication` and `/exitongameover`.

### Exit

After the result is printed, the runner leaves with `_Exit`. The gpg stream target flushes after
every line (`pubsync`), and `Destroy` has already closed the session, preferences and sim logs.
Skipping the engine's static destructors means a teardown-order problem cannot turn a finished
run into a crash status. A failed `Init` also reports and exits from `FailInit`.
`WIN_AppExecute` would `_Exit(1)` there, and 1 means mismatches.

### UI

The UI runs by default. It is needed for the benchmark hook, and it matches the x64 run, where
the UI runs too. The sim does not depend on it: the replay stream feeds the sim, and the user
side only consumes sync packets.

`/noui` creates the frame and log window but no UI head, so the UI Lua cannot build controls.
Its errors are logged as warnings, and no world views or text are created natively. Use it if a
UI code path crashes natively. The UI manager still exists, because `WLD_Teardown` calls
`g_UIManager->SetNewLuaState` unconditionally.

### Per-beat checksums (no `/checksumlog`)

I did not add `/checksumlog`. The engine's own facility is enough, and the x64 build has it too,
which a native-only switch would not:

- `/synclog <dir>` opens the sim log.
- `Sim::Sync` writes `beat N final checksum: <md5>` for every beat into
  `<dir>/<prefix>/beatNNNNN.log`.
  - Beats off the `sim_ChecksumPeriod` (50) carry zeros.
  - Checksum beats also dump armies, economy and every dirty entity's state.
- Old beat files are deleted, except those around a desync. `/con sim_KeepAllLogFiles=true`
  keeps all of them.

`SYNCLOG=1 macos/run_replay.sh` passes both switches and collects the checksum beats into
`$OUT/checksums-<id>.txt`, sorted by beat. Running `main_x64.exe` with the same switches
(benchmark `FAFRE_ARGS`) gives the file to compare beat by beat.

For bisecting inside a 50-beat window, the synclog dumps at the checksum beats are the tool.
`sim_ChecksumPeriod` is a per-sim variable (`TSimConVar`), not a user console variable, so
`/con` does not reach it before the sim exists. A shorter period would also change the digests
that the recorded checksums are compared against.

## Run folder

`/Users/scottharris/Gaming/faf-re-bin/run-native/` holds copies of `init.lua`, `init_faf.lua`,
`init_bench.lua`, `SupComDataPath.lua` and `SupComDataPathFAF.lua` from `run-replaydata-x64/`.
It also holds `fafengine -> ../v1.1.2/x64/fafengine`, as in the x64 run folder; this is the
engine hook folder with `lua/options/options.lua`, UI only.

FAF's `init.lua` reads two things from the parent of its own folder. `InitFileDir` is the
canonical (symlink-resolved) folder of the `/init` script, `/Volumes/SSD Programs/Gaming/faf-re-bin/run-native`,
so its parent is `faf-re-bin`. That folder therefore needs both:

- `faf-re-bin/fa_path.lua`: a copy, as the task asked. It already holds POSIX paths with spaces.
- `faf-re-bin/gamedata`: a symlink to `~/.faforever/replaydata/gamedata`, which I created. It
  provides FAF's `lua.nx2`, `units.nx2` and so on (game version 3839). Without it, the base
  game's Lua would load and the replay could not play.

The x64 run reads the same folder live: under Wine, `bin_x64/..` resolves lexically to
`~/.faforever/replaydata`. Nothing under `~/.faforever` was modified.

### Windows-only assumptions in the init Lua and on its path

- **`SupComDataPath.lua`** does `dofile(InitFileDir .. '\\init_faf.lua')`, with a backslash.
  Lua's `dofile`, `io.open` and `os.remove` use `std::fopen`/`std::remove` directly
  (`lua/LuaObject.cpp:6328,6382,6529,6765,15880,15914`), not the compat file layer, so this
  would fail on POSIX. It is not used: `/init init_bench.lua` replaces it.
- **`SHGetFolderPath('LOCAL_APPDATA')`** returns the compat layer's Windows form with a trailing
  backslash, `C:\Users\scottharris\Library\Application Support\`. `init.lua` then appends
  `Gas Powered Games/...`.
  - `io.dir` (`_findfirst64`) and the VFS mounts go through the compat layer and accept it.
  - The shader-cache clean-up's `os.remove` gets the Windows form and fails silently, so
    nothing is deleted. The folder does not exist on this Mac. This is harmless.
- **`fa_path` and `custom_vault_path`** are POSIX absolute paths with spaces. Both the file
  layer and `std::fopen` accept them.
- **Mount order**: `io.dir` order matters for mount priority (`MountAllowedContent`). The Files
  area already sorts directory listings in NTFS order (`WinCompatFiles.cpp:505`).
- **Launch and engine-hook folders**: `DISK_GetLaunchDir()` and `AppendEngineHookFolder` build
  `std::filesystem::path` from `_fullpath` / `GetModuleFileNameW`, which return
  `C:\Volumes\...\faf_replay` on POSIX. `parent_path()` of that is empty; I measured it in
  `build/runner-tests/pathprobe.cpp`. The consequences:
  - The launch directory silently becomes the current directory.
  - `fafengine` is looked up relative to the current directory.

  `run_replay.sh` runs from the run folder, so both come out as they do on x64, where the game
  runs from its bin folder. This is a real POSIX bug, though; see the requests below.

## Tests

1. **Compile.** `macos/tu.sh` passes for:
   - `runner/RunnerMain.cpp`, `runner/HeadlessApp.cpp`, `runner/ReplayReport.cpp`;
   - `moho/app/CScApp.cpp`.

   The runner files are also clean with `-Wall -Wextra`.
2. **`ReplayReport` unit test** (`build/runner-tests/report_test`). It is built from the real
   `ReplayReport.o` and the engine's `String.o`, with a test double for the gpg log functions.
   All 7 scenarios pass:
   - counting: Warn only, malformed lines ignored, first-mismatch beat logged;
   - the first exit reason wins;
   - the result is printed once;
   - exit codes 0, 1 and 2;
   - id sanitising;
   - the `unknown` fallback;
   - `die` does not log;
   - the signal-safe `crash-SIGSEGV` and `crash-SIGBUS` forms;
   - 64-bit beat counts.
3. **Launcher** with a fake runner (`build/runner-tests/fake_runner.sh`,
   `FAF_REPLAY_EXE=... OUT=build/runner-tests`):
   - the working directory is the run folder;
   - the exact argument list;
   - the result is saved;
   - the `BENCH` rate line prints (`10.00x`);
   - the mismatch count;
   - `SYNCLOG=1` collects the checksum beats (3 of 5 files, zero digests dropped, sorted);
   - the status is passed through;
   - the missing-runner check gives status 2.
4. **Link probe**: see "Result".

The runner cannot be run until the engine links. The next phase should:

- run `macos/run_replay.sh`;
- check the result line;
- compare `beats` and the game tick with the x64 run's log.

## Stubs

None. The runner adds no `FAF_POSIX_STUB`. Nothing it leaves out is reached on the headless path.

## Open issues

- **Not yet run.** Everything after link time is unverified: the UI Lua on a 1024×768 hidden
  head with the null device; `GetDelayToNextBeat` pacing; the stall-based end detection.
  - If the UI crashes natively, use `/noui`, which relies on the runner's own `/benchspeed`.
  - If the stream end does not reach game over, the run ends `stalled`, not `end-of-replay`.
    The log line `no new beat ... game over yes/no` tells which.
- **`gUserSyncBeats`** is an engine global without a header. The runner declares it `extern`.
  If sim-game makes it file-local, `beats` needs another source.
- **Checksum comparisons are not counted.** Only mismatches are logged, so the result cannot
  say how many recorded checksums were compared. A run with `mismatches=0` and a low `beats`
  value is visible, but not a run where verification silently never happened.

## Requests

### lead

- Confirm the `/Users/scottharris/Gaming/faf-re-bin/gamedata` symlink. It is outside my listed
  files, but `init.lua` needs it for the same reason as `fa_path.lua`.
  - The alternative keeps everything inside `run-native`: a `run-native/bin/` init folder, with
    `run-native/fa_path.lua` and `run-native/gamedata` next to it, mirroring
    `~/.faforever/replaydata`.

### render-null

- The runner calls `REN_InitHeadless()` after `UI_Init` and then creates `ren_Viewport` (a
  `WRenViewport`, never shown).
- It does not call `CD3DDevice::SetRenViewport`, because that runs `InitContext` again.
  If the device needs the viewport, either:
  - add a viewport parameter to the contract, or
  - say so and the runner will call it.
- The UI head size is read from `Device::GetInstance()->GetDeviceContext()->GetHead(0)`, so give
  the null head a size (for example 1024×768).

### sim-game / core-sys

- **`StartupHelpers.cpp:5657`** (`DISK_GetLaunchDir`) and **`:802`** (`AppendEngineHookFolder`):
  under `FAF_POSIX`, turn the Windows-form result of `_fullpath` / `GetModuleFileNameW` into a
  POSIX path before building `std::filesystem::path`. Use `faf_posix::NativePath`, or
  `'\\'` → `'/'` and drop the `C:`. Today `parent_path()` is empty, so:
  - the launch directory falls back to the current directory;
  - `/fafengine` is looked up relative to the current directory;
  - `DISK_GetLaunchDir() / ".." / "Installed.Prefs"` (`:6083`) is relative.

  The same pattern may exist wherever compat path outputs feed `std::filesystem`.
- **Lua's file functions**: `lua/LuaObject.cpp` `io.open`, `io.lines`, `os.remove`, `dofile` and
  `loadfile` use `std::fopen`/`std::remove` directly. On POSIX they reject the Windows-form
  paths that the engine itself hands to Lua (`SHGetFolderPath`, backslashes).
  - They could go through `fopen_s`, or the compat path translation under `FAF_POSIX`.
  - This is not on the replay path; it matters for FAF's shader-cache clean-up and
    `SupComDataPath.lua`.
- **Optional:** an engine counter of verified (matching) checksums in `Sim::VerifyChecksum`,
  readable after teardown, so that the result line can report `checked=`.

## After the link phase (macos/porting/link-run.md)

faf_replay links and plays replay 27890640 to the end with 0 mismatches (`SPEED=50`:
ticks 1..9610 in 11.7 s, 82x real time). Changes to this package's files and behaviour:

- **/noui is now the launcher's default** (`UI=1` restores the UI): without font metrics the
  in-game UI Lua never finishes building (fill-the-panel loops on 0-high text). The UI section
  above describes UI=1.
- The game viewport is created and published as `ren_Viewport` before `REN_InitHeadless`, so
  the null device binds it (render-null's request).
- /noui sets the game speed with the `WLD_GameSpeed` console command (range -10..50, as the
  benchmark hook does), not `WLD_SetGameSpeed` (stops at +10).
- The runner logs `faf_replay: sim rate: ticks A..B in S s = R ticks/s = Xx real time`;
  run_replay.sh prints it when there are no BENCH lines, and makes OUT/LOG absolute.
- Every exit goes through `_Exit` (a plain exit() aborts in a static-destruction ordering
  problem, see link-run.md), and the result is followed by `faf_crtmathTraceDump()`.
