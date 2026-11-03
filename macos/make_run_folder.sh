#!/bin/zsh
# Makes the run folder the development scripts start the game in (FAF_RUN_DIR,
# macos/dev_paths.sh) from the FAF client's files, laid out as the client lays out its own:
#   <OUT>/bin/          FAF's init files (copies), init_bench.lua (the replay checker's init),
#                       and fafengine, a link to this repository's data/fafengine (the engine's
#                       hook folder, looked for in the run folder)
#   <OUT>/fa_path.lua   a copy of the client's (the Forged Alliance folder, the game version)
#   <OUT>/gamedata      a link to the client's game data
# The client keeps the game data of the current game version in ~/.faforever, and that of the
# version it last played a replay of in ~/.faforever/replaydata: give that folder as the source
# to play replays of that version.
#
# Usage: macos/make_run_folder.sh [source]   (default $FAF_CLIENT_DIR, ~/.faforever)
# Environment:
#   OUT          the folder to make (default build/run, whose bin is the scripts' default
#                FAF_RUN_DIR; elsewhere, point FAF_RUN_DIR at <OUT>/bin)
#   BENCH_MOUNT  a folder holding the replay benchmark's benchhook/ (lua/ui/game/gamemain.lua):
#                init_bench.lua mounts it, so replays run with UI=1 print BENCH lines
# The source folder is only read.
set -eu
root=${0:A:h:h}
source "$root/macos/dev_paths.sh"
src=${${1:-$FAF_CLIENT_DIR}:a}
OUT=${${OUT:-$root/build/run}:a}
fail() { print -u2 -r -- "make_run_folder: $*"; exit 1 }
for f in bin/init.lua bin/init_faf.lua fa_path.lua; do
    [[ -f $src/$f ]] || fail "no $f in $src (the FAF client's folder, once the client has set it up)"
done
[[ -d $src/gamedata ]] || fail "no gamedata in $src"
for f in "$OUT/gamedata" "$OUT/bin/fafengine"; do
    [[ ! -e $f || -L $f ]] || fail "$f is not a link; move it away first"
done
[[ -z ${BENCH_MOUNT:-} || -d $BENCH_MOUNT/benchhook ]] || fail "no benchhook folder in $BENCH_MOUNT"

mkdir -p "$OUT/bin"
cp "$src"/bin/*.lua "$OUT/bin/"
cp "$src/fa_path.lua" "$OUT/fa_path.lua"
ln -sfn "$src/gamedata" "$OUT/gamedata"
ln -sfn "$root/data/fafengine" "$OUT/bin/fafengine"
{
    print -r -- "-- Replay checker init (macos/run_replay.sh): FAF's init.lua, plus the replay benchmark's"
    print -r -- "-- hook when macos/make_run_folder.sh was given one (BENCH_MOUNT)."
    print -r -- "dofile(InitFileDir .. '/init.lua')"
    if [[ -n ${BENCH_MOUNT:-} ]]; then
        print -r -- "table.insert(path, { dir = [[${BENCH_MOUNT:a}]], mountpoint = '/' })"
        print -r -- "table.insert(hook, '/benchhook')"
    fi
} > "$OUT/bin/init_bench.lua"
version=$(sed -n 's/^GameVersion *= *"\(.*\)".*/\1/p' "$OUT/fa_path.lua")
print -r -- "make_run_folder: $OUT/bin, from $src (game version ${version:-unknown})"
