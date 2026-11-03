<!-- Changes to the engine itself (the recovered code, the simulation, gameplay) that aren't about the Mac go to faf-re: https://github.com/Draiget/faf-re. See .github/CONTRIBUTING.md. -->

## What changed

<!-- What the PR does and why. Link the issue, if there is one. -->

## Mac

- Mac and macOS version: <!-- e.g. MacBook Pro M2 Max, macOS 27.0 -->
- Built with: <!-- macos/get.sh, macos/build_app.sh or the development tree -->

## Simulation

<!-- Tick one. Mac players share games and replays with Windows players, so whatever the simulation computes must match Windows bit for bit (macos/docs/DETERMINISM.md). -->

- [ ] **Not touched.** The app, the window, rendering, audio, movies, input, scripts or docs only.
- [ ] **Touched.** Math, the x87 emulation, C runtime functions, threading, compiler flags, or a faf-re file the simulation runs. List the replays you played below.

## Testing

<!-- The replays you played (macos/run_replay.sh or macos/verify_replays.sh), failures included, and what you checked by hand. -->

| Replay ID | Result |
|---|---|
| | <!-- all checksums match / first mismatch at beat N / crash --> |

- [ ] Checked by hand: <!-- what you looked at -->

## Notes for the reviewer

<!-- Anything you couldn't test, known side effects, follow-ups. -->
