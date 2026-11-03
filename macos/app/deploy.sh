#!/bin/zsh
# Installs a development build of the app into a folder of its own (DEST below), for a
# launcher that runs the game from there. One command from source to installed app:
#   1. builds faf_game and faf_replay (ninja -C build/macos)
#   2. checks the sim against a recorded replay (macos/run_replay.sh with REPLAY at
#      SPEED=50); any checksum mismatch stops here
#   3. packages the self-contained app (macos/app/make_app.sh)
#   4. installs it, unless a native game is running: the installed app moves to
#      previous/FAF Native.app and the new one takes its place
#   5. writes VERSION (the installed app's version line) and appends to deploy.log
#
# Usage: macos/app/deploy.sh             build, check, package, install
#        macos/app/deploy.sh rollback    swap the installed app and previous/FAF Native.app
# Environment:
#   DEST           the folder the app goes into (default build/deploy)
#   REPLAY         the replay for step 2 (required unless NOCHECK=1), with run_replay.sh's
#                  FAF_RUN_DIR holding its game version
#   NOCHECK=1      skip the replay check (step 2)
#   VERSION_NOTE   what this build is for, added to the version line
#   SIGN_IDENTITY  passed to make_app.sh (default ad hoc)
set -eu
root=${0:A:h:h:h}
DEST=${${DEST:-$root/build/deploy}:a}
app=$DEST/FAF\ Native.app
prev=$DEST/previous/FAF\ Native.app
fail() { print -u2 "deploy: $*"; exit 1 }
running() { pgrep -fl faf_game | grep -qF -- "$DEST/" }
note() { print -r -- "$(date '+%F %T') $*" >> "$DEST/deploy.log" }
# VERSION holds the installed app's version line (Info.plist FAFNativeVersion).
write_version() {
    /usr/libexec/PlistBuddy -c "Print :FAFNativeVersion" "$app/Contents/Info.plist" > "$DEST/VERSION" 2>/dev/null ||
        print "unknown (app without FAFNativeVersion)" > "$DEST/VERSION"
}

running && fail "a native game is running; install after it ends"
mkdir -p "$DEST"

if [[ ${1:-} == rollback ]]; then
    [[ -d $prev ]] || fail "no previous app at $prev"
    tmp=$DEST/.swap-$$
    mv "$app" "$tmp" && mv "$prev" "$app" && mv "$tmp" "$prev"
    write_version
    note "rollback: now $(<"$DEST/VERSION")"
    print "deploy: rolled back; installed: $(<"$DEST/VERSION")"
    exit 0
fi
[[ $# == 0 ]] || fail "usage: deploy.sh [rollback]"

cd "$root"
print "deploy: building"
ninja -C build/macos faf_game faf_replay > build/deploy-build.out 2>&1 ||
    { tail -20 build/deploy-build.out >&2; fail "the build failed (build/deploy-build.out)"; }
if [[ ${NOCHECK:-0} != 1 ]]; then
    [[ -n ${REPLAY:-} ]] || fail "set REPLAY to the replay to check against (or NOCHECK=1)"
    print "deploy: replay check"
    mkdir -p build/deploy-check
    SDL_AUDIO_DRIVER=dummy SPEED=50 OUT=$root/build/deploy-check macos/run_replay.sh > build/deploy-check.out 2>&1 ||
        { tail -5 build/deploy-check.out >&2; fail "the replay check failed (build/deploy-check.out)"; }
    grep -m1 '^FAF_REPLAY_RESULT' build/deploy-check.out
fi
print "deploy: packaging"
made=$(OUT=$root/build/app macos/app/make_app.sh) || fail "packaging failed"
new=${made%%$'\n'*}
[[ -d $new ]] || fail "make_app.sh did not produce an app"

running && fail "a native game started meanwhile; run deploy again after it ends"
# Copy next to the destination first (same volume), so the swap itself is two renames.
incoming=$DEST/.incoming-$$
rm -rf "$incoming"; mkdir -p "$incoming"
ditto "$new" "$incoming/FAF Native.app"
codesign --verify --deep --strict "$incoming/FAF Native.app" || fail "the copied app does not verify"
mkdir -p "$DEST/previous"
if [[ -d $app ]]; then rm -rf "$prev"; mv "$app" "$prev"; fi
mv "$incoming/FAF Native.app" "$app"
rmdir "$incoming"
write_version
note "deploy: $(<"$DEST/VERSION")"
print "deploy: installed $app"
print "deploy: version $(<"$DEST/VERSION")"
