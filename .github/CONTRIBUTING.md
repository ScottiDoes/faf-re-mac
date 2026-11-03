# Contributing

This repository is the macOS port of [faf-re](https://github.com/Draiget/faf-re): FAF Native, the engine built natively for Apple Silicon. Contributions here are about Mac compatibility.

## What belongs here

- Building on a Mac: [macos/](../macos) (CMake, `build_app.sh`, `get.sh`, `release.sh`).
- The FAF Native app: the setup window, the launcher and the disk image ([macos/app](../macos/app)).
- The layer that stands in for Windows: [src/platform/posix](../src/platform/posix) (files, threads, sockets, fonts, the C runtime, x87 math).
- Rendering on Vulkan and MoltenVK, audio, movies, input, Retina and display scaling.
- Bugs that only happen on a Mac, and docs for Mac players and builders.

## What doesn't

Changes to the engine itself (the recovered code, the simulation, gameplay, features for every platform) belong in [faf-re](https://github.com/Draiget/faf-re). They come to the Mac with the next sync. A bug that also happens on Windows goes there too.

## Keep the engine code close to faf-re

The port is merged with faf-re often, so every changed line in a faf-re file is a possible conflict.

- Put Mac code in `macos/` or `src/platform/posix/` where you can.
- When a faf-re file has to change, keep the change small and inside `#if defined(FAF_POSIX)`, so other builds stay as they are.
- Leave the top-level `README.md` and `CONTRIBUTING.md` alone: they're faf-re's. The Mac versions are in `.github/`.

## Games must match Windows

Mac players share online games and replays with Windows players, so the Mac must compute exactly what the Windows engine does, bit for bit ([macos/docs/DETERMINISM.md](../macos/docs/DETERMINISM.md)). A change that can reach the simulation (math, the x87 emulation, C runtime functions, threading, compiler flags) needs a replay check: make a run folder from your FAF client's files (`macos/make_run_folder.sh`), play FAF replays with the headless checker (`REPLAY=<file> macos/run_replay.sh`, or `macos/verify_replays.sh` for a set; [macos/porting/runner.md](../macos/porting/runner.md)) and list every replay in the pull request, failures included.

Changes to the app, the window, rendering, audio or docs don't need one. Say what you checked by hand instead.

## Pull requests

- One topic per pull request.
- Build it with [macos/BUILDING.md](../macos/BUILDING.md) and say which Mac and macOS version you tested on.
- Commit titles say what's true after the change, prefixed with the area: `macOS app: ...`, `macOS: ...`.
- Say what you couldn't test.

Files in `macos/` are under the MIT licence ([macos/LICENSE](../macos/LICENSE)), and contributions to them are too.

## Reporting bugs

Open an issue with your Mac (chip and macOS version), the FAF Native version (in the app's details popover, e.g. `5844bcf5, built 2026-11-03 14:33`), what you did, what happened, and a replay ID if there is one. For a desync, the replay ID and when it desynced help most.
