#!/usr/bin/env python3
"""Adds up run_set.py results (one results.json per machine or shard) as a Markdown table: each
run's machine, replays, jobs and wall time, the set's wall time (the slowest run, as they run side
by side) and the verdicts. Python 3.9 standard library only; run with `python3 -I`.

  report.py <results.json> ...
"""
import json
import sys


def clock(seconds):
    seconds = int(seconds)
    return f"{seconds // 3600}:{seconds // 60 % 60:02}:{seconds % 60:02}"


def main():
    runs = [json.load(open(path)) for path in sys.argv[1:]]
    if not runs:
        print("No results.")
        return 1
    print("| Run | Machine | Replays | At a time | Wall time | Replay time summed |")
    print("|---|---|---|---|---|---|")
    verdicts, replays = {}, 0
    for run in sorted(runs, key=lambda r: r["summary"]["shard"]):
        s = run["summary"]
        m = s["machine"]
        print(f"| {s['shard']} | {m['chip']} ({m['cores']} cores, {m['memory_gb']} GB), {m['model']} "
              f"| {s['replays']} | {s['jobs']} | {clock(s['wall_s'])} | {clock(s['replay_wall_s'])} |")
        replays += s["replays"]
        for verdict, n in s["verdicts"].items():
            verdicts[verdict] = verdicts.get(verdict, 0) + n
    wall = max(r["summary"]["wall_s"] for r in runs)
    print()
    print(f"**{replays} replays in {clock(wall)}** (the slowest of {len(runs)} runs side by side). "
          f"Verdicts: {', '.join(f'{n} {v}' for v, n in sorted(verdicts.items(), key=lambda x: -x[1]))}.")
    failed = [r for run in runs for r in run["replays"] if r.get("verdict") != "pass"]
    if failed:
        print()
        print("| Replay | Map | Verdict | Checked/recorded | Mismatches |")
        print("|---|---|---|---|---|")
        for r in sorted(failed, key=lambda r: r["id"]):
            print(f"| {r['id']} | {r.get('map', '')} | {r.get('verdict')} | {r.get('checked', '?')}/{r.get('recorded', '?')} "
                  f"| {r.get('mismatches', '?')} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
