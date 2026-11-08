#!/bin/zsh
# Packs a replay-timing set for GitHub's Macs: the game data cut down to what a headless replay
# reads (tools/parity-data/make_pack.py build, plus .fx: the Mac runner compiles the shaders),
# the set's maps, the replays, and the init files macos/run_replay.sh wants, then compresses and
# encrypts it, as the retail archives must not be public.
#
# Usage: macos/tests/replay-timing/make_pack.sh <data folder> <out folder>
#   <data folder>  pick_replays.py's --out (selected.json, replays/, vault/maps/)
# Environment:
#   PACK_KEY_FILE  file holding the passphrase (required); the workflow's MAC_PACK_KEY secret
#   FA_DIR         the Forged Alliance install (default: the one ~/.faforever/fa_path.lua names)
#   FAF_DIR        the FAF client's data folder (default ~/.faforever); only read
# Writes <out>/pack (a folder to play from locally) and <out>/mac-replay-pack.tar.zst.enc.
set -eu
root=${0:A:h:h:h:h}
data=${1:?data folder}; out=${2:?out folder}
key=${PACK_KEY_FILE:?set PACK_KEY_FILE}
FAF_DIR=${FAF_DIR:-$HOME/.faforever}
FA_DIR=${FA_DIR:-$(sed -n "s/^fa_path *= *[\"']\(.*\)[\"'].*/\1/p" "$FAF_DIR/fa_path.lua")}
[[ -f $data/selected.json && -d $data/vault/maps ]] || { print -u2 "make_pack: $data is not a pick_replays.py folder"; exit 2 }
[[ ! -e $out/pack ]] || { print -u2 "make_pack: $out/pack exists; move it away first"; exit 2 }
mkdir -p "$out"

python3 -I "$root/tools/parity-data/make_pack.py" build --fa "$FA_DIR" --faf "$FAF_DIR" \
    --init "$FAF_DIR/bin/init_faf.lua" --vault "$data/vault" --content bp,lua,scm,sca,scmap,fx \
    --out "$out/pack"

# run_replay.sh starts init_bench.lua from the run folder and checks for init_faf.lua there.
cp "$out/pack/bin/init_ci.lua" "$out/pack/bin/init_faf.lua"
print -r -- "dofile(InitFileDir .. '/init_ci.lua')" > "$out/pack/bin/init_bench.lua"
mkdir -p "$out/pack/replays"
python3 -I - "$data" "$out/pack/replays" <<'EOF'
import json, shutil, sys
data, dest = sys.argv[1:3]
selected = json.load(open(f"{data}/selected.json"))
for row in selected:
    shutil.copy(f"{data}/replays/{row['id']}.scfareplay", dest)
shutil.copy(f"{data}/selected.json", dest)
print(f"{len(selected)} replays")
EOF

tar -C "$out/pack" -cf - . | zstd -T0 -12 -q | \
    openssl enc -aes-256-cbc -pbkdf2 -iter 200000 -salt -pass "file:$key" -out "$out/mac-replay-pack.tar.zst.enc"
print -r -- "make_pack: $out/mac-replay-pack.tar.zst.enc ($(du -h "$out/mac-replay-pack.tar.zst.enc" | cut -f1))"
