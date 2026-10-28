#!/bin/zsh
# Builds the audio package's standalone test tools into build/m4-audio-tests (FAudio and
# SDL3 from Homebrew; no engine code). Usage: macos/tests/audio/build.sh
set -eu
root=${0:A:h:h:h:h}
out=$root/build/m4-audio-tests
mkdir -p "$out"
clang++ -std=c++20 -O2 -Wall -Wextra -I/opt/homebrew/include \
  "$root/macos/tests/audio/fact_bank_sweep.cpp" \
  -L/opt/homebrew/lib -lFAudio -lSDL3 -Wl,-rpath,/opt/homebrew/lib \
  -o "$out/fact_bank_sweep"
print "built $out/fact_bank_sweep"
