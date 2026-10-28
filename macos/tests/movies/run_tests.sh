#!/bin/zsh
# The movies package's headless tests (milestone 3; notes: macos/porting/movies.md). Builds
# macos/tests/movies into build/m4-movies-tests (FFmpeg from macos/ffmpeg, the SFD player's
# TUs and the DirectSound stand-in as the engine compiles them), then runs:
#   1. sfd-parser-tests       upstream's SFD header parser tests (20000 fuzz cases)
#   2. dsound_test            the DirectSound stand-in on SDL3, captured by SDL's disk driver
#   3. sfd-inspect            upstream's player checks: --decode, --play, --play-sound, --reject
#   4. player_soak            main_menu.sfd for 40 s as the menu plays it; upstream's
#                             IndependentSfdBackend.cpp (cb1b9fd7) for comparison when git is
#                             there; e3_demo_cut.sfd with sound, looping
#   5. movie_census           every .sfd in the FA install and in ~/.faforever/movies through
#                             the engine's decoder, with frame PNGs and the decoded e3 audio
# Everything lands in build/m4-movies-tests (game-derived output stays under build/).
# Environment: AUDIO=device runs the sound tests on the real output device (default: SDL's
# disk/dummy drivers); SKIP_CENSUS=1 skips step 5.
set -u
root=${0:A:h:h:h:h}
T=$root/build/m4-movies-tests
W=(python3 -I "$root/macos/tests/bringup/watchdog.py")
FA="/Users/scottharris/Gaming/Windows Steam.app/Contents/SharedSupport/prefix/drive_c/Program Files (x86)/Steam/steamapps/common/Supreme Commander Forged Alliance/movies"
FAF=$HOME/.faforever/movies
export PATH=/opt/homebrew/bin:/Users/scottharris/Gaming/rosettax87/.venv/bin:$PATH
failed=()
step() { print -r -- ""; print -r -- "=== $*" }
check() { local rc=$1; shift; if [[ $rc == 0 ]]; then print -r -- "--- PASS $*"; else print -r -- "--- FAIL $* (exit $rc)"; failed+=("$*"); fi }

mkdir -p "$T/audio" "$T/frames" "$T/census"
other=()
if git -C "$root" show cb1b9fd7:src/moho/movie/sfd/IndependentSfdBackend.cpp > "$T/IndependentSfdBackend.upstream.cpp" 2>/dev/null; then
    other=(-DFAF_MOVIES_OTHER_BACKEND="$T/IndependentSfdBackend.upstream.cpp")
fi
cmake -S "$root/macos/tests/movies" -B "$T" -G Ninja -DCMAKE_BUILD_TYPE=Release "${other[@]}" > "$T/cmake.log" 2>&1 \
    && ninja -C "$T" > "$T/ninja.log" 2>&1 || { print -u2 "build failed: $T/cmake.log, $T/ninja.log"; exit 2 }

step "1. parser tests"
"$T/sfd-parser-tests"; check $? sfd-parser-tests

step "2. DirectSound stand-in"
if [[ ${AUDIO:-} == device ]]; then
    "${W[@]}" 30 "$T/dsound_test"; check $? dsound_test
else
    SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/audio/dsound_test.raw "${W[@]}" 30 "$T/dsound_test" 2>&1 | grep -v "disk i/o"
    check ${pipestatus[1]} dsound_test
    # The device opens in its default format (the test logs it); the disk driver's is 44.1 kHz S16.
    python3 -I "$root/macos/tests/movies/analyse_audio.py" "$T/audio/dsound_test.raw" --rate 44100 --window 0.25 --quiet
fi

step "3. upstream player checks (tools/sfd-inspect)"
"${W[@]}" 120 "$T/sfd-inspect" --decode "$FA/Abasi.sfd" "$FA/e3_demo_cut.sfd" 2>&1 | grep -v swscaler; check ${pipestatus[1]} "sfd-inspect --decode"
SDL_AUDIO_DRIVER=dummy "${W[@]}" 120 "$T/sfd-inspect" --play "$FA/Abasi.sfd" "$FA/e3_demo_cut.sfd" "$FA/main_menu.sfd" 2>&1 | grep -v swscaler
check ${pipestatus[1]} "sfd-inspect --play"
if [[ ${AUDIO:-} == device ]]; then
    "${W[@]}" 120 "$T/sfd-inspect" --play-sound "$FA/e3_demo_cut.sfd" 2>&1 | grep -v swscaler
else
    SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/audio/play_sound_e3.raw "${W[@]}" 120 "$T/sfd-inspect" --play-sound "$FA/e3_demo_cut.sfd" 2>&1 | grep -v "swscaler\|disk i/o\|Writing to"
fi
check ${pipestatus[1]} "sfd-inspect --play-sound"
"${W[@]}" 60 "$T/sfd-inspect" --reject "$FA/FMV_loading02.sfd" 2>&1 | grep -v swscaler; check ${pipestatus[1]} "sfd-inspect --reject"

step "4. player soak"
SDL_AUDIO_DRIVER=dummy "${W[@]}" 90 "$T/player_soak" --seconds 40 --loop "$FA/main_menu.sfd" 2>&1 | grep -v swscaler
check ${pipestatus[1]} "player_soak main_menu.sfd"
if [[ -x $T/player_soak_other ]]; then
    print "(upstream backend, for comparison; expected to fail with \"Excessive SFD stream interleave\")"
    SDL_AUDIO_DRIVER=dummy "${W[@]}" 90 "$T/player_soak_other" --seconds 40 --loop "$FA/main_menu.sfd" 2>&1 | grep -v swscaler
fi
if [[ ${AUDIO:-} == device ]]; then
    "${W[@]}" 90 "$T/player_soak" --sound --seconds 30 --loop "$FA/e3_demo_cut.sfd" 2>&1 | grep -v swscaler
else
    SDL_AUDIO_DRIVER=dummy "${W[@]}" 90 "$T/player_soak" --sound --max-drift 1.0 --seconds 30 --loop "$FA/e3_demo_cut.sfd" 2>&1 | grep -v swscaler
fi
check ${pipestatus[1]} "player_soak --sound e3_demo_cut.sfd"

if [[ ${SKIP_CENSUS:-0} != 1 ]]; then
    step "5. census (FA install)"
    "${W[@]}" 1500 "$T/movie_census" --jobs 6 --png "$T/frames" --tsv "$T/census/fa_movies.tsv" \
        --shot main_menu.sfd@300 --shot thqlogo.sfd@60 --shot gpglogo.sfd@90 --shot FMV_SCX_Intro.sfd@2500 \
        --shot X01.sfd@1000 --shot e3_demo_cut.sfd@200 --shot e3_demo_cut.sfd@audio --shot Abasi.sfd@40 \
        --shot UEF_load.sfd@100 "$FA" > "$T/census/fa_movies.txt" 2>&1
    check $? "movie_census FA"
    grep -E "^(movies=|  [A-Z]+:|  sizes)" "$T/census/fa_movies.txt"
    if [[ -d $FAF ]]; then
        step "5. census (~/.faforever/movies)"
        "${W[@]}" 1500 "$T/movie_census" --jobs 6 --png "$T/frames" --tsv "$T/census/faf_movies.tsv" \
            --shot A01_Rhiza_M01_00747.sfd@60 --shot A01_B01.sfd@100 "$FAF" > "$T/census/faf_movies.txt" 2>&1
        check $? "movie_census FAF"
        grep -E "^(movies=|  [A-Z]+:|  sizes)" "$T/census/faf_movies.txt"
    fi
fi

print
if (( ${#failed} )); then
    print "FAILED: ${(j:, :)failed}"
    exit 1
fi
print "ALL PASS"
