#!/bin/zsh
# Test (a) of the audio package: every cue of FA's banks (and FAF's in ~/.faforever/sounds)
# through FACT and SDL3's disk audio driver, in SHARDS parallel processes (each loads every
# bank and plays its share of the sound banks). Results: $OUT/sweep.csv (one row per cue),
# $OUT/sweep-<i>.err (load log), $OUT/sweep-<i>.f32 (the raw mixed output, 48 kHz stereo
# float; delete when done, it is large). Summary: macos/tests/audio/sweep_report.py.
# Usage: macos/tests/audio/run_sweep.sh   (env: SHARDS=6 TIMESCALE=0.25 CAP_MS=1500 LIMIT=2400)
set -u
root=${0:A:h:h:h:h}
OUT=${OUT:-$root/build/m4-audio-tests/sweep}
SHARDS=${SHARDS:-6}
TIMESCALE=${TIMESCALE:-0.25}
CAP_MS=${CAP_MS:-1500}
LIMIT=${LIMIT:-2400}
fa=$(sed -n 's/^fa_path *= *[\x27"]\(.*\)[\x27"].*/\1/p' /Users/scottharris/Gaming/.faf/faf-re-bin/fa_path.lua)
[[ -d $fa/sounds ]] || { print -u2 "run_sweep: no FA sounds at $fa"; exit 2 }
exe=$root/build/m4-audio-tests/fact_bank_sweep
[[ -x $exe ]] || { print -u2 "run_sweep: build it first (macos/tests/audio/build.sh)"; exit 2 }
mkdir -p "$OUT"
dirs=("$fa/sounds")
[[ -d ~/.faforever/sounds ]] && dirs+=(~/.faforever/sounds)
pids=()
for ((i = 0; i < SHARDS; i++)); do
    python3 -I "$root/macos/tests/bringup/watchdog.py" $LIMIT "$exe" --xgs "$fa/sounds/SupCom.xgs" \
        --raw "$OUT/sweep-$i.f32" --timescale $TIMESCALE --cap-ms $CAP_MS --shard $i/$SHARDS \
        "${dirs[@]}" > "$OUT/sweep-$i.csv" 2> "$OUT/sweep-$i.err" &
    pids+=($!)
done
rc=0
for p in $pids; do wait $p || rc=$?; done
head -1 "$OUT/sweep-0.csv" > "$OUT/sweep.csv"
for ((i = 0; i < SHARDS; i++)); do tail -n +2 "$OUT/sweep-$i.csv" >> "$OUT/sweep.csv"; done
grep -h "^played" "$OUT"/sweep-*.err
print "run_sweep: $(($(wc -l < "$OUT/sweep.csv") - 1)) cues in $OUT/sweep.csv (exit $rc)"
exit $rc
