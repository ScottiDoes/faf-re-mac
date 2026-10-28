# Package audio-movie (macOS port, milestone 1)

Owns `src/moho/audio/**`, `src/moho/movie/**`, `macos/exclusions.d/audio-movie.cmake`,
`src/platform/posix/stubs/audio-movie/`.

Goal for milestone 1: every translation unit compiles, the sim-side sound state is
untouched, and the playback engines (XACT, X3DAudio, DirectSound, FFmpeg) are null.

## Approach

**Sound is a null playback engine behind the game's own `/nosound` path.**
`func_InitSound` (AudioEngine.cpp) sets `mNoSound = 1` on POSIX and logs
`FAF_POSIX_STUB: SND: no XACT runtime on this platform; running as with /nosound`. Every
`AudioEngine` then comes up the way it does under `/nosound` on Windows: an
`AudioEngineImpl` with no XACT instance, registered in `sSoundConfiguration`, every call a
no-op (`SND_Enabled()` is false, `CSndParams::GetEngine` / `CSndVar::DoResolve` resolve
nothing, `HSound` loops complete at once). This is the state the Windows replay-parity
runner already uses (`tools/replay-parity/parity.py` passes `/nosound /nomovie`).

**Sim state is unchanged.** `CSimSoundManager`, `SAudioRequest`, `SParamKey`, `CSndParams`,
`CSndVar`, `HSound` and all their TypeInfo/serializer/reflection TUs compile unedited.
None of them consults the audio engine; their archive formats use fixed-width
`WriteUInt`/`WriteInt` and `SAudioRequest` has no `long`/`wchar_t` (checked for README rules 2 and 3).

**Movies are unavailable.** The SFD player (`IndependentSfdBackend`, `SofdecRuntime`,
`CMovie`, header/metadata parsers) compiles unedited. Only the FFmpeg decoder and the
DirectSound sink are replaced: `sfd::FfmpegDecoder::Open` throws, which `sfd::Player::Open`
already turns into its failed status (4) plus an error report, and `CMovie::OpenMovie` into
`initial mwPlyGetStat failed for movie ...` and `false`. The game treats that like any
missing or unplayable movie. Runs with `/nomovie` never open one.

## Edits to upstream files (all `#if defined(FAF_POSIX)`, Windows code unchanged)

| File | Change |
|---|---|
| `src/moho/audio/AudioEngine.h` | `<xact3.h>` comes from the stand-in on POSIX |
| `src/moho/audio/AudioEngine.cpp` | `<dsound.h>` comes from the stand-in. `func_CreateXACTinstance` returns `REGDB_E_CLASSNOTREG` (0x80040154) without the registry or COM (unreached). `func_InitSound` forces `mNoSound` |

## New files

| File | What |
|---|---|
| `stubs/audio-movie/include/xact3.h` | XACT3 SDK value types at the layouts AudioEngine.h asserts: notification descriptor and payload (pack 1), runtime parameters, streaming parameters, notification types, error codes. `IXACT3*` stay opaque. The engine's own XACT 2 vtables are in AudioEngine.h |
| `stubs/audio-movie/include/dsound.h` | `IDirectSound` / `IDirectSoundBuffer` in SDK vtable order, `DSBUFFERDESC`, DSSCL/DSBCAPS/DSSPEAKER/DSERR constants, `DirectSoundCreate` |
| `stubs/audio-movie/include/PosixWaveFormat.h` | `WAVEFORMATEX` (18 bytes) and `WAVEFORMATEXTENSIBLE` (0x28), byte-packed as in mmreg.h, behind the SDK's guard macros |
| ~~`stubs/audio-movie/AudioMovieStub.h`~~ | Removed in wave 2 (render-null.md): the stubs use the shared `FAF_POSIX_STUB` (compat/Errors.h) |
| `stubs/audio-movie/DirectSoundPosix.cpp` | `DirectSoundCreate` returns `DSERR_NODRIVER` and a null device. Both callers (func_InitSound, CMovieManager) already handle that: "Failed to create DirectSound.", stereo, no movie audio |
| `stubs/audio-movie/X3DAudioPosix.cpp` | `X3DAudioInitialize` zeroes the 20-byte handle; `X3DAudioCalculate` is a logged no-op. Both are unreached without an XACT instance |
| `stubs/audio-movie/XactClockPosix.cpp` | `SND_InstallXactClock`, a logged no-op |
| `stubs/audio-movie/FfmpegSfdDecoderPosix.cpp` | `sfd::FfmpegDecoder`: `Open` throws "FFmpeg is not built for this platform" |
| `stubs/audio-movie/DirectSoundMovieSinkPosix.cpp` | `sfd::AudioSink`: the constructor throws "No movie audio device", as the real one does with a null device. It is never constructed |

Layouts were checked with a scratch TU that `#undef`s `static_assert` (LayoutAsserts.h
turns every assert off on POSIX): the notification sizes and offsets, the
`AudioRuntimeParams`/`XACT_RUNTIME_PARAMETERS` offsets, `XACT_STREAMING_PARAMETERS` (0x18,
packetSize at 16), `WAVEFORMATEXTENSIBLE` (0x14/0x18/0x28) and `DSBUFFERDESC` (40).

## Excluded TUs (macos/exclusions.d/audio-movie.cmake)

| TU | Why | Replaced by |
|---|---|---|
| `moho/audio/XactClock.cpp` | Patches xactengine2_9.dll's PE import table (VirtualProtect, GetModuleHandleExW, IMAGE_*) | `XactClockPosix.cpp` |
| `moho/movie/sfd/FfmpegSfdDecoder.cpp` | FFmpeg is not built for macOS | `FfmpegSfdDecoderPosix.cpp` |
| `moho/movie/sfd/DirectSoundMovieSink.cpp` | DirectSound | `DirectSoundMovieSinkPosix.cpp` |

None of them has static initializers, so the static-init order is unaffected.

## Status

Compiles (`macos/tu.sh`): all of `src/moho/audio/*.cpp` and `src/moho/movie/**/*.cpp` that
are not excluded, plus the five stubs. Two TUs still wait on Win32 declarations from other
areas (core-sys). These are their only remaining errors, and both compile when the
declarations are supplied:

- `moho/audio/AudioEngine.cpp`: Files: `CreateFileA`, `GENERIC_READ`, `FILE_SHARE_READ`,
  `OPEN_EXISTING`, `CloseHandle`. The streaming wave-bank loader passes flags
  `0x60000000` (`FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING`). Modules: `FreeLibrary`.
  On POSIX none of these runs, because there is no XACT instance.
- `moho/movie/sfd/IndependentSfdBackend.cpp`: `OutputDebugStringA` (also needed by
  gpg/core/utils/Logging.cpp).

## Lead requests

1. Make `<dsound.h>` resolve for `moho/misc/StartupHelpers.cpp` (sim-game; CMovieManager
   uses `IDirectSound`, `DSBUFFERDESC`, `WAVEFORMATEX`, `DSSCL_PRIORITY`,
   `DSBCAPS_CTRLVOLUME`, `DSBVOLUME_MIN`, `DirectSoundCreate`). Either add
   `src/platform/posix/stubs/audio-movie/include` to faf_engine's BEFORE include
   directories, or move `dsound.h`, `xact3.h` and `PosixWaveFormat.h` into
   `src/platform/posix/include/`. My own TUs include them by explicit path, so they do not
   depend on this.
2. core-sys compat: the Files/Modules/Errors declarations listed under Status.
3. `macos/tu.sh`: for a file not in compile_commands.json, the fallback substitutes the
   resolved path (`${1:A}` -> `/Volumes/SSD Programs/...`, which contains a space) into the
   command string unquoted, and `shlex.split` breaks it. Substitute after splitting (or
   `shlex.quote(src)`).
4. `FAF_POSIX_STUB` (README rule 6) is not defined anywhere. The stubs here use a local
   `FAF_AUDIO_MOVIE_STUB` that logs `FAF_POSIX_STUB: <what>` once. They can switch when a
   shared macro exists.

## Milestone 3: sound and movies on macOS

**XACT via FAudio (FACT).**
- Vendor FAudio as a submodule and build it static with faf_codegen. No SDL is needed for
  FACT/F3DAudio themselves, but the mastering output needs an FAudio platform backend
  (SDL3 or a small CoreAudio one).
- The engine talks to the **XACT 2.9** COM vtables recovered in AudioEngine.h:
  `moho::IXACTEngine`, `IXACTSoundBank`, `IXACTCue`, `IXACTWaveBank` (riid
  893ff2e4-..., CLSID 343e68e6-...). It does not use XACT3's.
- The POSIX `func_CreateXACTinstance` should return an adapter object that implements
  those vtable slots on FACT's C API: `FACTCreateEngine`, `FACTAudioEngine_Initialize`,
  `_DoWork`, `_CreateSoundBank`, `_CreateInMemoryWaveBank`, `_CreateStreamingWaveBank`,
  `_GetCategory`, `_Stop`, `_SetVolume`, `_Pause`, `_Get/SetGlobalVariable`,
  `_RegisterNotification`, `_GetFinalMixFormat`; `FACTSoundBank_GetCueIndex`, `_Prepare`,
  `_Play`; and `FACTCue_*` including `SetMatrixCoefficients` and `Get/SetVariable`. Then
  drop the forced `mNoSound` in `func_InitSound`.
- Check first that FACT parses Supreme Commander's banks: `SupCom.xgs` and the
  `*.xsb`/`*.xwb` files are XACT 2.x content, while FACT targets XACT3 content versions.
  Compare the version fields. A converter may be needed.
- Map `AudioRuntimeParams` to `FACTRuntimeParameters`: global settings buffer, lookahead,
  and the notification callback. `FACTNotification` is byte-packed like
  `XACT_NOTIFICATION`, but convert field by field.
- Streaming wave banks: `FACTStreamingParameters.file` plus `fileIOCallbacks`. Hand FACT
  read/overlapped-result callbacks over the core-sys POSIX `HANDLE` from `CreateFileA`.
- Replace `X3DAudioPosix.cpp` with F3DAudio's `F3DAudioInitialize` / `F3DAudioCalculate`.
  The arguments match: listener, emitter, DSP settings, 20-byte handle, flags 0x61.
- `DirectSoundCreate` in func_InitSound only reads the speaker layout. Read the default
  CoreAudio output's channel count instead. `AudioEngine`'s constructor already resizes the
  DSP buffers from `GetFinalMixFormat`.
- `XactClock` stays excluded. FACT's clock does not wrap at 2^31 ms.
- None of this touches the sim: XACT only consumes `SAudioRequest`s on the user side.

**Movies via FFmpeg.**
- The submodule `dependencies/ffmpeg` (7.1.3) has a faf/ CMake build that only accepts
  clang-cl and the generated `faf/x64` and `faf/x86` configurations.
- Generate `faf/arm64-darwin/` (config.h, config_components.h, libavutil/avconfig.h,
  sources.cmake) by running FFmpeg's configure on macOS with the same
  `FAF_CONFIGURE_OPTIONS` (generate.sh) plus `--target-os=darwin --arch=aarch64 --cc=clang`.
  `--disable-asm` is the simple choice; NEON is fine too, since movie decoding has no
  determinism requirement.
- Teach faf/CMakeLists.txt to accept AppleClang, add it to macos/deps.cmake, and remove the
  `FfmpegSfdDecoder.cpp` exclusion and its stub.
- Movie audio needs a sink with `sfd::AudioSink`'s interface on CoreAudio (AudioQueue or
  AUHAL). `Clock()` must be the played-sample count, which is the player's master clock.
  CMovieManager (StartupHelpers.cpp) then needs a device handle other than
  `IDirectSound*`.

## Open issues

- `func_InitSound` still calls the `DirectSoundCreate` stub, so each start logs "Failed to
  create DirectSound." once (and CMovieManager once more without `/nosound`). This is
  harmless and kept so the Windows control flow stays unchanged.
- `IndependentSfdBackend` volume uses `long` (64-bit on macOS). It is in-memory only and
  clamped to -10000..0, so it is not a rule-2 case.
