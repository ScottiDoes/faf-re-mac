#!/bin/zsh
# Render the FAF Native icon (FAFNative-art.png in the macOS icon shape) at every app-icon resolution.
set -eu
root=${0:A:h}
output=${1:?Usage: make_icon.sh output.icns}
work=$(mktemp -d /tmp/faf-native-icon.XXXXXX)
trap 'rm -rf "$work"' EXIT
iconset=$work/FAFNative.iconset
mkdir -p "$iconset" "${output:h}"
master=$iconset/icon_512x512@2x.png
swift "$root/render_icon.swift" "$root/FAFNative-art.png" "$master"

for size in 16 32 128 256 512; do
    sips -z $size $size "$master" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    pixels=$((size * 2))
    if (( pixels != 1024 )); then
        sips -z $pixels $pixels "$master" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    fi
done
iconutil -c icns -o "$output" "$iconset"
