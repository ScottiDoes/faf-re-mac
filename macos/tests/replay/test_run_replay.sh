#!/bin/zsh
# test_run_replay.sh: tests macos/run_replay.sh with a stand-in runner (no engine): a relative
# REPLAY is made absolute before the script enters the run folder (WAVE3 F3), REPLAYID comes
# from the file name, the result line's recorded/checked/verdict are summarised, and the
# runner's exit status is passed through. Output in build/m4-lockin-tests/run_replay (OUT), with
# a stand-in run folder there (FAF_RUN_DIR).
set -u
root=${0:A:h:h:h:h}
OUT=${OUT:-$root/build/m4-lockin-tests/run_replay}
mkdir -p "$OUT/sub dir"
fake=$OUT/fake_runner.sh
cat > "$fake" <<'EOF'
#!/bin/zsh
# Stand-in for faf_replay: records its cwd and arguments, checks the replay path it got.
print -r -- "cwd=$PWD" > "$FAKE_TRACE"
print -r -- "args=${(j:|:)@}" >> "$FAKE_TRACE"
replay= id= log=
while (( $# )); do case $1 in /replay) replay=$2; shift;; /replayid) id=$2; shift;; /log) log=$2; shift;; esac; shift; done
print -r -- "replay=$replay" >> "$FAKE_TRACE"
[[ -f $replay ]] && print "exists=1" >> "$FAKE_TRACE" || print "exists=0" >> "$FAKE_TRACE"
print "info: faf_replay: sim rate: ticks 1..100 in 1.0 s = 100.0 ticks/s = 10.00x real time" > "$log"
print "FAF_REPLAY_RESULT replay=$id beats=101 tick=100 recorded=6 checked=4 mismatches=0 exit=end-of-replay verdict=pass"
exit ${FAKE_RC:-0}
EOF
chmod +x "$fake"
print -n "fake" > "$OUT/sub dir/replay_12345678.scfareplay"
export FAF_RUN_DIR=${OUT:A}/run/bin
mkdir -p "$FAF_RUN_DIR" "${FAF_RUN_DIR:h}/gamedata"
touch "$FAF_RUN_DIR/init_faf.lua" "$FAF_RUN_DIR/init_bench.lua" "${FAF_RUN_DIR:h}/fa_path.lua"

failures=0
check() { if eval "$2"; then print "ok    $1"; else print "FAIL  $1"; failures=$((failures + 1)); fi }

trace=$OUT/trace.txt
out=$(cd "$OUT" && FAKE_TRACE=$trace FAKE_RC=0 FAF_REPLAY_EXE=$fake OUT=$OUT REPLAY="sub dir/replay_12345678.scfareplay" \
      zsh "$root/macos/run_replay.sh" /simworkers 8)
rc=$?
check "exit status 0 passed through" '(( rc == 0 ))'
check "runs in the run folder" 'grep -qx "cwd=$FAF_RUN_DIR" $trace'
check "relative REPLAY made absolute" 'grep -qx "replay=${OUT:A}/sub dir/replay_12345678.scfareplay" $trace'
check "the runner finds the replay" 'grep -qx "exists=1" $trace'
check "REPLAYID from the file name" 'grep -q "|/replayid|12345678|" $trace'
check "extra switches passed on" 'grep -q "|/simworkers|8$" $trace'
check "result saved" '[[ -f $OUT/replay-12345678.result ]]'
check "summary line" '[[ $out == *"recorded player checksums: 6, compared by the sim: 4 (verdict pass)"* ]]'
check "sim rate line" '[[ $out == *"sim rate: ticks 1..100"* ]]'

out=$(cd "$OUT" && FAKE_TRACE=$trace FAKE_RC=2 FAF_REPLAY_EXE=$fake OUT=$OUT REPLAYID=custom \
      REPLAY="sub dir/replay_12345678.scfareplay" zsh "$root/macos/run_replay.sh")
rc=$?
check "exit status 2 passed through" '(( rc == 2 ))'
check "REPLAYID kept when given" 'grep -q "|/replayid|custom|" $trace'

out=$(cd "$OUT" && FAF_REPLAY_EXE=$fake OUT=$OUT REPLAY="no such.scfareplay" zsh "$root/macos/run_replay.sh" 2>&1)
rc=$?
check "missing replay: status 2" '(( rc == 2 ))'

(( failures == 0 )) && print "ALL PASS" || print "$failures FAILED"
exit $(( failures != 0 ))
