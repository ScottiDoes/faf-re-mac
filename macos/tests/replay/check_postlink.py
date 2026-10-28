#!/usr/bin/env python3
"""check_postlink.py <executable> [--map <file>] [--tree <build dir>] [--skip-binary-fma]

The post-link checks of a native executable (faf_replay or faf_game), the ones the wave-2
verifier ran by hand (WAVE2_REPORT.md section 2, link-run.md, crt.md), in one command.
Python 3.9 standard library and the Xcode command-line tools only; run with `python3 -I`.

  map-order     the ld map's object order: StaticInitPreRegister.cpp.o first, then exactly the
                src/sources.cmake objects in order (stand-ins at their original's position),
                then platform/posix objects, then the executable's own objects
                (check_map_order.py, promoted from build/link-run-tests)
  init-order    the order the executable's initializers really run in (__TEXT,__init_offsets):
                the first is StaticInitPreRegister.cpp's RunFirstInitializers, and every later
                one belongs to an object at or after the previous one's in the map, so static
                initializers keep src/sources.cmake order
  preregister   __DATA,__faf_prereg (GPG_PREREGISTER_INIT, StaticInitPhase.h) holds one
                8-byte entry per gGpgPreRegisterInit_* symbol of the engine objects: none was
                dropped by the linker, none is outside the section
  libm          macos/tests/crt/check_symbols.sh: no libm or rand result from libSystem that the
                Windows UCRT could disagree with. An import that no engine object references
                (only a media library linked into the executable does) is reported, not failed
  fma-objects   macos/tests/crt/check_fma.sh over the engine and app objects the map lists, Wild
                Magic and zlib: no fused multiply-add outside the x87 whitelist

The object checks read the objects the ld map lists (the ones really linked), not every object
in the build tree, which can also hold stale ones.
  fma-binary    the linked executable disassembled; a fused multiply-add in a function that
                the map attributes to one of those objects fails (other libraries are listed)

Exit status: 0 every check passed, 1 a check failed, 2 an input is missing or a tool failed.
"""
import argparse
import bisect
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FMA_WHITELIST = ("/legacy/math/X87Math.", "/platform/posix/math/")
FUSED = re.compile(r"\s(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls|fmlal2?|fmlsl2?|bfmlal[bt]?)(\.[0-9a-z]+)?\s")
results = []


def report(name, ok, detail):
    results.append((name, ok))
    tag = {True: "PASS", False: "FAIL", None: "SKIP"}[ok]
    print("%-12s %s  %s" % (name, tag, detail), flush=True)


def run(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, errors="replace", **kw)


class LinkMap:
    def __init__(self, path):
        self.objects = {}
        self.symbols = []  # (address, size, file index, name), sorted by address
        mode = None
        sym_re = re.compile(r"^0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+\[\s*(\d+)\]\s+(.*)$")
        obj_re = re.compile(r"^\[\s*(\d+)\]\s+(.*)$")
        with open(path, errors="replace") as f:
            for line in f:
                if line.startswith("# Object files:"):
                    mode = "obj"
                    continue
                if line.startswith("# Sections:"):
                    mode = "sec"
                    continue
                if line.startswith("# Symbols:"):
                    mode = "sym"
                    continue
                if line.startswith("# Dead Stripped Symbols:"):
                    mode = None
                    continue
                if mode == "obj":
                    m = obj_re.match(line)
                    if m:
                        self.objects[int(m.group(1))] = m.group(2).strip()
                elif mode == "sym":
                    m = sym_re.match(line)
                    if m:
                        self.symbols.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), m.group(4)))
        self.symbols.sort()
        self.addresses = [s[0] for s in self.symbols]

    def at(self, address):
        """The symbol whose [address, address + size) holds `address`, else None."""
        i = bisect.bisect_right(self.addresses, address) - 1
        while i >= 0:
            start, size, index, name = self.symbols[i]
            if start <= address < start + max(size, 1):
                return self.symbols[i]
            if start < address:
                return None
            i -= 1
        return None


def engine_object(path):
    """True for the objects the determinism rules cover (engine, app, Wild Magic, zlib)."""
    return ("faf_engine.dir" in path or "faf_runner.dir" in path or "faf_game_main.dir" in path or
            "libWm3Foundation.a" in path or "libfaf_zlib.a" in path)


def check_map_order(exe, mapfile, app_dir):
    script = os.path.join(HERE, "check_map_order.py")
    p = run([sys.executable, "-I", script, os.path.join(ROOT, "src", "sources.cmake"), os.path.join(ROOT, "macos"),
             mapfile, "--app", app_dir])
    lines = [l for l in p.stdout.splitlines() if l.strip() and not l.startswith("other inputs")]
    detail = "; ".join(lines[1:4]) if p.returncode == 0 else " | ".join(lines)
    if p.returncode != 0:
        # The expected order comes from today's src/sources.cmake and exclusion lists; an
        # executable linked before one of them changed cannot match it.
        lists = [os.path.join(ROOT, "src", "sources.cmake"), os.path.join(ROOT, "macos", "exclusions.cmake")]
        d = os.path.join(ROOT, "macos", "exclusions.d")
        lists += [os.path.join(d, f) for f in os.listdir(d) if f.endswith(".cmake")]
        newer = [os.path.relpath(f, ROOT) for f in lists if os.path.getmtime(f) > os.path.getmtime(mapfile)]
        if newer:
            detail += " | NOTE: the executable was linked before %s changed: relink it, then check again" % ", ".join(
                sorted(newer))
    report("map-order", p.returncode == 0, detail)


def segment_base(exe):
    out = run(["otool", "-l", exe]).stdout
    m = re.search(r"segname __TEXT\n\s+vmaddr 0x([0-9a-f]+)", out)
    return int(m.group(1), 16) if m else 0x100000000


def section(exe, segname, sectname):
    out = run(["otool", "-l", exe]).stdout
    m = re.search(r"sectname %s\n\s+segname %s\n\s+addr 0x([0-9a-f]+)\n\s+size 0x([0-9a-f]+)" % (sectname, segname), out)
    return (int(m.group(1), 16), int(m.group(2), 16)) if m else None


def check_init_order(exe, lmap):
    sect = section(exe, "__TEXT", "__init_offsets")
    if sect is None:
        report("init-order", False, "no __TEXT,__init_offsets section (an older linker's __mod_init_func is not decoded)")
        return
    out = run(["otool", "-s", "__TEXT", "__init_offsets", exe]).stdout
    words = []
    for line in out.splitlines()[2:]:
        parts = line.split()
        words += [int(w, 16) for w in parts[1:]]
    base = segment_base(exe)
    entries = []
    for offset in words:
        sym = lmap.at(base + offset)
        entries.append((offset, sym))
    if not entries:
        report("init-order", False, "no initializers")
        return
    first = entries[0][1]
    first_ok = (first is not None and "RunFirstInitializers" in first[3] and
                lmap.objects.get(first[2], "").endswith("StaticInitPreRegister.cpp.o"))
    unknown = [o for o, s in entries if s is None]
    backwards = []
    last = -1
    for offset, sym in entries:
        if sym is None:
            continue
        if sym[2] < last:
            backwards.append((offset, sym))
        last = max(last, sym[2])
    ok = first_ok and not unknown and not backwards
    detail = "%d initializers; first %s (%s)" % (
        len(entries), first[3] if first else "?", os.path.basename(lmap.objects.get(first[2], "?")) if first else "?")
    if unknown:
        detail += "; %d not attributable to a map symbol" % len(unknown)
    if backwards:
        o, s = backwards[0]
        detail += "; %d run before an earlier object's, first %s in %s" % (len(backwards), s[3], lmap.objects.get(s[2]))
    else:
        detail += "; object order never goes backwards"
    report("init-order", ok, detail)


def nm_objects(paths):
    """{path: (defined names, undefined names)} for object files, via one nm -A pass per batch."""
    out = {}
    batch = 200
    for i in range(0, len(paths), batch):
        chunk = paths[i:i + batch]
        p = run(["nm", "-A"] + chunk)
        if p.returncode != 0:
            raise RuntimeError("nm failed: %s" % p.stderr.strip()[:300])
        for line in p.stdout.splitlines():
            # "<path>: <address> <type> <name>" or "<path>:                  U <name>"
            k = line.rfind(": ")
            if k < 0:
                continue
            path, rest = line[:k], line[k + 2:].split()
            if len(rest) < 2:
                continue
            defined, undefined = out.setdefault(path, (set(), set()))
            (undefined if rest[-2] == "U" else defined).add(rest[-1])
    return out


def object_paths(tree, app_dir, lmap):
    """The engine and app objects the map says were linked (a build tree can also hold stale
    objects, e.g. from an earlier configure through another path to the same sources)."""
    paths = []
    for path in lmap.objects.values():
        if ("/faf_engine.dir/" in path or "/%s/" % app_dir in path) and path.endswith(".o") and "(" not in path:
            paths.append(path if os.path.isabs(path) else os.path.join(tree, path))
    missing = [p for p in paths if not os.path.exists(p)]
    if missing:
        raise RuntimeError("%d linked objects are missing from the tree, e.g. %s" % (len(missing), missing[0]))
    return paths


def check_preregister(exe, lmap, objects_nm, engine_dir):
    sect = section(exe, "__DATA", "__faf_prereg")
    if sect is None:
        report("preregister", False, "no __DATA,__faf_prereg section")
        return
    addr, size = sect
    entries = size // 8
    in_binary = [s for s in lmap.symbols if "gGpgPreRegisterInit_" in s[3]]
    outside = [s for s in in_binary if not addr <= s[0] < addr + size]
    in_objects = sum(1 for path, (defined, _) in objects_nm.items() if engine_dir in path
                     for name in defined if "gGpgPreRegisterInit_" in name)
    ok = size % 8 == 0 and entries > 0 and len(in_binary) == entries == in_objects and not outside
    report("preregister", ok, "%d entries (%d bytes); %d gGpgPreRegisterInit_ symbols in the executable%s, %d in the "
           "engine objects" % (entries, size, len(in_binary), "" if not outside else " (%d outside the section)" %
                               len(outside), in_objects))


def check_libm(exe, objects_nm, archives):
    script = os.path.join(ROOT, "macos", "tests", "crt", "check_symbols.sh")
    p = run(["zsh", script, exe])
    forbidden = re.findall(r"^\s+FORBIDDEN\s+(\S+)", p.stdout, re.M)
    if p.returncode not in (0, 1):
        report("libm", False, "check_symbols.sh failed: %s" % (p.stderr.strip() or p.stdout.strip())[:300])
        return
    undefined_by_engine = set()
    for path, (_, undefined) in objects_nm.items():
        undefined_by_engine |= undefined
    for archive in archives:
        for line in run(["nm", "-u", archive]).stdout.splitlines():
            if line.strip() and not line.endswith(":"):
                undefined_by_engine.add(line.strip())
    engine_refs = [s for s in forbidden if "_" + s in undefined_by_engine]
    others = [s for s in forbidden if s not in engine_refs]
    summary = p.stdout.splitlines()[0] if p.stdout else ""
    detail = summary
    if engine_refs:
        detail += "; FORBIDDEN and used by engine code: %s" % " ".join(engine_refs)
    if others:
        detail += "; imported only by other libraries in the executable (not engine code): %s" % " ".join(others)
    if not forbidden:
        detail += "; no forbidden import"
    report("libm", not engine_refs, detail)


def check_fma_objects(objects, archives):
    script = os.path.join(ROOT, "macos", "tests", "crt", "check_fma.sh")
    p = run(["zsh", script] + objects + archives)
    lines = p.stdout.splitlines()
    fused = [l.strip() for l in lines if l.strip().startswith("FUSED")]
    allowed = sum(1 for l in lines if l.strip().startswith("allowed"))
    head = lines[0] if lines else p.stderr.strip()[:200]
    detail = "%s; %d whitelisted x87 functions" % (head, allowed)
    if fused:
        detail += "; " + " | ".join(fused[:5])
    report("fma-objects", p.returncode == 0, detail)


def check_fma_binary(exe, lmap):
    p = run(["objdump", "-d", "--no-show-raw-insn", exe])
    if p.returncode != 0:
        report("fma-binary", False, "objdump failed: %s" % p.stderr.strip()[:200])
        return
    func_re = re.compile(r"^([0-9a-f]+) <(.+)>:$")
    current = None
    hits = {}
    for line in p.stdout.splitlines():
        m = func_re.match(line)
        if m:
            current = (int(m.group(1), 16), m.group(2))
            continue
        if current is not None and FUSED.search(line):
            hits[current] = hits.get(current, 0) + 1
    bad, allowed, other = [], [], {}
    for (address, name), count in hits.items():
        sym = lmap.at(address)
        obj = lmap.objects.get(sym[2], "?") if sym else "?"
        if engine_object(obj):
            (allowed if any(w in obj for w in FMA_WHITELIST) else bad).append((name, obj, count))
        else:
            key = re.sub(r"\(.*\)$", "", os.path.basename(obj))
            other[key] = other.get(key, 0) + 1
    detail = "%d functions with fused multiply-add: %d in whitelisted x87 objects, %d elsewhere in engine objects" % (
        len(hits), len(allowed), len(bad))
    if other:
        detail += "; in other code: %s" % ", ".join("%s (%d)" % kv for kv in sorted(other.items()))
    if bad:
        detail += "; FUSED: " + " | ".join("%s in %s" % (n, os.path.basename(o)) for n, o, _ in bad[:5])
    report("fma-binary", not bad, detail)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("--map")
    ap.add_argument("--tree")
    ap.add_argument("--skip-binary-fma", action="store_true")
    args = ap.parse_args()
    exe = os.path.abspath(args.exe)
    tree = os.path.abspath(args.tree or os.path.dirname(exe))
    mapfile = args.map or exe + ".map"
    app_dir = "faf_game_main.dir" if os.path.basename(exe).startswith("faf_game") else "faf_runner.dir"
    for path, what in ((exe, "executable"), (mapfile, "ld map"), (os.path.join(tree, "CMakeFiles", "faf_engine.dir"),
                                                                    "engine objects")):
        if not os.path.exists(path):
            print("check_postlink: no %s at %s" % (what, path), file=sys.stderr)
            return 2
    if os.path.getmtime(mapfile) + 5 < os.path.getmtime(exe):
        print("check_postlink: %s is older than %s" % (mapfile, exe), file=sys.stderr)
        return 2
    archives = [os.path.join(tree, a) for a in ("libWm3Foundation.a", "libfaf_zlib.a") if os.path.exists(os.path.join(tree, a))]
    print("check_postlink: %s" % exe, flush=True)
    lmap = LinkMap(mapfile)
    try:
        objects = object_paths(tree, app_dir, lmap)
        objects_nm = nm_objects(objects)
    except RuntimeError as error:
        print("check_postlink: %s" % error, file=sys.stderr)
        return 2
    check_map_order(exe, mapfile, app_dir)
    check_init_order(exe, lmap)
    check_preregister(exe, lmap, objects_nm, "faf_engine.dir")
    check_libm(exe, objects_nm, archives)
    check_fma_objects(objects, archives)
    if args.skip_binary_fma:
        report("fma-binary", None, "skipped (--skip-binary-fma)")
    else:
        check_fma_binary(exe, lmap)
    failed = [n for n, ok in results if ok is False]
    print("check_postlink: %s" % ("all checks passed" if not failed else "FAILED: " + " ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
