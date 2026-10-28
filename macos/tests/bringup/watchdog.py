# watchdog.py <seconds> <cmd...>: runs cmd in its own process group; after <seconds> sends the
# group SIGTERM, then SIGKILL 5 s later. Exit status: the command's, or 124 when the time ran
# out. <seconds> <= 0 means no limit. Python stdlib only (run with python3 -I).
import os, signal, subprocess, sys

limit = float(sys.argv[1])
p = subprocess.Popen(sys.argv[2:], start_new_session=True)
try:
    rc = p.wait(timeout=limit if limit > 0 else None)
except subprocess.TimeoutExpired:
    try:
        os.killpg(p.pid, signal.SIGTERM)
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.wait()
    except ProcessLookupError:
        pass
    print("watchdog: stopped after %.0f s" % limit, file=sys.stderr, flush=True)
    rc = 124
except KeyboardInterrupt:
    os.killpg(p.pid, signal.SIGKILL)
    p.wait()
    rc = 130
sys.exit(rc if rc >= 0 else 128 - rc)
