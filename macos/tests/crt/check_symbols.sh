#!/bin/zsh
# check_symbols.sh <binary>...
#
# CI guard (macos/docs/DETERMINISM.md §4.4, §6.5): the executable must not take a libm
# function whose result can differ from the Windows build's static UCRT, nor rand/srand,
# from libSystem. Those must be defined in the executable itself (faf_crtmath and MSVC rand,
# src/platform/posix/crt), so ld64 binds every engine, Wild Magic and Lua reference to them.
#
# Reads `nm -m`: an undefined symbol "(from libSystem)" that libsystem_m exports (the SDK's
# libsystem_m.tbd) fails the check unless it is exact by definition (floor, ldexp, frexp,
# lrint, fma, nextafter, classification helpers, ...: same bits on both sides). rand and
# srand from libSystem fail too. fmin/fmax, fenv rounding control and other generators
# (random, rand_r, drand48, arc4random) are reported as warnings to review.
#
# Exit status: 0 clean, 1 a forbidden import, 2 usage or tool error.
set -euo pipefail
if (( $# == 0 )); then
  print -u2 "usage: ${0:t} <binary>..."
  exit 2
fi
sdk=$(xcrun --show-sdk-path)
tbd=$sdk/usr/lib/system/libsystem_m.tbd
[[ -f $tbd ]] || { print -u2 "${0:t}: no $tbd"; exit 2; }

exec python3 -I - "$tbd" "$@" <<'PY'
import re
import subprocess
import sys

tbd, binaries = sys.argv[1], sys.argv[2:]
libm = set(re.findall(r"'?(_[A-Za-z0-9_$]+)'?", open(tbd).read()))

# Exact by definition: identical bits from any correct implementation.
exact_roots = """fabs floor ceil trunc round rint nearbyint lrint llrint lround llround ldexp scalbn
scalbln frexp modf copysign fma nextafter nexttoward ilogb logb fdim nan""".split()
exact = set()
for root in exact_roots:
    for suffix in ("", "f", "l"):
        exact.add("_" + root + suffix)
exact_prefixes = ("___fpclassify", "___isfinite", "___isinf", "___isnan", "___isnormal", "___signbit",
                  "___inline_")
warn_libm = {"_fmin", "_fminf", "_fminl", "_fmax", "_fmaxf", "_fmaxl", "_fesetround", "_fesetenv",
             "_feupdateenv", "_feholdexcept"}
quiet_libm = {"_fegetround", "_fegetenv", "_feclearexcept", "_fetestexcept", "_feraiseexcept",
              "_fegetexceptflag", "_fesetexceptflag"}
forbidden_libc = {"_rand", "_srand"}
warn_libc = {"_random", "_srandom", "_rand_r", "_drand48", "_erand48", "_lrand48", "_nrand48", "_mrand48",
             "_jrand48", "_srand48", "_seed48", "_lcong48", "_arc4random", "_arc4random_uniform",
             "_arc4random_buf"}
# What src/platform/posix/crt defines (reported as found / missing for information).
owned = ["_" + n for n in """acos asin atan atan2 cos sin tan cosh sinh tanh exp log log10 pow fmod sqrt
acosf asinf atanf atan2f cosf sinf tanf coshf sinhf tanhf expf exp2f logf log2f log10f powf sqrtf
rand srand""".split()]

line_re = re.compile(r"^\s*(?:[0-9a-f]+\s+)?\((undefined|[^)]*)\)\s+(?:weak\s+)?(?:private\s+)?external\s+(\S+)(?:\s+\(from (\S+)\))?")

status = 0
for binary in binaries:
    try:
        out = subprocess.run(["nm", "-m", binary], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print("%s: nm failed: %s" % (binary, e), file=sys.stderr)
        sys.exit(2)
    undefined = {}
    defined = set()
    for line in out.splitlines():
        m = line_re.match(line)
        if not m:
            continue
        section, sym, lib = m.groups()
        if section == "undefined":
            undefined[sym] = lib or "?"
        else:
            defined.add(sym)
    bad, warnings = [], []
    for sym, lib in sorted(undefined.items()):
        if lib != "libSystem":
            continue
        if sym in forbidden_libc:
            bad.append((sym, "MSVC rand must be defined in the executable (crt/MsvcRand.cpp)"))
        elif sym in warn_libc:
            warnings.append((sym, "non-MSVC random generator: must not reach sim state"))
        elif sym in libm:
            if sym in exact or sym.startswith(exact_prefixes) or sym in quiet_libm:
                continue
            if sym in warn_libm:
                warnings.append((sym, "review: signed-zero/rounding-mode semantics (DETERMINISM.md §5)"))
            else:
                bad.append((sym, "libm result may differ from UCRT 10.0.22000: define it in faf_crtmath"))
    print("%s: %d libSystem imports checked" % (binary, sum(1 for l in undefined.values() if l == "libSystem")))
    print("  defined here: %s" % " ".join(s[1:] for s in owned if s in defined))
    for sym, why in bad:
        print("  FORBIDDEN %-16s %s" % (sym[1:], why))
    for sym, why in warnings:
        print("  warning   %-16s %s" % (sym[1:], why))
    if bad:
        status = 1
    else:
        print("  ok: no libm/rand result from libSystem that the UCRT could disagree with")
sys.exit(status)
PY
