#!/bin/zsh
# Builds the fonts package's tests into build/m2-fonts-tests (no cmake; the engine's own
# compile command, from build/macos/compile_commands.json, for every file):
#   fonts_test: the Gdi area's fonts and text (WinCompatGdi.cpp, WinCompatGdiFont.cpp) and
#               the engine's own CD3DFont.cpp (through its POSIX stand-in) on top of them,
#               with test doubles for the texture upload and the quad batcher.
# Usage: macos/tests/fonts/build.sh   (then see run.sh)
set -e
root=${0:A:h:h:h:h}
out=$root/build/m2-fonts-tests
mkdir -p $out/obj
export PATH=/opt/homebrew/bin:$PATH

# The engine's flags (borrowed from WinCompatMisc.cpp's command), minus its source and output.
flags=$(python3 -I - "$root/build/macos/compile_commands.json" <<'PY'
import json, shlex, sys
entries = json.load(open(sys.argv[1]))
ref = next(x for x in entries if x["file"].endswith("platform/posix/WinCompatMisc.cpp"))
args = shlex.split(ref["command"])
out = []
skip = False
for i, a in enumerate(args[1:]):
    if skip:
        skip = False
        continue
    if a in ("-o", "-c"):
        skip = True
        continue
    out.append(a)
print(shlex.join(out))
PY
)

sources=(
  src/platform/posix/WinCompatGdi.cpp
  src/platform/posix/WinCompatGdiFont.cpp
  src/platform/posix/WinCompatStrings.cpp
  src/platform/posix/stubs/render/moho/CD3DFontPosix.cpp
  src/moho/render/textures/DXTCodec.cpp
  src/moho/misc/CountedObject.cpp
  macos/tests/fonts/fonts_test.cpp
  macos/tests/fonts/test_doubles.cpp
)
objects=()
cd $root/build/macos
for s in $sources; do
  o=$out/obj/${s:t:r}.o
  objects+=$o
  if [[ ! -f $o || $root/$s -nt $o || $root/src/platform/posix/compat/Gdi.h -nt $o || $root/src/platform/posix/WinCompatGdiFont.h -nt $o ]]; then
    print "compile $s"
    eval "c++ $flags -I'$root' -c '$root/$s' -o '$o'"
  fi
done
c++ -arch arm64 -o $out/fonts_test $objects -framework CoreText -framework CoreGraphics -framework CoreFoundation -lz
print "built $out/fonts_test"
