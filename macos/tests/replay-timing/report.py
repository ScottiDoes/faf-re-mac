#!/usr/bin/env python3
"""Adds up run_set.py results (one results.json per machine or shard) as a Markdown table: each
run's machine, replays, jobs and wall time, the set's wall time (the slowest run, as they run side
by side) and the verdicts. Python 3.9 standard library only; run with `python3 -I`.

  report.py [--known-desyncs <file>] <results.json> ...

Exits 1 when a replay that is not in --known-desyncs (one replay id a line, `#` comments; games
that desynced when they were played live) does not pass, or a known one now passes, so the list
stays true.
"""
import argparse
import json
import sys


def clock(seconds):
    seconds = int(seconds)
    return f"{seconds // 3600}:{seconds // 60 % 60:02}:{seconds % 60:02}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--known-desyncs")
    parser.add_argument("results", nargs="*")
    args = parser.parse_args()
    known = set()
    if args.known_desyncs:
        with open(args.known_desyncs) as f:
            known = {line.split("#")[0].strip() for line in f if line.split("#")[0].strip()}
    runs = [json.load(open(path)) for path in args.results]
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
    played = {str(r["id"]): r.get("verdict") for run in runs for r in run["replays"]}
    new = sorted(i for i, v in played.items() if v != "pass" and i not in known)
    fixed = sorted(i for i in known if played.get(i) == "pass")
    missing = sorted(i for i in known if i not in played)
    if args.known_desyncs:
        print()
        if new:
            print(f"**Failed:** {', '.join(map(str, new))} did not pass and are not known desyncs.")
        if fixed:
            print(f"**Known desyncs that now pass:** {', '.join(map(str, fixed))}; take them off the list.")
        if missing:
            print(f"Known desyncs not played: {', '.join(map(str, missing))}.")
        if not new and not fixed:
            print(f"Every replay passed except the {len(known & set(played))} known desyncs.")
    return 1 if args.known_desyncs and (new or fixed) else 0


if __name__ == "__main__":
    sys.exit(main())
