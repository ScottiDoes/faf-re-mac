#!/bin/zsh
# check_fma.sh <object | archive | directory>...
#
# CI guard (macos/docs/DETERMINISM.md §5 rule 1, §6.5): no fused multiply-add in simulation
# code. The engine builds with -ffp-contract=off, so a fused instruction can only come from
# an explicit std::fma / intrinsic, or from a flag change. Disassembles every object
# (directories are searched for *.o, archives are read member by member) and reports each
# function that contains fmadd/fmsub/fnmadd/fnmsub (scalar) or fmla/fmls/fmlal/fmlsl
# (vector), except in the whitelist of deliberate std::fma users:
#   - src/legacy/math/X87Math.cpp (RoundExactProductToFloat: exact error terms)
#   - src/platform/posix/math/   (the software x87's error-free transforms)
#
# Exit status: 0 clean, 1 a fused instruction outside the whitelist, 2 usage or tool error.
set -euo pipefail
if (( $# == 0 )); then
  print -u2 "usage: ${0:t} <object|archive|directory>..."
  exit 2
fi

exec python3 -I - "$@" <<'PY'
import concurrent.futures
import os
import re
import subprocess
import sys

WHITELIST = ("/legacy/math/X87Math.cpp", "/legacy/math/X87Math.", "/platform/posix/math/")
FUSED = re.compile(r"\s(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls|fmlal2?|fmlsl2?|bfmlal[bt]?)(\.[0-9a-z]+)?\s")
FUNC = re.compile(r"^[0-9a-f]+ <(.+)>:$")
MEMBER = re.compile(r"^(.+)\((.+)\):\s+file format")


def inputs(args):
    for arg in args:
        if os.path.isdir(arg):
            for base, _, files in os.walk(arg):
                for name in sorted(files):
                    if name.endswith(".o"):
                        yield os.path.join(base, name)
        elif os.path.exists(arg):
            yield arg
        else:
            print("%s: no such file" % arg, file=sys.stderr)
            sys.exit(2)


def whitelisted(path):
    # Object paths mirror the source tree (CMakeFiles/<t>.dir/<abs source path>.o; CMake
    # writes the source path with '_' for spaces, so compare on the tail only).
    return any(w in path for w in WHITELIST)


def scan(path):
    proc = subprocess.run(["objdump", "-d", "--no-show-raw-insn", path], capture_output=True, text=True)
    if proc.returncode != 0:
        return path, None, proc.stderr.strip()
    hits = {}
    member = path
    func = "?"
    for line in proc.stdout.splitlines():
        m = MEMBER.match(line)
        if m:
            member = "%s(%s)" % (m.group(1), m.group(2))
            continue
        m = FUNC.match(line)
        if m:
            func = m.group(1)
            continue
        m = FUSED.search(line)
        if m:
            key = (member, func)
            hits.setdefault(key, []).append(m.group(1) + (m.group(2) or ""))
    return path, hits, None


paths = list(inputs(sys.argv[1:]))
bad, allowed, errors = {}, {}, []
with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    for path, hits, err in pool.map(scan, paths):
        if err is not None:
            errors.append("%s: objdump failed: %s" % (path, err))
            continue
        for (member, func), insns in hits.items():
            (allowed if whitelisted(member) else bad)[(member, func)] = insns


def demangle(names):
    if not names:
        return {}
    out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    return dict(zip(names, out))


names = demangle(sorted({f for _, f in list(bad) + list(allowed)}))


def show(func):
    return names.get(func, func)


for err in errors:
    print(err, file=sys.stderr)
print("check_fma: %d object file(s) scanned" % len(paths))
for (member, func), insns in sorted(allowed.items()):
    print("  allowed   %s: %s (%d: %s)" % (member, show(func), len(insns), " ".join(sorted(set(insns)))))
for (member, func), insns in sorted(bad.items()):
    print("  FUSED     %s: %s (%d: %s)" % (member, show(func), len(insns), " ".join(sorted(set(insns)))))
if bad:
    print("check_fma: %d function(s) outside the whitelist contain fused multiply-add" % len(bad))
    sys.exit(1)
if errors:
    sys.exit(2)
print("check_fma: ok, no fused multiply-add outside the whitelist")
PY
