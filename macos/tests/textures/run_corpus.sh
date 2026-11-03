#!/bin/zsh
# Decodes every texture file the game can load with the POSIX ImageCodec (texture_corpus) and
# dumps samples to PNG: the FA install's gamedata/*.scd (from ../fa_path.lua next to the native
# run folder), FAF's gamedata archives, the FA maps and the vault's maps and mods.
#   macos/tests/textures/run_corpus.sh [dump dir]      (build.sh first)
# Exit status 0 when no file failed.
set -u
root=${0:A:h:h:h:h}
bin=$root/build/m2-textures-tests
dump=${1:-$root/build/m2-textures-tests/samples}
fa=$(sed -n 's/^fa_path = "\(.*\)"$/\1/p' /Users/scottharris/Gaming/.faf/faf-re-bin/fa_path.lua)
vault=$(sed -n 's/^custom_vault_path = "\(.*\)"$/\1/p' /Users/scottharris/Gaming/.faf/faf-re-bin/fa_path.lua)
sources=($fa/gamedata/*.scd /Users/scottharris/Gaming/.faf/faf-re-bin/gamedata/*(N) $fa/maps)
[[ -n $vault && -d $vault/maps ]] && sources+=($vault/maps)
[[ -n $vault && -d $vault/mods ]] && sources+=($vault/mods)
samples=(
  des_sanddark02_albedo des_rock01_normal                        # terrain strata (SCMP_016's)
  units/uel0101/uel0101_albedo uel0101_specteam                  # unit albedo, spec/team
  aeon/dialogs/menu-btns/close_btn_up                            # UI button
  cursors/attack-01.dds reclaim-disabled.tga                     # cursors (DDS, FAF's TGA)
  icon_land1_antishield_rest icons_strategic/1/bomber_antinavy   # strategic icons (A1R5G5B5, DXT3)
  faction_icon-lg/uef_med.png envcube_desert01a scmp_016.scmap   # 16-bit PNG, env cube, the replay's map
)
args=()
for s in $samples; do args+=(--sample $s); done
FAF_TEST_QUIET=1 $bin/texture_corpus --dump $dump $args $sources
