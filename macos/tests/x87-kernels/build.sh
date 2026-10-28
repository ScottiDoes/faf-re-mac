#!/bin/zsh
# Builds the x87 kernel test driver into build/x87-kernels-tests (never build/macos).
#   macos/tests/x87-kernels/build.sh            release build (engine codegen flags)
#   macos/tests/x87-kernels/build.sh ubsan      + -fsanitize=undefined,address, -O1
#   macos/tests/x87-kernels/build.sh contract   with -ffp-contract=fast (results must not change)
set -e
root=${0:A:h:h:h:h}
out=$root/build/x87-kernels-tests
mkdir -p $out
math=$root/src/platform/posix/math
flags=(-std=c++20 -arch arm64 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math -fwrapv -fno-strict-aliasing
       -Wall -Wextra -DFAF_X87_KERNELS_TESTING -I$math)
name=kernel_test
case ${1:-} in
  ubsan) flags=(${flags:#-O3}); flags+=(-O1 -g -fsanitize=undefined,address -fno-sanitize-recover=undefined); name=kernel_test_ubsan ;;
  contract) flags=(${flags:#-ffp-contract=off}); flags+=(-ffp-contract=fast); name=kernel_test_contract ;;
esac
clang++ $flags $root/macos/tests/x87-kernels/kernel_test.cpp $math/X87Kernels.cpp -o $out/$name
echo "built $out/$name"
