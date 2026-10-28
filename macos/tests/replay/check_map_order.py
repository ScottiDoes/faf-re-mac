# check_map_order.py <sources.cmake> <macos dir> <executable.map> [--app <object dir>]
# Checks that StaticInitPreRegister.cpp.o is the first object and that the engine objects
# follow src/sources.cmake order (replacements at the original's position, as the
# exclusions.cmake and exclusions.d/*.cmake lists say), then the posix platform objects, then
# the executable's own objects (--app: faf_runner.dir for faf_replay, the default, or
# faf_game_main.dir for faf_game). Exit status 0 when all three hold.
# Promoted from build/link-run-tests/check_map_order.py (package link-run, wave 2); run with
# python3 -I. macos/tests/replay/check_postlink.py runs it.
import re, sys, glob, os
args = sys.argv[1:]
app = "faf_runner.dir"
if "--app" in args:
    i = args.index("--app"); app = args[i + 1]; del args[i:i + 2]
if len(args) != 3:
    sys.exit("usage: check_map_order.py <sources.cmake> <macos dir> <executable.map> [--app <object dir>]")
src_cmake, macos_dir, mapfile = args
text = open(src_cmake).read()
entries = [e.replace('${FAF_DEPENDENCIES_DIR}/', '../dependencies/') for e in re.findall(r'^\s*"?([A-Za-z0-9_./+${}-]+\.(?:cpp|c|cc))"?\s*$', text, re.M)]
def cmake_strings(path):
    t = open(path).read()
    return re.findall(r'"((?:[^"\\]|\\.)*)"', t)
repl, excl = [], []
for f in [os.path.join(macos_dir, "exclusions.cmake")] + sorted(glob.glob(os.path.join(macos_dir, "exclusions.d", "*.cmake"))):
    t = open(f).read()
    t = re.sub(r'#[^\n]*', '', t)
    kind = None
    for tok in re.finditer(r'(FAF_MACOS_EXCLUDED_SOURCES|FAF_MACOS_REPLACED_SOURCES)|"((?:[^"\\]|\\.)*)"', t):
        if tok.group(1): kind = tok.group(1); continue
        s = tok.group(2).replace('\\\\', '\\')
        if '=>' in s:
            a, b = s.split('=>'); repl.append((re.compile(a), b))
        elif kind == 'FAF_MACOS_EXCLUDED_SOURCES' and not s.startswith('$'):
            excl.append(re.compile(s))
expected = []
for e in entries:
    r = next((b for a, b in repl if a.search(e)), None)
    if r: expected.append(r); continue
    if any(x.search(e) for x in excl): continue
    expected.append(e)
objs = []
for line in open(mapfile, errors='replace'):
    m = re.match(r'^\[\s*\d+\]\s+(.*)$', line)
    if m: objs.append(m.group(1))
    elif line.startswith('# Sections'): break
eng = [o for o in objs if 'faf_engine.dir' in o]
run = [o for o in objs if app in o]
def rel(o):
    m = re.search(r'/faf-re/src/(.*)\.o$', o)
    if m: return m.group(1)
    m = re.search(r'/faf-re/(dependencies/.*)\.o$', o); return '../' + m.group(1) if m else o
eng_rel = [rel(o) for o in eng]
print("objects in map:", len(objs), "engine:", len(eng), "%s:" % app, len(run))
print("first object:", objs[1] if len(objs) > 1 else None)
ok = eng_rel[0] == 'platform/posix/startup/StaticInitPreRegister.cpp'
print("StaticInitPreRegister first engine object:", ok)
n = len(expected)
seq_ok = eng_rel[1:1+n] == expected
print("expected sources.cmake objects:", n, "in order right after it:", seq_ok)
if not seq_ok:
    for i, (a, b) in enumerate(zip(eng_rel[1:1+n], expected)):
        if a != b: print(" first difference at", i, "map:", a, "expected:", b); break
    print(" map count after first:", len(eng_rel) - 1)
rest = eng_rel[1+n:]
print("trailing engine objects (posix platform):", len(rest), "all under platform/posix:", all(r.startswith('platform/posix/') for r in rest))
idx_last_eng = max(objs.index(o) for o in eng); idx_first_run = min(objs.index(o) for o in run) if run else -1
print("%s objects after all engine objects:" % app, idx_first_run > idx_last_eng)
others = [o for o in objs[1:] if 'faf_engine.dir' not in o and app not in o]
print("other inputs:", len(others), others[:3], '...', others[-3:])
sys.exit(0 if ok and seq_ok and idx_first_run > idx_last_eng else 1)
