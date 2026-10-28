# watchdog.py <seconds> <cmd...>: runs cmd, kills its process group after <seconds>.
import os, signal, subprocess, sys
limit = float(sys.argv[1])
p = subprocess.Popen(sys.argv[2:], start_new_session=True)
try:
    rc = p.wait(timeout=limit)
except subprocess.TimeoutExpired:
    os.killpg(p.pid, signal.SIGKILL)
    p.wait()
    print("WATCHDOG: killed after %.0f s" % limit, flush=True)
    rc = 124
sys.exit(rc)
