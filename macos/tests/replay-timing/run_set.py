#!/usr/bin/env python3
"""Plays a set of replays with macos/run_replay.sh, --jobs at a time, and times it: how long the
whole set took on this machine, and each replay's wall time and verdict. Python 3.9 standard
library only; run with `python3 -I`. The same tool runs on a Mac at home and on GitHub's Macs.

  run_set.py --replays <folder of <id>.scfareplay> --out <folder> [--jobs N] [--shard i/n]
             [--list selected.json] [--speed 50] [--maxmin 60]

Environment: FAF_RUN_DIR (the run folder) and FAF_REPLAY_EXE (the runner), as run_replay.sh
reads them. Replays are started longest game first (--list gives the lengths), so the set ends
with the short ones. --shard i/n plays every n'th replay of that order, starting at i, for one
of n machines. Writes <out>/results.json and prints a table and the totals.
"""
import argparse
import concurrent.futures
import json
import os
import platform
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RUN_REPLAY = os.path.normpath(os.path.join(HERE, "..", "..", "run_replay.sh"))


def machine():
    def sysctl(name):
        try:
            return subprocess.run(["sysctl", "-n", name], capture_output=True, text=True).stdout.strip()
        except OSError:
            return ""
    return {"model": sysctl("hw.model"), "chip": sysctl("machdep.cpu.brand_string"),
            "cores": os.cpu_count(), "memory_gb": round(int(sysctl("hw.memsize") or 0) / 2**30),
            "macos": platform.mac_ver()[0]}


def play(replay, out, args):
    replay_id = os.path.basename(replay).split(".")[0]
    run_out = os.path.join(out, "runs", replay_id)
    os.makedirs(run_out, exist_ok=True)
    env = dict(os.environ, REPLAY=replay, OUT=run_out, SPEED=str(args.speed), MAXMIN=str(args.maxmin),
               SDL_AUDIO_DRIVER="dummy")
    start = time.monotonic()
    with open(os.path.join(run_out, "run.txt"), "w") as log:
        code = subprocess.run([RUN_REPLAY], env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    wall = time.monotonic() - start
    row = {"id": replay_id, "wall_s": round(wall, 1), "exit": code}
    result = os.path.join(run_out, f"replay-{replay_id}.result")
    if os.path.exists(result):
        text = open(result).read()
        for key in ("tick", "recorded", "checked", "mismatches", "verdict", "exit"):
            match = re.search(rf"\b{key}=(\S+)", text)
            if match:
                row["end" if key == "exit" else key] = match.group(1)
    else:
        row["verdict"] = "no-result"
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--replays", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--list")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--shard", default="0/1")
    parser.add_argument("--speed", type=int, default=50)
    parser.add_argument("--maxmin", type=int, default=60)
    args = parser.parse_args()
    for name in ("FAF_RUN_DIR", "FAF_REPLAY_EXE"):
        if not os.environ.get(name):
            parser.error(f"set {name}")

    info = {}
    if args.list:
        for row in json.load(open(args.list)):
            info[str(row["id"])] = row
    files = sorted(f for f in os.listdir(args.replays) if f.endswith(".scfareplay"))
    if info:
        files = [f for f in files if f.split(".")[0] in info]
    files.sort(key=lambda f: -info.get(f.split(".")[0], {}).get("minutes", 0))
    index, count = (int(x) for x in args.shard.split("/"))
    files = files[index::count]
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    print(f"{len(files)} replays, {args.jobs} at a time, on {machine()}", flush=True)
    start = time.monotonic()
    rows = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(play, os.path.join(args.replays, f), out, args) for f in files]
        for future in concurrent.futures.as_completed(futures):
            row = future.result()
            row.update({k: info[row["id"]][k] for k in ("map", "players", "minutes") if row["id"] in info})
            rows.append(row)
            print(f"  {len(rows):3}/{len(files)} {row['id']} {row.get('verdict')} "
                  f"{row.get('checked', '?')}/{row.get('recorded', '?')} in {row['wall_s']:.0f} s", flush=True)
    total = time.monotonic() - start

    game_minutes = sum(r.get("minutes", 0) for r in rows)
    verdicts = {}
    for r in rows:
        verdicts[r.get("verdict")] = verdicts.get(r.get("verdict"), 0) + 1
    summary = {"machine": machine(), "jobs": args.jobs, "shard": args.shard, "speed": args.speed,
               "replays": len(rows), "wall_s": round(total, 1),
               "replay_wall_s": round(sum(r["wall_s"] for r in rows), 1),
               "game_minutes": round(game_minutes, 1), "verdicts": verdicts}
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump({"summary": summary, "replays": sorted(rows, key=lambda r: r["id"])}, f, indent=1)
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
