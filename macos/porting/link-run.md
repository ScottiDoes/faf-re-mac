# Porting notes: link-run (link and run faf_replay)

Running log, newest entries at the bottom of each level. Resume from the last "NEXT" line.

## Commands

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:/Users/scottharris/Gaming/rosettax87/.venv/bin:$PATH
cmake -S macos -B build/macos
ninja -C build/macos -k 0 -j 10 faf_engine
ninja -C build/macos faf_replay
```

## Log

- Start: full reconfigure and rebuild (layout changes from wave 2: boost count block, msvc8::vector<T,false>).

### Level 1: faf_engine compiles (reached)

- `cmake -S macos -B build/macos` then `ninja -k 0 -j 10 faf_engine`: 961/961 steps, 0 failures,
  1 min 26 s wall (log `build/macos/link-run-engine.log`). Warnings: 30 -Wundefined-bool-conversion,
  2 -Winvalid-noreturn, 2 boost -Wself-assign-field (all upstream or justified, see platform.md).
  The packages' wave-2 fixes already covered the 7 stragglers; nothing to fix here.

### Level 2: faf_replay links (reached)

- First link failed: `-Wl,-map,<build>/faf_replay.map` in `target_link_libraries` was split at the
  space in `/Volumes/SSD Programs`. Fixed in `macos/CMakeLists.txt`: the map option moved to
  `target_link_options(faf_replay PRIVATE "LINKER:-map,...")`, which CMake quotes.
- Second link: clean, no undefined symbols, no duplicate symbols, no ld warnings.
  `build/macos/faf_replay` 15.4 MB.
- Map order (`build/link-run-tests/check_map_order.py src/sources.cmake macos build/macos/faf_replay.map`):
  StaticInitPreRegister.cpp.o is object [1]; the next 934 objects are exactly src/sources.cmake
  minus exclusions with each stand-in at its original's position; then 25 platform/posix objects;
  then the 3 runner objects. PASS.
- `macos/tests/crt/check_symbols.sh build/macos/faf_replay`: 286 libSystem imports, all 33 math
  functions and rand/srand defined in the executable. PASS.
- `macos/tests/crt/check_fma.sh` on the current Volumes/ subtree of faf_engine.dir, faf_runner.dir,
  libWm3Foundation.a, libfaf_zlib.a: 965 objects, 0 FMA. Also `otool -tv faf_replay`: 0
  fmadd/fmsub/fnmadd/fnmsub/fmla/fmls in the linked binary. PASS.
- Runner changes made before the link (requests from render-null and crt):
  - `runner/HeadlessApp.cpp`: CreateHeadlessFrame now creates the game viewport and sets
    `ren_Viewport` BEFORE `REN_InitHeadless()`, so the device binds it (CreateAppFrame order;
    D3DWindowOnDeviceInit runs). /noui binds no viewport.
  - `runner/ReplayReport.cpp`: `ReportFinish` calls `faf_crtmathTraceDump()` after printing the
    result (every runner exit path is `_Exit`, which skips the atexit dump).

### Level 3: faf_replay starts (reached)

- No-argument run: static initializers run, usage printed, `exit=usage`. Then the process
  aborted in static destruction: `DISK_ResetWatch` (an atexit handler CDiskWatch registers during
  static init, before CDiskWatch.cpp's own `std::mutex` is registered for destruction) locks the
  destroyed mutex, libc++ throws "mutex lock failed". Fixed in the runner: every exit path is
  `std::_Exit` after flushing (`LeaveProcess` in RunnerMain.cpp), as the normal end already was.
  The engine issue stays (any POSIX `exit()` path would hit it): open issue below.
- With `/replay`: the platform self-tests pass, 75/75 (FPCR, saturation, fused-vs-unfused,
  MSVC rand, faf_crtmath bindings and NaN rules, Wm3 constants, x87 known answers).
- Crash 1 (SIGBUS in `luaS_newudata` during `DISK_SetupDataAndSearchPaths`, the first Lua state):
  root cause is an ABI mismatch. `WrapFileTypeInfo` stores `RRef* f(RRef* out, WrapFile*)` into
  `RType::ctorRefFunc_` (`RRef (*)(void*)`) through `reinterpret_cast`: valid only under MSVC's
  hidden-return-pointer convention. On AAPCS64 the storage pointer arrives in x0 and the
  "WrapFile*" is whatever x1 holds, so the callee wrote through a stale register. It showed only
  with the small pool on (register contents differ). Fix in `src/lua/LuaObject.cpp`: under
  FAF_POSIX two by-value adapters (`NewWrapFileStorageRefByValue`,
  `ConstructWrapFileStorageRefByValue`) are stored at all four registration sites; the Windows
  text is in the `#else` branches. A search found no other cast of an `RRef`-returning
  function type (`new/cpy/ctor/mov_ref_func_t` are the only by-value function-pointer types).
  Diagnosis: an instrumented copy of Global.cpp (pool range and free-list checks, built and
  linked only in build/link-run-tests/dbg) showed the pool never returned the bad pointer.
- Hang 1 (deadlock, main thread in `StatItem::GetString` -> `boost::mutex::do_lock`):
  `STAT_GetLuaTable` holds a StatItem's lock and `GetString` -> `SetValueCopy` takes it again.
  On Windows boost 1.34's `boost::mutex` and `try_mutex` are critical sections (re-entrant); the
  pthread build made them non-recursive. Fix in `dependencies/boost_1_34_1/libs/thread/src/mutex.cpp`
  (FAF_POSIX branch): both are created as `PTHREAD_MUTEX_RECURSIVE`. `boost_1_34_1_faf_macos.patch`
  regenerated with `git -C dependencies/boost_1_34_1 diff` (7 files; the -N entry for
  sp_counted_base_faf_posix.hpp kept); `git apply --reverse --check` passes.

### Level 4: the replay session runs (reached)

- Data mounts (FAF gamedata, vault maps/mods), blueprints load (606 units, 393 projectiles),
  the map and sim Lua load, armies are created, the null renderer loads all 11 effects and binds
  the game viewport, beats advance.
- Hang 2 (UI only): with the game UI running, the user Lua state grows without bound
  (`FAF_LUAGC_LOG=1`: live memory doubles every collection, 113 M allocations and 8 GB within
  a minute, then the 32-bit `nblocks` counter wraps and the GC runs on every check). Cause: the
  POSIX font stub (`CD3DFont::Create` returns no font, CD3DFontPosix.cpp) gives every text
  control height 0, and FAF's fill-the-panel loops never end, e.g. `lua/ui/game/transmissionlog.lua:88`
  `while entries[n].time.Bottom() + entries[1].time.Height() < container.Bottom() do <add entry>`
  (also objectiveDetail.lua:168, :325). Not a sim problem. `macos/run_replay.sh` now passes
  `/noui` by default (`UI=1` restores the UI); the runner then sets the game speed itself.
- Runner changes for this level: the /noui game speed goes through the `WLD_GameSpeed` console
  command, as the benchmark hook does (`WLD_SetGameSpeed` clamps to +10, the console command to
  +50); the runner logs its own sim rate (`faf_replay: sim rate: ...`, first moving tick to last);
  run_replay.sh makes OUT/LOG absolute (the run happens in the run folder) and prints that rate
  when there are no BENCH lines.

### Level 5: the replay runs to the end (reached)

- `SPEED=50 macos/run_replay.sh` (pool on, /noui):
  `FAF_REPLAY_RESULT replay=27890640 beats=9710 mismatches=0 exit=end-of-replay`,
  last game tick 9610 (= the x64 benchmark's `BENCH end tick=9610`), 0 "Checksum for beat" lines.
  Sim rate 9609 ticks in 11.9 s = 810.7 ticks/s = 81.1x real time (x64 build under Rosetta,
  same replay, speed +50: 17.3x). Wall time 26.6 s including load and the 10 s end-of-stream
  wait; max RSS 658 MB.
- Speed 10 (the script's default): 110.7 s wall, 10.0x real time (paced by the game speed).
- `beats=` varies run to run (9624..9710): the sim keeps issuing empty beats after the stream's
  end-game command until the stall timeout. The game tick (9610) is the comparable number.
- Verification really happens (instrumented copy of Sim.cpp in build/link-run-tests/dbg, not in
  the tree): `Sim::VerifyChecksum` is reached for both players every 50 beats, beat 0 through
  ~9600 (over 350 comparisons, all "match"), never "old", "future" or "invalid source".
  Negative control: `FAF_DBG_CORRUPT_BEAT=5000` flips one byte of the incoming digest for beat
  5000 in that debug binary only; the runner then reports `mismatches=2` (one per player), exit 1.

### After level 5: run-folder clean-up and re-checks

- The runs left two entries in the run folder, literally named
  `C:\Users\scottharris\Library\Application Support` (a directory tree ending in `cache`) and
  `C:\...\Game.prefs.new`: `USER_GetAppCacheDir` (std::filesystem::create_directories) and
  `USER_SavePreferences` (std::ofstream) handed the engine's Windows-form paths to the C++
  library, which does not go through the compat file layer; the prefs rename then failed
  ("unable to move file to C from C [error 2]"). Fix:
  - compat Files: new POSIX extension `faf_posixHostPathA/W` (compat/Files.h,
    WinCompatFiles.cpp): the host path the layer would use (separators, drive, case).
  - StartupHelpers.cpp: a FAF_POSIX `HostPathForStdLib` helper; the cache directory is created
    and the `.new` prefs file opened by host path (both guarded, Windows text in `#else`).
  - Result: no stray files; `~/Library/Application Support/Gas Powered Games/Supreme Commander
    Forged Alliance/Game.prefs` and `cache/` are written as the game does on Windows in
    %LOCALAPPDATA%. I removed the two stray entries my runs had created in run-native.
- Full rebuild after the compat header change: 0 failures. Map order, check_symbols.sh and the
  binary FMA scan re-run: all PASS.
- Final run, `SPEED=50 macos/run_replay.sh`: `beats=9712 mismatches=0 exit=end-of-replay`,
  ticks 1..9610 in 11.7 s = 82.2x real time. Native log vs the x64 benchmark log: the only
  warnings unique to native are the /noui UI-Lua errors ("UI layer has not been initialized"
  and the imports that need it), "../Installed.Prefs" and "Blueprints.lua - Preferences file is
  not found" (no prefs yet in the native location; not on the sim path, checksums match).
- Default command `macos/run_replay.sh` (speed 10, /noui), output in build/macos:
  `FAF_REPLAY_RESULT replay=27890640 beats=9660 mismatches=0 exit=end-of-replay`,
  ticks 1..9610 in 96.0 s = 10.0x real time (paced). Log build/macos/replay-27890640.log.
- `FAF_CRTMATH_TRACE=1` run (same result, 0 mismatches), trace in
  build/link-run-tests/run13/crtmath_trace.txt. Hot sim-side libm sites: fmod 6.2 M calls
  (CEfxEmitter::Tick, NormalizeAngleSignedRadians from the aim/builder/rotate manipulators,
  WrapPositiveModulo; fmod is exact, so implementation-independent), sinf/cosf 1.24 M each
  (CDecalBuffer::IsDecalVisibleForArmy, CWldTerrainDecal::Update/ComputeCorner,
  cfunc_CreateSplat/CreateDecal), atan2f 97 k (CreateDecalFromTransform from Lua
  CreateSplatOnBone), pow 30 k (msvc8::powf in CSimDriver::ThreadRun pacing, WLD_GetSimRate),
  Lua math.mod 2.7 k. No domain errors or NaN operands outside the self-test.

## How to run

```
cd "/Volumes/SSD Programs/Gaming/faf-re"
export PATH=/opt/homebrew/bin:$PATH
ninja -C build/macos faf_replay
SPEED=50 macos/run_replay.sh          # full speed; default SPEED=10 is paced at 10x
# UI=1 adds the game UI (hangs until fonts exist), SYNCLOG=1 per-beat checksums,
# FAF_CRTMATH_TRACE=1 FAF_CRTMATH_TRACE_FILE=<file> libm call-site trace
```

Debugging: lldb cannot attach here (DevToolsSecurity is off; `lldb -b -o run` waits for an
authorization prompt nobody answers). Crash reports work: the runner re-raises fatal signals,
macOS writes ~/Library/Logs/DiagnosticReports/faf_replay-*.ips, and
`python3 -I build/link-run-tests/crashbt.py [file.ips]` prints the crashing thread. For a hang,
`sample <pid> 3`. `build/link-run-tests/dbg/relink.py <out> <src-rel>=<obj>` relinks a test copy
of faf_replay with instrumented objects without touching build/macos.

## Files changed in this phase

- `macos/CMakeLists.txt`: map-file link option via `target_link_options` (quoted path).
- `src/platform/posix/runner/HeadlessApp.cpp`, `Runner.h`: viewport before REN_InitHeadless;
  /noui speed via the `WLD_GameSpeed` console command; own sim-rate log line.
- `src/platform/posix/runner/RunnerMain.cpp`: every exit through `LeaveProcess` (`_Exit`).
- `src/platform/posix/runner/ReplayReport.cpp`: `faf_crtmathTraceDump()` after the result.
- `macos/run_replay.sh`: /noui default (`UI=1`), absolute OUT/LOG, sim-rate fallback.
- `src/lua/LuaObject.cpp`: WrapFile by-value reflection adapters (FAF_POSIX).
- `dependencies/boost_1_34_1/libs/thread/src/mutex.cpp` (+ regenerated
  `dependencies/patches/boost_1_34_1_faf_macos.patch`): recursive boost::mutex/try_mutex.
- `src/platform/posix/compat/Files.h`, `src/platform/posix/WinCompatFiles.cpp`:
  `faf_posixHostPathA/W`.
- `src/moho/misc/StartupHelpers.cpp`: host paths for the cache directory and prefs file.
- Test tooling (untracked, build/link-run-tests): check_map_order.py, crashbt.py,
  dbg/relink.py and the instrumented Global/Sim copies; run folders run5..run13.

## Open issues

1. **UI mode** (`UI=1`): needs font metrics. Either the Core Text font of milestone 2 or a
   metrics-only `CD3DFont` (ascent/descent/advance without glyphs) in CD3DFontPosix.cpp; the
   latter is a render decision. The UI is user-side only; replay checksums do not depend on it.
2. **Static destruction on POSIX `exit()`**: `DISK_ResetWatch` (atexit, registered from static
   init before CDiskWatch.cpp's own initializer) locks `gDiskWatchInitMutex` after libc++ has
   destroyed it and aborts. The runner never calls exit(); a real game binary would. Engine
   fix: make that mutex never-destroyed under FAF_POSIX (CDiskWatch.cpp:33), and check other
   atexit-from-static-init users the same way.
3. **Windows-form paths into the C++ library**: fixed only for the cache dir and prefs. Others
   noted by the runner package: `DISK_GetLaunchDir`, `AppendEngineHookFolder`
   (std::filesystem parent of `C:\...`), and Lua io/os functions that call std::fopen /
   std::remove directly (shader-cache `os.remove`). `faf_posixHostPathA/W` is the tool for them.
4. **MSVC-ABI function-pointer casts**: the WrapFile one was the only `RRef`-returning cast; the
   general class (a function stored through a pointer type whose by-value struct return MSVC
   passes as a hidden first argument) could exist elsewhere with other struct types. A
   systematic scan (every `reinterpret_cast` to a function-pointer type, comparing return types)
   is worth doing before milestone 2.
5. `beats=` in the result line is the user-side beat count and depends on how long the sim
   idles after the stream ends; the game tick (9610) is the stable figure. Consider adding
   `tick=` to the FAF_REPLAY_RESULT line (ReplayReport.cpp and its unit test).
6. CDiskWatch's privilege request logs the OpenProcessToken stub once per run (harmless).
