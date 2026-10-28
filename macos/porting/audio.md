# Package audio (milestone 3): the game's XACT sound on FAudio's FACT

Owns `src/moho/audio/**` (FAF_POSIX-guarded edits), `src/platform/posix/audio/**`,
`src/platform/posix/stubs/audio-movie/` except the movie files (`include/xact3.h`,
`include/PosixWaveFormat.h`), `macos/exclusions.d/audio-movie.cmake`, `macos/tests/audio/**`,
`build/m4-audio*`. Wave-1 history: `audio-movie.md`.

## Result

**The native game plays its sound.** Every AudioEngine the game creates gets an XACT engine
object whose vtables (the recovered xactengine2_9 interfaces in `AudioEngine.h`) are backed by
FAudio 26.10's FACT, with X3DAudio on F3DAudio and output through SDL3.

**What was checked:**
- All 229 banks load: 115 sound banks with 4,596 cues and 114 wave banks, from FA and from
  FAF's `~/.faforever/sounds`, in memory and streaming.
- 4,586 of the 4,596 cues make sound. The 10 silent ones are silent in the data itself:
  - volume 0;
  - an RPC on a variable that is 0 by default;
  - or movie voice tracks whose first seconds are silence.
- In the game, with no `/nosound`:
  - **Main menu:** music and UI sounds. The music-volume option works, and quitting is clean.
  - **Replay 27890640:** in real time past the 10-minute mark, and at +10 to the end of the
    replay. Both have battle sounds, music, 3D panning, no clipping and 0 checksum
    mismatches.
- `/nosound` still gives the null engine, and the headless replay still passes.

## 1. How the engine uses sound (research)

### Configuration: `func_InitSound`

`func_InitSound` (`AudioEngine.cpp`) builds the process-wide `SoundConfiguration`:
- reads `/audition` and `/nosound`;
- unless `/nosound`:
  - look-ahead 250 ms;
  - notification callback `func_HandleSoundEvent`;
  - the global settings buffer: `/sounds/SupCom.xgs` through the VFS, kept for the whole
    process;
- then asks DirectSound for the speaker layout.

The speaker value only sizes the first 3D DSP buffer: `AudioEngine`'s constructor resizes it
from `GetFinalMixFormat` (a FAF change). So the DirectSound answer does not matter:
- with no device, it stays stereo;
- the movies package now has a real `DirectSoundCreate` (logs "DirectSound: SDL audio
  driver ..."), which also gives stereo.

### Engines: `AudioEngine::Create(path)`

`AudioEngine::Create(path)` does the following:
- `func_RetreiveXACTCOMInterface` → `func_CreateXACTinstance` (CoCreateInstance of
  xactengine2_9) → `SND_InstallXactClock` → `Initialize(&mRuntimeParams)`;
- logs the MD5 of the global settings;
- `func_AudioInitialize`: the XACT global `SpeedOfSound` and the final mix's speaker mask go
  to `X3DAudioInitialize`;
- registers notifications 10, 11, 17 and 18 (GUI connect/disconnect, wave bank prepared,
  streaming invalid content), all persistent;
- `func_LoadSoundPath`, for every `*.xwb` in the folder (not recursive): if bit 0 of the
  bank-data flags says streaming, `CreateFileA` (flags 0x60000000), `CreateStreamingWaveBank`
  (packet size 0x12) and a per-bank WAVEBANKPREPARED registration; otherwise
  `DISK_MemoryMapFile` and `CreateInMemoryWaveBank`;
- then every `*.xsb`: `DISK_ReadFile` and `CreateSoundBank`;
- `/nomusic` sets the "Music" volume to 0;
- the listener goes to the origin.

### The engines that exist (all user side)

| Engine | Folder | Created by |
|---|---|---|
| `CUserSoundManager::mVoiceEngine` | `/sounds`: every SFX, Interface, Music, FMV_BG, Op_Briefing bank (89 wave banks, 90 sound banks) | The manager's constructor |
| `mTutorialEngine` | `/sounds/voice/<lang>`: the VO banks | `AudioSetLanguage` (`Sim.cpp:26142`) |
| `mAmbientEngine` | `/sounds/voice/<lang>/tutorials` (empty in FA) | `AudioSetLanguage` (`Sim.cpp:26148`) |

Each is its own XACT engine. Here that means its own FACT engine, FAudio instance and SDL
audio stream. SDL mixes the streams of the one output device.

### Cues

- **Playing:**
  - `AudioEngine::Play` → `IXACTSoundBank::Prepare` or `Play`; with no out-pointer, a fire
    and forget cue;
  - `IsStopped` tests a state of exactly 0x20;
  - CUserSoundManager keeps one-shots in `mPendingDestroyCues` and destroys them once
    stopped;
  - loops (`StartLoop`/`StopLoop` requests, entity loops with RPC loop variables) keep their
    cue in a record or in the `HSound`.
- **Script API:** `PlaySound`, `StopSound`, `PauseSound`, `SetVolume`/`GetVolume`
  (categories), `StopAllSounds`, `PlayVoice` with ducking (`Duck`/`DuckLength` globals), and
  `PlayTutorialVO`.
- **Camera globals:** `CameraDistance` and `ZoomPercent`, set every frame.

### 3D

`AudioEngine::Calculate3D` → `func_X3DAudioCalculate`, with flags 0x61 (matrix, Doppler,
emitter angle) and a flat default distance curve. The engine then applies what XACT3DApply
would (`ApplySettingsToCue`):
- `SetMatrixCoefficients`;
- the cue variables `Distance`, `DopplerPitchScalar` and `OrientationAngle`.

Distance attenuation is therefore the XSB's RPC curves on `Distance`, not X3DAudio's volume
curve.

### Pumping: `SND_Frame`

`SND_Frame` (`CScApp.cpp:1121`, each frame) calls `DoWork` on every engine. `SND_Mute` sets
the "Global" category volume when the window is minimised.

### Categories and volumes

SupCom.xgs defines 39 categories, among them Global, Music, World, Units, Ambient, Weapons,
Interface, FMV, VO and the languages.

The options' master, fx, music and voice volumes reach `AudioEngine::SetVolume(category)`
→ `IXACTEngine::SetVolume`.

### `XactClock.cpp`

This is a FAF addition. It patches xactengine2_9.dll's `QueryPerformanceCounter` and
`GetTickCount` imports to a clock that starts with the process:
- **The bug:** XACT 2.9 compares a 32-bit millisecond clock as signed, so its worker tick
  stops between 24.86 and 49.7 days of Windows uptime ("sound partly disappears after 24.8
  days").
- **Why there is nothing to patch here:** FACT's clock is `FAudio_timems()`, SDL's
  milliseconds since SDL started in this process. That is already what the patch gives XACT.

### No feedback into the simulation

- The sim only queues `SAudioRequest`s (`CSimSoundManager`).
- `CSndParams`/`CSndVar` resolution (`SND_FindEngine`, `SND_GetGlobalVarIndex`),
  `SND_*Float`, `IsStopped` and the cue state are read only by `CUserSoundManager` and by
  `HSound::UpdateLoopCompletionState`. Nothing calls the latter.
- No sim code calls into the audio engine.
- XACT/FACT threads and SDL's audio thread run separately from the sim thread. FAudio's mixer
  floating point is on its own threads, and the FPCR is per thread.
- `/nosound` and sound give the same sim, as on Windows (FAF players mix both in one game).

## 2. Implementation

### New: `src/platform/posix/audio/`

| File | What |
|---|---|
| `FactXact.h` | Contract: `faf_posix::audio::CreateXactEngine(mode, outEngine)`, what CoCreateInstance does for `func_CreateXACTinstance` |
| `FactXact.cpp` | The adapters, `FactEngine` / `FactSoundBank` / `FactWaveBank` / `FactCue`, implementing `moho::IXACTEngine` / `IXACTSoundBank` / `IXACTWaveBank` / `IXACTCue` on FACT. Details below |
| `X3DAudioFact.cpp` | `X3DAudioInitialize` / `X3DAudioCalculate` on `F3DAudioInitialize8` / `F3DAudioCalculate`. Listener, emitter, distance curves and DSP settings are copied field by field into F3DAudio's packed structures, and the results copied back |
| `XactClockPosix.cpp` | `SND_InstallXactClock`: nothing to patch (above). Logs once if `/realtickcount` or `FAF_TEST_TICK_OFFSET_DAYS` asks for the patch's test behaviour |

### The adapters (`FactXact.cpp`)

- **Return values:** XACT's HRESULTs. FACT's own codes (FACTENGINE_E_* are XACT's numbers)
  pass through; FACT's bare `1` failures become E_FAIL. An invalid variable or category index
  gives XACT's E_INVALIDVARIABLEINDEX or E_INVALIDCATEGORY where FACT returns 1.
- **Before `Initialize`, or after it failed,** every call returns E_NOTINITIALIZED, as XACT
  does. FACT would dereference its missing FAudio instance.
- **`Initialize`:**
  - copies XACT 2.5's `AudioRuntimeParams` field by field into `FACTRuntimeParameters`;
  - `pXAudio2` and `pMasteringVoice` are null, so FACT creates its own FAudio instance and
    mastering voice on SDL's default output;
  - the renderer id is converted from 32-bit `wchar_t` to UTF-16;
  - logs `SND: FACT (FAudio 26.10.0) started: <n> channel(s) (mask ...), <rate> Hz, SDL audio
    driver '<name>'`;
  - with no SDL output (no device, or a bad `SDL_AUDIO_DRIVER`), FAudio still starts and mixes
    to nowhere; the adapter then warns `SND: SDL has no audio output (...)`.
- **Streaming wave banks:**
  - The engine hands XACT its `CreateFileA` HANDLE. FACT reads through the engine-level file
    callbacks. Here they call the Files area's `ReadFile` with an OVERLAPPED offset, a pread
    that completes before it returns, on that HANDLE (or the engine's own callbacks, if it
    ever supplies them).
  - `GetOverlappedResult` returns the bytes already read.
  - A failed read reports 0 bytes; FACT plays silence for that packet and does not spin.
- **Notifications:** FACT's `FACTNotification` (the same packed layout) is converted field by
  field. FACT object pointers are mapped back to the adapters the engine knows, through a
  registry, and the engine's callback is called. `func_HandleSoundEvent` logs "Wavebank
  prepared: %p" with the adapter pointer that "SND: Loaded WaveBank ... at %p" printed (89
  per start).
- **Cue lifetime:**
  - A cue adapter lives until the engine calls `IXACTCue::Destroy`.
  - When FACT frees the cue earlier (`IXACTSoundBank::Destroy`, `IXACTEngine::ShutDown`,
    `AudioSetLanguage`'s `Shutdown`), the adapter turns "dead": it reports
    XACT_CUESTATE_STOPPED and ignores Stop.
  - So the engine's pending-destroy lists drain instead of touching freed memory.
  - Fire-and-forget cues (no out-pointer) stay FACT-managed.
  - A cue FACT created alongside an error other than "instance limit" is destroyed, since the
    engine drops it.
- **Locking:** the registry mutex is never held while calling into FACT, because FACT calls
  the notification callback with its own API lock held.
- **Not called by the engine:** `PrepareWave`, `PrepareInMemoryWave`,
  `PrepareStreamingWave`, the reserved sound-bank and cue slots. They return E_NOTIMPL and
  log `FAF_POSIX_STUB` once.
- **`/xactdebug` and `/audition`** (mode != 0) log a `FAF_POSIX_STUB` and get the retail
  engine.

### Edits to upstream files (`#if defined(FAF_POSIX)` only; the Windows view is upstream's)

| File | Change |
|---|---|
| `src/moho/audio/AudioEngine.cpp` | Includes `platform/posix/audio/FactXact.h` next to the dsound stand-in. `func_CreateXACTinstance` returns `faf_posix::audio::CreateXactEngine(mode, outEngine)`. The wave-1 block in `func_InitSound` that forced `mNoSound` is removed, so this function is upstream's code again |

### Stand-ins and the build

- **`stubs/audio-movie/include/xact3.h`:**
  - adds `XACT_RENDERER_DETAILS` (for `GetRendererDetails`; WCHAR text in memory only);
  - the header comment now says FACT is the runtime.
- **`PosixWaveFormat.h`** is unchanged.
- **Removed:** `stubs/audio-movie/X3DAudioPosix.cpp` (replaced by `audio/X3DAudioFact.cpp`)
  and `stubs/audio-movie/XactClockPosix.cpp` (moved to `audio/XactClockPosix.cpp`).
- **`macos/exclusions.d/audio-movie.cmake`:** `XactClock.cpp` ⇒
  `platform/posix/audio/XactClockPosix.cpp`. It has no static initializers, so the init
  order is unchanged.
- **New `platform/posix/audio/*.cpp`** files come in through the existing `platform/posix`
  glob.
- **Linking:** FAudio is already linked (`FAF_MEDIA_LIBRARIES`); SDL3 headers are on the
  engine's include path.

## 3. Tests

Everything is in `build/m4-audio-tests/` (untracked; game-derived audio stays there).

**The build:**
```
cmake -S macos -B build/m4-audio -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build/m4-audio -j 6 faf_game faf_replay
```
That built 1,652 steps with 0 errors. The new TUs give no warnings with `-Wall -Wextra`
beyond the pre-existing header ones.

### (a) Every cue through FACT: `macos/tests/audio/fact_bank_sweep.cpp`

`build.sh` builds it, `run_sweep.sh` runs it and `sweep_report.py` summarises it.

**What the harness does:**
- It is standalone: FAudio and SDL3, no engine code.
- It loads SupCom.xgs, every `.xwb` and every `.xsb` the way `AudioEngine` does. That includes
  streaming banks with packet size 0x12 through positioned-read callbacks on a file
  descriptor.
- It plays every cue alone.
- It calls `DoWork` every 5 ms, as the frame loop would.
- It measures the mixed output per cue with an engine-procedure tap
  (`FAudio_SetEngineProcedureEXT`). Meanwhile SDL's disk driver writes the mixed stream to a
  file.

**Settings:** 6 shards, SDL disk driver at timescale 0.25 (about 3.5x real time), at most
1.5 s of wall time (about 5 s of audio) per cue.

**Result:** 229 banks, all loaded. 4,596 cues: **4,586 audible, 10 silent, 0 play errors**.

The 10 silent cues and 3 quiet ones, explained from the data:

| Cue(s) | Why |
|---|---|
| `X_FMV/SCX_Outro_VO` | Its sound's volume byte is 0, i.e. -96 dB (XSB parse). It is silent by authoring, and plays its 3.6 s wave inaudibly |
| `UnitRumble/Rumble1` | An infinite loop whose volume RPCs read the global `Rumble1`, initially 0. It sounds only when CUserSoundManager's RPC loops set that variable |
| `X_FMV/SCX_Post_Outro_VO` | Movie voice track whose wave is silent for its first 6.4 s. Audible in a 9 s run |
| `X_FMV/SCX_INTRO_VO` (quiet, -68 dBFS peak) | The wave's first 12 s are its noise floor (-72 dBFS peak in the file). FACT reproduced that at +4 dB |
| `FMV_Vanilla/*` (7 silent, 1 quiet), `Op_Briefing_Vanilla/E03_B04` (quiet) | FAF's ADPCM movie and briefing soundtracks, which start with long silences. **All 13 FMV_Vanilla cues are audible** when rerun with a 25 s cap (`fmvvanilla.csv`: RMS -20 to -28 dBFS, peaks 0.30 to 0.70) |

**Clipping:** 382 cues, all voice-over (X0*_VO, XGG, Briefings, FAF's coop VO), peak above
full scale by up to +3.8 dB when played alone.
- **The authored gain:** Global +6 dB, VO 0 dB and US -5.8 dB (XGS category bytes 255/180/129
  through FACT's decibel curve), plus each sound's own volume, on waves recorded near
  -1 dBFS.
- **Is it the same on Windows?** XACT applies the same authored gains, so the Windows game
  should reach the same levels, but no Windows capture was compared.
- **In the game,** VO plays over ducked world sound. The replay capture never clipped.

**FACT behaviour seen:**
- A cue's state is exactly 0x08 (playing) or 0x20 (stopped), which is what the engine's
  `IsStopped` / `kCueStatePlaying` equality tests expect.
- Reads always had `OffsetHigh` = 0.
- No streaming-invalid-content notification.

### (b) In the game, with sound

`macos/tests/audio/run_game_sound.sh` is `run_game.sh` without `/nosound`, keeping `/nomovie`
by default (`MOVIES=1` to drop it). It runs my build and captures to `$OUT/audio.f32` (F32,
48 kHz, stereo) through SDL's disk driver. `analyze_audio.py` reports per-second RMS and peak,
clipping, silent stretches and per-channel RMS, and writes WAV excerpts.

| Run | Result |
|---|---|
| `menu1`: main menu, idle, 40 s | Music from 1.3 s, -31 dBFS RMS, peak -8.9 dBFS, 0 clipped, no silent stretch |
| `menu2`: main menu with synthetic input (hover six buttons 1 s apart, click Options, Escape, hover and click Exit) | UI sounds 1 s apart over the music (+8 to +15 dB 100 ms bursts at 4.9, 5.8, 6.8, 8.0 s ...). Frames: the Options dialog and the exit confirmation draw |
| `menu-nomusic`: prefs with `music_volume = 0` | Only the 4 s menu-open interface sound, then 28 s of silence. The options reach `SetVolume("Music")` and FACT's category volume |
| `menu-exit`: Exit → Yes, with `FAF_GAME_STATIC_EXIT=1` (every static destructor) | Exit status 0. Three "SND: FACT engine shut down" lines, no crash |
| `menu-nodevice`: `SDL_AUDIO_DRIVER=bogus` (no usable output) | The game runs normally for 25 s. DirectSound reports no device (movies package); the three FACT engines still start, driver '?', and nothing is heard. The adapter's warning for this case was added after the run |
| Music check (`menu1`) | The captured music's 100 ms envelope against the source wave (Music.xwb wave 4, the `Main_Menu` cue): correlation 0.92 at a constant 1.4 s offset over two 15 s windows. It is the right track, streamed at the right rate, without drift. The level is -18.9 dB under the source; the sound's authored volume is -19.3 dB |
| `replay-speed10`: replay 27890640 at +10 (init_bench), to `BENCH end tick=9610` | 0 checksum mismatches. 180 s captured: RMS -26.6 dBFS, peak -6.4 dBFS, 0 clipped, sound in 98% of seconds. Per-second L/R balance from -23.5 to +4.5 dB, so the 3D panning works. No SND warnings, no XACT3DApply failure |
| `replay-realtime`: replay 27890640 at its own speed (real time), 13 min (watchdog 780 s) | Reached game time 12:51 with **0 checksum mismatches**. 762 s captured: sound in 759 s (all but the first 3 s of loading), RMS -34.3 dBFS, peak -9.9 dBFS, **0 clipped samples**, no NaN. Per minute: RMS -31.5 to -38.0 dBFS. The camera stays on the whole-map overview, where world sounds are distance-attenuated |
| `replay-nomusic`: the same, 75 s, `music_volume = 0` | Seconds 5-70: RMS -46.9 dBFS with sound in 77% of seconds, against -34.5 dBFS and 100% with music. So the in-game music (UserMusic.lua's `Base_Building`/`Battle` cues) is the bed, with world sounds on top |
| `replay-spew`: +10 with `/spewsound`, 70 s | **7,012 world one-shots** played. Banks: URL 2115, URLWeapon 1901, Impacts 1608, AmbientTest 718, URB 321, URAWeapon 95 ... Top cues: `CYB_Expl_Med_Impact` 1491, `URL0107_Move_Stop`, `URL0107_Laser_Auto` 672, `Gen_Tree_Crush`, `URL0104_Nano_Dart`, `URB2101_Laser_Auto`, `URL0202_Cannon_Dbl`. Also `Play2D Music/Base_Building` and the score-window UI cue. These are the battle sounds |
| `replay-final`: the final build, +10 to `BENCH end tick=9610` | 0 mismatches. 129 s captured: RMS -25.3 dBFS, peak -4.5 dBFS, 0 clipped |
| `nosound`: `macos/run_game.sh` (passes `/nosound`) with this build | No FACT engine and no bank loaded: today's null engine |

**WAV excerpts** (16-bit, 48 kHz, stereo; game-derived, so under `build/` only):
- `build/m4-audio-tests/replay-27890640-battle-10m05s.wav`: 25 s of the real-time replay from
  capture second 605 (about 10:05 game time), its loudest 25 s after the 400 s mark;
- `build/m4-audio-tests/main-menu-music.wav`: 25 s of the idle main menu's music (capture
  seconds 12-37).

**Logs:** every start shows the following, and no SND warning or `FAF_POSIX_STUB` from the
audio files:
- "SND: FACT (FAudio 26.10.0) started: 2 channel(s) (mask 0x3), 48000 Hz", once per engine;
- the 89 + 11 banks loaded;
- 89 "Wavebank prepared".

### (c) Headless

`FAF_REPLAY_EXE=build/m4-audio/faf_replay OUT=build/m4-audio-tests/headless SPEED=50
macos/run_replay.sh` gives:

```
FAF_REPLAY_RESULT replay=27890640 beats=9717 tick=9610 recorded=386 checked=386 mismatches=0 exit=end-of-replay verdict=pass
```

That is 68x real time, on a busy machine. With the final build (`headless-final`): `beats=9710
... checked=386 mismatches=0 ... verdict=pass` at 73.7x. `/nosound` is passed, so the engines come up without
an XACT instance, as before.

## 4. Open issues and notes

1. **`macos/run_game.sh` still passes `/nosound`.** Owned by the lead / integration. Drop it
   so the game has sound by default; the headless runner keeps it.
2. **Several engines, several SDL streams.** One per AudioEngine (3 in a game), each with its
   own FACT thread (10 ms tick). That is cheap, and it is how XACT did it.
3. **The default output device.** FACT opens SDL's default playback device (renderer index
   0). SDL3 moves default-device streams when the system default changes; this was not
   tested with a real device switch tonight (speakers off, disk driver).
   - With a multichannel default device, FACT mixes to that channel count. The engine sizes
     its 3D matrices from `GetFinalMixFormat`, so a 5.1 or 7.1 output works by construction;
     not tested.
4. **FAF's own banks are not mounted.** `~/.faforever/sounds` (`*_Vanilla`, coop VO) is not
   mounted by `init_faf.lua`, neither in the native run folder nor in the FAF client's own
   bin folder. Normal FAF games therefore do not load them, on Windows either. The sweep
   shows they work if a mod or coop init mounts them.
5. **Movie audio** is the movies package's: the SFD's own audio, and the movie VO cues in
   X_FMV/FMV_BG through this engine. The VO cues play (sweep).
6. **`XactClock`.** `/realtickcount` and `FAF_TEST_TICK_OFFSET_DAYS` have no effect (logged).
   FACT's clock cannot hit the 24.8-day bug.

## 5. Requests

**To the lead / integration:**
1. `macos/run_game.sh`: drop `/nosound` from the default arguments (keep it in
   `macos/run_replay.sh`). Until then, `macos/tests/audio/run_game_sound.sh` runs the game with
   sound (`AUDIO=device` for the speakers, `AUDIO=disk` to capture).
2. `src/platform/posix/README.md` layout table, a new row:
   `| audio/ | The XACT runtime: the engine's XACT 2 interfaces on FAudio's FACT (FactXact.h is
   the contract), X3DAudio on F3DAudio, the XactClock stand-in |`. Also: `stubs/` no longer
   holds the audio stand-ins.
3. Nothing is needed in `macos/CMakeLists.txt`: FAudio is linked, and `platform/posix/audio`
   is picked up by the glob.

**To the movies package (FYI):**
- `func_InitSound` calls `DirectSoundCreate` once per start, only for `GetSpeakerConfig`, then
  `Release`.
- With `SDL_AUDIO_DRIVER=disk`, that device opens first (S16LE 44100) and FACT's (F32 48000)
  after it, on the same output file. The captures are clean, so the DirectSound device is
  closed by then. Keep `Release` closing it.

## 6. Files

- **Source:**
  - `src/platform/posix/audio/FactXact.h`, `FactXact.cpp`, `X3DAudioFact.cpp`,
    `XactClockPosix.cpp` (new);
  - `src/moho/audio/AudioEngine.cpp` (FAF_POSIX);
  - `src/platform/posix/stubs/audio-movie/include/xact3.h`;
  - removed `stubs/audio-movie/X3DAudioPosix.cpp` and `XactClockPosix.cpp`;
  - `macos/exclusions.d/audio-movie.cmake`.
- **Tests** (`macos/tests/audio/`):
  - `fact_bank_sweep.cpp`, `build.sh`, `run_sweep.sh`, `sweep_report.py`;
  - `run_game_sound.sh`, `analyze_audio.py`.
- **Results** (`build/m4-audio-tests/`):
  - `sweep/sweep.csv`, `sweep/report.txt`, `fmvvanilla.csv`, `xfmv.csv`, `rumble.csv`;
  - the run folders with `game.log`, `audio.f32` and `levels.txt`;
  - `headless/`, `headless-final/`;
  - the two WAVs.
- **Build tree:** `build/m4-audio`.
- **Prefs files I added** to the native prefs folder: `audio-test.prefs` (a copy of
  `bringup-high.prefs`) and `audio-nomusic.prefs` (the same with `music_volume = 0`).
  `Game.prefs` is untouched.
