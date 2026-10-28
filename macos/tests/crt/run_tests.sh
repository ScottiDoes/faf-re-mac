#!/bin/zsh
# Builds and runs the crt package's tests in build/crt-tests (never touches build/macos,
# whose compile_commands.json and Wild Magic archive it only reads).
#
#   macos/tests/crt/run_tests.sh            build + run everything
#
# - crt_unit_test: faf_crtmath special values, equality with Apple libm off the NaN lanes,
#   bindings, MSVC rand, trace mode (spawns itself traced), cost with tracing off.
# - crt_selftest: faf_posixRunSelfTests against the x87 test double; crt_selftest_wrong must
#   fail. X87Math.cpp is compiled with -D_M_X64 for these only, to get its msvc8_x87_*
#   declarations before the core-sys guard lands.
# - kat_oracle.py --check: the self-test's known answers from exact arithmetic.
# - check_symbols.sh on the test binaries (must pass) and on a plain libm program (must fail);
#   check_fma.sh on the crt objects (must pass) and on an fmadd object (must fail).
set -euo pipefail
here=${0:A:h}
root=${here:h:h:h}
out=$root/build/crt-tests
mkdir -p $out
cc=(python3 -I $here/engine_cc.py)
crt=$root/src/platform/posix/crt
wm3=$root/build/macos/libWm3Foundation.a

print "== compile (engine flags)"
for f in CrtMath CrtMathTrace MsvcRand SelfTest; do
  $cc $crt/$f.cpp $out/$f.o
done
$cc $root/src/legacy/math/X87Math.cpp $out/X87Math.o -D_M_X64
$cc $here/crt_unit_test.cpp $out/crt_unit_test.o
$cc $here/selftest_main.cpp $out/selftest_main.o
clang++ -std=c++20 -O2 -arch arm64 -ffp-contract=off -c $here/x87_test_double.cpp -o $out/x87_test_double.o
clang++ -std=c++20 -O2 -arch arm64 -ffp-contract=off -DFAF_X87_TEST_WRONG -c $here/x87_test_double.cpp \
  -o $out/x87_test_double_wrong.o

print "== link"
crtobjs=($out/CrtMath.o $out/CrtMathTrace.o $out/MsvcRand.o)
clang++ -arch arm64 -o $out/crt_unit_test $out/crt_unit_test.o $crtobjs
for v in "" _wrong; do
  clang++ -arch arm64 -o $out/crt_selftest$v $out/selftest_main.o $out/SelfTest.o $out/X87Math.o $crtobjs \
    $out/x87_test_double$v.o $wm3 -framework CoreServices -framework ApplicationServices
done

rc=0
print "== crt_unit_test"
$out/crt_unit_test $out || rc=1
print "== crt_selftest"
$out/crt_selftest || rc=1
print "== crt_selftest_wrong (must report failures)"
$out/crt_selftest_wrong --expect-fail 2>$out/selftest_wrong.log || rc=1
print "   $(grep -c FAIL $out/selftest_wrong.log) FAIL lines (see $out/selftest_wrong.log)"
print "== kat_oracle.py --check"
python3 -I $here/kat_oracle.py --check | tail -1 || rc=1

print "== check_symbols.sh"
$here/check_symbols.sh $out/crt_unit_test || rc=1
$here/check_symbols.sh $out/crt_selftest || rc=1
cat > $out/plain_libm.c <<'C'
#include <math.h>
#include <stdlib.h>
int main(int argc, char** argv) { (void)argv; return (int)(sin((double)argc) + pow(argc, 0.5)) + rand(); }
C
clang -O1 -fno-builtin -o $out/plain_libm $out/plain_libm.c
if $here/check_symbols.sh $out/plain_libm >$out/plain_libm.log 2>&1; then
  print "FAIL check_symbols.sh accepted a binary that takes sin/pow/rand from libSystem"; rc=1
else
  print "   negative control rejected: $(grep -c '^  ' $out/plain_libm.log) symbols flagged"
fi

print "== check_fma.sh"
$here/check_fma.sh $crtobjs $out/SelfTest.o || rc=1
print 'double f(double a, double b, double c) { return a * b + c; }' > $out/fused.c
clang -O2 -arch arm64 -ffp-contract=fast -c $out/fused.c -o $out/fused.o
if $here/check_fma.sh $out/fused.o >$out/fused.log 2>&1; then
  print "FAIL check_fma.sh accepted an object with fmadd"; rc=1
else
  print "   negative control rejected: $(grep -c fmadd $out/fused.log) fmadd line(s)"
fi

print "== result: $([[ $rc == 0 ]] && print PASS || print FAIL)"
exit $rc
