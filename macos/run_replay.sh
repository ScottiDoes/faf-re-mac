#!/bin/zsh
# Headless native replay run (milestone 1): plays a FAF replay with build/macos/faf_replay and
# checks the sim against every checksum the players recorded. It passes the replay benchmark's
# arguments (~/Gaming/faf/bench/bench_replay.sh, mode fafre: /init init_bench.lua /nobugreport
# /log /replay /replayid /benchspeed) plus /nosound /nomovie, from the native run folder
# /Users/scottharris/Gaming/faf-re-bin/run-native (FAF's init files; init.lua reads
# ../fa_path.lua and mounts ../gamedata, both next to that folder).
#
# Usage: macos/run_replay.sh [extra faf_replay switches ...]
# Environment:
#   REPLAY            replay file (default ~/Gaming/faf/bench/replay_27890640.scfareplay); a
#                     relative path is taken from the directory the script is started in
#   REPLAYID          its id (default: the first run of digits in the file name, else the
#                     name without its extension)
#   SPEED             /benchspeed game speed, -10..10 (default 10, the replay slider's maximum)
#   MAXMIN            wall-clock limit in minutes (default 60)
#   OUT               folder for the log, result and checksum files (default build/macos)
#   LOG               engine log (default $OUT/replay-<id>.log)
#   FAF_REPLAY_EXE    runner to use (default build/macos/faf_replay)
#   EXITONGAMEOVER=1  stop at the first game over instead of the end of the replay stream
#   UI=1              run the game UI Lua too (default: /noui); the benchmark hook then prints
#                     BENCH lines. Without the UI the runner sets the /benchspeed game speed
#                     itself and logs its own sim rate. (To watch a replay in a window, use
#                     macos/run_game.sh.)
#   SYNCLOG=1         also keep the sim's per-beat logs (/synclog $OUT/synclog-<id>, every
#                     beat kept) and collect the checksum of every checksum beat into
#                     $OUT/checksums-<id>.txt; main_x64.exe takes the same switches
#                     (FAFRE_ARGS) for a beat-by-beat comparison with the x64 build
# Output: the runner's line
#   FAF_REPLAY_RESULT replay=<id> beats=<last beat> tick=<game tick> recorded=<r> checked=<c>
#     mismatches=<count> exit=<reason> verdict=<pass|mismatch|incomplete|fpcr|unverified>
# (also saved as $OUT/replay-<id>.result; recorded = player checksums in the replay, checked =
# how many of them the sim compared), the sim rate from the benchmark hook's BENCH lines, and
# the runner's exit status: 0 pass, 1 checksum mismatches, 2 the run did not complete or
# compared nothing (or a thread's FPCR was not 0), 3 engine Die; 128+n when signal n killed
# it. See macos/porting/runner.md and lockin.md; macos/verify_replays.sh runs a whole set.
set -u
root=${0:A:h:h}
build=$root/build/macos
RUN=/Users/scottharris/Gaming/faf-re-bin/run-native
EXE=${FAF_REPLAY_EXE:-$build/faf_replay}
REPLAY=${REPLAY:-/Users/scottharris/Gaming/faf/bench/replay_27890640.scfareplay}
REPLAY=${REPLAY:A}   # absolute: the run happens in the run folder (WAVE3 F3)
if [[ -z ${REPLAYID:-} ]]; then
    # The first run of digits in the file name (replay_27890640, 27890640-Player), else the name.
    if [[ ${REPLAY:t:r} =~ '[0-9]+' ]]; then REPLAYID=$MATCH; else REPLAYID=${REPLAY:t:r}; fi
    REPLAYID=${REPLAYID// /_}
fi
SPEED=${SPEED:-10}
MAXMIN=${MAXMIN:-60}
OUT=${OUT:-$build}
OUT=${OUT:A}   # absolute: the run happens in the run folder
LOG=${LOG:-$OUT/replay-$REPLAYID.log}
LOG=${LOG:A}
RESULT=$OUT/replay-$REPLAYID.result

fail() { print -u2 "run_replay: $*"; exit 2 }
[[ -x $EXE ]] || fail "no runner at $EXE (ninja -C build/macos faf_replay)"
[[ -f $REPLAY ]] || fail "no replay at $REPLAY"
[[ -f $RUN/init_bench.lua ]] || fail "no run folder at $RUN"
[[ -f $RUN/../fa_path.lua ]] || fail "missing ${RUN:h}/fa_path.lua (copy of ~/.faforever/replaydata/fa_path.lua)"
[[ -d $RUN/../gamedata ]] || fail "missing ${RUN:h}/gamedata (link to ~/.faforever/replaydata/gamedata)"

args=(/init "$RUN/init_bench.lua" /nobugreport /log "$LOG" /replay "$REPLAY" /replayid "$REPLAYID"
      /benchspeed "$SPEED" /nosound /nomovie /timeout $(( MAXMIN * 60 )))
[[ ${EXITONGAMEOVER:-0} == 1 ]] && args+=(/exitongameover)
[[ ${UI:-0} == 1 ]] || args+=(/noui)
synclog=$OUT/synclog-$REPLAYID
if [[ ${SYNCLOG:-0} == 1 ]]; then
    rm -rf "$synclog"; mkdir -p "$synclog"
    args+=(/synclog "$synclog" /con "sim_KeepAllLogFiles=true")
fi
args+=("$@")

mkdir -p "$OUT" || fail "cannot create $OUT"
rm -f "$LOG" "$RESULT"
print "run_replay: $REPLAYID at speed $SPEED, log $LOG"
# The engine resolves the init file's folder, its launch directory and the fafengine hook
# folder against the run folder, as the benchmark runs the game from its bin folder.
cd "$RUN" || fail "cannot enter $RUN"
"$EXE" "${args[@]}" | tee "$RESULT"
rc=${pipestatus[1]}

# Sim rate from the benchmark hook's BENCH lines (the same summary bench_replay.sh prints).
grep -oE "BENCH wall=[0-9.]+ tick=[0-9]+ speed=-?[0-9]+ target=-?[0-9]+" "$LOG" 2>/dev/null | awk '
    { split($2, w, "="); split($3, t, "="); split($4, s, "="); split($5, g, "="); wall = w[2] + 0; tick = t[2] + 0; spd = s[2] + 0; tgt = g[2] + 0 }
    spd == tgt && tick > 0 && !started { started = 1; w0 = wall; t0 = tick }
    started && tick > lt { w1 = wall; t1 = tick }
    { lt = tick }
    END {
        if (!started || w1 <= w0) { exit 1 }
        printf "  sim rate: %.1f min of game time in %.1f s = %.2fx real time (speed %+d)\n",
            (t1 - t0) / 600, w1 - w0, (t1 - t0) / 10 / (w1 - w0), tgt }' ||
    # No benchmark hook (/noui): the runner's own measurement (first moving tick to last).
    { grep -oE "faf_replay: sim rate: .*" "$LOG" 2>/dev/null | sed 's/^faf_replay: /  /' | grep . || print "  sim rate: no data" }
print "  checksum mismatches in the log: $(grep -c 'Checksum for beat' "$LOG" 2>/dev/null)"
result=$(grep -m1 '^FAF_REPLAY_RESULT' "$RESULT" 2>/dev/null)
if [[ $result == *" checked="* ]]; then
    recorded=${${result##* recorded=}%% *} checked=${${result##* checked=}%% *}
    print "  recorded player checksums: $recorded, compared by the sim: $checked (verdict ${result##* verdict=})"
fi

if [[ ${SYNCLOG:-0} == 1 ]]; then
    sums=$OUT/checksums-$REPLAYID.txt
    # "beat N final checksum: <md5>" per beat; beats off the checksum period carry zeros.
    find "$synclog" -name 'beat*.log' -print0 | xargs -0 grep -h 'final checksum' 2>/dev/null |
        grep -v ': 0\{32\}$' | sort -t' ' -k2,2n > "$sums"
    print "  sim checksums: $sums ($(wc -l < "$sums" | tr -d ' ') checksum beats)"
fi
exit $rc
