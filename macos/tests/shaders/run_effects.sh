#!/bin/zsh
# The game's real effects through the macOS shader path (macos/porting/shaders.md, test 4).
#
#   macos/tests/shaders/run_effects.sh [build tree]   (default build/m2-shaders)
#
# Needs a configured and built tree (cmake -S macos -B build/m2-shaders -G Ninja
# -DCMAKE_BUILD_TYPE=Release && ninja -C build/m2-shaders faf_replay). Links effects_test
# from faf_replay's objects with the runner swapped for the test, extracts the effects from the
# game data that ~/Gaming/faf-re-bin/run-native/init_faf.lua mounts (FAF's effects.nx2
# ahead of FA's effects.scd; plus faforever.faf's copies, which that init file does not
# mount), runs every set with a fresh shader cache, then a warm-cache and a parallel run
# of the FAF set, then spirv-val and spirv-cross (MSL, what MoltenVK does) on every module.
# Everything lands in build/m2-shaders-tests.
set -u
root=${0:A:h:h:h:h}
tree=${1:-$root/build/m2-shaders}
sdk=${FAF_VULKAN_SDK:-$HOME/Gaming/VulkanSDK/1.4.363.0/macOS}
out=$root/build/m2-shaders-tests
mkdir -p $out
export PATH=/opt/homebrew/bin:$PATH

# --- build ---------------------------------------------------------------------------
cmd=$(python3 -I - "$tree/compile_commands.json" "$root/src/gpg/gal/effects/EffectRuntime.cpp" "$root/macos/tests/shaders/effects_test.cpp" "$out/effects_test.o" <<'PY'
import json, shlex, sys, os
db, ref, src, obj = sys.argv[1:5]
entries = json.load(open(db))
e = next(x for x in entries if os.path.realpath(x["file"]) == os.path.realpath(ref))
args = shlex.split(e["command"])
args = [src if os.path.realpath(a) == os.path.realpath(ref) or a == e["file"] else a for a in args]
i = args.index("-o"); args[i + 1] = obj
print(shlex.join(args))
PY
) || exit 1
(cd $tree && eval "$cmd") || { print -u2 "effects_test.cpp does not compile"; exit 1; }
# faf_replay's link inputs in its order (StaticInitPreRegister.o first: the engine's
# initializers rely on it), the runner's objects swapped for the test's.
objects=(${(f)"$(python3 -I - "$tree/build.ninja" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
m = re.search(r'^build faf_replay: \S+ (.*?)(?:\|\||\n)', s, re.M | re.S)
parts = [p.replace('$ ', ' ').replace('$:', ':') for p in re.split(r'(?<!\$) ', m.group(1).strip()) if p]
for p in parts:
    if 'faf_runner.dir' not in p and p.endswith('.o'):
        print(p)
PY
)"})
(( ${#objects} > 900 )) || { print -u2 "no faf_replay link line in $tree/build.ninja"; exit 1; }
(cd $tree && clang++ -arch arm64 $objects $out/effects_test.o libWm3Foundation.a libfaf_zlib.a \
  -Wl,-rpath,/opt/homebrew/lib /opt/homebrew/lib/libSDL3.0.dylib \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework ImageIO -framework QuartzCore -framework Metal \
  -framework CoreServices -framework ApplicationServices -o $out/effects_test) || { print -u2 "effects_test does not link"; exit 1; }

# --- effects -------------------------------------------------------------------------
fa="$HOME/Gaming/Windows Steam.app/Contents/SharedSupport/prefix/drive_c/Program Files (x86)/Steam/steamapps/common/Supreme Commander Forged Alliance"
python3 -I - "$out/effects" "$fa/gamedata/effects.scd" fa "$HOME/Gaming/faf-re-bin/gamedata/effects.nx2" faf "$HOME/Gaming/faf-re-bin/gamedata/faforever.faf" faforever <<'PY' || exit 1
import sys, zipfile, os
out, a = sys.argv[1], sys.argv[2:]
for i in range(0, len(a), 2):
    d = os.path.join(out, a[i + 1]); os.makedirs(d, exist_ok=True)
    z = zipfile.ZipFile(a[i])
    for n in z.namelist():
        low = n.lower()
        if low.startswith('effects/') and low.endswith(('.fx', '.compat')) and '/' not in n[8:]:
            open(os.path.join(d, os.path.basename(n)), 'wb').write(z.read(n))
PY

rm -rf $out/spv $out/cache-*(N)
mkdir -p $out/spv
overall=0
run() { # label compat cache parallel required files...
  local label=$1 compat=$2 cache=$3 parallel=$4 required=$5; shift 5
  mkdir -p $cache
  FAF_SHADER_CACHE_DIR=$cache FAF_TEST_PARALLEL=$parallel $out/effects_test $compat $out/spv $label $@ > $out/$label.log 2> $out/$label.stderr
  local rc=$?
  grep -E "^(TOTAL|DXC|shader cache)" $out/$label.log
  if (( rc != 0 )); then
    if (( required )); then
      overall=1; print "  $label: FAILED (rc=$rc), see $out/$label.log"
    else
      print "  $label: failures (informational: the game does not load this set), see $out/$label.log"
    fi
  fi
}
# FAF's effects with FAF's compat preamble (what the game mounts), cold then warm cache.
run faf $out/effects/faf/d3d10states.compat $out/cache-faf 0 1 $out/effects/faf/*.fx
run faf-warm $out/effects/faf/d3d10states.compat $out/cache-faf 0 1 $out/effects/faf/*.fx
run faf-parallel $out/effects/faf/d3d10states.compat $out/cache-faf-parallel 1 1 $out/effects/faf/*.fx
# FA's own effects with FA's preamble (shadowed by FAF's in the game); faforever.faf's
# copies with FAF's preamble.
run fa $out/effects/fa/d3d10states.compat $out/cache-fa 0 0 $out/effects/fa/*.fx
run faforever $out/effects/faf/d3d10states.compat $out/cache-faforever 0 1 $out/effects/faforever/*.fx
# Front-end checks and the GPU-driven programs.
FAF_SHADER_CACHE_DIR=$out/cache-misc $out/effects_test - $out/spv engine > $out/engine.log 2> $out/engine.stderr || { overall=1; print "  engine: FAILED"; }
grep -E "^(CHECK front|GPU-DRIVEN)" $out/engine.log
# The default cache location (no FAF_SHADER_CACHE_DIR): one small effect, twice.
(unset FAF_SHADER_CACHE_DIR; $out/effects_test $out/effects/faf/d3d10states.compat $out/spv default-cache $out/effects/faf/ui.fx > $out/default-cache.log 2>&1 &&
  $out/effects_test $out/effects/faf/d3d10states.compat $out/spv default-cache $out/effects/faf/ui.fx >> $out/default-cache.log 2>&1) \
  || { overall=1; print "  default-cache: FAILED"; }
grep -E "^(shader cache|EFFECT)" $out/default-cache.log | sed -E 's/ +/ /g' | cut -c1-200

# Same SPIR-V serial and parallel.
if ! diff -rq $out/spv/faf $out/spv/faf-parallel > /dev/null; then
  overall=1; print "parallel run produced different files:"; diff -rq $out/spv/faf $out/spv/faf-parallel | head
else
  print "parallel run: identical modules"
fi

# --- validation ----------------------------------------------------------------------
python3 -I - "$sdk/bin" "$out/spv" <<'PY' || overall=1
import os, subprocess, sys
bin, root = sys.argv[1:3]
counts = {}
failures = []
for dirpath, _, files in sorted(os.walk(root)):
    for f in sorted(files):
        if not f.endswith('.spv'):
            continue
        p = os.path.join(dirpath, f)
        kind = 'raw' if f.endswith('.raw.spv') else ('linked' if '.linked-' in f else 'module')
        c = counts.setdefault(kind, [0, 0, 0])
        c[0] += 1
        r = subprocess.run([bin + '/spirv-val', '--target-env', 'vulkan1.1', '--scalar-block-layout', p], capture_output=True, text=True)
        if r.returncode != 0:
            c[1] += 1
            failures.append(('spirv-val', p, (r.stdout + r.stderr)[:600]))
        if kind != 'raw':
            r = subprocess.run([bin + '/spirv-cross', '--msl', '--msl-version', '30000', p, '--output', '/dev/null'], capture_output=True, text=True)
            if r.returncode != 0:
                c[2] += 1
                failures.append(('spirv-cross', p, (r.stdout + r.stderr)[:600]))
for kind, (n, val, msl) in sorted(counts.items()):
    print(f"VALIDATE {kind}: {n} modules, spirv-val failures {val}" + ("" if kind == 'raw' else f", spirv-cross --msl failures {msl}"))
for tool, p, msg in failures[:20]:
    print(f"  {tool} {os.path.relpath(p, root)}: {msg.strip()}")
sys.exit(1 if failures else 0)
PY
# MoltenVK's view: every module the device would create (stripped, and the linked pixel
# shaders) to MSL with MoltenVK's converter, then through the Metal runtime compiler.
clang++ -std=c++20 -fobjc-arc -O1 $root/macos/tests/shaders/metal_compile.mm -framework Foundation -framework Metal \
  -o $out/metal_compile || { overall=1; print -u2 "metal_compile does not build"; }
rm -rf $out/msl
for set in faf fa faforever engine; do
  for f in $out/spv/$set/**/*.spv(N); do
    [[ $f == *.raw.spv ]] && continue
    rel=${f#$out/spv/}
    mkdir -p $out/msl/${rel:h}
    cp $f $out/msl/$rel
  done
done
$sdk/bin/MoltenVKShaderConverter -d $out/msl -r -si -mo -q > $out/msl.log 2>&1 || { overall=1; print "MoltenVKShaderConverter failed, see $out/msl.log"; }
spvs=($out/msl/**/*.spv(N)); metals=($out/msl/**/*.metal(N))
print "MSL: ${#metals} of ${#spvs} modules converted"
(( ${#metals} == ${#spvs} )) || overall=1
$out/metal_compile $metals > $out/metal.log 2>&1 || overall=1
grep -E "^(METAL|FAIL)" $out/metal.log | head -20
print "run_effects: $([[ $overall == 0 ]] && print PASS || print FAIL)"
exit $overall
