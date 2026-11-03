#!/bin/zsh
# Packages the game as a self-contained macOS app, build/app/FAF Native.app: a setup and status
# window, and the game with the libraries it otherwise takes from Homebrew and the LunarG Vulkan
# SDK, so at run time it needs nothing outside the bundle but macOS and the game files.
#   Contents/MacOS/FAF Native             the setup and status window (macos/app/setup, SwiftUI)
#   Contents/Resources/faf-launch         the FAF client's launcher (macos/app/faf-launch)
#   Contents/Resources/background-art.jpg the simple window's backdrop (macos/app/dmg, as on the disk image)
#   Contents/Resources/licenses/          the bundled libraries' licences
#   Contents/Helpers/FAF Native Game.app  the game, an app of its own (so macOS and per-app tools
#                                         see it as one while it runs; Game-Info.plist):
#     Contents/MacOS/faf_game                            build/macos/faf_game
#     Contents/Frameworks/libSDL3.0.dylib                Homebrew sdl3: window, input, audio output
#     Contents/Frameworks/libFAudio.0.dylib              Homebrew faudio: XACT sound (FactXact.cpp)
#     Contents/Frameworks/libvulkan.1.dylib              Vulkan SDK: loader   } the game takes these
#     Contents/Frameworks/libMoltenVK.dylib              Vulkan SDK: driver   } before the SDK's
#     Contents/Frameworks/libdxcompiler.dylib            Vulkan SDK: DXC      } (VulkanLoader.cpp,
#     Contents/Resources/vulkan/icd.d/MoltenVK_icd.json  MoltenVK's manifest  }  DxcBridge.cpp)
#     Contents/Resources/fafengine-macos                 the engine's own Lua hooks (StartupHelpers.cpp)
# The game and FAudio reach SDL3 and FAudio through @rpath = the game app's Frameworks (Homebrew's
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
#   BUNDLE_ID      the app's bundle identifier (default local.faf-re-mac); the game app's is
#                  <BUNDLE_ID>.game
#   VERSION_NOTE   added to the version line (Info.plist FAFNativeVersion, deploy.sh's VERSION)
#   FAF_VULKAN_SDK the Vulkan SDK's macOS folder (default: the one the game was configured with,
#                  from its build folder's CMakeCache.txt)
#   MACOS_DEPLOYMENT_TARGET  the oldest macOS for the setup window (default 14.0; the game's own
#                  comes from how it was built)
# SDL3 and FAudio are taken from wherever the game links them: Homebrew's by path, or those
# macos/build_app.sh builds into macos/vendor/prefix, through the game's @rpath.
# Prints the app's path and its version line.
set -eu
root=${0:A:h:h:h}
EXE=${FAF_GAME_EXE:-$root/build/macos/faf_game}
OUT=${OUT:-$root/build/app}
OUT=${OUT:A}
SIGN_IDENTITY=${SIGN_IDENTITY:--}
BUNDLE_ID=${BUNDLE_ID:-local.faf-re-mac}
TARGET=${MACOS_DEPLOYMENT_TARGET:-14.0}
SDK=${FAF_VULKAN_SDK:-$(sed -n 's/^FAF_VULKAN_SDK:PATH=//p' "${EXE:h}/CMakeCache.txt" 2>/dev/null)}
SDK=${SDK:-$HOME/Gaming/.faf/VulkanSDK/1.4.363.0/macOS}
fail() { print -u2 "make_app: $*"; exit 1 }
# A Mach-O file's load commands and rpaths, one per line (paths may hold spaces).
loads() { otool -L "$1" | awk '/^\t/ { sub(/^\t/, ""); sub(/ \(compatibility version.*$/, ""); print }' }
rpaths() { otool -l "$1" | awk '/LC_RPATH/ {r = 1} r && $1 == "path" { sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; r = 0 }' }
# install_name_tool and codesign, without their expected notes (the old signature goes away and
# is replaced at the end); errors still show and stop the script.
nametool() { install_name_tool "$@" 2>&1 | { grep -v 'will invalidate the code signature' >&2 || true }; return ${pipestatus[1]} }
sign() { codesign "$@" 2>&1 | { grep -v 'replacing existing signature' >&2 || true }; return ${pipestatus[1]} }
[[ -x $EXE ]] || fail "no game at $EXE (ninja -C build/macos faf_game)"
for f in libvulkan.1.dylib libMoltenVK.dylib libdxcompiler.dylib; do
    [[ -f $SDK/lib/$f ]] || fail "no $f in the Vulkan SDK at $SDK/lib"
done

# The two libraries the game links, as its load commands name them, and the files those are.
resolve() {
    local name=$1 r
    if [[ $name == @rpath/* ]]; then
        for r in ${(f)"$(rpaths "$EXE")"}; do
            r=${r/@executable_path/${EXE:h}}; r=${r/@loader_path/${EXE:h}}
            [[ -f $r/${name#@rpath/} ]] && { print -r -- "$r/${name#@rpath/}"; return }
        done
    fi
    print -r -- "$name"
}
sdl_name=$(loads "$EXE" | grep -m1 -E 'libSDL3[.0-9]*\.dylib$') || true
faudio_name=$(loads "$EXE" | grep -m1 -E 'libFAudio[.0-9]*\.dylib$') || true
sdl=$(resolve "$sdl_name") faudio=$(resolve "$faudio_name")
[[ -f $sdl && -f $faudio ]] || fail "SDL3 or FAudio not found from the game's load commands ('$sdl_name', '$faudio_name')"

# Version line: commit, uncommitted source changes, date.
cd "$root"
commit=$(git rev-parse --short=8 HEAD)
build=$(git rev-list --count HEAD)
dirty=$(git status --porcelain -- src macos CMakeLists.txt | awk '{print $NF}' | tr '\n' ' ')
version="$commit${dirty:+ + uncommitted: ${dirty% }}, built $(date '+%F %R')${VERSION_NOTE:+: $VERSION_NOTE}"
minos=$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/ {b = 1} b && $1 == "minos" {print $2; exit}')

app=$OUT/FAF\ Native.app
game=$app/Contents/Helpers/FAF\ Native\ Game.app
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/licenses" \
    "$game/Contents/MacOS" "$game/Contents/Frameworks" "$game/Contents/Resources/vulkan/icd.d"
"$root/macos/app/icon/make_icon.sh" "$app/Contents/Resources/FAFNative.icns"
for plist in Info Game-Info; do
    [[ $plist == Info ]] && dest=$app/Contents/Info.plist || dest=$game/Contents/Info.plist
    sed -e "s|@BUNDLE_ID@|$BUNDLE_ID|" -e "s|@BUILD@|$build|" -e "s|@MINOS@|${minos:-14.0}|" \
        -e "s|@VERSION@|${version//&/\\&}|" "$root/macos/app/$plist.plist" > "$dest"
    plutil -lint -s "$dest" || fail "$plist.plist does not parse"
done

# The setup window: Swift, compiled straight into the app (no Xcode project).
xcrun --sdk macosx swiftc -parse-as-library -O -target arm64-apple-macos$TARGET \
    -o "$app/Contents/MacOS/FAF Native" "$root"/macos/app/setup/*.swift || fail "the setup app does not compile"

fw=$game/Contents/Frameworks
cp "$EXE" "$game/Contents/MacOS/faf_game"
cp -L "$sdl" "$fw/libSDL3.0.dylib"
cp -L "$faudio" "$fw/libFAudio.0.dylib"
# The SDK's libraries are universal; the app keeps their arm64 half (the game is arm64 only).
for f in libvulkan.1.dylib libMoltenVK.dylib libdxcompiler.dylib; do
    if [[ $(lipo -archs "$SDK/lib/$f") == arm64 ]]; then cp -L "$SDK/lib/$f" "$fw/$f"; else lipo -thin arm64 "$SDK/lib/$f" -output "$fw/$f"; fi
done
chmod u+w "$game/Contents/MacOS/faf_game" "$fw"/*.dylib   # Homebrew installs its libraries read-only

# Load commands: SDL3 and FAudio through @rpath, which is the game app's Frameworks folder.
exe=$game/Contents/MacOS/faf_game
nametool -id @rpath/libSDL3.0.dylib "$fw/libSDL3.0.dylib"
nametool -id @rpath/libFAudio.0.dylib \
    -change "$(loads "$fw/libFAudio.0.dylib" | grep -m1 -E 'libSDL3[.0-9]*\.dylib$')" @rpath/libSDL3.0.dylib \
    "$fw/libFAudio.0.dylib"
nametool -change "$sdl_name" @rpath/libSDL3.0.dylib -change "$faudio_name" @rpath/libFAudio.0.dylib "$exe"
for f in "$exe" "$fw/libSDL3.0.dylib" "$fw/libFAudio.0.dylib"; do
    for r in ${(f)"$(rpaths "$f")"}; do nametool -delete_rpath "$r" "$f"; done
done
nametool -add_rpath @executable_path/../Frameworks "$exe"
nametool -add_rpath @loader_path "$fw/libFAudio.0.dylib"

# MoltenVK's manifest; a relative library_path is taken from the manifest's own folder.
sed 's|"library_path": *"[^"]*"|"library_path": "../../../Frameworks/libMoltenVK.dylib"|' \
    "$SDK/share/vulkan/icd.d/MoltenVK_icd.json" > "$game/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json"
grep -q '"../../../Frameworks/libMoltenVK.dylib"' "$game/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" ||
    fail "could not point MoltenVK_icd.json at the bundle"

# The engine's own Lua hooks: the game looks for them in its bundle (never in this source tree).
ditto "$root/macos/fafengine-macos" "$game/Contents/Resources/fafengine-macos"

cp "$root/macos/app/faf-launch" "$app/Contents/Resources/faf-launch"
chmod 755 "$app/Contents/Resources/faf-launch"
cp "$root/macos/app/dmg/background-art.jpg" "$app/Contents/Resources/background-art.jpg"

lic=$app/Contents/Resources/licenses
# licence <destination> <candidate...>: the first candidate that exists (Homebrew's layout, or
# macos/vendor/prefix/licenses from build_app.sh).
licence() {
    local dest=$1 c; shift
    for c; do [[ -f $c ]] && { cp "$c" "$dest"; return }; done
    fail "no licence file for ${dest:t:r} (looked for: $*)"
}
licence "$lic/SDL3.txt" "${sdl:h:h}/LICENSE.txt" "${sdl:h:h}/licenses/SDL3.txt"
licence "$lic/FAudio.txt" "${faudio:h:h}/LICENSE" "${faudio:h:h}/licenses/FAudio.txt"
cp "${SDK:h}/Licenses/LICENSE.txt" "$lic/VulkanSDK-loader-MoltenVK-DXC.txt"
cp "$root/dependencies/ffmpeg/COPYING.LGPLv2.1" "$lic/FFmpeg-LGPLv2.1.txt"

# Nothing may load from outside the bundle: every non-system load command and rpath must be
# relative (@rpath, @executable_path, @loader_path).
for f in "$exe" "$fw"/*.dylib "$app/Contents/MacOS/FAF Native"; do
    outside=$(loads "$f" | grep -vE '^(@rpath|@executable_path|@loader_path)/|^/usr/lib/|^/System/Library/' || true)
    outside+=$(rpaths "$f" | grep -vE '^@|^/usr/lib/' || true)
    [[ -z $outside ]] || fail "${f:t} still loads from outside the bundle: $outside"
done

# Sign inside out: the libraries, the game app, then the app (each bundle signature covers its
# executable, Info.plist and Resources; the outer one also seals the signed game app).
opts=(--force --sign "$SIGN_IDENTITY")
[[ $SIGN_IDENTITY == - ]] || opts+=(--options runtime --timestamp)
for f in "$fw"/*.dylib; do sign "${opts[@]}" "$f"; done
sign "${opts[@]}" --identifier "$BUNDLE_ID.game" "$game"
sign "${opts[@]}" --identifier "$BUNDLE_ID" "$app"
codesign --verify --deep --strict "$app" || fail "the signature does not verify"

print "$app"
print "$version"
