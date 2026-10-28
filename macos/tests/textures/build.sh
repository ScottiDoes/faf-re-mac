#!/bin/zsh
# Builds the textures package's tests into build/m2-textures-tests (never build/macos):
#   codec_tests      unit tests of the POSIX ImageCodec (BC blocks, DDS, resampling, ImageIO, options)
#   texture_corpus   decodes every texture file of the real game data (see run_corpus.sh)
#   contact_sheet    lays decoded images out on one PNG to look at
# Every codec source is compiled with the engine's own command (engine_cc.py), plus the
# compat layer's WinCompat*.cpp (the decode cache and EncodeToFile use its Win32 file calls)
# and test_support.cpp in place of the engine's logging / shader-cache directory.
set -e
root=${0:A:h:h:h:h}
here=${0:A:h}
out=$root/build/m2-textures-tests
mkdir -p $out/obj
cc() { python3 -I $here/engine_cc.py "$1" "$out/obj/${${1:t}%.cpp}.o" ${@:2} }

sources=(
  $root/src/platform/posix/gal/ImageBc.cpp
  $root/src/platform/posix/gal/ImageDds.cpp
  $root/src/platform/posix/gal/ImageDecode.cpp
  $root/src/platform/posix/gal/ImageFiles.cpp
  $root/src/platform/posix/gal/ImagePixels.cpp
  $root/src/platform/posix/gal/ImageResample.cpp
  $root/src/platform/posix/stubs/render/gal/ImageCodecPosix.cpp
  $root/src/gpg/gal/backends/dxgi/DxgiFormats.cpp
  $root/src/platform/posix/WinCompatErrors.cpp
  $root/src/platform/posix/WinCompatFiles.cpp
  $root/src/platform/posix/WinCompatMisc.cpp
  $root/src/platform/posix/WinCompatStrings.cpp
  $root/src/platform/posix/WinCompatSync.cpp
  $root/src/platform/posix/WinCompatThreads.cpp
  $root/src/platform/posix/WinCompatTime.cpp
  $root/src/platform/posix/WinCompatMemory.cpp
  $here/test_support.cpp
  $here/codec_tests.cpp
  $here/texture_corpus.cpp
  $here/contact_sheet.cpp
)
pids=()
for s in $sources; do cc $s & pids+=($!); done
fail=0
for p in $pids; do wait $p || fail=1; done
(( fail == 0 )) || { print -u2 "compile failed"; exit 1; }

common=($out/obj/{ImageBc,ImageDds,ImageDecode,ImageFiles,ImagePixels,ImageResample,ImageCodecPosix,DxgiFormats,test_support}.o
        $out/obj/WinCompat{Errors,Files,Misc,Strings,Sync,Threads,Time,Memory}.o)
libs=(-framework CoreFoundation -framework CoreGraphics -framework ImageIO -framework CoreServices -lz)
clang++ -arch arm64 $common $out/obj/codec_tests.o $libs -o $out/codec_tests
clang++ -arch arm64 $common $out/obj/texture_corpus.o $libs -o $out/texture_corpus
clang++ -arch arm64 $common $out/obj/contact_sheet.o $libs -o $out/contact_sheet
echo "built $out/codec_tests $out/texture_corpus $out/contact_sheet"
