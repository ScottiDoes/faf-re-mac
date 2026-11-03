#!/bin/zsh
# Replay verification in one command (headless: no window, no sound, no movie).
#
# Usage: macos/verify_replays.sh [replay.scfareplay | replay.fafreplay ...]
#
# Plays every replay (default ~/Gaming/.faf/faf/bench/corpus/*.scfareplay) with macos/run_replay.sh
# at SPEED=50, once with /simworkers 0 (serial sim) and once with /simworkers 8 (threaded sim),
# and checks each run against the checksums every player recorded. Then:
#   - the negative control: a copy of one replay with one digest byte flipped at its middle
#     checksum beat must give exactly one mismatch per player who recorded that beat, exit 1;
#     and a copy without any checksum record must not pass (verdict=unverified, exit 2);
#   - the sync check: one replay with the sim's per-beat log on, the sim's own digest of every
#     checksum beat compared with every recorded digest (not relying on the mismatch warning);
#   - the post-link checks of the executable: ld map order (StaticInitPreRegister first, then
#     src/sources.cmake order), the order initializers really run in, the GPG_PREREGISTER_INIT
#     section, libm imports, fused multiply-add (macos/tests/replay/check_postlink.py);
#   - the runner's unit test (macos/tests/replay/build_unit_test.sh).
# A .fafreplay (FAF client or vault download) is converted into $OUT/converted first. A replay
# whose featured mod is not installed (coop, ...) or whose game version is not the installed
# gamedata's is skipped with a note. Prints a table (replay, map, players, recorded checksums,
# checked, mismatches, final tick, x real time, workers, verdict) and saves it with every log in
# $OUT (RESULTS.txt, results.json, runs/<id>-w<N>/).
#
# Environment:
#   FAF_REPLAY_EXE  runner to test (default build/macos/faf_replay); the post-link checks read
#                   its build tree (the .map next to it, CMakeFiles/faf_engine.dir)
#   OUT             output folder (default build/verify-replays)
#   SPEED           /benchspeed (default 50)
#   WORKERS         /simworkers values (default "0 8")
#   MAXMIN          wall-clock limit per run in minutes (default 20; a watchdog adds 2)
#   NEGCTL          replay id for the negative controls (default: the fastest passing replay
#                   with at least two players); 0 skips them
#   SYNCCHECK       replay id for the sync check (same default); 0 skips it (it writes the
#                   sim's per-beat logs: about 430 MB for 27890640)
#   POSTLINK=0      skip the post-link checks;  UNITTEST=0  skip the unit test
#   FEATURED_MODS   featured mods the run folder's data serves (default "faf ladder1v1")
#
# Exit status: 0 everything passed, 1 something failed, 2 nothing could run.
# See macos/porting/lockin.md.
set -u
root=${0:A:h:h}
export PATH=/opt/homebrew/bin:$PATH
exec python3 -I "$root/macos/tests/replay/verify_replays.py" "$@"
