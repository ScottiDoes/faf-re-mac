#!/bin/zsh
# Compile one translation unit of the macOS build without ninja (safe to run many at
# once): macos/tu.sh <source path> [extra clang args...]
# Uses its command from build/macos/compile_commands.json; a file the build does not
# list yet (new since the last configure) borrows the command of a platform/posix file.
# Output goes to a temp object; exit status and diagnostics are the compiler's.
set -e
root=${0:A:h:h}
db=$root/build/macos/compile_commands.json
src=${1:A}; shift
[[ -f $src ]] || { print -u2 "no such file: $1"; exit 2; }
cmd=$(python3 - "$db" "$src" <<'PY'
import json, shlex, sys, os
db, src = sys.argv[1], sys.argv[2]
entries = json.load(open(db))
def norm(p): return os.path.realpath(p)
by_file = {norm(e["file"]): e for e in entries}
e = by_file.get(norm(src))
if e is None:
    # Borrow a platform/posix command; swap the file after splitting (paths may hold spaces).
    ref = next(x for x in entries if x["file"].endswith("platform/posix/WinCompatMisc.cpp"))
    args = [src if a == ref["file"] else a for a in shlex.split(ref["command"])]
else:
    args = shlex.split(e["command"])
# Drop the output object; compile to a throwaway one.
if "-o" in args:
    i = args.index("-o"); del args[i:i+2]
print(shlex.join(args))
PY
)
obj=$(mktemp -t faf_tu).o
trap 'rm -f $obj' EXIT
cd $root/build/macos
eval "$cmd -o $obj $*"
