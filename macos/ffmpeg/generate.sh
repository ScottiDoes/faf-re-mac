#!/bin/sh
# Regenerates macos/ffmpeg/arm64-darwin/: what FFmpeg 7.1.3's configure writes for
# faf-re's configuration on Apple Silicon macOS (config.h, config_components.h,
# libavutil/avconfig.h, libavutil/ffversion.h, the component lists) and sources.cmake (the
# per-library source lists and flags, evaluated from FFmpeg's Makefiles by the submodule's
# faf/gen_sources.py). macos/ffmpeg.cmake compiles the libraries from that; nothing is
# configured at build time and nothing is written into dependencies/ffmpeg.
#
#   sh macos/ffmpeg/generate.sh
#
# Needs Apple clang (Xcode or the Command Line Tools) and python3. The configuration is
# the one dependencies/ffmpeg/faf/generate.sh uses on Windows (docs/FAF_SFD_COMPATIBILITY.md)
# plus the macOS toolchain options and:
#   --disable-autodetect --enable-pthreads   no system libraries or frameworks
#                                            (VideoToolbox, AudioToolbox, iconv, zlib,
#                                            ...); FFmpeg's threads are pthreads
#   NEON assembly stays on                   (movie decoding is user-side only; no
#                                            determinism requirement)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/dependencies/ffmpeg"
OUT="$HERE/arm64-darwin"
PYTHON=${PYTHON:-python3}

FAF_CONFIGURE_OPTIONS="--disable-x86asm --disable-everything --disable-programs --disable-doc --disable-network
--enable-static --disable-shared
--enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample
--enable-decoder=mpeg1video,mpeg2video,adpcm_adx,mp1,mp2,mp3
--enable-parser=mpegvideo,mpegaudio,adx
--enable-demuxer=mpegps,mpegvideo,adx,mp3
--enable-protocol=file"
FAF_MACOS_OPTIONS="--cc=clang --cxx=clang++ --target-os=darwin --arch=aarch64 --disable-debug
--disable-autodetect --enable-pthreads"
FAF_LIBRARIES="avutil swresample swscale avcodec avformat"

[ -f "$SRC/configure" ] || { echo "generate.sh: no FFmpeg source at $SRC (git submodule)" >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/ffmpeg-faf-arm64.XXXXXX")
case "$work" in *[[:blank:]]*) echo "generate.sh: TMPDIR contains whitespace: $work" >&2; exit 1 ;; esac
# configure refuses out-of-tree builds from a source path with whitespace
# (/Volumes/SSD Programs/...): give it a link without one.
ln -s "$SRC" "$work/src"
mkdir "$work/build"

echo "generate.sh: configuring in $work/build"
# shellcheck disable=SC2086
(cd "$work/build" && "$work/src/configure" $FAF_MACOS_OPTIONS $FAF_CONFIGURE_OPTIONS) \
  > "$work/configure.out" 2>&1 || {
  tail -40 "$work/build/ffbuild/config.log" >&2; echo "generate.sh: configure failed, see $work" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/libavutil"
# The configure line recorded in config.h names the temporary link; keep it stable.
sed "s|$work/src|dependencies/ffmpeg|g" "$work/build/config.h" > "$OUT/config.h"
cp "$work/build/config_components.h" "$OUT/"
cp "$work/build/libavutil/avconfig.h" "$OUT/libavutil/"
(cd "$work/build" && sh "$work/src/ffbuild/version.sh" "$work/src" libavutil/ffversion.h)
cp "$work/build/libavutil/ffversion.h" "$OUT/libavutil/"
for lib in $FAF_LIBRARIES; do
  for list in "$work/build/lib$lib"/*_list.c; do
    [ -f "$list" ] || continue
    mkdir -p "$OUT/lib$lib"
    cp "$list" "$OUT/lib$lib/"
  done
done
"$PYTHON" -I "$SRC/faf/gen_sources.py" "$work/src" "$work/build/ffbuild/config.mak" "$OUT/sources.cmake" $FAF_LIBRARIES
# gen_sources.py writes the source root it was given; the link is temporary.
sed -i '' "s|$work/src|\${FFMPEG_FAF_SOURCE_DIR}|g" "$OUT/sources.cmake"
if [ -z "${KEEP:-}" ]; then rm -rf "$work"; else echo "generate.sh: kept $work"; fi
echo "generate.sh: wrote $OUT"
