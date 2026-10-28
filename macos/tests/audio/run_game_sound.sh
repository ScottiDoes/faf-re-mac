#!/bin/zsh
# The game with sound (audio package tests): like macos/run_game.sh, but without /nosound,
# and with SDL3's audio output captured to a file by default.
# Usage: macos/tests/audio/run_game_sound.sh [extra faf_game switches ...]
# Environment (besides run_game.sh's REPLAY, REPLAYID, SPEED, INIT, SIZE, PREFS, TIME, OUT,
# LOG, FAF_GAME_EXE, FAF_VULKAN_DRIVER, ECHO):
#   AUDIO=disk         SDL's disk driver: the mixed output goes to $OUT/audio.f32 (32-bit float,
#                      48 kHz, stereo; macos/tests/audio/analyze_audio.py reads it). Default.
#   AUDIO=device       the real output device (SDL's default driver)
#   AUDIO=dummy        SDL's dummy driver (output discarded)
#   MOVIES=1           drop /nomovie too (default: movies off, so only sound is under test)
# Exit status: the game's, 124 when TIME ran out.
set -u
root=${0:A:h:h:h:h}
RUN=/Users/scottharris/Gaming/faf-re-bin/run-native
EXE=${FAF_GAME_EXE:-$root/build/m4-audio/faf_game}
OUT=${OUT:-$root/build/m4-audio-tests/game}
mkdir -p "$OUT" || exit 2
OUT=${OUT:A}
LOG=${LOG:-$OUT/game.log}
LOG=${LOG:A}
TIME=${TIME:-0}
SIZE=${SIZE:-2560 1600}
AUDIO=${AUDIO:-disk}
fail() { print -u2 "run_game_sound: $*"; exit 2 }
[[ -x $EXE ]] || fail "no game at $EXE"
INIT=${INIT:-}
if [[ -z $INIT ]]; then
    if [[ -n ${SPEED:-} && -n ${REPLAY:-} ]]; then INIT=init_bench.lua; else INIT=init_faf.lua; fi
fi
[[ $INIT == /* ]] || INIT=$RUN/$INIT
args=(/init "$INIT" /nobugreport /log "$LOG")
[[ ${MOVIES:-0} == 1 ]] || args+=(/nomovie)
[[ -n ${PREFS:-} ]] && args+=(/prefs "$PREFS")
[[ $SIZE != prefs ]] && args+=(/windowed ${=SIZE})
if [[ -n ${REPLAY:-} ]]; then
    REPLAY=${REPLAY:A}
    [[ -f $REPLAY ]] || fail "no replay at $REPLAY"
    if [[ -z ${REPLAYID:-} ]]; then
        REPLAYID=${${REPLAY:t}//[^0-9]/}
        [[ -n $REPLAYID ]] || REPLAYID=0
    fi
    args+=(/replay "$REPLAY" /replayid "$REPLAYID")
    [[ -n ${SPEED:-} ]] && args+=(/benchspeed "$SPEED")
fi
args+=("$@")
case $AUDIO in
    disk)
        export SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$OUT/audio.f32
        export SDL_AUDIO_FORMAT=F32 SDL_AUDIO_FREQUENCY=48000 SDL_AUDIO_CHANNELS=2
        rm -f "$OUT/audio.f32" ;;
    dummy) export SDL_AUDIO_DRIVER=dummy ;;
    device) ;;
    *) fail "AUDIO must be disk, dummy or device" ;;
esac
rm -f "$LOG"
print "run_game_sound: ${REPLAY:+replay ${REPLAY:t}, }audio $AUDIO, log $LOG"
cd "$RUN" || fail "cannot enter $RUN"
tailpid=
if [[ ${ECHO:-0} == 1 ]]; then
    : > "$LOG"; tail -n +1 -F "$LOG" >&2 2>/dev/null & tailpid=$!
fi
python3 -I "$root/macos/tests/bringup/watchdog.py" "$TIME" "$EXE" "${args[@]}"
rc=$?
[[ -n $tailpid ]] && kill $tailpid 2>/dev/null
print "run_game_sound: exit status $rc"
if [[ -f $LOG ]]; then
    grep -m1 -E "SND: FACT" "$LOG" | sed 's/^/  /'
    print "  checksum mismatches in the log: $(grep -c 'Checksum for beat' "$LOG")"
    print "  SND warnings: $(grep -c -E 'warning:.*SND|SND: Error' "$LOG")"
fi
[[ -f $OUT/audio.f32 ]] && print "  captured audio: $OUT/audio.f32 ($(( $(stat -f %z "$OUT/audio.f32") / 384000 )) s)"
exit $rc
