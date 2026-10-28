#!/bin/zsh
# Generates the full reference vector sets (about 10^6 per kernel) into
# build/x87-kernels-tests/full. Usage: run_oracle.sh [scale] [workers per kernel]
set -e
here=${0:A:h}
out=${here:h:h:h}/build/x87-kernels-tests/full
mkdir -p $out
scale=${1:-1000000}
w=${2:-3}
python3 -I $here/oracle.py trig $scale $out/trig.txt $w &
python3 -I $here/oracle.py atan $(( scale * 112 / 100 )) $out/atan.txt $w &
python3 -I $here/oracle.py yl2x $scale $out/yl2x.txt $w &
python3 -I $here/oracle.py f2xm1 $scale $out/f2xm1.txt $w &
wait
wc -l $out/*.txt
