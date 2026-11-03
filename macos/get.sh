#!/bin/zsh
# Gets, builds and installs FAF Native.app in one command, from Terminal on a Mac with Apple silicon:
#   zsh -c "$(curl -fsSL https://raw.githubusercontent.com/ScottiDoes/faf-re-mac/macos-native/macos/get.sh)"
# It clones this repository into ~/faf-re-mac (or updates the clone an earlier run made), builds
# the app with macos/build_app.sh, and copies it into /Applications. Run it again to update.
# (Not "curl ... | zsh": build_app.sh reads the answer to the Vulkan SDK's licence question from
# the terminal.)
#
# Environment:
#   FAF_NATIVE_SOURCE=<dir>        where the source goes (default ~/faf-re-mac)
#   FAF_NATIVE_APPLICATIONS=<dir>  where the app goes (default /Applications); "none" only builds it
#   FAF_NATIVE_REPO, FAF_NATIVE_BRANCH  what to build (default this repository's macos-native branch)
#   and build_app.sh's own: ACCEPT_VULKAN_SDK_LICENSE, SIGN_IDENTITY, BUNDLE_ID, OUT, JOBS, ...
set -eu
repo=${FAF_NATIVE_REPO:-https://github.com/ScottiDoes/faf-re-mac.git}
branch=${FAF_NATIVE_BRANCH:-macos-native}
src=${${FAF_NATIVE_SOURCE:-$HOME/faf-re-mac}:A}
apps=${FAF_NATIVE_APPLICATIONS:-/Applications}

step() { print -P "\n%B==> $*%b" }
note() { print -r -- "    $*" }
fail() { print -u2 -r -- ""; print -u2 -r -- "get.sh: $*"; exit 1 }

[[ $(uname -m) == arm64 ]] || fail "FAF Native needs a Mac with Apple silicon."
xcode-select -p >/dev/null 2>&1 ||
    fail "No developer tools: install Apple's Command Line Tools with  xcode-select --install  (or Xcode), then run this again."

if [[ -d $src/.git ]]; then
    step "Updating $src"
    git -C "$src" pull --ff-only ||
        fail "$src could not be updated (changes of your own, or another branch?). Fix that, or set FAF_NATIVE_SOURCE to a new folder."
elif [[ -e $src ]]; then
    fail "$src is there but is not a git clone: move it away, or set FAF_NATIVE_SOURCE to another folder."
else
    step "Getting the source into $src"
    git clone -b "$branch" "$repo" "$src"
fi

"$src/macos/build_app.sh"
app=${${OUT:-$src/build/dist}:A}/"FAF Native.app"
[[ -d $app ]] || fail "the build did not leave $app"

if [[ $apps == none ]]; then
    step "Built (not installed)"
    note "$app"
    exit 0
fi

step "Installing into $apps"
pgrep -xq "FAF Native" && fail "FAF Native is open: quit it, then run this again (the build is kept)."
pgrep -xq faf_game && fail "a game is running: run this again after it ends (the build is kept)."
mkdir -p "$apps"
[[ -w $apps ]] || fail "$apps can't be written to by you: set FAF_NATIVE_APPLICATIONS=~/Applications, or copy $app there yourself."
dest=$apps/"FAF Native.app"
# The new copy goes in beside the old one first, so the swap is two renames.
work=$(mktemp -d "$apps/.faf-native-install.XXXXXX")
ditto "$app" "$work/FAF Native.app" || { rm -rf "$work"; fail "could not copy the app into $apps"; }
[[ -d $dest ]] && mv "$dest" "$work/previous.app"
mv "$work/FAF Native.app" "$dest"
rm -rf "$work"
note "$dest"
print
print "Done. Open FAF Native from $apps: its window walks through the rest (the Forged Alliance"
print "files and the FAF client). To update later, run the same command again."
