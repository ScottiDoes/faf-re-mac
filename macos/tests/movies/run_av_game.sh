#!/bin/zsh
# The game in a window WITH sound and movies (package movies, milestone 3; notes:
# macos/porting/movies.md): macos/run_game.sh's arguments without /nosound /nomovie, plus a
# capture of everything the game plays through SDL's disk audio driver.
#
# Usage: macos/tests/movies/run_av_game.sh [extra faf_game switches ...]
# Environment (as macos/run_game.sh: REPLAY, REPLAYID, SPEED, INIT, SIZE, PREFS, TIME, OUT,
# LOG, FAF_GAME_EXE, SHOTS, ECHO; OUT defaults to build/m4-movies-tests/game), plus:
#   AUDIO=disk     (default) SDL's disk driver writes the mixed output to $OUT/audio.raw in
#                  the device format it logs at open (see $OUT/stderr.txt: "Writing to file
#                  [...], format=... channels=... freq=..."); analyse it with
#                  macos/tests/movies/analyse_audio.py
#   AUDIO=dummy    output discarded;  AUDIO=device  the real output device
#   NOSOUND=1 / NOMOVIE=1   pass /nosound / /nomovie after all
# Exit status: the game's (0 normal quit), 124 when TIME ran out.
set -u
root=${0:A:h:h:h:h}
source "$root/macos/dev_paths.sh"
RUN=$FAF_RUN_DIR
SDK=$FAF_VULKAN_SDK
EXE=${FAF_GAME_EXE:-$root/build/m4-movies/faf_game}
OUT=${OUT:-$root/build/m4-movies-tests/game}
mkdir -p "$OUT" || { print -u2 "run_av_game: cannot create $OUT"; exit 2 }
OUT=${OUT:A}
LOG=${LOG:-$OUT/game.log}
LOG=${LOG:A}
TIME=${TIME:-0}
SIZE=${SIZE:-2560 1600}
AUDIO=${AUDIO:-disk}

fail() { print -u2 "run_av_game: $*"; exit 2 }
[[ -x $EXE ]] || fail "no game at $EXE"
faf_check_run_folder run_av_game
faf_note_run_folder

INIT=${INIT:-}
if [[ -z $INIT ]]; then
    if [[ -n ${SPEED:-} && -n ${REPLAY:-} ]]; then INIT=init_bench.lua; else INIT=init_faf.lua; fi
fi
[[ $INIT == /* ]] || INIT=$RUN/$INIT
[[ -f $INIT ]] || fail "no init file $INIT"

args=(/init "$INIT" /nobugreport /log "$LOG")
[[ ${NOSOUND:-0} == 1 ]] && args+=(/nosound)
[[ ${NOMOVIE:-0} == 1 ]] && args+=(/nomovie)
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
    disk) export SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$OUT/audio.raw; rm -f "$OUT/audio.raw" ;;
    dummy) export SDL_AUDIO_DRIVER=dummy ;;
    device) ;;
    *) fail "AUDIO=disk|dummy|device" ;;
esac
if [[ -n ${SHOTS:-} ]]; then
    shots=$OUT/shots
    mkdir -p "$shots"
    rm -f "$shots"/*.png(N) "$shots"/*.ppm(N)
    export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot${VK_INSTANCE_LAYERS:+:$VK_INSTANCE_LAYERS}
    export VK_SCREENSHOT_FRAMES=$SHOTS VK_SCREENSHOT_DIR=$shots
    export VK_ADD_LAYER_PATH=${VK_ADD_LAYER_PATH:-$SDK/share/vulkan/explicit_layer.d}
    export DYLD_LIBRARY_PATH=$SDK/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}
fi

rm -f "$LOG"
print "run_av_game: ${REPLAY:+replay ${REPLAY:t}, }audio $AUDIO, log $LOG"
[[ $TIME != 0 ]] && print "run_av_game: stops after $TIME s"
cd "$RUN" || fail "cannot enter $RUN"
python3 -I "$root/macos/tests/bringup/watchdog.py" "$TIME" "$EXE" "${args[@]}" 2> "$OUT/stderr.txt"
rc=$?
print "run_av_game: exit status $rc"
if [[ -f $LOG ]]; then
    print "  checksum mismatches in the log: $(grep -c 'Checksum for beat' "$LOG")"
    grep -E "OpenMovie|movie|Movie|DirectSound|FAF SFD" "$LOG" | head -40 | sed 's/^/  /'
fi
grep -m2 -E "Writing to file|disk i/o" "$OUT/stderr.txt" | sed 's/^/  /'
if [[ -n ${SHOTS:-} ]] && ls "$shots"/*.ppm >/dev/null 2>&1; then
    python3 -I "$root/macos/tests/bringup/ppm2png.py" "$shots"/*.ppm && rm -f "$shots"/*.ppm
fi
exit $rc
