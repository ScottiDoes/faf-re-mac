#!/bin/zsh
# build_unit_test.sh [build dir]: builds and runs runner_unit_test (macos/tests/replay/
# runner_unit_test.cpp) from the runner's real ReplayReport.cpp, ReplayChecks.cpp and
# FpcrWatch.cpp, the engine's msvc8::string (legacy/containers/String.cpp) and a test double
# for the gpg log (test_double.cpp). Every file is compiled with the engine's own command
# (the platform/posix entry of <build dir>/compile_commands.json: same flags, defines and
# force-included WinCompat.h), so the code under test is built as the runner builds it.
#
#   build dir   a configured macos/ tree (default build/m4-lockin, else build/macos)
#   OUT         where objects and the binary go (default build/m4-lockin-tests)
#   RUN=0       build only
# Exit status: the test's (0 all pass), or 2 when it cannot be built.
set -euo pipefail
root=${0:A:h:h:h:h}
tree=${1:-$root/build/m4-lockin}
[[ -f $tree/compile_commands.json ]] || tree=$root/build/macos
[[ -f $tree/compile_commands.json ]] || { print -u2 "build_unit_test: no compile_commands.json (configure macos/ first)"; exit 2 }
OUT=${OUT:-$root/build/m4-lockin-tests}
mkdir -p "$OUT"

python3 -I - "$tree" "$root" "$OUT" <<'PY' || exit 2
import json, os, shlex, subprocess, sys
tree, root, out = sys.argv[1:4]
src = os.path.join(root, "src")
entries = json.load(open(os.path.join(tree, "compile_commands.json")))
ref = next(e for e in entries if e["file"].endswith("platform/posix/WinCompatMisc.cpp"))
base = shlex.split(ref["command"])
i = base.index("-o"); del base[i:i + 2]
sources = [
    os.path.join(src, "platform/posix/runner/ReplayReport.cpp"),
    os.path.join(src, "platform/posix/runner/ReplayChecks.cpp"),
    os.path.join(src, "platform/posix/runner/FpcrWatch.cpp"),
    os.path.join(src, "legacy/containers/String.cpp"),
    os.path.join(root, "macos/tests/replay/test_double.cpp"),
    os.path.join(root, "macos/tests/replay/runner_unit_test.cpp"),
]
objects = []
for path in sources:
    obj = os.path.join(out, os.path.basename(path) + ".o")
    args = [path if a == ref["file"] else a for a in base] + ["-o", obj]
    if path not in args:
        sys.exit("cannot place %s in the reference command" % path)
    print("compile", os.path.relpath(path, root), flush=True)
    if subprocess.run(args, cwd=ref.get("directory", tree)).returncode != 0:
        sys.exit("compile failed: %s" % path)
    objects.append(obj)
exe = os.path.join(out, "runner_unit_test")
print("link", exe, flush=True)
if subprocess.run([base[0]] + objects + ["-o", exe]).returncode != 0:
    sys.exit("link failed")
PY

[[ ${RUN:-1} == 1 ]] || exit 0
exec "$OUT/runner_unit_test"
