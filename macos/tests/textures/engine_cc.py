#!/usr/bin/env python3 -I
"""Compile one source with the engine's command (build/macos/compile_commands.json) into an
object of your choosing: engine_cc.py <source> <output> [extra clang args...]

A file the build does not list yet (the textures package's new files, the tests) borrows the
command of platform/posix/WinCompatMisc.cpp: engine flags, WinCompat.h force-include, include
path. Reads build/macos, never writes there.
"""
import json
import os
import shlex
import subprocess
import sys

root = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
db_path = os.path.join(root, "build", "macos", "compile_commands.json")
src = os.path.realpath(sys.argv[1])
out = os.path.abspath(sys.argv[2])
extra = sys.argv[3:]

entries = json.load(open(db_path))
by_file = {os.path.realpath(e["file"]): e for e in entries}
entry = by_file.get(src)
if entry is None:
    ref = next(x for x in entries if x["file"].endswith("platform/posix/WinCompatMisc.cpp"))
    args = [src if a == ref["file"] else a for a in shlex.split(ref["command"])]
    directory = ref["directory"]
else:
    args = shlex.split(entry["command"])
    directory = entry["directory"]
if "-o" in args:
    i = args.index("-o")
    del args[i:i + 2]
args += ["-o", out] + extra
sys.exit(subprocess.call(args, cwd=directory))
