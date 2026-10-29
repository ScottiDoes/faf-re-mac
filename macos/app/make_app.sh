#!/bin/zsh
# Packages the game as a self-contained macOS app, build/app/FAF Native.app: the game plus the
# libraries it otherwise takes from Homebrew and the LunarG Vulkan SDK, so at run time it needs
# nothing outside the bundle but macOS and the game files.
#   Contents/MacOS/faf_game                            the game (build/macos/faf_game)
#   Contents/Frameworks/libSDL3.0.dylib                Homebrew sdl3: window, input, audio output
#   Contents/Frameworks/libFAudio.0.dylib              Homebrew faudio: XACT sound (FactXact.cpp)
#   Contents/Frameworks/libvulkan.1.dylib              Vulkan SDK: loader   } the game takes these
#   Contents/Frameworks/libMoltenVK.dylib              Vulkan SDK: driver   } before the SDK's
#   Contents/Frameworks/libdxcompiler.dylib            Vulkan SDK: DXC      } (VulkanLoader.cpp,
#   Contents/Resources/vulkan/icd.d/MoltenVK_icd.json  MoltenVK's manifest  }  DxcBridge.cpp)
#   Contents/Resources/licenses/                       the bundled libraries' licences
# The game and FAudio reach SDL3 and FAudio through @rpath = Contents/Frameworks (Homebrew's
# rpath is replaced); the check at the end fails the build if any load command still points
# outside the bundle. FFmpeg is linked in statically (LGPL v2.1+, macos/ffmpeg.cmake). The game
# writes faf_diag.log and crash reports to ~/Library/Logs/FAF Native, not into the bundle
# (CrashHooks.cpp), so the signature stays intact.
#
# Usage: macos/app/make_app.sh
# Environment:
#   FAF_GAME_EXE   game to package (default build/macos/faf_game)
#   OUT            folder for the app (default build/app)
#   SIGN_IDENTITY  codesign identity (default "-", ad hoc). Any other identity also gets the
#                  hardened runtime and a secure timestamp, as notarization requires.
#   VERSION_NOTE   added to the version line (Info.plist FAFNativeVersion, deploy.sh's VERSION)
# Prints the app's path and its version line.
set -eu
root=${0:A:h:h:h}
EXE=${FAF_GAME_EXE:-$root/build/macos/faf_game}
OUT=${OUT:-$root/build/app}
OUT=${OUT:A}
SIGN_IDENTITY=${SIGN_IDENTITY:--}
SDK=$HOME/Gaming/VulkanSDK/1.4.363.0/macOS   # macos/CMakeLists.txt FAF_VULKAN_SDK
fail() { print -u2 "make_app: $*"; exit 1 }
# install_name_tool and codesign, without their expected notes (the old signature goes away and
# is replaced at the end); errors still show and stop the script.
nametool() { install_name_tool "$@" 2>&1 | { grep -v 'will invalidate the code signature' >&2 || true }; return ${pipestatus[1]} }
sign() { codesign "$@" 2>&1 | { grep -v 'replacing existing signature' >&2 || true }; return ${pipestatus[1]} }
[[ -x $EXE ]] || fail "no game at $EXE (ninja -C build/macos faf_game)"
for f in libvulkan.1.dylib libMoltenVK.dylib libdxcompiler.dylib; do
    [[ -f $SDK/lib/$f ]] || fail "no $f in the Vulkan SDK at $SDK/lib"
done

# The two libraries the game links by path (Homebrew's), as its load commands name them.
sdl=$(otool -L "$EXE" | awk '/libSDL3[.0-9]*\.dylib/ {print $1; exit}')
faudio=$(otool -L "$EXE" | awk '/libFAudio[.0-9]*\.dylib/ {print $1; exit}')
[[ -f $sdl && -f $faudio ]] || fail "the game does not link SDL3 and FAudio by path (otool -L: '$sdl', '$faudio')"

# Version line: commit, uncommitted source changes, date.
cd "$root"
commit=$(git rev-parse --short=8 HEAD)
build=$(git rev-list --count HEAD)
dirty=$(git status --porcelain -- src macos CMakeLists.txt | awk '{print $NF}' | tr '\n' ' ')
version="$commit${dirty:+ + uncommitted: ${dirty% }}, built $(date '+%F %R')${VERSION_NOTE:+: $VERSION_NOTE}"
minos=$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/ {b = 1} b && $1 == "minos" {print $2; exit}')

app=$OUT/FAF\ Native.app
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/Resources/vulkan/icd.d" "$app/Contents/Resources/licenses"
sed -e "s|@BUILD@|$build|" -e "s|@MINOS@|${minos:-14.0}|" -e "s|@VERSION@|${version//&/\\&}|" \
    "$root/macos/app/Info.plist" > "$app/Contents/Info.plist"
plutil -lint -s "$app/Contents/Info.plist" || fail "Info.plist does not parse"

fw=$app/Contents/Frameworks
cp "$EXE" "$app/Contents/MacOS/faf_game"
cp -L "$sdl" "$fw/libSDL3.0.dylib"
cp -L "$faudio" "$fw/libFAudio.0.dylib"
# The SDK's libraries are universal; the app keeps their arm64 half (the game is arm64 only).
for f in libvulkan.1.dylib libMoltenVK.dylib libdxcompiler.dylib; do
    if [[ $(lipo -archs "$SDK/lib/$f") == arm64 ]]; then cp -L "$SDK/lib/$f" "$fw/$f"; else lipo -thin arm64 "$SDK/lib/$f" -output "$fw/$f"; fi
done
chmod u+w "$app/Contents/MacOS/faf_game" "$fw"/*.dylib   # Homebrew installs its libraries read-only

# Load commands: SDL3 and FAudio through @rpath, which is the bundle's Frameworks folder.
exe=$app/Contents/MacOS/faf_game
nametool -id @rpath/libSDL3.0.dylib "$fw/libSDL3.0.dylib"
nametool -id @rpath/libFAudio.0.dylib \
    -change "$(otool -L "$fw/libFAudio.0.dylib" | awk '/libSDL3[.0-9]*\.dylib/ {print $1; exit}')" @rpath/libSDL3.0.dylib \
    "$fw/libFAudio.0.dylib"
nametool -change "$sdl" @rpath/libSDL3.0.dylib -change "$faudio" @rpath/libFAudio.0.dylib "$exe"
for r in $(otool -l "$exe" | awk '/LC_RPATH/ {r = 1} r && $1 == "path" {print $2; r = 0}'); do
    nametool -delete_rpath "$r" "$exe"
done
nametool -add_rpath @executable_path/../Frameworks "$exe"
nametool -add_rpath @loader_path "$fw/libFAudio.0.dylib"

# MoltenVK's manifest; a relative library_path is taken from the manifest's own folder.
sed 's|"library_path": *"[^"]*"|"library_path": "../../../Frameworks/libMoltenVK.dylib"|' \
    "$SDK/share/vulkan/icd.d/MoltenVK_icd.json" > "$app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json"
grep -q '"../../../Frameworks/libMoltenVK.dylib"' "$app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" ||
    fail "could not point MoltenVK_icd.json at the bundle"

lic=$app/Contents/Resources/licenses
cp "${sdl:h:h}/LICENSE.txt" "$lic/SDL3.txt"
cp "${faudio:h:h}/LICENSE" "$lic/FAudio.txt"
cp "${SDK:h}/Licenses/LICENSE.txt" "$lic/VulkanSDK-loader-MoltenVK-DXC.txt"
cp "$root/dependencies/ffmpeg/COPYING.LGPLv2.1" "$lic/FFmpeg-LGPLv2.1.txt"

# Nothing may load from outside the bundle: every non-system load command and rpath must be
# relative (@rpath, @executable_path, @loader_path).
for f in "$exe" "$fw"/*.dylib; do
    outside=$(otool -L "$f" | awk '/^\t/ {print $1}' | grep -vE '^(@rpath|@executable_path|@loader_path)/|^/usr/lib/|^/System/Library/' || true)
    outside+=$(otool -l "$f" | awk '/LC_RPATH/ {r = 1} r && $1 == "path" {print $2; r = 0}' | grep -v '^@' || true)
    [[ -z $outside ]] || fail "${f:t} still loads from outside the bundle: $outside"
done

# Sign the libraries, then the bundle (which signs faf_game and seals Info.plist and Resources).
opts=(--force --sign "$SIGN_IDENTITY")
[[ $SIGN_IDENTITY == - ]] || opts+=(--options runtime --timestamp)
for f in "$fw"/*.dylib; do sign "${opts[@]}" "$f"; done
sign "${opts[@]}" --identifier local.faf-re-mac.game "$app"
codesign --verify --deep --strict "$app" || fail "the signature does not verify"

print "$app"
print "$version"
