#!/bin/zsh
# Draws FAF Native's setup window into PNGs in build/setup-snapshot without showing it
# (snapshot_setup.swift), from a copy of build/app/FAF Native.app (make_app.sh first), in three
# situations, light and dark:
#   mac-*      this Mac as it is
#   fresh-*    a new player: an empty home folder (no FAF client, no Forged Alliance)
#   copied-*   a home with a small stand-in Forged Alliance install and FAF client data, after the
#              window's "Copy into FAF Native" (the copy itself runs, into that home)
#   applied-*  the same home after "Apply for Me" (writes its .faforever/client.prefs, with a backup)
#   client-*   with CLIENT_INSTALL=1 only: a home kept in build/client-install-test where the window
#              downloads and installs the FAF client (about 260 MB, once; later runs reuse it)
# Usage: [CLIENT_INSTALL=1] macos/tests/app/snapshot_setup.sh
set -eu
root=${0:A:h:h:h:h}
out=$root/build/setup-snapshot
rm -rf "$out"; mkdir -p "$out"
ditto "$root/build/app/FAF Native.app" "$out/FAF Native.app"
sources=("$root"/macos/app/setup/*.swift)
sources=(${sources:#*/FAFNativeApp.swift})   # the app's entry point; snapshot_setup.swift has its own
xcrun --sdk macosx swiftc -parse-as-library -target arm64-apple-macos14.0 \
    -o "$out/FAF Native.app/Contents/MacOS/snapshot" "${sources[@]}" "${0:A:h}/snapshot_setup.swift"
snap=$out/FAF\ Native.app/Contents/MacOS/snapshot
"$snap" "$out/mac"

mkdir -p "$out/fresh-home"
FAF_NATIVE_TEST_HOME=$out/fresh-home "$snap" "$out/fresh"

# A stand-in install: the files the window checks for, a few bytes each.
h=$out/copied-home fa=$out/copied-home/Games/Forged\ Alliance
mkdir -p "$fa/bin" "$fa/gamedata" "$fa/sounds" "$fa/movies" "$fa/maps/test_map" "$fa/fonts" "$h/.faforever/bin" "$h/.faforever/gamedata"
for f in bin/SupremeCommander.exe gamedata/{units,textures,meshes,env,effects}.scd sounds/test.xwb movies/test.sfd \
    maps/test_map/test.scmap fonts/test.ttf; do print "stand-in $f" > "$fa/$f"; done
print 'init' > "$h/.faforever/bin/init.lua"
print 'GameType = "faf"\nGameVersion = "3840"' > "$h/.faforever/fa_path.lua"
cat > "$h/.faforever/client.prefs" <<JSON
{"forgedAlliance": {"installationPath": "$fa", "executableDecorator": "\"%s\""}}
JSON
FAF_NATIVE_TEST_HOME=$h SNAPSHOT_COPY=1 "$snap" "$out/copied"
print "copied files: $(find "$h/Library/Application Support/FAF Native/Forged Alliance" -type f | wc -l | tr -d ' ') (stand-in: $(find "$fa" -type f | wc -l | tr -d ' '))"
cat "$h/Library/Application Support/FAF Native/config.json"
FAF_NATIVE_TEST_HOME=$h SNAPSHOT_APPLY=1 "$snap" "$out/applied"
print "client.prefs now:"; cat "$h/.faforever/client.prefs"; print; ls "$h/.faforever"

if [[ ${CLIENT_INSTALL:-0} == 1 ]]; then
    ch=$root/build/client-install-test/home
    mkdir -p "$ch"
    FAF_NATIVE_TEST_HOME=$ch SNAPSHOT_INSTALL_CLIENT=1 "$snap" "$out/client"
fi
