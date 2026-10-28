#!/bin/zsh
# The game in a window (milestone 2): runs build/macos/faf_game from the native run folder
# /Users/scottharris/Gaming/faf-re-bin/run-native (FAF's init files; init_faf.lua reads
# ../fa_path.lua and mounts ../gamedata, both next to that folder), as the FAF client starts
# the game: /init init_faf.lua /nobugreport, plus /nosound /nomovie (no audio or movies on
# macOS yet) and a log file. Rendering is faf-re's Vulkan renderer on MoltenVK (or
# KosmicKrisp) from the LunarG Vulkan SDK; the engine finds the SDK by itself.
#
# Usage: macos/run_game.sh [extra faf_game switches ...]
# Environment:
#   REPLAY=<file>      start this replay (/replay <file> /replayid <id>); without it the game
#                      starts at the main menu
#   REPLAYID           the replay's id (default: the digits in the file name, else 0)
#   SPEED=<n>          replay game speed -10..10, held by the replay benchmark's hook
#                      (init_bench.lua + /benchspeed <n>); default: the replay's own speed
#   INIT               init file, absolute or in the run folder (default init_faf.lua;
#                      init_bench.lua with SPEED)
#   SIZE="<w> <h>"     a window of this client size in its backing store's pixels (/windowed w h;
#                      2 per point on a Retina desktop). Default: the game's own choice
#                      from Game.prefs (primary_adapter): a resolution is exclusive full
#                      screen at that display mode, the panel's native one by default;
#                      "windowed" is the saved window size
#   PREFS=<name>       preferences file in ~/Library/Application Support/Gas Powered Games/
#                      Supreme Commander Forged Alliance (/prefs <name>; default Game.prefs).
#                      bringup-high.prefs there has the bring-up's high-fidelity profile
#   TIME=<s>           watchdog: stop the game after s seconds (default 0 = no limit; set it
#                      for unattended runs)
#   OUT                folder for the log and captures (default build/macos/game)
#   LOG                engine log (default $OUT/game.log)
#   FAF_GAME_EXE       game to run (default build/macos/faf_game)
#   FAF_VULKAN_DRIVER  moltenvk (default) or kosmickrisp (or a driver manifest .json path)
#   VALIDATE=1         Vulkan validation (FAF_VULKAN_VALIDATION=1): messages in the log as
#                      "Vulkan validation: ..."
#   SHOTS=<frames>     capture presented frames with VK_LAYER_LUNARG_screenshot: a comma-
#                      separated list of frame numbers ("600,1200"; 60 frames a second with
#                      vsync); PNGs in $OUT/shots
#   ECHO=1             also print the engine log to stderr as it is written
# Every other environment variable passes through (FAF_SHELL_HIGHDPI=0 for one pixel per point,
# FAF_SHADER_CACHE_DIR, VK_* ...).
# Exit status: the game's (0 normal quit), 124 when TIME ran out.
set -u
root=${0:A:h:h}
build=$root/build/macos
RUN=/Users/scottharris/Gaming/faf-re-bin/run-native
SDK=/Users/scottharris/Gaming/VulkanSDK/1.4.363.0/macOS
EXE=${FAF_GAME_EXE:-$build/faf_game}
OUT=${OUT:-$build/game}
mkdir -p "$OUT" || { print -u2 "run_game: cannot create $OUT"; exit 2 }
OUT=${OUT:A}
LOG=${LOG:-$OUT/game.log}
LOG=${LOG:A}
TIME=${TIME:-0}
SIZE=${SIZE:-prefs}

fail() { print -u2 "run_game: $*"; exit 2 }
[[ -x $EXE ]] || fail "no game at $EXE (ninja -C build/macos faf_game)"
[[ -f $RUN/init_faf.lua ]] || fail "no run folder at $RUN"
[[ -f $RUN/../fa_path.lua ]] || fail "missing ${RUN:h}/fa_path.lua (copy of ~/.faforever/replaydata/fa_path.lua)"
[[ -d $RUN/../gamedata ]] || fail "missing ${RUN:h}/gamedata (link to ~/.faforever/replaydata/gamedata)"

INIT=${INIT:-}
if [[ -z $INIT ]]; then
    if [[ -n ${SPEED:-} && -n ${REPLAY:-} ]]; then INIT=init_bench.lua; else INIT=init_faf.lua; fi
fi
[[ $INIT == /* ]] || INIT=$RUN/$INIT
[[ -f $INIT ]] || fail "no init file $INIT"

args=(/init "$INIT" /nobugreport /log "$LOG" /nosound /nomovie)
[[ -n ${PREFS:-} ]] && args+=(/prefs "$PREFS")
if [[ $SIZE != prefs ]]; then
    args+=(/windowed ${=SIZE})
fi
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

[[ ${VALIDATE:-0} == 1 ]] && export FAF_VULKAN_VALIDATION=1
if [[ -n ${SHOTS:-} ]]; then
    shots=$OUT/shots
    mkdir -p "$shots"
    export VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot${VK_INSTANCE_LAYERS:+:$VK_INSTANCE_LAYERS}
    export VK_SCREENSHOT_FRAMES=$SHOTS VK_SCREENSHOT_DIR=$shots
    export VK_ADD_LAYER_PATH=${VK_ADD_LAYER_PATH:-$SDK/share/vulkan/explicit_layer.d}
    export DYLD_LIBRARY_PATH=$SDK/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}
fi

rm -f "$LOG"
print "run_game: ${REPLAY:+replay ${REPLAY:t}, }driver ${FAF_VULKAN_DRIVER:-moltenvk}, log $LOG"
[[ $TIME != 0 ]] && print "run_game: stops after $TIME s"
# The engine resolves the init file's folder, its launch directory and the fafengine hook
# folder against the run folder, as the FAF client runs the game from its bin folder.
cd "$RUN" || fail "cannot enter $RUN"
tailpid=
if [[ ${ECHO:-0} == 1 ]]; then
    : > "$LOG"; tail -n +1 -F "$LOG" >&2 2>/dev/null & tailpid=$!
fi
python3 -I "$root/macos/tests/bringup/watchdog.py" "$TIME" "$EXE" "${args[@]}"
rc=$?
[[ -n $tailpid ]] && kill $tailpid 2>/dev/null

print "run_game: exit status $rc"
if [[ -f $LOG ]]; then
    grep -m1 -E "Vulkan: ready" "$LOG" | sed 's/^/  /'
    print "  checksum mismatches in the log: $(grep -c 'Checksum for beat' "$LOG")"
    print "  Vulkan validation messages: $(grep -c 'Vulkan validation' "$LOG")"
fi
if [[ -n ${SHOTS:-} ]] && ls "$shots"/*.ppm >/dev/null 2>&1; then
    python3 -I "$root/macos/tests/bringup/ppm2png.py" "$shots"/*.ppm && rm -f "$shots"/*.ppm
fi
exit $rc
