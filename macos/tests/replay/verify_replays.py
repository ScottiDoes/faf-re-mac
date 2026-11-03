#!/usr/bin/env python3
"""The driver of macos/verify_replays.sh (read that file's header for usage). Python 3.9
standard library only; run with `python3 -I`.

Steps, each reported with PASS / FAIL / SKIP:
  1. inputs     every replay: a .fafreplay is converted into OUT/converted first; replay_tool.py
                reads its header and command stream (map, players, recorded checksums, how many
                the sim compares). A replay whose featured mod or game version is not the one the
                run folder mounts is skipped with a note (it would not load, or it would desync).
  2. runs       macos/run_replay.sh (SPEED, /simworkers N for N in WORKERS) under a watchdog. A
                run passes with exit status 0 and verdict=pass, recorded and checked equal to
                replay_tool.py's independent count, and the same final tick for every N.
  3. negctl     one replay with one digest byte flipped at its middle checksum beat in every
                player's record: expect exit status 1, mismatches = the records at that beat, every
                mismatch line naming that beat, and the sim's digest in each line equal to the
                record's digest before the flip (the sim reproduced the players' checksum).
     nochecks   the same replay with every checksum record removed: the runner must not call a
                run that compared nothing a pass (exit status 2, verdict=unverified, checked=0).
  4. synccheck  one replay with SYNCLOG=1: the sim's own digest of every checksum beat against
                every recorded digest (replay_tool.py synccheck), without the warning path.
  5. postlink   check_postlink.py on the executable (map order, initializer order, the
                preregister section, libm imports, fused multiply-add).
  6. unittest   build_unit_test.sh: the runner's result, checksum count and FPCR code.
"""
import json
import os
import re
import shlex
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TOOL = os.path.join(HERE, "replay_tool.py")
WATCHDOG = os.path.join(ROOT, "macos", "tests", "bringup", "watchdog.py")
RUN_REPLAY = os.path.join(ROOT, "macos", "run_replay.sh")
HOME = os.path.expanduser("~")
# What macos/run_replay.sh runs with: the run folder (macos/dev_paths.sh), whose init.lua mounts
# ../gamedata.
RUN_FOLDER = os.path.abspath(os.environ.get("FAF_RUN_DIR") or os.path.join(ROOT, "build", "run", "bin"))
GAMEDATA = os.path.join(RUN_FOLDER, "..", "gamedata")
FAF_REPLAYS = os.path.join(os.environ.get("FAF_CLIENT_DIR") or os.path.join(HOME, ".faforever"), "replays")  # read only

steps = []  # (step, name, status, detail)


def note(step, name, status, detail):
    steps.append((step, name, status, detail))
    print("%-9s %-24s %-4s %s" % (step, name, status, detail), flush=True)


def env_list(name, default):
    return os.environ.get(name, default).split()


def tool(*args):
    p = subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def installed_version():
    try:
        text = zipfile.ZipFile(os.path.join(GAMEDATA, "lua.nx2")).read("lua/version.lua").decode("latin1")
    except (OSError, KeyError, zipfile.BadZipFile):
        return None
    m = re.search(r'^local Version = "(\d+)"', text, re.M)
    return m.group(1) if m else None


def featured_mod(path, replay_id, meta_path, map_path):
    """(featured mod, where it came from). Never writes anything."""
    candidates = []
    if meta_path:
        candidates.append(meta_path)
    candidates.append(os.path.splitext(path)[0] + ".json")
    for meta in candidates:
        try:
            with open(meta) as f:
                mod = json.load(f).get("featured_mod")
            if mod:
                return mod, os.path.basename(meta)
        except (OSError, ValueError):
            pass
    if replay_id.isdigit() and os.path.isdir(FAF_REPLAYS):
        for name in sorted(os.listdir(FAF_REPLAYS)):
            if name.startswith(replay_id + "-") and name.endswith(".fafreplay"):
                try:
                    with open(os.path.join(FAF_REPLAYS, name), "rb") as f:
                        mod = json.loads(f.readline()).get("featured_mod")
                    if mod:
                        return mod, "~/.faforever/replays/" + name
                except (OSError, ValueError):
                    pass
    if "coop" in map_path.lower():
        return "coop", "the map name"
    return "faf", "assumed (no metadata found)"


def replay_id_of(path):
    m = re.search(r"\d+", os.path.splitext(os.path.basename(path))[0])
    return m.group(0) if m else os.path.splitext(os.path.basename(path))[0].replace(" ", "_")


def short_map(map_path):
    parts = [p for p in map_path.replace("\\", "/").split("/") if p]
    return parts[-2] if len(parts) >= 2 else map_path


def parse_result(path):
    try:
        with open(path) as f:
            for line in f:
                if line.startswith("FAF_REPLAY_RESULT"):
                    return dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
    except OSError:
        pass
    return {}


def sim_rate(log):
    try:
        with open(log, errors="replace") as f:
            for line in f:
                m = re.search(r"faf_replay: sim rate: .* = ([0-9.]+)x real time", line)
                if m:
                    return float(m.group(1))
    except OSError:
        pass
    return None


def run_replay(exe, replay, replay_id, out, speed, workers, maxmin, extra_env=None):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, FAF_REPLAY_EXE=exe, REPLAY=replay, REPLAYID=replay_id, SPEED=str(speed),
               MAXMIN=str(maxmin), OUT=out)
    env.pop("SYNCLOG", None)
    env.pop("UI", None)
    env.pop("EXITONGAMEOVER", None)
    if extra_env:
        env.update(extra_env)
    cmd = [sys.executable, "-I", WATCHDOG, str(maxmin * 60 + 120), "zsh", RUN_REPLAY, "/simworkers", str(workers)]
    start = time.monotonic()
    with open(os.path.join(out, "run.out"), "w") as log:
        rc = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    wall = time.monotonic() - start
    result = parse_result(os.path.join(out, "replay-%s.result" % replay_id))
    return rc, result, wall, sim_rate(os.path.join(out, "replay-%s.log" % replay_id))


def as_int(text, default=-2):
    try:
        return int(text)
    except (TypeError, ValueError):
        return default


def main():
    exe = os.path.abspath(os.environ.get("FAF_REPLAY_EXE", os.path.join(ROOT, "build", "macos", "faf_replay")))
    out = os.path.abspath(os.environ.get("OUT", os.path.join(ROOT, "build", "verify-replays")))
    speed = int(os.environ.get("SPEED", "50"))
    workers = [int(w) for w in env_list("WORKERS", "0 8")]
    maxmin = int(os.environ.get("MAXMIN", "20"))
    allowed_mods = set(env_list("FEATURED_MODS", "faf ladder1v1"))
    negctl_choice = os.environ.get("NEGCTL", "")
    synccheck_choice = os.environ.get("SYNCCHECK", "")
    replays = sys.argv[1:]
    if not replays:
        corpus = os.environ.get("FAF_REPLAY_CORPUS", "")
        replays = sorted(os.path.join(corpus, f) for f in os.listdir(corpus) if f.endswith(".scfareplay")) \
            if corpus and os.path.isdir(corpus) else []
    if not replays:
        print("verify_replays: no replays given and none in FAF_REPLAY_CORPUS", file=sys.stderr)
        return 2
    if not os.access(exe, os.X_OK):
        print("verify_replays: no runner at %s" % exe, file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    version = installed_version()
    print("verify_replays: %s" % exe)
    print("verify_replays: output in %s; SPEED=%d, /simworkers %s; installed gamedata version %s" % (
        out, speed, " and ".join(map(str, workers)), version or "unknown"))

    # 1. inputs
    entries = []
    for path in replays:
        path = os.path.abspath(path)
        replay_id = replay_id_of(path)
        meta = None
        if path.endswith(".fafreplay"):
            conv = os.path.join(out, "converted")
            os.makedirs(conv, exist_ok=True)
            dst = os.path.join(conv, "%s.scfareplay" % replay_id)
            meta = os.path.join(conv, "%s.json" % replay_id)
            rc, so, se = tool("convert", path, dst, "--meta", meta)
            if rc != 0:
                note("inputs", replay_id, "FAIL", "cannot convert: %s" % se.strip())
                continue
            path = dst
        rc, so, se = tool("info", "--json", path)
        if rc != 0:
            note("inputs", replay_id, "FAIL", "cannot read: %s" % se.strip())
            continue
        info = json.loads(so)
        mod, source = featured_mod(path, replay_id, meta, info["map"])
        entry = {"id": replay_id, "path": path, "info": info, "mod": mod, "runs": {}}
        if mod not in allowed_mods:
            entry["skip"] = "featured mod %s (from %s) is not installed; the run folder mounts FAF's %s data" % (
                mod, source, "/".join(sorted(allowed_mods)))
        elif version and info["game_version"] != version:
            entry["skip"] = "recorded on game version %s; the installed gamedata is %s (it would desync)" % (
                info["game_version"], version)
        elif info["truncated"]:
            entry["note"] = "stream: %s" % info["truncated"]
        if info["recorded"] == 0 and "skip" not in entry:
            entry["note"] = "the replay holds no player checksum: it cannot pass (verdict unverified)"
        note("inputs", replay_id, "SKIP" if "skip" in entry else "ok",
             entry.get("skip") or "%s, %d players, %d recorded checksums (%d compared in a full run), %d beats" % (
                 short_map(info["map"]), len(info["sources"]), info["recorded"], info["compared"],
                 info["stream_beats"]))
        entries.append(entry)

    # 2. runs
    for entry in entries:
        if "skip" in entry:
            continue
        info = entry["info"]
        for n in workers:
            run_dir = os.path.join(out, "runs", "%s-w%d" % (entry["id"], n))
            rc, res, wall, rate = run_replay(exe, entry["path"], entry["id"], run_dir, speed, n, maxmin)
            problems = []
            if rc != 0:
                problems.append("exit status %d" % rc)
            if res.get("verdict") != "pass":
                problems.append("verdict %s" % res.get("verdict", "none (no result line)"))
            if as_int(res.get("recorded")) != info["recorded"]:
                problems.append("recorded=%s, replay_tool counts %d" % (res.get("recorded"), info["recorded"]))
            if as_int(res.get("checked")) != info["compared"]:
                problems.append("checked=%s, replay_tool says a full run compares %d" % (
                    res.get("checked"), info["compared"]))
            entry["runs"][n] = {"rc": rc, "result": res, "wall": wall, "rate": rate, "dir": run_dir,
                                "problems": problems}
            note("run", "%s /simworkers %d" % (entry["id"], n), "PASS" if not problems else "FAIL",
                 "%s; %.0f s wall%s" % (" ".join("%s=%s" % (k, res.get(k)) for k in (
                     "tick", "recorded", "checked", "mismatches", "exit", "verdict")) if res else "no result line",
                     wall, "" if not problems else "; " + "; ".join(problems)))
        ticks = {r["result"].get("tick") for r in entry["runs"].values()}
        if len(entry["runs"]) > 1:
            same = len(ticks) == 1
            if not same:
                for r in entry["runs"].values():
                    r["problems"].append("final tick differs between worker settings")
            note("run", "%s ticks" % entry["id"], "PASS" if same else "FAIL",
                 "final tick %s for /simworkers %s" % (" / ".join(str(r["result"].get("tick")) for r in
                                                         entry["runs"].values()), " / ".join(map(str, entry["runs"]))))

    passed = [e for e in entries if e["runs"] and all(not r["problems"] for r in e["runs"].values())]

    def pick(choice, label):
        if choice == "0":
            return None
        if choice:
            hit = [e for e in entries if e["id"] == choice and "skip" not in e]
            if not hit:
                note(label, choice, "FAIL", "%s=%s names no replay that ran" % (label.upper(), choice))
            return hit[0] if hit else None
        # Default: the fastest passing replay that at least two players recorded checksums in.
        multi = [e for e in passed if e["info"]["players"] >= 2] or passed
        return min(multi, key=lambda e: e["info"]["stream_beats"]) if multi else None

    # 3. negative control
    neg = pick(negctl_choice, "negctl")
    if neg is not None:
        info = neg["info"]
        beat = info["mid_checksum_beat"]
        neg_dir = os.path.join(out, "negctl")
        os.makedirs(neg_dir, exist_ok=True)
        bad_path = os.path.join(neg_dir, "%s-flipped-%d.scfareplay" % (neg["id"], beat))
        rc, so, se = tool("corrupt", neg["path"], bad_path, str(beat), "--json")
        flip = json.loads(so) if rc == 0 else None
        if flip is None:
            note("negctl", neg["id"], "FAIL", "cannot corrupt: %s" % se.strip())
        else:
            expected = flip["compared"]
            rc, res, wall, rate = run_replay(exe, bad_path, neg["id"], os.path.join(neg_dir, "run"), speed, 0, maxmin)
            log = os.path.join(neg_dir, "run", "replay-%s.log" % neg["id"])
            lines = []
            try:
                with open(log, errors="replace") as f:
                    lines = re.findall(r"Checksum for beat (\d+) mismatched: ([0-9a-f]{32}) \(sim\) != ([0-9a-f]{32})", f.read())
            except OSError:
                pass
            originals = {r["digest"] for r in flip["records"]}
            flipped = {r["flipped_digest"] for r in flip["records"]}
            problems = []
            if rc != 1 or res.get("verdict") != "mismatch":
                problems.append("exit status %d, verdict %s (want 1, mismatch)" % (rc, res.get("verdict")))
            if as_int(res.get("mismatches")) != expected:
                problems.append("mismatches=%s, want %d" % (res.get("mismatches"), expected))
            if len(lines) != expected or any(int(b) != beat for b, _, _ in lines):
                problems.append("%d mismatch lines in the log, beats %s" % (len(lines), sorted({b for b, _, _ in lines})))
            if any(simd not in originals or recd not in flipped for _, simd, recd in lines):
                problems.append("a mismatch line's digests are not the recorded and the flipped digest")
            note("negctl", neg["id"], "PASS" if not problems else "FAIL",
                 "digest byte 0 flipped at beat %d in %d players' records -> exit %d, mismatches=%s, %d log lines at "
                 "beat %d, sim digest = recorded digest %s%s" % (
                     beat, flip["flipped"], rc, res.get("mismatches"), len(lines), beat,
                     sorted(originals)[0] if len(originals) == 1 else sorted(originals),
                     "" if not problems else "; " + "; ".join(problems)))

        # The other control: nothing to compare must not look like a pass.
        stripped = os.path.join(neg_dir, "%s-no-checksums.scfareplay" % neg["id"])
        rc, so, se = tool("strip", neg["path"], stripped)
        if rc != 0:
            note("nochecks", neg["id"], "FAIL", "cannot strip: %s" % se.strip())
        else:
            rc, res, wall, rate = run_replay(exe, stripped, neg["id"], os.path.join(neg_dir, "nochecks"), speed, 0,
                                             maxmin)
            ok = (rc == 2 and res.get("verdict") == "unverified" and res.get("checked") == "0" and
                  res.get("recorded") == "0" and res.get("mismatches") == "0" and
                  res.get("exit") in ("end-of-replay", "gameover"))
            note("nochecks", neg["id"], "PASS" if ok else "FAIL",
                 "every checksum record removed -> exit %d, %s" % (rc, " ".join("%s=%s" % (k, res.get(k)) for k in (
                     "tick", "recorded", "checked", "mismatches", "exit", "verdict"))))

    # 4. synccheck
    sync = pick(synccheck_choice, "synccheck") if synccheck_choice != "0" else None
    if sync is not None:
        sync_dir = os.path.join(out, "synccheck")
        rc, res, wall, rate = run_replay(exe, sync["path"], sync["id"], sync_dir, speed, 0, maxmin,
                                         {"SYNCLOG": "1"})
        sums = os.path.join(sync_dir, "checksums-%s.txt" % sync["id"])
        src, so, se = tool("synccheck", sync["path"], sums, "--synclog", os.path.join(sync_dir, "synclog-%s" % sync["id"]))
        ok = rc == 0 and src == 0
        note("synccheck", sync["id"], "PASS" if ok else "FAIL",
             (so.strip() or se.strip()).replace("synccheck: ", "") + ("" if rc == 0 else "; run exit status %d" % rc))

    # 5. post-link checks
    if os.environ.get("POSTLINK", "1") != "0":
        p = subprocess.run([sys.executable, "-I", os.path.join(HERE, "check_postlink.py"), exe],
                           capture_output=True, text=True)
        with open(os.path.join(out, "postlink.txt"), "w") as f:
            f.write(p.stdout + p.stderr)
        for line in p.stdout.splitlines()[1:-1]:
            m = re.match(r"^(\S+)\s+(PASS|FAIL|SKIP)\s+(.*)$", line)
            if m:
                note("postlink", m.group(1), m.group(2), m.group(3))
        if p.returncode != 0 and not any(s[0] == "postlink" and s[2] == "FAIL" for s in steps):
            note("postlink", "check_postlink.py", "FAIL", (p.stderr.strip() or p.stdout.strip())[-300:])

    # 6. unit test
    if os.environ.get("UNITTEST", "1") != "0":
        tree = os.path.dirname(exe)
        p = subprocess.run(["zsh", os.path.join(HERE, "build_unit_test.sh"), tree],
                           env=dict(os.environ, OUT=os.path.join(out, "unittest")), capture_output=True, text=True)
        with open(os.path.join(out, "unittest.txt"), "w") as f:
            f.write(p.stdout + p.stderr)
        oks = sum(1 for l in p.stdout.splitlines() if l.rstrip().endswith(" ok"))
        fails = [l.split("  ")[0].strip() for l in p.stdout.splitlines() if l.rstrip().endswith("FAIL")]
        note("unittest", "runner_unit_test", "PASS" if p.returncode == 0 else "FAIL",
             "%d scenarios ok%s" % (oks, "" if not fails else "; FAIL: " + ", ".join(fails)) if oks or fails
             else (p.stderr.strip()[-300:] or "did not build"))

    # The table.
    header = ("replay", "map", "players", "recorded", "checked", "mismatches", "tick", "x real", "workers",
              "verdict", "wall s")
    rows = []
    for e in entries:
        info = e["info"]
        if "skip" in e:
            rows.append((e["id"], short_map(info["map"]), str(len(info["sources"])), str(info["recorded"]), "-", "-",
                         "-", "-", "-", "skipped", "-"))
            continue
        for n, r in e["runs"].items():
            res = r["result"]
            rows.append((e["id"], short_map(info["map"]), str(len(info["sources"])), res.get("recorded", "?"),
                         res.get("checked", "?"), res.get("mismatches", "?"), res.get("tick", "?"),
                         "%.1f" % r["rate"] if r["rate"] else "?", str(n),
                         res.get("verdict", "none") + ("" if not r["problems"] else " (FAIL)"), "%.0f" % r["wall"]))
    widths = [max(len(h), *(len(row[i]) for row in rows)) if rows else len(h) for i, h in enumerate(header)]
    table = ["  ".join(h.ljust(w) for h, w in zip(header, widths))]
    table += ["  ".join(c.ljust(w) for c, w in zip(row, widths)) for row in rows]
    notes = ["%s: %s" % (e["id"], e["skip"]) for e in entries if "skip" in e]
    notes += ["%s: %s" % (e["id"], e["note"]) for e in entries if "note" in e]
    failed = [s for s in steps if s[2] == "FAIL"]
    ran = sum(len(e["runs"]) for e in entries)
    lines = ["", "verify_replays: %s" % exe, "  players = command sources (human players); recorded = player checksums "
             "in the replay; checked = the ones the sim compared; x real = game time / wall time between the first and "
             "last tick (SPEED=%d)" % speed, ""] + table
    if notes:
        lines += [""] + ["  skipped/notes: " + n for n in notes]
    lines += ["", "verify_replays: %d runs, %d steps failed: %s" % (
        ran, len(failed), "PASS" if not failed and ran else ("FAIL" if failed else "NOTHING RAN"))]
    for s in failed:
        lines.append("  FAIL %s %s: %s" % (s[0], s[1], s[3]))
    print("\n".join(lines))
    with open(os.path.join(out, "RESULTS.txt"), "w") as f:
        f.write("\n".join(lines[1:]) + "\n\nSteps:\n" + "\n".join("%-9s %-24s %-4s %s" % s for s in steps) + "\n")
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump({"exe": exe, "speed": speed, "workers": workers, "steps": steps,
                   "replays": [{k: v for k, v in e.items() if k != "runs"} | {"runs": {str(n): {
                       k: v for k, v in r.items()} for n, r in e["runs"].items()}} for e in entries]},
                  f, indent=1, sort_keys=True, default=str)
    if failed:
        return 1
    return 0 if ran else 2


if __name__ == "__main__":
    sys.exit(main())
