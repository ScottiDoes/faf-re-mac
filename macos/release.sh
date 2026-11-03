#!/bin/zsh
# Builds a release of FAF Native that opens on any Mac: build_app.sh signs the app with a Developer
# ID, Apple notarizes it, the ticket is stapled to it, the disk image is made from the stapled app,
# then notarized and stapled too, and Gatekeeper checks both. With a tag it also publishes a GitHub
# release of this commit with the disk image as FAF-Native.dmg, so this link always gets the newest:
#   https://github.com/<owner>/<repo>/releases/latest/download/FAF-Native.dmg
#
# Without a Developer ID, AD_HOC=1 makes the same release signed ad hoc and not notarized: other
# Macs then block its first launch until it's allowed in System Settings (Open Anyway), and the
# release is titled "(not notarized)", with notes saying how to open it.
#
# Usage: SIGN_IDENTITY=<identity> NOTARY_PROFILE=<profile> macos/release.sh [tag]
#        AD_HOC=1 macos/release.sh [tag]
# Environment:
#   SIGN_IDENTITY   a "Developer ID Application" identity (its SHA-1 if two certificates share a name)
#   NOTARY_PROFILE  a notarytool keychain profile (xcrun notarytool store-credentials <profile> ...)
#   AD_HOC=1        no Developer ID: skip signing with one and notarizing
#   GH_REPO         owner/name to publish to (default: the GitHub remote of this branch)
#   and build_app.sh's own (OUT, JOBS, ...)
# Run it from a clean checkout of a commit that is on GitHub: the app reports that commit.
set -eu
root=${0:A:h:h}
tag=${1:-}
adhoc=${AD_HOC:-0}
OUT=${${OUT:-$root/build/dist}:A}
export OUT

step() { print -P "\n%B==> $*%b" }
note() { print -r -- "    $*" }
fail() { print -u2 -r -- ""; print -u2 -r -- "release: $*"; exit 1 }

# notarize <file>: submits it and waits; fails with Apple's log unless it is accepted.
notarize() {
    local result id state
    result=$(xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait --output-format json) || true
    id=$(print -r -- "$result" | plutil -extract id raw -o - - 2>/dev/null) || fail "notarytool could not submit ${1:t}"
    state=$(print -r -- "$result" | plutil -extract status raw -o - -)
    note "${1:t}: $state ($id)"
    [[ $state == Accepted ]] || { xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" >&2; fail "Apple did not accept ${1:t}"; }
}

step "Checking"
if [[ $adhoc == 1 ]]; then
    unset SIGN_IDENTITY NOTARY_PROFILE
    note "AD_HOC=1: signed ad hoc, not notarized (other Macs block the first launch until it's allowed)"
else
    [[ -n ${SIGN_IDENTITY:-} && -n ${NOTARY_PROFILE:-} ]] ||
        fail "set SIGN_IDENTITY (a Developer ID Application identity) and NOTARY_PROFILE (a notarytool profile), or AD_HOC=1 to release without a Developer ID"
    security find-identity -v -p codesigning | grep -F -- "$SIGN_IDENTITY" | grep -q "Developer ID Application" ||
        fail "no valid Developer ID Application identity matches $SIGN_IDENTITY in the keychain (or AD_HOC=1 to release without one)"
fi
git -C "$root" diff --quiet HEAD -- || fail "this checkout has changes: release a clean commit"
commit=$(git -C "$root" rev-parse HEAD)
short=$(git -C "$root" rev-parse --short=8 HEAD)   # as build_app.sh names the zip and disk image
note "commit $short"
if [[ -n $tag ]]; then
    command -v gh >/dev/null || fail "publishing needs the GitHub CLI (gh)"
    if [[ -z ${GH_REPO:-} ]]; then
        remote=$(git -C "$root" config "branch.$(git -C "$root" branch --show-current).remote" || true)
        url=$(git -C "$root" remote get-url "${remote:-origin}" 2>/dev/null || true)
        [[ $url =~ 'github\.com[:/]([^/]+/[^/]+)$' ]] && GH_REPO=${match[1]%.git}
    fi
    [[ -n ${GH_REPO:-} ]] || fail "set GH_REPO=<owner>/<name>: this branch's remote isn't on GitHub"
    export GH_REPO
    gh api "repos/$GH_REPO/commits/$commit" --silent 2>/dev/null || fail "commit $short is not on $GH_REPO yet: push it first"
    gh release view "$tag" >/dev/null 2>&1 && fail "$GH_REPO already has a release $tag"
    note "will publish $tag to $GH_REPO"
fi

"$root/macos/build_app.sh"
app=$OUT/"FAF Native.app"
zip=$OUT/FAF-Native-$short.zip
dmg=$OUT/FAF-Native-$short.dmg
codesign --verify --deep --strict "$app" || fail "the app's signature does not verify"

if [[ $adhoc != 1 ]]; then
    step "Notarizing the app"
    notarize "$zip"
    xcrun stapler staple -q "$app"

    step "The disk image, from the stapled app"
    rm -f "$zip" "$dmg"
    ditto -c -k --keepParent "$app" "$zip"
    "$root/macos/app/dmg/make_dmg.sh" "$app" "$dmg" >/dev/null
    notarize "$dmg"
    xcrun stapler staple -q "$dmg"

    step "Checking with Gatekeeper"
    spctl -a -t exec -vv "$app" 2>&1 | grep -q "source=Notarized Developer ID" || fail "Gatekeeper does not accept the app"
    spctl -a -t install -vv "$dmg" 2>&1 | grep -q "source=Notarized Developer ID" || fail "Gatekeeper does not accept the disk image"
    note "app and disk image: Notarized Developer ID"
fi
cp "$dmg" "$OUT/FAF-Native.dmg"
sum=$(shasum -a 256 "$OUT/FAF-Native.dmg" | cut -d' ' -f1)
note "$OUT/FAF-Native.dmg ($(du -h "$dmg" | cut -f1 | tr -d ' '), SHA-256 $sum)"

if [[ -z $tag ]]; then
    print "\nDone (not published: give a tag to publish a GitHub release)."
    exit 0
fi

step "Publishing $tag"
title="FAF Native ${tag#faf-native-}"
if [[ $adhoc == 1 ]]; then
    title+=" (not notarized)"
    notes="FAF Native $short, for Macs with Apple silicon and macOS 14 or later. **Not notarized:** it isn't signed with a Developer ID, so macOS blocks it the first time it's opened.

Open **FAF-Native.dmg** and drag FAF Native onto Applications. Then either:
- open it, choose **Done** when macOS refuses, and allow it under System Settings → Privacy & Security → **Open Anyway**; or
- in Terminal, run \`xattr -dr com.apple.quarantine \"/Applications/FAF Native.app\"\`.

Its window then walks through the rest (your Forged Alliance files and the FAF client)."
else
    notes="FAF Native $short, for Macs with Apple silicon and macOS 14 or later. Signed with a Developer ID and notarized by Apple.

Open **FAF-Native.dmg** and drag FAF Native onto Applications, then open it from there: its window walks through the rest (your Forged Alliance files and the FAF client)."
fi
notes+="

SHA-256 of FAF-Native.dmg: \`$sum\`"
gh release create "$tag" "$OUT/FAF-Native.dmg" --target "$commit" --title "$title" --notes "$notes"
print "\nDone: https://github.com/$GH_REPO/releases/tag/$tag"
