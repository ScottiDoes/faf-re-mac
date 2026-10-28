#!/bin/zsh
# Builds and runs the shell package's test (shell_test.cpp) against a full engine build:
# compiled with the engine's flags (build/m2-shell/compile_commands.json) and linked with the
# engine's objects as faf_replay is, the runner's objects replaced by the test's.
#   macos/tests/shell/build_test.sh [--no-run]
# Needs build/m2-shell (cmake -S macos -B build/m2-shell -G Ninja -DCMAKE_BUILD_TYPE=Release;
# ninja -C build/m2-shell faf_replay). Output: build/m2-shell-tests/shell_test.
# SHELL_TEST_SHOW=0 keeps the test windows hidden; a watchdog (perl alarm) ends it after 90 s.
set -e
root=${0:A:h:h:h:h}
tree=$root/build/m2-shell
out=$root/build/m2-shell-tests
mkdir -p $out
python3 -I - "$tree" "$root/macos/tests/shell/shell_test.cpp" "$out/shell_test.o" "$out/shell_test" <<'PY'
import json, os, shlex, subprocess, sys
tree, src, obj, exe = sys.argv[1:5]
entries = json.load(open(os.path.join(tree, "compile_commands.json")))
ref = next(e for e in entries if e["file"].endswith("platform/posix/WinCompatMisc.cpp"))
args = shlex.split(ref["command"])
args = [src if a == ref["file"] else a for a in args]
i = args.index("-o"); args[i + 1] = obj
subprocess.check_call(args, cwd=ref["directory"])
link = subprocess.check_output(["ninja", "-C", tree, "-t", "commands", "faf_replay"], text=True).strip().splitlines()[-1]
link = link.split("&&")[1] if link.startswith(":") else link
largs = shlex.split(link)
largs = [a for a in largs if "faf_runner.dir" not in a]
# Drop the link map of faf_replay; add the test object after the engine's.
if "-map" in largs:
    j = largs.index("-map"); del largs[j - 1:j + 3]  # -Xlinker -map -Xlinker <file>
i = largs.index("-o"); largs[i + 1] = exe
largs.insert(i, obj)
subprocess.check_call(largs, cwd=tree)
PY
print "built $out/shell_test"
[[ ${1:-} == --no-run ]] && exit 0
cd $out
perl -e 'alarm shift; exec @ARGV' 90 ./shell_test
