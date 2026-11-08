#!/bin/zsh
# Builds FAF Native.app (the native Apple silicon Forged Alliance engine, its setup window and the
# FAF client's launcher) from a clone of this repository, in one command:
#   git clone <this repository> faf-re && faf-re/macos/build_app.sh
# The app lands in build/dist: FAF Native.app, and FAF-Native-<commit>.dmg and .zip to pass on.
# macos/BUILDING.md says more (what the app needs on a player's Mac, newer versions, signing).
#
# This Mac needs Apple silicon, macOS 14 or later, and Xcode or Apple's Command Line Tools
# (xcode-select --install). Everything else is fetched once, checked against the SHA-256 pinned
# in macos/vendor/versions.sh, and kept in macos/vendor (ignored by git):
#   1. CMake and Ninja, unless this Mac has them
#   2. the engine's source dependencies: the git submodules (Wild Magic 3.8, zlib 1.2.3, Vulkan
#      headers, FFmpeg) and Boost 1.34.1, with the FAF patches from dependencies/patches
#   3. SDL3 and FAudio, built from source, and the LunarG Vulkan SDK (Vulkan loader, MoltenVK, DXC)
#   4. the game (build/release), then the app (macos/app/make_app.sh)
# A newer SDL3, FAudio or Vulkan SDK: put its file into macos/vendor/drop-in (README there).
# Each step is skipped when its result is already there, so running it again is quick.
#
# Usage: macos/build_app.sh
# Environment:
#   ACCEPT_VULKAN_SDK_LICENSE=1  accept the Vulkan SDK's licences without being asked
#   FAF_VULKAN_SDK=<dir>         use a Vulkan SDK installed elsewhere (its macOS folder)
#   SIGN_IDENTITY, BUNDLE_ID     passed to make_app.sh (default: ad hoc, local.faf-re-mac)
#   OUT                          folder for the app, the disk image and the zip (default build/dist)
#   JOBS                         parallel compile jobs (default: all cores)
#   FAF_BUILD_OWN_TOOLS=1        fetch CMake and Ninja even if this Mac has them (to test that path)
#   TARGETS                      what to build in build/release (default faf_game)
#   NO_APP=1                     stop after the build: no app, disk image or zip (CI builds
#                                faf_replay this way, TARGETS=faf_replay)
set -eu
autoload -U is-at-least
root=${0:A:h:h}
vendor=$root/macos/vendor
source "$vendor/versions.sh"
downloads=$vendor/downloads dropin=$vendor/drop-in prefix=$vendor/prefix src=$vendor/src
tools=$vendor/tools stamps=$vendor/stamps logs=$vendor/logs
OUT=${OUT:-$root/build/dist}
OUT=${OUT:A}
jobs=${JOBS:-$(sysctl -n hw.ncpu)}
mkdir -p "$downloads" "$prefix" "$src" "$tools" "$stamps" "$logs"

step() { print -P "\n%B==> $*%b" }
note() { print -r -- "    $*" }
fail() { print -u2 -r -- ""; print -u2 -r -- "build_app: $*"; exit 1 }
sha256() { shasum -a 256 "$1" | cut -d' ' -f1 }

# fetch <url> <sha256> [file name]: the file in macos/vendor/downloads, downloaded if missing and
# checked either way. Prints its path.
fetch() {
    local url=$1 sum=$2 name=${3:-${1:t}}
    local file=$downloads/$name
    if [[ -f $file && $(sha256 "$file") == $sum ]]; then print -r -- "$file"; return; fi
    note "downloading $name" >&2
    local show=(-sS); [[ -t 2 ]] && show=(--progress-bar)   # a progress bar only on a terminal
    curl -fL --retry 3 "${show[@]}" -o "$file.part" "$url" >&2 || fail "could not download $url"
    local have=$(sha256 "$file.part")
    [[ $have == $sum ]] || { rm -f "$file.part"; fail "$name has SHA-256 $have, not $sum (macos/vendor/versions.sh)"; }
    mv "$file.part" "$file"
    print -r -- "$file"
}

# newest_dropin <glob>: the newest-named matching file in macos/vendor/drop-in, if any.
newest_dropin() {
    local found=("$dropin"/${~1}(NnOn))
    (( $#found )) && print -r -- "$found[1]"
    return 0
}

# run <log name> <command...>: output to macos/vendor/logs/<name>.log; its tail on failure.
run() {
    local name=$1; shift
    "$@" > "$logs/$name.log" 2>&1 || { tail -25 "$logs/$name.log" >&2; fail "$name failed (macos/vendor/logs/$name.log)"; }
}

# ---------------------------------------------------------------------------------------------
step "Checking this Mac"
[[ $(uname -m) == arm64 ]] || fail "FAF Native needs a Mac with Apple silicon."
os=$(sw_vers -productVersion)
is-at-least "$MACOS_DEPLOYMENT_TARGET" "$os" || fail "macOS $MACOS_DEPLOYMENT_TARGET or later is needed; this Mac has $os."
xcrun --find clang >/dev/null 2>&1 && xcrun --find swiftc >/dev/null 2>&1 ||
    fail "No compilers: install Xcode from the App Store, or Apple's Command Line Tools with  xcode-select --install"
command -v git >/dev/null || fail "git is missing (it comes with Xcode and the Command Line Tools)."
git -C "$root" rev-parse --git-dir >/dev/null 2>&1 ||
    fail "Run this from a git clone of the repository: the engine's dependencies are git submodules."
note "macOS $os, $(xcrun clang --version | head -1)"

# ---------------------------------------------------------------------------------------------
step "Build tools"
cmake_ok() { [[ -x $1 ]] && is-at-least "$CMAKE_MIN_VERSION" "$("$1" --version | awk 'NR == 1 {print $3}')" }
CMAKE=$(command -v cmake || true) NINJA=$(command -v ninja || true)
[[ ${FAF_BUILD_OWN_TOOLS:-0} == 1 ]] && CMAKE= NINJA=
if [[ -z $CMAKE ]] || ! cmake_ok "$CMAKE"; then
    CMAKE=$tools/cmake-$CMAKE_VERSION-macos-universal/CMake.app/Contents/bin/cmake
    [[ -x $CMAKE ]] || tar -xzf "$(fetch "$CMAKE_URL" "$CMAKE_SHA256")" -C "$tools"
fi
if [[ -z $NINJA ]]; then
    NINJA=$tools/ninja
    [[ -x $NINJA ]] || { unzip -oq "$(fetch "$NINJA_URL" "$NINJA_SHA256")" -d "$tools" && chmod +x "$NINJA"; }
fi
cmake_ok "$CMAKE" && [[ -x $NINJA ]] || fail "no usable CMake ($CMAKE) or Ninja ($NINJA)"
export PATH="${NINJA:h}:${CMAKE:h}:$PATH"
note "CMake $("$CMAKE" --version | awk 'NR == 1 {print $3}') ($CMAKE)"
note "Ninja $("$NINJA" --version) ($NINJA)"

# ---------------------------------------------------------------------------------------------
step "Engine source dependencies"
# patch_tree <stamp name> <folder> <patch...>: applies the FAF patches once, in order; a stamp in
# macos/vendor/stamps records which (by checksum), so a changed patch asks for a clean folder.
patch_tree() {
    local name=$1 dir=$2; shift 2
    local want="" p
    for p; do want+="$p $(sha256 "$root/dependencies/patches/$p")"$'\n'; done
    local stamp=$stamps/patches-$name
    if [[ -f $stamp ]]; then
        [[ $(<"$stamp")$'\n' == "$want" ]] && { note "${dir#$root/}: patched"; return; }
        fail "${dir#$root/} carries other patches than dependencies/patches has now: reset it (git -C ${dir#$root/} checkout . or delete it) and remove $stamp"
    fi
    for p; do
        # GIT_CEILING_DIRECTORIES: a downloaded tree (boost) is patched as itself, not as part of this repository.
        GIT_CEILING_DIRECTORIES=${dir:h} git -C "$dir" -c core.autocrlf=false apply --unsafe-paths --whitespace=nowarn \
            "$root/dependencies/patches/$p" || fail "$p does not apply to ${dir#$root/}: reset that folder and run again"
        note "${dir#$root/}: applied $p"
    done
    print -rn -- "$want" > "$stamp"
}
cd "$root"
for sub in WildMagic3p8 zlib-1.2.3 vulkan ffmpeg; do
    if [[ ! -e dependencies/$sub/.git ]]; then
        note "fetching the dependencies/$sub submodule"
        run "submodule-$sub" git -c core.autocrlf=false submodule update --init -- "dependencies/$sub"
    fi
done
patch_tree wildmagic dependencies/WildMagic3p8 wildmagic3p8_faf_required.patch wildmagic3p8_faf_macos.patch
patch_tree zlib dependencies/zlib-1.2.3 zlib_1_2_3_faf_required.patch
boost=$root/dependencies/boost_1_34_1
if [[ ! -f $boost/boost/version.hpp ]]; then
    archive=$(fetch "$BOOST_URL" "$BOOST_SHA256")
    rm -rf "$src/boost-extract"; mkdir -p "$src/boost-extract"
    tar -xjf "$archive" -C "$src/boost-extract"
    mv "$src/boost-extract/boost_1_34_1" "$boost"; rmdir "$src/boost-extract"
    rm -f "$stamps/patches-boost"
fi
patch_tree boost "$boost" boost_1_34_1_faf_required.patch boost_1_34_1_faf_macos.patch

# ---------------------------------------------------------------------------------------------
step "SDL3 and FAudio"
# build_library <name> <version> <archive> <licence file> <cmake options...>: into macos/vendor/prefix,
# for MACOS_DEPLOYMENT_TARGET, unless that version is already there.
build_library() {
    local name=$1 version=$2 archive=$3 licence=$4; shift 4
    local stamp=$stamps/$name-$version-macos$MACOS_DEPLOYMENT_TARGET
    if [[ -f $stamp ]]; then note "$name $version: built"; return; fi
    local dir=$src/$name-$version
    rm -rf "$dir"; mkdir -p "$dir"
    tar -xzf "$archive" -C "$dir" --strip-components 1
    note "$name $version: building"
    run "$name-configure" "$CMAKE" -S "$dir" -B "$dir/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" "$@"
    run "$name-build" "$CMAKE" --build "$dir/build" -j "$jobs"
    run "$name-install" "$CMAKE" --install "$dir/build"
    mkdir -p "$prefix/licenses"
    cp "$dir/$licence" "$prefix/licenses/$name.txt"
    rm -f "$stamps/$name"-*(N)
    : > "$stamp"
}
# A drop-in package wins over the pinned one (its version from its name).
if sdl_archive=$(newest_dropin "SDL3-*.tar.gz") && [[ -n $sdl_archive ]]; then
    sdl_version=${${sdl_archive:t}#SDL3-}; sdl_version=${sdl_version%.tar.gz}
    note "SDL3 $sdl_version from the drop-in folder (not checked against a pinned checksum)"
else
    sdl_archive=$(fetch "$SDL3_URL" "$SDL3_SHA256") sdl_version=$SDL3_VERSION
fi
build_library SDL3 "$sdl_version" "$sdl_archive" LICENSE.txt \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
if faudio_archive=$(newest_dropin "FAudio-*.tar.gz") && [[ -n $faudio_archive ]]; then
    faudio_version=${${faudio_archive:t}#FAudio-}; faudio_version=${faudio_version%.tar.gz}
    note "FAudio $faudio_version from the drop-in folder (not checked against a pinned checksum)"
else
    faudio_archive=$(fetch "$FAUDIO_URL" "$FAUDIO_SHA256" "$FAUDIO_FILE") faudio_version=$FAUDIO_VERSION
fi
# A new SDL3 means FAudio is linked again.
[[ -f $stamps/faudio-sdl3 && $(<"$stamps/faudio-sdl3") == "$sdl_version" ]] || rm -f "$stamps/FAudio"-*(N)
build_library FAudio "$faudio_version" "$faudio_archive" LICENSE -DBUILD_SDL3=ON -DBUILD_SHARED_LIBS=ON
print -rn -- "$sdl_version" > "$stamps/faudio-sdl3"

# ---------------------------------------------------------------------------------------------
step "Vulkan SDK"
if [[ -n ${FAF_VULKAN_SDK:-} ]]; then
    sdk=${FAF_VULKAN_SDK:A}
    note "the one in $sdk (FAF_VULKAN_SDK)"
else
    if sdk_zip=$(newest_dropin "vulkansdk-macos-*.zip") && [[ -n $sdk_zip ]]; then
        sdk_version=${${sdk_zip:t}#vulkansdk-macos-}; sdk_version=${sdk_version%.zip}
        note "$sdk_version from the drop-in folder (not checked against a pinned checksum)"
    else
        sdk_zip= sdk_version=$VULKAN_SDK_VERSION
    fi
    sdk=$vendor/VulkanSDK/$sdk_version/macOS
    if [[ ! -f $sdk/lib/libvulkan.1.dylib ]]; then
        [[ -n $sdk_zip ]] || sdk_zip=$(fetch "$VULKAN_SDK_URL" "$VULKAN_SDK_SHA256")
        if [[ ${ACCEPT_VULKAN_SDK_LICENSE:-0} != 1 ]]; then
            print "    The LunarG Vulkan SDK $sdk_version is installed with its own installer, which needs you to"
            print "    accept its licences (https://vulkan.lunarg.com/license/). It goes into macos/vendor only."
            [[ -t 0 ]] || fail "not asked (no terminal): set ACCEPT_VULKAN_SDK_LICENSE=1 to accept them"
            read -r "answer?    Accept the Vulkan SDK licences? [y/N] "
            [[ $answer == [yY]* ]] || fail "the Vulkan SDK's licences were not accepted"
        fi
        unpack=$src/vulkansdk-$sdk_version
        rm -rf "$unpack"; mkdir -p "$unpack"
        unzip -q "$sdk_zip" -d "$unpack"
        installer=("$unpack"/*.app(N))
        (( $#installer )) || fail "$sdk_zip has no installer app"
        note "installing it into ${${sdk:h}#$root/}"
        run vulkan-sdk-install "$installer[1]/Contents/MacOS/${installer[1]:t:r}" --root "${sdk:h}" \
            --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core
        rm -rf "$unpack"
    else
        note "$sdk_version: installed"
    fi
fi
[[ -f $sdk/lib/libvulkan.1.dylib && -f $sdk/lib/libMoltenVK.dylib && -f $sdk/lib/libdxcompiler.dylib && -f $sdk/include/dxc/dxcapi.h ]] ||
    fail "the Vulkan SDK at $sdk lacks the loader, MoltenVK or DXC"

# ---------------------------------------------------------------------------------------------
step "The game"
game_build=$root/build/release
note "configuring build/release (macOS $MACOS_DEPLOYMENT_TARGET and later)"
run game-configure "$CMAKE" -S "$root/macos" -B "$game_build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" \
    -DCMAKE_PREFIX_PATH="$prefix" -DSDL3_DIR="$prefix/lib/cmake/SDL3" -DFAudio_DIR="$prefix/lib/cmake/FAudio" \
    -DFAF_VULKAN_SDK="$sdk"
note "compiling (the first time takes a while; progress every 10%)"
"$CMAKE" --build "$game_build" --target ${=TARGETS:-faf_game} -j "$jobs" 2>&1 | tee "$logs/game-build.log" |
    awk '/^\[[0-9]+\/[0-9]+\]/ { split(substr($1, 2), n, /[\/\]]/); p = int(n[1] * 10 / n[2]) * 10
                                 if (p > shown) { shown = p; printf "    %d%%\n", p; fflush() } }'
(( pipestatus[1] == 0 )) || { grep -m 20 -E 'error|FAILED' "$logs/game-build.log" >&2; fail "the game did not build (macos/vendor/logs/game-build.log)"; }

[[ ${NO_APP:-0} != 1 ]] || { print "Done: $game_build (${TARGETS:-faf_game}); NO_APP=1, so no app."; exit 0 }

# ---------------------------------------------------------------------------------------------
step "The app"
made=$(FAF_GAME_EXE=$game_build/faf_game FAF_VULKAN_SDK=$sdk OUT=$OUT MACOS_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET \
    "$root/macos/app/make_app.sh") || fail "packaging failed"
app=${made%%$'\n'*}
zip=$OUT/FAF-Native-$(git -C "$root" rev-parse --short=8 HEAD).zip
rm -f "$zip"
ditto -c -k --keepParent "$app" "$zip"
note "$app"
note "$zip ($(du -h "$zip" | cut -f1 | tr -d ' '))"
dmg=$("$root/macos/app/dmg/make_dmg.sh" "$app" "${zip%.zip}.dmg") || fail "the disk image was not made"
note "$dmg ($(du -h "$dmg" | cut -f1 | tr -d ' '))"
note "version: ${made#*$'\n'}"
print
print "Done. Copy FAF Native.app into Applications (or open the disk image and drag it there) and"
print "open it: its window walks through the rest (the Forged Alliance files and the FAF client)."
print "A copy sent to another Mac is opened the first time from System Settings > Privacy & Security >"
print "Open Anyway, unless the app was signed with a Developer ID and notarized (macos/BUILDING.md)."
