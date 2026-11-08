#!/usr/bin/env python3
"""Picks FAF vault replays of the installed game version for a replay-timing run and downloads
them with their maps. Python 3.9 standard library only; run with `python3 -I`.

  pick_replays.py --out <data folder> [--count 100] [--budget-mb 650] [--start-id N]

Steps, each resumable (what a step already has on disk is not fetched again):
  1. scan:   reads the JSON header of every --step'th replay id downward from --start-id
             (the first 16 KB, one request a second) into <out>/headers.jsonl
  2. choose: keeps `faf` games without sim mods in their FAF header that ended, with 2 or more players and --min-minutes to
             --max-minutes of game on a map that can be had (not a generated one), and takes
             --count of them, maps already on disk first, then the maps most games share, so
             few maps are downloaded
  3. fetch:  downloads each chosen replay (<out>/replays/<id>.fafreplay), converts it
             (<out>/replays/<id>.scfareplay; macos/tests/replay/replay_tool.py) and drops it
             unless its engine version is --version and the sim mod table inside it is empty
             (base-game content only: no vault AIs, no Total Mayhem; read with
             tools/replay-parity/replayfile.py); downloads each missing map into
             <out>/vault/maps/<folder>, stopping at --budget-mb in all
  4. writes <out>/selected.json (id, map, players, minutes)
Maps already on disk: the game's own (--fa-dir) need nothing, as the run folder mounts them;
the FAF vault's (--vault) are copied into <out>/vault/maps. Only --out is written to.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

AGENT = "faf-native-replay-timing (macOS port test)"
REPLAY_URL = "https://replay.faforever.com/{}"
MAP_URL = "https://content.faforever.com/maps/{}.zip"
HERE = os.path.dirname(os.path.abspath(__file__))
REPLAY_TOOL = os.path.join(HERE, "..", "replay", "replay_tool.py")

_last_request = 0.0


def request(url, byte_range=None):
    """GET with one request a second at most; returns the body, or None for a 404."""
    global _last_request
    wait = _last_request + 1.0 - time.monotonic()
    if wait > 0:
        time.sleep(wait)
    headers = {"User-Agent": AGENT}
    if byte_range:
        headers["Range"] = "bytes=0-{}".format(byte_range - 1)
    for attempt in range(3):
        _last_request = time.monotonic()
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as reply:
                return reply.read()
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            if attempt == 2:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
        time.sleep(5 * (attempt + 1))
    return None


def scan(args, headers_path):
    seen = {}
    if os.path.exists(headers_path):
        with open(headers_path) as f:
            for line in f:
                row = json.loads(line)
                seen[row["id"]] = row
    usable = sum(1 for row in seen.values() if usable_reason(row, args) is None)
    with open(headers_path, "a") as out:
        replay_id = args.start_id
        probes = 0
        while probes < args.max_probes and usable < args.want_candidates:
            if replay_id not in seen:
                body = request(REPLAY_URL.format(replay_id), byte_range=16384)
                row = {"id": replay_id, "missing": body is None}
                if body is not None:
                    line = body.split(b"\n", 1)[0]
                    try:
                        header = json.loads(line)
                        row.update({k: header.get(k) for k in (
                            "featured_mod", "mapname", "complete", "num_players", "sim_mods",
                            "launched_at", "game_end", "game_type")})
                    except ValueError:
                        row["unreadable"] = True
                out.write(json.dumps(row) + "\n")
                out.flush()
                seen[replay_id] = row
                probes += 1
                if usable_reason(row, args) is None:
                    usable += 1
                if probes % 50 == 0:
                    print(f"scan: {probes} read, {usable} usable, at id {replay_id}", flush=True)
            replay_id -= args.step
    print(f"scan: {usable} usable replays in {len(seen)} headers", flush=True)
    return list(seen.values())


def usable_reason(row, args):
    """None when the replay fits the run, else why not."""
    if row.get("missing") or row.get("unreadable"):
        return "no replay"
    if row.get("featured_mod") != "faf":
        return "featured mod"
    if not row.get("complete"):
        return "incomplete"
    if (row.get("num_players") or 0) < 2:
        return "players"
    if row.get("sim_mods"):
        return "sim mods"
    if not row.get("launched_at") or not row.get("game_end"):
        return "no length"
    minutes = (row["game_end"] - row["launched_at"]) / 60
    if minutes < args.min_minutes or minutes > args.max_minutes:
        return "length"
    mapname = row.get("mapname") or ""
    if not mapname or mapname.lower().startswith("neroxis_map_generator"):
        return "generated map"
    return None


def local_maps(args):
    found = {}
    for root in (os.path.join(args.fa_dir, "maps"), os.path.join(args.vault, "maps")):
        if os.path.isdir(root):
            for name in os.listdir(root):
                if os.path.isdir(os.path.join(root, name)):
                    found.setdefault(name.lower(), os.path.join(root, name))
    return found


def safe_extract(archive, dest):
    """Extracts a map zip below dest; returns the top folder names it held."""
    tops = set()
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = info.filename.replace("\\", "/").split("/")
            if info.filename.startswith(("/", "\\")) or ".." in parts or ":" in info.filename:
                raise ValueError(f"unsafe path in {archive}: {info.filename}")
            if parts[0]:
                tops.add(parts[0])
        z.extractall(dest)
    return tops


def convert(fafreplay, scfareplay):
    """Converts; returns the engine version (e.g. '3840') or None."""
    result = subprocess.run([sys.executable, "-I", REPLAY_TOOL, "convert", fafreplay, scfareplay],
                            capture_output=True, text=True)
    match = re.search(r"v1\.50\.(\d+)", result.stdout)
    return match.group(1) if result.returncode == 0 and match else None


def sim_mods_problem(scfareplay):
    """None when the replay's own sim mod table is empty, else what it holds. The FAF header's
    sim_mods can leave mods out (vault AIs such as M28AI), so the table in the body decides."""
    sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "replay-parity"))
    from replayfile import lua_value
    with open(scfareplay, "rb") as f:
        data = f.read()
    # The map line, then "\r\n\x1a\0" (vault files) or "\r\n\0" (replay_tool.py convert), the
    # table's size and the table: { [uid] = { name = ..., ... }, ... }.
    match = re.search(rb"Replay v[\d.]+\r\n[^\0]*\0\r\n\x1a?\0", data[:8192])
    if not match:
        return "unreadable mod table (no map line)"
    try:
        mods, _ = lua_value(data, match.end() + 4)
    except (ValueError, IndexError, KeyError) as error:
        return f"unreadable mod table ({error})"
    if not isinstance(mods, dict):
        return "unreadable mod table (not a table)"
    names = [str(v.get("name")) for v in mods.values() if isinstance(v, dict)]
    return "sim mods " + ", ".join(names) if mods else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", required=True)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--version", default="3840")
    parser.add_argument("--budget-mb", type=float, default=650)
    parser.add_argument("--start-id", type=int, default=28085000)
    parser.add_argument("--step", type=int, default=11)
    parser.add_argument("--max-probes", type=int, default=3000)
    parser.add_argument("--want-candidates", type=int, default=600)
    parser.add_argument("--min-minutes", type=float, default=5)
    parser.add_argument("--max-minutes", type=float, default=90)
    parser.add_argument("--fa-dir", default=os.path.expanduser(
        "~/Gaming/Forged Alliance Forever/Supreme Commander Forged Alliance"))
    parser.add_argument("--vault", default=os.path.expanduser(
        "~/My Games/Gas Powered Games/Supreme Commander Forged Alliance"))
    args = parser.parse_args()

    out = os.path.abspath(args.out)
    for sub in ("replays", "dl", os.path.join("vault", "maps")):
        os.makedirs(os.path.join(out, sub), exist_ok=True)

    rows = scan(args, os.path.join(out, "headers.jsonl"))
    candidates = [r for r in rows if usable_reason(r, args) is None]
    have = local_maps(args)
    # The game's own maps: the run folder (and a pack's fa/maps) mounts them already.
    retail_dir = os.path.join(args.fa_dir, "maps")
    retail = {n.lower() for n in os.listdir(retail_dir)} if os.path.isdir(retail_dir) else set()
    by_map = {}
    for row in candidates:
        by_map.setdefault(row["mapname"].lower(), []).append(row)
    # Maps on disk first, then the maps most candidates share; newest games first within a map.
    order = sorted(by_map, key=lambda m: (m not in have, -len(by_map[m]), m))

    vault_maps = os.path.join(out, "vault", "maps")
    spent = sum(os.path.getsize(os.path.join(out, "dl", f)) for f in os.listdir(os.path.join(out, "dl")))
    selected, failed_maps = [], set()
    for mapname in order:
        if len(selected) >= args.count:
            break
        target = os.path.join(vault_maps, mapname)
        if not os.path.exists(target) and mapname not in retail:
            if mapname in have:
                # A copy, not a link: make_pack.py's tree walk does not follow links.
                shutil.copytree(have[mapname], target)
            else:
                if spent / 1e6 >= args.budget_mb:
                    continue
                archive = os.path.join(out, "dl", mapname + ".zip")
                if not os.path.exists(archive):
                    body = request(MAP_URL.format(urllib.parse.quote(mapname)))
                    if body is None:
                        failed_maps.add(mapname)
                        continue
                    with open(archive, "wb") as f:
                        f.write(body)
                    spent += len(body)
                try:
                    tops = safe_extract(archive, vault_maps)
                except (zipfile.BadZipFile, ValueError) as error:
                    print(f"map {mapname}: {error}", flush=True)
                    failed_maps.add(mapname)
                    continue
                if not os.path.exists(target) and len(tops) == 1:
                    os.rename(os.path.join(vault_maps, tops.pop()), target)
        for row in sorted(by_map[mapname], key=lambda r: -r["id"]):
            if len(selected) >= args.count:
                break
            raw = os.path.join(out, "replays", f"{row['id']}.fafreplay")
            converted = os.path.join(out, "replays", f"{row['id']}.scfareplay")
            if not os.path.exists(converted):
                if not os.path.exists(raw):
                    body = request(REPLAY_URL.format(row["id"]))
                    if body is None:
                        continue
                    with open(raw, "wb") as f:
                        f.write(body)
                    spent += len(body)
                version = convert(raw, converted)
                if version != args.version:
                    print(f"{row['id']}: engine version {version}, not {args.version}; dropped", flush=True)
                    if os.path.exists(converted):
                        os.remove(converted)
                    continue
            problem = sim_mods_problem(converted)
            if problem:
                print(f"{row['id']}: {problem}; dropped", flush=True)
                continue
            selected.append({"id": row["id"], "map": row["mapname"], "players": row["num_players"],
                             "minutes": round((row["game_end"] - row["launched_at"]) / 60, 1)})
        print(f"choose: {len(selected)} replays, {spent / 1e6:.0f} MB downloaded", flush=True)

    with open(os.path.join(out, "selected.json"), "w") as f:
        json.dump(selected, f, indent=1)
    print(f"selected {len(selected)} replays on {len({s['map'].lower() for s in selected})} maps, "
          f"{spent / 1e6:.0f} MB downloaded; maps not found: {len(failed_maps)}", flush=True)
    return 0 if len(selected) >= args.count else 1


if __name__ == "__main__":
    sys.exit(main())
