# Package movies (macOS port, milestone 3)

Owns `src/moho/movie/**` (FAF_POSIX-guarded edits), `src/platform/posix/stubs/audio-movie/`
`{DirectSoundPosix.cpp, include/dsound.h}` (and the two stubs it removed),
`macos/exclusions.d/movies.cmake`, `macos/ffmpeg.cmake`, `macos/ffmpeg/**`,
`macos/tests/movies/**`, build trees `build/m4-movies*`.

Result: **the SFD movie player runs on macOS with FFmpeg 7.1.3 and plays movie audio.** The
start-up movies (THQ, GPG, NVIDIA, the FA intro), the main menu's background movie, the
faction loading movies and the engine's Lua `Movie` control all play in `faf_game` on the
Vulkan renderer. The engine's own `FfmpegSfdDecoder.cpp` and `DirectSoundMovieSink.cpp`
compile unchanged; DirectSound is a real stand-in on SDL3 audio. Headless and windowed
replays still match every checksum.

## 1. FFmpeg for arm64 macOS

**Approach: a generated configuration plus a CMake source list** (as upstream's
`dependencies/ffmpeg/faf/` does for clang-cl), not configure/make at build time.

| File | What |
|---|---|
| `macos/ffmpeg/generate.sh` | Runs FFmpeg's `configure` in a temporary folder with upstream's `FAF_CONFIGURE_OPTIONS` (generate.sh in the submodule; docs/FAF_SFD_COMPATIBILITY.md) plus `--cc=clang --target-os=darwin --arch=aarch64 --disable-debug --disable-autodetect --enable-pthreads`, copies `config.h`, `config_components.h`, `libavutil/avconfig.h`, `libavutil/ffversion.h` and the component lists, and runs the submodule's `faf/gen_sources.py` (read only) for `sources.cmake`. configure refuses an out-of-tree source path with whitespace, so it is given a temporary symlink. Reproducible: two runs give identical output |
| `macos/ffmpeg/arm64-darwin/` | That output (checked in, like faf/x64) |
| `macos/ffmpeg/CMakeLists.txt` | Static `ffmpeg_avutil`, `ffmpeg_swresample`, `ffmpeg_swscale`, `ffmpeg_avcodec`, `ffmpeg_avformat` from the unmodified submodule sources, FFmpeg's own flags (`-O3`, no NDEBUG; the directory clears CMake's per-config C/ASM flags), configure's include dirs and defines (`HAVE_AV_CONFIG_H`, `BUILDING_<lib>`). NEON `.S` files assemble with `$(CPPFLAGS) $(ASFLAGS)` as FFmpeg's make does. Copies the public headers (`lib*/*.h` + avconfig.h) to `<build>/ffmpeg/include` |
| `macos/ffmpeg.cmake` | `enable_language(ASM)` (it must be in the directory that links), `add_subdirectory(macos/ffmpeg)`, `FAF_FFMPEG_LIBRARIES` = the five targets, and the public include dir on **`FfmpegSfdDecoder.cpp` only** (source property) |

- **Component set**: exactly upstream's: decoders mpeg1video, mpeg2video, adpcm_adx, mp1,
  mp2, mp3; parsers mpegvideo, mpegaudio, adx; demuxers mpegps, mpegvideo, adx, mp3;
  protocol file. LGPL v2.1+ (`CONFIG_GPL 0`, `CONFIG_NONFREE 0`, `CONFIG_GPLV3 0`).
- **No system extras**: `--disable-autodetect` turns off VideoToolbox, AudioToolbox, iconv,
  zlib, SecureTransport... configure still lists CoreFoundation/CoreVideo/CoreMedia in
  avutil's EXTRALIBS (it probes them unconditionally); nothing references them, so
  ffmpeg/CMakeLists.txt drops them and fails the configure if an unexpected library shows up.
  `otool -L faf_game` shows no new dylib or framework.
- **Why only public headers, only on one TU**: FFmpeg's source root holds a `VERSION` file,
  which shadows libc++'s `<version>` on the case-insensitive file system (every TU broke
  with `-I dependencies/ffmpeg`). The decoder TU compiles with the force-included
  `WinCompat.h` and FFmpeg's headers without any clash, so no bridge is needed.
- **Build cost**: 267 objects, about 5 s on 6 jobs, built automatically by a clean
  configure of `build/macos` (`faf_game` and `faf_replay` link it). 23 warnings, all
  FFmpeg's own (implicit int-to-float constant conversions, deprecated fields).
- **Regenerate** after an FFmpeg update: `sh macos/ffmpeg/generate.sh` (Apple clang,
  python3). `KEEP=1` keeps the configure folder.
- **Distribution**: LGPL v2.1 static linking needs FFmpeg's notices and relinkability
  (docs/FAF_SFD_COMPATIBILITY.md, "FFmpeg dependency and build").

## 2. DirectSound on SDL3 (`stubs/audio-movie/DirectSoundPosix.cpp`)

`DirectSoundCreate` used to fail (DSERR_NODRIVER). It is now a working subset of
DirectSound 8 with Windows return semantics:

- **Device**: holds a reference on SDL's audio subsystem and keeps the default output open
  (a logical device without streams, mixed as silence) for its lifetime, as DirectSound's
  primary buffer holds the endpoint; without it the physical device closed and reopened
  with each movie's buffer. `GetSpeakerConfig` reports the output's channel count (1 mono,
  2 stereo, 4 quad, 6 5.1, 8 7.1 → `DSSPEAKER_7POINT1`, which func_InitSound maps to stereo
  as on Windows). `SetCooperativeLevel`, `Compact`, `GetCaps`, `SetSpeakerConfig` succeed.
  `DSERR_NODRIVER` when SDL has no output.
- **Secondary buffer**: the buffer's bytes plus an `SDL_AudioStream` on its own logical
  device (opened paused). `Lock`/`Unlock` hand out the ring (wrap split, `DSBLOCK_*`
  flags), `Play` resumes the device, SDL's get callback copies from the play cursor
  (wrapping when looping, stopping at the end otherwise), `Stop` pauses and keeps the
  position, `SetCurrentPosition` moves the cursor and clears what SDL had taken.
  `GetCurrentPosition` = cursor less what SDL still has queued; write cursor 10 ms ahead.
  `SetVolume` (hundredths of a dB) → stream gain (`DSBVOLUME_MIN` = silence),
  `SetFrequency` → frequency ratio, `DSERR_CONTROLUNAVAIL` without the `DSBCAPS_CTRL*` flag.
  Formats: PCM 8/16/32-bit, IEEE float 32, 1-8 channels.
- **Locking**: the stream lock (SDL holds it during the callback) guards the play state;
  pausing/resuming a logical device happens after it is released (SDL's device thread locks
  device, then stream).
- **Not reproduced** (unused by the engine): pan (stored, logged once), DuplicateSoundBuffer,
  3D/FX/notification buffers. A primary buffer exists for its format calls.
- **Shutdown**: SDL calls are skipped once SDL_Quit has torn audio down (`CScApp::Destroy`
  releases the movie manager before `shell::Shutdown` anyway).
- **Test hook**: `FAF_DSOUND_DUMP=<folder>` writes what SDL takes from every secondary
  buffer to `<folder>/dsound-<n>-<rate>hz-<ch>ch-<bits>.raw`.

func_InitSound (audio package) calls `DirectSoundCreate` + `GetSpeakerConfig` + `Release`
unchanged; it now gets the real layout ("Failed to create DirectSound." is gone).

## 3. The player on macOS

- `FfmpegSfdDecoder.cpp`, `DirectSoundMovieSink.cpp`: compiled unchanged (movies.cmake no
  longer replaces them; `FfmpegSfdDecoderPosix.cpp` and `DirectSoundMovieSinkPosix.cpp`
  are deleted). Movie audio: CMovieManager's DirectSound device → `sfd::SetSoundDevice` →
  one `AudioSink` (looping 2 s ring, 48 kHz S16 stereo) per movie with embedded audio; its
  played-byte count is the player's master clock, so video is slaved to the audio actually
  taken by the output.
- `IndependentSfdBackend.cpp`, three `#if defined(FAF_POSIX)` blocks (Windows view checked
  identical to cb1b9fd7 with `unifdef -UFAF_POSIX`):
  1. **Host path**: CMovie hands the player the VFS's Windows-form path
     (`\users\...\movies\thqlogo.sfd`); `std::ifstream` (SfdMetadata) and FFmpeg open files
     themselves, so `Player::Open` converts it with `faf_posixHostPathA` (core-sys). Without
     it every movie failed with "Missing, malformed or unsupported FAF SFD header".
  2. **Interleave limit**: `Decode` failed a movie when the queue passed 128 MB. Tick keeps
     0.5 s queued, 16-18 frames at 30 fps = 120-135 MB for 1824x1024 (7.5 MB/frame), so
     **main_menu.sfd stopped within seconds** (in game: 18 frames queued, "Excessive SFD
     stream interleave"; `player_soak` with upstream's file fails after ~5 s). Now
     max(128 MB, 32 frames of the movie's size).
  3. **Catch-up**: after a stall of the worker (busy or suspended process), Tick decoded
     everything that came due before showing any of it; it now takes due frames as they
     arrive (same frame shown as before). Upstream's player failed main_menu.sfd after a
     0.15 s SIGSTOP; now 1 s stalls pass.
  These are upstream bugs (they apply to Windows too): see Requests.

## 4. Where movies play (FAF's Lua)

`lua/ui/splash/splash.lua`: thqlogo, gpglogo, nvidia_logo, fmv_scx_intro, each with XACT
sound/voice cues (bank FMV_BG / X_FMV); skipped with Esc/Enter/Space/click or the
`movie.nologo` preference. `lua/ui/menus/main.lua`: main_menu.sfd looping behind the menu
(option `mainmenu_bgmovie`, default on) plus the AMB_Menu_Loop ambient cue.
`lua/ui/game/gamemain.lua`: the faction loading movie (UEF_load.sfd ...) at game start.
Campaign: `operationbriefing.lua` (A0x_B0x... briefings, QAI_loop), campaign movies, credits.
FA's movies have **no embedded audio** except e3_demo_cut.sfd (unused by the game): their
sound is XACT cues started by Lua's `Movie:Play` once `SoundIsPrepared`, i.e. the audio
package's FAudio; that is how movie sound is synchronised on Windows too. FAF's init
(init_faf.lua) mounts only the FA install's movies; `~/.faforever/movies` (portraits,
briefings, coop) is left to the featured mods.

## 5. Tests (all under `macos/tests/movies`, outputs in `build/m4-movies-tests`)

`macos/tests/movies/run_tests.sh` runs 1-5 (AUDIO=device for the real output).

| Test | Result |
|---|---|
| 1. `sfd-parser-tests` (upstream, unchanged) | PASS: truncation, relocation, malformed metadata, 20000 fuzz cases |
| 2. `dsound_test` (39 checks) under SDL's disk driver | ALL PASS. Capture: 1 kHz tone at -8.7 dBFS peak / -11.7 RMS for 1 s, then exactly -20.0 dB after `SetVolume(-2000)`, no clipping; play cursor 37 ms behind wall time after 0.5 s; one-shot stops at its end |
| 3. `sfd-inspect` (upstream tools/sfd-inspect/main.cpp, unchanged) | `--decode` Abasi/e3: 150 frames, 362 frames + 579,808 stereo samples, video/audio end 12.0797/12.0847 s (= Windows numbers in FAF_SFD_COMPATIBILITY.md); `--play` Abasi, e3, main_menu: PASS (paused PTS 3.10 vs 3.10 s); `--play-sound` e3 through the sink: PASS (2.97 s; capture = decoded level, -20 dB after the test's SetVolume(-2000)); `--reject` FMV_loading02: PASS |
| 4. `player_soak` (CMovie's calls, 60 Hz frame grabs, loop) | main_menu.sfd 40 s: 1186 frames, drift 65 ms, PASS (upstream backend: fails after 4.95-5.44 s). e3 with sound on CoreAudio 30 s, 2 loops: drift 80 ms PASS. Abasi 16 s, 3 loops: PASS. Under the disk/dummy drivers the audio clock runs ~3 % slow against wall time (their sleep-based pacing), hence `--max-drift 1.0` there |
| 5. `movie_census`: every .sfd through `ReadMetadata` + the engine's decoder, 6 threads | **FA install: 1004 movies, 246,281 frames (8209 s), 1003 OK, 1 REJECTED** (FMV_loading02, mode 33), 24-34 s. Every decoded frame count equals the header's; e3_demo_cut the only one with audio (ADX 48 kHz stereo). **~/.faforever/movies: 1327, 1145 OK, 4 WARN, 90 REJECTED, 88 NOHEADER** (below) |
| Frame PNGs (looked at) | `build/m4-movies-tests/frames/`: main_menu_300, thqlogo_60, gpglogo_90, e3_demo_cut_200, Abasi_40, FMV_SCX_Intro_2500, X01_1000, UEF_load_100, credits_generic_1500, A01_B01_100 (mode 33: colour on top, alpha below), coop and FAF portrait frames; `frames/small/` downscaled. Colours and geometry correct |
| In game, splash (`run_av_game.sh`, disk capture) | thqlogo → gpglogo (frame 400) → nvidia (736) → intro (1140), each exactly its length at 60 fps; shots show THQ, NVIDIA, the intro (`game-splash/shots`). With the audio package's FAudio in the tree the capture holds the cues: sound 1-12 s, 13-17 s, intro from 20 s |
| In game, main menu | Esc twice → main_menu.sfd loops behind the menu, animating (shots 1100/2200 differ); ambient + music captured. Before fix 2 it failed within seconds. A 110 s run (`game-menu-long`) covers its 77.9 s loop: still playing at frame 6000, no player error |
| In game, engine movie API (`hook/init_movies.lua`, a Lua hook over the main menu) | e3_demo_cut (with sound), A01_B01 (refused: mode 33), a FAF coop movie (refused: no Sofdec header), A01_Rhiza portrait, Abasi, UEF_load: all as expected, on the disk driver and on CoreAudio (48 kHz). **e3's audio through DirectSound in game (FAF_DSOUND_DUMP): sample-identical to the decoded audio for all 12 s**, one 256-frame silence 32 ms in where the player places the second audio packet at its PTS, no underrun or gap after that |
| Windowed replay with sound + movies (27890640, SPEED=10) | UEF loading movie shown, replay to its end (tick 9610), **0 checksum mismatches** in the log |
| Headless replays from `build/m4-movies/faf_replay` | 27890640: 386/386, mismatches=0, pass. 8-player 27943742: 2031/2031, mismatches=0, pass |

### Census findings (not port issues: faf-re's player does the same on Windows)

- **REJECTED (90 FAF + 1 FA)**: special composition (mode 33, effect 1): the campaign
  briefings A01_B01 ... E06_B03 (+ `_QAI`), QAI_LOOP, FMV_loading02. The frame holds the
  colour picture on top and its alpha below; upstream's player refuses these at open
  (FAF_SFD_COMPATIBILITY.md "Known limitations"). Campaign/coop briefings show no movie.
- **NOHEADER (88 FAF)**: `FAF_Coop_Operation_Tight_Spot_*`, `Pro_16_*`, `spinUEFBlue_*`
  are plain MPEG-1 program streams without Sofdec's `SofdecStream` header. The header
  parser (and CMovie's own probe) refuse them; FFmpeg decodes them fine (17,975 frames).
  The original Sofdec plays them in FAF coop.
- **WARN (4 FAF portraits, 112x112)**: one video timestamp steps back 33 ms (bad PTS in
  the file); the player shows the late frame at once. Upstream's sfd-inspect `--decode`
  would call this an error; playback is unaffected.
- FFmpeg prints `[swscaler] No accelerated colorspace conversion found from yuv420p to
  bgra.` on stderr per movie open (not in the game log); harmless.

## 6. Determinism

Nothing here is sim-side: FFmpeg is its own static libraries with FFmpeg's flags (not
`faf_codegen`; they change no global FP state), DirectSound and the player run on user
threads, and nothing feeds audio state or timing back. Both headless replays and a
windowed replay with sound and movies on match every checksum.

## 7. Tools added

- `macos/tests/movies/run_av_game.sh`: the game without `/nosound /nomovie`, AUDIO=disk
  (default; capture in `$OUT/audio.raw`, format in `$OUT/stderr.txt`), dummy or device;
  same environment as run_game.sh (REPLAY, SPEED, INIT, SHOTS, TIME, ...).
- `macos/tests/movies/analyse_audio.py`: RMS/peak/zero-crossing/correlation per window,
  clipping, silent stretches, WAV export. `correlate_audio.py`: finds a reference WAV in
  a capture by envelope correlation (weak when other sound dominates; the dump hook is
  exact).
- `movie_census --shot NAME@FRAME|last|audio`, `player_soak`, `dsound_test` (above).

## 8. Open issues

- Mode-33 briefings and headerless MPEG-PS movies (above): upstream player features.
- `Tick` decodes while holding the player mutex, so the main thread's
  `mwPlyGetCurFrm` can wait for one decode (~2.4 ms for 1824x1024). Upstream design; no
  frame-rate problem seen.
- The audio clock under SDL's disk/dummy drivers is a few percent slow (test artefact).
- XACT movie cues and the menu ambient are the audio package's (they played in my runs
  with its work in the tree).

## 9. Requests

See the structured result (lead_requests / requests_for_others).
