# Porting notes: package "lockin" (replay verification you can trust, in one command)

Wave 4 (milestone 3 + polish). Headless only: no window, no sound, no movie. Nothing here
touches sim code; the sim's results cannot change.

## Result

```
macos/verify_replays.sh                       # the corpus, build/macos/faf_replay
FAF_REPLAY_EXE=$PWD/build/m4-lockin/faf_replay OUT=build/m4-lockin-tests/verify macos/verify_replays.sh
macos/verify_replays.sh ~/.faforever/replays/27943742-TheGoodGeek.fafreplay some.scfareplay
```

Final run (`build/m4-lockin/faf_replay`, linked 00:41 from the tree as it stood then, with the
sound and movie packages' work in progress; `build/m4-lockin-tests/verify3/RESULTS.txt`):

```
replay    map                                       players  recorded  checked  mismatches  tick   x real  workers  verdict
27890223  SCMP_021                                  1        5         5        0           226    95.6    0        pass
27890223  SCMP_021                                  1        5         5        0           226    118.1   8        pass
27890254  SCCA_Coop_E06.v0023                       1        12        -        -           -      -       -        skipped
27890563  chiron.v0003                              2        121       121      0           3060   162.8   0        pass
27890563  chiron.v0003                              2        121       121      0           3060   216.1   8        pass
27890640  SCMP_016                                  2        386       386      0           9610   72.5    0        pass
27890640  SCMP_016                                  2        386       386      0           9610   73.1    8        pass
27943742  astro_crater_battles_4x4_rich_huge.v0004  8        2031      2031     0           12687  19.6    0        pass
27943742  astro_crater_battles_4x4_rich_huge.v0004  8        2031      2031     0           12687  18.4    8        pass

  27890254: featured mod coop (from ~/.faforever/replays/27890254-TheGoodGeek.fafreplay) is not
  installed; the run folder mounts FAF's faf/ladder1v1 data

negctl    27890563  PASS digest byte 0 flipped at beat 1550 in 2 players' records -> exit 1,
                    mismatches=2, 2 log lines at beat 1550, sim digest = recorded digest 88ddb9b3...
nochecks  27890563  PASS every checksum record removed -> exit 2, tick=3060 recorded=0 checked=0
                    mismatches=0 exit=end-of-replay verdict=unverified
synccheck 27890563  PASS 121 recorded digests compared with the sim's own: 121 equal, 0 differ;
                    'Ignoring verify' lines in the sim log: 0
postlink  map-order, init-order (4145 initializers), preregister (590 = 590 = 590), libm,
          fma-objects (983 objects), fma-binary: all PASS
unittest  runner_unit_test: 22 scenarios ok
verify_replays: 8 runs, 0 steps failed: PASS   (exit 0, about 9 minutes)
```

Every recorded checksum of every installed-mod replay was compared, with the serial and the
threaded sim, and none mismatched; both settings end on the same tick. `x real` was measured
while other agents built and ran games (load average about 30), so it is below the quiet-machine
figures (82x for 27890640, 20x for 27943742). **Second opinion**: `build/macos/faf_replay`
(00:02, the pre-wave-4 runner, used read only) gives 0 mismatches and the same final ticks on
27943742 (`/simworkers 0`, tick 12687) and 27890640 (`/simworkers 8`, tick 9610).

## What changed

| File | What |
|---|---|
| `src/platform/posix/runner/ReplayChecks.cpp` (new) | Reads the `/replay` file's command stream as the engine does and counts the recorded player checksums and how many the sim compares (below) |
| `src/platform/posix/runner/FpcrWatch.cpp` (new) | The per-thread FPCR check (task 4) |
| `src/platform/posix/runner/ReplayReport.cpp` | The result line gains `tick=`, `recorded=`, `checked=`, `verdict=`; a run that compared nothing exits 2 |
| `src/platform/posix/runner/Runner.h`, `RunnerMain.cpp`, `HeadlessApp.cpp` | Declarations; `RunnerMain` loads the replay's checksums before the run (`faf_posixHostPathA` for the path), `/nofpcrcheck`; `HeadlessApp` samples every thread's FPCR every 2 s once the game ticks and once before the teardown |
| `macos/run_replay.sh` | F3: `REPLAY` made absolute (`${REPLAY:A}`); `REPLAYID` defaults to the first run of digits in the file name (was always 27890640); prints `recorded`/`checked`/`verdict` |
| `macos/verify_replays.sh` (new) | The one command (a wrapper; the driver is `macos/tests/replay/verify_replays.py`) |
| `macos/tests/replay/replay_tool.py` (new) | `info`, `checked`, `convert` (.fafreplay → .scfareplay), `corrupt` (negative control; replaces `build/verify-tests/corrupt_replay.py`), `strip`, `synccheck` |
| `macos/tests/replay/check_postlink.py` (new) | The wave-2 post-link checks in one script |
| `macos/tests/replay/check_map_order.py` | Promoted from `build/link-run-tests/`, with `--app` for faf_game |
| `macos/tests/replay/runner_unit_test.cpp`, `test_double.cpp`, `build_unit_test.sh` (new) | The runner's unit test, promoted from `build/runner-tests/report_test.cpp` and extended (22 scenarios) |
| `macos/tests/replay/test_run_replay.sh` (new) | `run_replay.sh` with a stand-in runner (F3, REPLAYID, summary, exit status), promoted from `build/runner-tests/fake_runner.sh` |

No engine file outside `runner/` was edited; no lead-owned file was touched. `macos/CMakeLists.txt`
already globs `runner/*.cpp` (`CONFIGURE_DEPENDS`), so the two new files build without a CMake
change.

## 1. The result line

```
FAF_REPLAY_RESULT replay=27890640 beats=9719 tick=9610 recorded=386 checked=386 mismatches=0 exit=end-of-replay verdict=pass
```

| Field | Meaning |
|---|---|
| `beats` | as before: the last sim beat the user side applied (`gUserSyncBeats - 1`) |
| `tick` | the last game tick the user side saw (`CWldSession::mGameTick`, from the sync packets); 9610 = the x64 benchmark's `BENCH end tick` |
| `recorded` | `CMDST_VerifyChecksum` records in the replay (one per player per checksum beat) |
| `checked` | how many of them the sim compared by the end of the run |
| `mismatches` | as before: the sim's "Checksum for beat N mismatched" warnings |
| `exit` | as before: why the run ended |
| `verdict` | `pass` (0), `mismatch` (1), `incomplete` (2: not gameover/end-of-replay), `fpcr` (2: a thread ran with FPCR != 0), `unverified` (2: `checked` is 0, or the replay could not be counted: `recorded=-1 checked=-1`) |

The exit status follows the verdict; die is still 3 and a fatal signal 128+n (the crash line
carries `verdict=mismatch` or `incomplete`). The log gets one more line:
`faf_replay: recorded checksums 386, compared 386 (386 when the whole stream plays), stream beats 9609`.

### How `checked` is counted without touching the sim

A matching checksum leaves no trace (`Sim::VerifyChecksum` returns silently), and the task forbids
sim changes. So the runner reads the same command stream the engine reads and applies the
engine's own rules to it (`ReplayChecks.cpp` header; `replay_tool.py` docstring):

- `CReplayClient::Start` forwards a command only while the last `CMDST_SetCommandSource` named one
  of the header's sources (`mCurrentSourceAllowed` starts false).
- `Sim::VerifyChecksum` ignores a record while the sim has no valid command source, and compares
  it only when its beat lies in `[curBeat - 128, curBeat)`.
- `curBeat` counts `Sim::Sync` calls: one opening Sync before the first command
  (`SimDriver.cpp:1150`, beat 0's digest), then one per `CMDST_Advance`. So a record after the
  k-th Advance is decoded at `curBeat = k + 1`.
- `CWldSession::DoBeat` counts the sync packets the user side applied (`gUserSyncBeats`, one per
  Sync, the opening one included). Once N were applied, the sim had decoded every record with
  `curBeat < N`. `checked` = the comparable records with `curBeat < N`: exact for a run that
  played to the end, a lower bound for one cut short.

Getting the opening Sync right mattered: without it every replay's first record (beat 0, written
before the first Advance) and a single-player game's every record would look "in the future".

**Evidence that the model is the engine's behaviour** (not just a re-reading of the code):
1. **Two independent implementations agree** on all 5 corpus replays: the C++ parser
   (`runner_unit_test --count`) and `replay_tool.py info` give the same recorded / comparable /
   stream-beat counts (5/5/225, 12/12/557, 121/121/3063, 386/386/9609, 2031/2031/12686), and every
   verify run checks `recorded`/`checked` against the Python count.
2. **The engine's own sim log** (`SYNCLOG=1`, every beat kept: 9720 beat files for 27890640) has
   0 "Ignoring verify of beat" lines, the line `Sim::VerifyChecksum` writes for every record it
   skips: the sim compared all 386.
3. **The synccheck step** compares the sim's own digest of every checksum beat (from the sim log)
   with every recorded digest: 386 of 386 equal for 27890640, 121 of 121 for 27890563. This
   end-to-end check does not rely on the mismatch warning or on the runner at all. Its control:
   on a corrupted copy of 27890640 it reports 384 equal, 2 differ, first at beat 5000.
4. **The negative control**: flipping a digest byte at the middle checksum beat gives exactly one
   mismatch per player who recorded it, and each mismatch line's sim digest equals the record's
   digest before the flip.

## 2. F3 (WAVE3 section 4)

`run_replay.sh` now does `REPLAY=${REPLAY:A}` before it changes into the run folder, so a relative
`REPLAY=` works. It also derives `REPLAYID` from the file name when `REPLAYID` is not set (the
old default was always 27890640, so `REPLAY=other.scfareplay` alone named its logs and result
after the wrong replay).

## 3. macos/verify_replays.sh

Steps (each printed as PASS / FAIL / SKIP, saved in `$OUT/RESULTS.txt` and `results.json`):

1. **inputs**: `.fafreplay` → `$OUT/converted/<id>.scfareplay` (+ the JSON header as `<id>.json`);
   `replay_tool.py info`. Skips, with a note, a replay whose featured mod is not `faf`/`ladder1v1`
   (looked up in the converted JSON, a `<replay>.json` next to the file, or the matching
   `~/.faforever/replays/<id>-*.fafreplay`, read only; else a "coop" map name) or whose game
   version differs from the installed gamedata's (`lua.nx2:lua/version.lua`, 3839 today).
2. **runs**: `macos/run_replay.sh` per replay with `/simworkers 0` and `/simworkers 8`, SPEED=50,
   under `macos/tests/bringup/watchdog.py` (MAXMIN + 2 minutes). Pass: exit 0, `verdict=pass`,
   `recorded` and `checked` equal to replay_tool's counts, the same final tick for both settings.
3. **negctl**: `replay_tool.py corrupt` at the middle checksum beat, `/simworkers 0`. Pass: exit 1,
   `verdict=mismatch`, `mismatches` = the records at that beat, every log line at that beat, each
   line's sim digest = the recorded digest before the flip and its other digest = the flipped one.
   **nochecks**: `replay_tool.py strip` (every checksum record removed, nothing else), same
   replay. Pass: exit 2, `verdict=unverified`, `recorded=0 checked=0`, a normal end of replay.
   This is the real-binary control for "a run that compared nothing must not look like a pass".
4. **synccheck**: the same replay with `SYNCLOG=1`, then `replay_tool.py synccheck` with the sim
   log (39 MB for 27890563, 430 MB for 27890640; under `$OUT/synccheck/`, which `run_replay.sh`
   clears on the next SYNCLOG run).
5. **postlink**: `check_postlink.py` on the executable (below).
6. **unittest**: `build_unit_test.sh` with the executable's build tree.

The default negctl/nochecks/synccheck replay is the fastest passing replay that at least two
players recorded checksums in (27890563 in the corpus). Exit status 0 only when nothing failed and at
least one run happened.

### Post-link checks (check_postlink.py)

Collected from WAVE2_REPORT.md section 2, link-run.md and crt.md; all were run by hand before.

| Check | What |
|---|---|
| map-order | `check_map_order.py`: StaticInitPreRegister.cpp.o first, the 934 `src/sources.cmake` objects in order (stand-ins at their originals' positions), then platform/posix, then the app objects |
| init-order | **New.** Decodes `__TEXT,__init_offsets` (the order dyld runs initializers in) and attributes every entry to its object through the map: the first is `RunFirstInitializers` (StaticInitPreRegister.cpp) and the object index never goes backwards. This checks the runtime order itself, not only the map's object list. 4145 initializers in faf_replay |
| preregister | `__DATA,__faf_prereg` has one 8-byte entry per `gGpgPreRegisterInit_*` symbol: 590 entries = 590 symbols in the executable, all inside the section = 590 in the engine objects (none dropped) |
| libm | `macos/tests/crt/check_symbols.sh`, with attribution: a forbidden import that no engine object (faf_engine, the app, Wild Magic, zlib) references is listed, not failed. Today: `cbrt exp2 hypot`, imported by FFmpeg only |
| fma-objects | `macos/tests/crt/check_fma.sh` over faf_engine.dir, the app objects, libWm3Foundation.a, libfaf_zlib.a (982 objects): none |
| fma-binary | `objdump -d` of the executable; every function with a fused multiply-add attributed through the map: 0 in engine objects; 39 in FFmpeg (libswresample 23, libavcodec 9, libavutil 3, libavformat 2, libswscale 2), listed |

The FFmpeg attribution became necessary this wave: `macos/ffmpeg.cmake` links FFmpeg into
faf_replay too, and a whole-binary scan or a plain `check_symbols.sh` now fails on FFmpeg's own
code. Neither is a determinism problem: FFmpeg runs user-side, and the engine objects are clean.

## 4. The per-worker FPCR check (WAVE2 section 7, x87-core request)

Done runner-side, with no engine change. The sim's threads ("Sim", "Issue", the `/simworkers`
pool) are started inside the engine and `gpg::SetThreadName` is a no-op, so the runner can neither
run code on them nor tell them apart. Instead `FpcrWatch.cpp` reads every thread of the process
from outside: `task_threads()` lists them and `thread_get_state(ARM_NEON_STATE64)` returns each
thread's FPCR (the kernel stops the thread for the read; timing cannot change the sim's results).
The headless runner starts no audio or window threads, so every thread must read 0.

- Sampled every 2 s of wall time once the game ticks, and once more before the teardown.
  faf_replay 27890640: `FPCR check: 15 samples of every thread (up to 12 threads), all 0` with
  `/simworkers 0`. With `/simworkers 8` the samples cover 8 more threads (19-20: the log says
  `SimWorkerPool: started 8 worker threads`), all 0 in every corpus run.
- A nonzero FPCR on any thread is logged with the thread's id and name and makes the verdict
  `fpcr` (exit 2). `/nofpcrcheck` turns the sampling off.
- **Control:** the unit test starts a thread that sets FPCR.FZ (`msr fpcr`, bit 24); the sample
  reports `FPCR 0x01000000` for it and the verdict is `fpcr`.
- Not covered: a thread that changes FPCR and restores it between two samples. MXCSR.FTZ
  writes through `immintrin.h` are recorded, never applied, so the known path cannot set it.
- In faf_game (sound on), audio threads may legitimately run with FZ set (Core Audio IO threads
  often do); the check is in the runner only, which is where determinism is verified.

## 5. Tests

| What | Inputs | Result |
|---|---|---|
| `runner_unit_test` (`macos/tests/replay/build_unit_test.sh`) | 22 scenarios: parser rules (no source yet, invalid source, future beat, 128-beat window, short and long records, size-2 message, cut-off message, bad signature, short header, missing file), result line and verdicts (pass, mismatch, unverified x3, incomplete x3, signal forms), FPCR clean and FZ worker | ALL PASS |
| C++ vs Python counts | 5 corpus replays | identical |
| `replay_tool.py convert` | the 5 `.fafreplay` files in `~/.faforever/replays` (read only) | byte-identical to the corpus `.scfareplay` files |
| `replay_tool.py corrupt` vs `build/verify-tests/corrupt_replay.py` | 27890640 beat 5000, 27943742 beat 6000 | identical output files (2 and 8 records) |
| `synccheck` | 27890640 (SYNCLOG run) and its corrupted copy | 386/386 equal, 0 ignored; control 2 differ |
| `check_postlink.py` | build/m4-lockin/faf_replay | all 6 PASS |
| `test_run_replay.sh` | stand-in runner, relative `REPLAY` with a space in the path | 12 of 12 ok |
| `run_replay.sh` | 27890640, SPEED=50, my tree | `tick=9610 recorded=386 checked=386 mismatches=0 verdict=pass` |
| `check_postlink.py` on a stale binary | `build/macos/faf_replay` (00:02) and my first link, both linked before the sound package moved `XactClockPosix.cpp` | map-order FAIL with "linked before macos/exclusions.d/audio-movie.cmake changed: relink it" (a real negative); PASS after relinking mine |
| `verify_replays.sh` | the corpus, 3 times (verify1 before the relink, verify3 final) | all runs pass every time; verify3 exit 0 (table above) |
| `verify_replays.sh` with a `.fafreplay` | `~/.faforever/replays/27890223-TheGoodGeek.fafreplay` (read only) | converted into `$OUT/converted`, run, negctl and nochecks PASS |

## Stubs

None added.

## Open issues

- Sim rates in the table are measured while other agents build and run (load average 28-33
  during this pass), so they are well below the quiet-machine figures (82x for 27890640).
- `checked` relies on the engine rules above. If `Sim::VerifyChecksum`, `CReplayClient::Start`
  or `CSimDriver`'s opening Sync change, update `ReplayChecks.cpp` and `replay_tool.py` together;
  the synccheck and "Ignoring verify" counts are the tripwires.
- FFmpeg's libm references (sin, cos, exp, log, pow, atan2, ...) bind to faf_crtmath's definitions
  in the executable (ld prefers the executable's own symbols). Harmless for movies, but
  `FAF_CRTMATH_TRACE=1` counts in faf_game will include FFmpeg's calls while a movie plays.
  FFmpeg calls no `rand`/`srand` (only `arc4random_buf`), and MSVC rand's state is thread-local
  anyway.
- The x87 quick suites (`build/x87-core-tests/x87core_tests --quick`, `x87core_tests_kernels
  --quick kat integration`) are not part of verify_replays.sh: they are separate test programs
  built in another package's tree, not checks of the executable under test. The executable's own
  start-up self-test (75 checks, including the x87 known answers) runs in every replay run and
  refuses to start the sim when one fails.

## Requests

### lead
- `src/platform/posix/README.md` layout table, `runner/` row: "The headless replay runner
  (`faf_replay`: `main()`, the result line, the replay's checksum count, the per-thread FPCR
  check); built into its own executable. `macos/verify_replays.sh` runs it over a replay set."
- WAVE2 section 7 "post-link check script" and "per-worker FPCR check" can be marked done
  (`macos/tests/replay/check_postlink.py`, `FpcrWatch.cpp`).
- runner.md (wave-2 runner package) still documents the old four-field result line; a pointer to
  lockin.md section 1 would do.
