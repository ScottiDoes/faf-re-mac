#!/usr/bin/env python3
"""Finds the first difference between two builds' per-beat sim logs (/synclog).

Usage: compare_beatlogs.py <dir A> <dir B> [first beat] [last beat] [--context N]

Each directory holds beat<NNNNN>.log files (in any subfolder, as /synclog writes them: one dated
folder per run). For every beat both have, in order, the lines are compared after dropping line
endings (a Windows build writes CRLF). The first beat that differs is reported with the first
differing line, the entity it belongs to (the last "  0x<id>" / "bp:" lines above it) and N lines
of context from both sides. Beats only one side has are listed.

Python 3.9 standard library only; run with `python3 -I`.
"""
import os
import re
import sys


def beat_files(root):
    files = {}
    for folder, _, names in os.walk(root):
        for name in names:
            m = re.fullmatch(r"beat(\d+)\.log", name)
            if m:
                files[int(m.group(1))] = os.path.join(folder, name)
    return files


def lines_of(path):
    with open(path, "rb") as f:
        return [line.rstrip(b"\r\n").decode("latin1") for line in f]


NUMBER = re.compile(r"-?\d+\.\d+")


def same_line(a, b):
    """Equal, or equal but for printed decimals one unit apart in their last digit: the MSVC CRT
    rounds a tie (616.9453125 at %f) away from zero, Apple's printf to even. The float bits the
    logs print in hex next to the sim state are compared exactly."""
    if a == b:
        return True
    if NUMBER.sub("#", a) != NUMBER.sub("#", b):
        return False
    for x, y in zip(NUMBER.findall(a), NUMBER.findall(b)):
        if x == y:
            continue
        digits = len(x.split(".")[1])
        if digits != len(y.split(".")[1]) or abs(float(x) - float(y)) > 1.5 * 10 ** -digits:
            return False
    return True


def owner(lines, index):
    """The entity block a line is in: the nearest entity id and blueprint lines above it."""
    entity = blueprint = section = None
    for line in reversed(lines[: index + 1]):
        if blueprint is None and line.startswith("    bp:"):
            blueprint = line.strip()
        if entity is None and re.fullmatch(r"  0x[0-9a-f]{8}", line):
            entity = line.strip()
        if section is None and line in ("Armies", "Dirty Entities") or (section is None and line.startswith("********** beat")):
            section = line
        if entity and section:
            break
    return entity, blueprint, section


def main(argv):
    context = 12
    if "--context" in argv:
        i = argv.index("--context")
        context = int(argv[i + 1])
        del argv[i : i + 2]
    if len(argv) < 2:
        print(__doc__)
        return 2
    a_dir, b_dir = argv[0], argv[1]
    a, b = beat_files(a_dir), beat_files(b_dir)
    lo = int(argv[2]) if len(argv) > 2 else min(set(a) | set(b))
    hi = int(argv[3]) if len(argv) > 3 else max(set(a) | set(b))
    common = sorted(n for n in set(a) & set(b) if lo <= n <= hi)
    only_a = sorted(n for n in set(a) - set(b) if lo <= n <= hi)
    only_b = sorted(n for n in set(b) - set(a) if lo <= n <= hi)
    if only_a:
        print("beats only in A: %d (%d..%d)" % (len(only_a), only_a[0], only_a[-1]))
    if only_b:
        print("beats only in B: %d (%d..%d)" % (len(only_b), only_b[0], only_b[-1]))
    if not common:
        print("no beat in %d..%d is in both directories" % (lo, hi))
        return 2
    for beat in common:
        la, lb = lines_of(a[beat]), lines_of(b[beat])
        n = min(len(la), len(lb))
        first = next((i for i in range(n) if not same_line(la[i], lb[i])), None)
        if first is None and len(la) == len(lb):
            continue
        if first is None:
            first = n
        same = common.index(beat)
        print("%s; first difference in beat %d at line %d"
              % ("beats %d..%d identical (%d beats)" % (common[0], common[same - 1], same) if same
                 else "the first beat compared differs", beat, first + 1))
        print("  A: %s (%d lines)\n  B: %s (%d lines)" % (a[beat], len(la), b[beat], len(lb)))
        entity, blueprint, section = owner(la, max(first - 1, 0))
        print("  section: %s, entity: %s %s" % (section, entity or "-", blueprint or ""))
        start = max(0, first - context)
        print("  --- A ---")
        for i in range(start, min(len(la), first + context)):
            print("  %s %6d %s" % (">" if i == first else " ", i + 1, la[i]))
        print("  --- B ---")
        for i in range(start, min(len(lb), first + context)):
            print("  %s %6d %s" % (">" if i == first else " ", i + 1, lb[i]))
        return 1
    print("beats %d..%d identical in both (%d beats)" % (common[0], common[-1], len(common)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
