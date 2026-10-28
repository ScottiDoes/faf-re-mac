# Porting notes: wave-3 verifier (key: verify)

The result is in `macos/porting/WAVE3_REPORT.md`. This file records what I ran and the tools I
left behind, so the pass can be repeated. I changed no source file and committed nothing. My
files are in `build/verify3-tests/` (untracked).

## What I did

1. **Snapshot.** `git diff` and `git status`, plus a hash of every untracked file under src/,
   macos/ and dependencies/patches, saved as `build/verify3-tests/wave3-start.*`. At the end
   all three were unchanged, so nothing moved under me during the pass.
2. **Clean rebuild.**
   - Backed up the bring-up's binaries and maps to `build/verify3-tests/*.pre-wave3*`.
   - Ran `ninja -C build/macos -t clean faf_replay faf_game` (1,382 files), then
     `ninja -C build/macos -k 0 -j 10 faf_replay faf_game > build/macos/census-wave3.log`.
   - Result: 1,383 steps, 0 failed, 0 errors, the same 34 warnings as wave 2, 1 min 59 s wall.
   - The new binaries match the bring-up's byte for byte except the LC_UUID, one `__TIME__`
     string and the two ad-hoc code-signature hashes (82 bytes in all).
3. **Post-link checks** on both binaries:
   - `check_map_order.py` passes for both. For faf_game its "runner objects after engine"
     line prints False only because faf_game has no runner objects.
   - `check_symbols.sh` passes for both.
   - `check_fma.sh` over the four object directories plus Wm3 and zlib passes.
   - `otool` finds 0 fused instructions in either binary.
4. **Headless replays** (`macos/run_replay.sh`, outputs under `build/verify3-tests/`):
   - `run50/`, `SPEED=50`: 0 mismatches, 77.7x real time, self-test 75/75.
   - `run50-w8/`, `/simworkers 8`: 0 mismatches, 83.5x real time.
   - `negctl/`, beat 5000 digest flipped: `mismatches=2`, exit 1.
   - `run50-ui/`, `UI=1`: 0 mismatches, 76.6x real time.
5. **Windowed runs** (`macos/run_game.sh`, each under the script's `TIME` watchdog):

   | Folder | What | Result |
   |---|---|---|
   | `gameA` | bringup's how_to_run exactly, plus `TIME=100 SHOTS=1500,3600,5400 FAF_GPU_PROFILE=600` | Plays in real time; 60 fps; 0 mismatches |
   | `gameB` | `SPEED=10`, shots hook, profiling | Reached tick 9610 with 0 mismatches. The frame dumps went nowhere (path with a space; tooling note T1 below) |
   | `gameC` | Same as gameB with a dump path without spaces | 11 engine frame dumps, converted to `<shot>.png`. Reached tick 9610, 0 mismatches |
   | `gameKK` | KosmicKrisp with validation, 70 s | 1 validation message (the known unused-output warning), 0 mismatches |
   | `gameMenu`..`gameMenu7` | The main menu (no REPLAY) | Stalls on the logo after 5 to 8 frames; see finding F1 in the report |

6. **Rule audit:**
   - `unifdef -UFAF_POSIX` over HEAD and the working copy of all 64 modified tracked files
     (`build/verify3-tests/unifdef/`);
   - mtimes of the lead-owned files;
   - every file changed after the lead's set-up (21:21);
   - FAF_POSIX_STUB counts;
   - the boost, Wild Magic and zlib trees against their patches;
   - new items in /opt/homebrew, ~/Gaming, the venv and ~/.faforever.
7. **Size probe** `build/verify3-tests/probe/SizeProbe3.cpp`, compiled with `tu.sh`:
   - Passes at the Windows x64 sizes: MSG 48, WNDCLASSEXW 80, MINMAXINFO 40, TEXTMETRICA 56,
     LOGFONTA 60, KERNINGPAIR 8, BITMAP 32, SIZE 8, BITMAPINFOHEADER 40, RGBQUAD 4.
   - The negative control `SizeProbe3Neg.cpp` fails as it should.
   - `SizeProbe3W.cpp` records that TEXTMETRICW is 68 bytes and LOGFONTW 156, because WCHAR is
     the 4-byte wchar_t (README rule 3). Both live in memory only.

## Tools (in `build/verify3-tests/`)

| File | Use |
|---|---|
| `tools/pngcrop.py <in> <out> x y w h [scale]` | Crops (and upscales) an 8-bit RGB/RGBA PNG for a close look |
| `tools/pngcrop.py --stats a.png [b.png]` | Mean colour; with `b.png`, the share of pixels that differ |
| `probe/SizeProbe3*.cpp` | Window and GDI struct sizes (see above) |
| `wave3-start.{diff,status,untracked.sha}` | The tree as I found it |
| `unifdef/{head,work,summary.txt}` | The Windows view of every modified file |

`build/verify-tests/corrupt_replay.py` (wave 2) makes the negative-control replay. Pass it an
**absolute** `REPLAY` path. `run_replay.sh` changes into the run folder, so a relative path
ends with `exit=load-failed`.

## Findings worth remembering

- **Main menu idle stall.** `macos/run_game.sh` without a replay and with an existing profile
  presents 5 to 8 frames, showing only the FA logo, then nothing until an event wakes the loop.
  - The main thread sits in `MsgWaitForMultipleObjectsEx` with no window to paint
    (`gameMenu4/sample.txt`).
  - SDL's SIGTERM handler turns the watchdog's signal into a quit, and from that moment frames
    flow at 60 fps with the "Are you sure you'd like to quit?" dialog.
  - Replays are not affected.
- **The shots hook's `dump_Frame <dir>` silently writes nothing when `<dir>` contains a space.**
  Use the `/Users/scottharris/Gaming/faf-re/...` path, which has no space.
- **`run_game.sh` with `SHOTS` frames that were never reached** prints a zsh "no matches found"
  line. It is cosmetic.
- **The game rewrites the prefs file it was given on exit.** `bringup-high.prefs` and
  `Game.prefs` in the native prefs folder were rewritten by these runs, as by the bring-up's.

## Open items

None of my own. Requests and next steps are in WAVE3_REPORT.md.
