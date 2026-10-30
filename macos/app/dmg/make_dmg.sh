#!/bin/zsh
# Packs FAF Native.app into a compressed disk image to share. Opening it shows a window with the
# app, a shortcut to Applications and a background asking to drag one onto the other; the disk
# itself takes the app's icon.
#   background.swift  draws the background (background-art.jpg, made with an image generator,
#                     under the title, the panels, the arrow and the instructions), 1x and 2x
#   layout.swift      writes the .DS_Store that gives Finder the window's size, the background and
#                     where each icon sits, so nothing has to script Finder to arrange it
#
# Usage: macos/app/dmg/make_dmg.sh "FAF Native.app" FAF-Native.dmg
# Environment:
#   SIGN_IDENTITY  codesign identity for the disk image (default "-": left unsigned, since an ad hoc
#                  signature on a disk image proves nothing). Notarize the image, not just the app.
# Prints the disk image's path.
set -eu
here=${0:A:h}
app=${1:?Usage: make_dmg.sh "FAF Native.app" output.dmg}
output=${2:?Usage: make_dmg.sh "FAF Native.app" output.dmg}
fail() { print -u2 "make_dmg.sh: $*"; exit 1 }
[[ -f $app/Contents/Resources/FAFNative.icns ]] || fail "$app is not FAF Native.app"
app=${app:A} output=${output:A}

# The window's content in points, and the centre of each icon in it; background.swift draws the
# panels and the arrow to match. The picture runs 20 points lower than the window shows, so no
# blank strip appears if a title bar is shorter than the 28 points layout.swift allows for.
volume="FAF Native" width=640 height=420 icon_size=128 app_x=170 applications_x=470 icons_y=190

work=$(mktemp -d /tmp/faf-native-dmg.XXXXXX)
mounted=
cleanup() { [[ -z $mounted ]] || hdiutil detach -quiet -force "$mounted" 2>/dev/null; rm -rf "$work" }
trap cleanup EXIT

stage=$work/stage
mkdir -p "$stage/.background"
ditto "$app" "$stage/${app:t}"
ln -s /Applications "$stage/Applications"
cp "$app/Contents/Resources/FAFNative.icns" "$stage/.VolumeIcon.icns"
swift "$here/background.swift" "$here/background-art.jpg" "$stage/.background/background.tiff" \
    $width $((height + 20)) $app_x $applications_x $icons_y || fail "the background did not render"

# A writable image first, a little larger than its contents, so the .DS_Store fits once the files
# have the IDs that its alias and bookmark to the background record.
hdiutil create -quiet -srcfolder "$stage" -volname "$volume" -fs HFS+ -format UDRW \
    -size $(( $(du -sm "$stage" | cut -f1) + 16 ))m "$work/rw.dmg" || fail "hdiutil could not create the image"
# Mounted out of sight (-nobrowse), so Finder and Spotlight leave it alone. macOS 26 and later call
# that option deprecated in favour of diskutil image, which macOS 14 lacks; the note is dropped.
mounted=$(hdiutil attach -nobrowse -noautoopen -noverify -mountrandom "$work" "$work/rw.dmg" 2> >(grep -v 'is deprecated' >&2) |
    awk -F'\t' '/Apple_HFS/ { sub(/[ \t]+$/, "", $NF); print $NF }')
[[ -d $mounted ]] || fail "the image did not mount"
xattr -wx com.apple.FinderInfo 0000000000000000040000000000000000000000000000000000000000000000 "$mounted"  # custom icon
swift "$here/layout.swift" "$mounted" ${width}x$height $icon_size \
    "${app:t}=$app_x,$icons_y" "Applications=$applications_x,$icons_y" || fail "the window layout was not written"
rm -rf "$mounted/.fseventsd"
hdiutil detach -quiet "$mounted" || fail "the image did not unmount"
mounted=

mkdir -p "${output:h}"
rm -f "$output"
hdiutil convert -quiet "$work/rw.dmg" -format ULFO -o "$output" || fail "hdiutil could not compress the image"
if [[ ${SIGN_IDENTITY:--} != - ]]; then
    codesign --sign "$SIGN_IDENTITY" --timestamp "$output" || fail "the disk image could not be signed"
fi
hdiutil verify -quiet "$output" || fail "the disk image does not verify"
print -r -- "$output"
