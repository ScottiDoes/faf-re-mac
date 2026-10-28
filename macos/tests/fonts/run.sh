#!/bin/zsh
# Builds and runs the fonts package's tests (notes: macos/porting/fonts.md):
#   fonts_test   -> build/m2-fonts-tests/out/{metrics.tsv,sheet.png,sheet_zoom.png}, PASS/FAIL
#   reference.py -> the metrics recomputed from the font files' tables, PASS/FAIL
# Usage: macos/tests/fonts/run.sh [fonts dir]   (default: the FA install from ~/Gaming/faf-re-bin/fa_path.lua)
set -e
here=${0:A:h}
root=${here:h:h:h}
fonts=$1
if [[ -z $fonts ]]; then
  fa=$(sed -n 's/^fa_path *= *"\(.*\)"/\1/p' ~/Gaming/faf-re-bin/fa_path.lua)
  fonts="$fa/fonts"
fi
out=$root/build/m2-fonts-tests/out
mkdir -p $out
$here/build.sh
result=0
$root/build/m2-fonts-tests/fonts_test --fonts "$fonts" --out $out || result=1
python3 -I $here/reference.py "$fonts" $out/metrics.tsv || result=1
exit $result
