#!/usr/bin/env python3
"""replay_tool.py: read, convert and corrupt Forged Alliance replays.

Python 3.9 standard library only; run with `python3 -I`.

  info <file.scfareplay> [--json]
      The header (game version, map, command sources, armies) and the command stream: beats
      (CMDST_Advance), the recorded player checksums (CMDST_VerifyChecksum: op 3, length 23,
      a 16-byte MD5 digest then the int32 beat it covers) per beat and per source, and how
      many of them the sim compares when the whole stream plays.
  checked <file.scfareplay> <applied-beats>
      The number of recorded checksums the sim has compared once the user side applied
      <applied-beats> sim beats (the runner's checked= for that run; a cross-check).
  convert <in.fafreplay> <out.scfareplay> [--meta <out.json>]
      FAF vault/client replay -> the .scfareplay the engine's /replay reads. A .fafreplay is one
      JSON line, then base64 of (4-byte big-endian length + zlib stream), or (version 2, the
      vault's form today) one zstd frame of the raw replay, decoded by zstd_decode.py. --meta writes
      the JSON header (featured_mod, mapname, num_players, ...) next to the output.
  corrupt <in.scfareplay> <out.scfareplay> <beat> [--byte K] [--json]
      Negative control: a copy with digest byte K (default 0) flipped in every checksum record
      that covers <beat>. Prints how many records changed (one per player who recorded it);
      --json also lists each record's source and its digest before and after.
  strip <in.scfareplay> <out.scfareplay>
      A copy without any checksum record (the other commands untouched): a run of it compares
      nothing, so the runner must not call it a pass (verdict=unverified, exit status 2).
  synccheck <file.scfareplay> <checksums.txt> [--synclog <dir>] [--json]
      An end-to-end check that does not rely on the engine's mismatch warning: compares every
      recorded digest the sim compares with the sim's own digest of that beat, as /synclog
      wrote it ("beat N final checksum: <md5>", collected by SYNCLOG=1 macos/run_replay.sh
      into checksums-<id>.txt). With --synclog, also counts the sim's "Ignoring verify of beat"
      lines (records the sim skipped), which the rules above say never happen in a full run.
      Exit 0 when every compared record equals the sim's digest and nothing was skipped.

How the sim uses the records (src/moho): CReplayClient::Start forwards a command only while the
last CMDST_SetCommandSource named one of the header's sources (CReplayClient.cpp:218-249);
CDecoder hands CMDST_VerifyChecksum to Sim::VerifyChecksum, which compares it with its own
digest of that beat when the beat lies in [curBeat - 128, curBeat) and logs
"Checksum for beat N mismatched" when they differ (Sim.cpp:7941); it ignores a record while no
valid command source is set. curBeat counts Sim::Sync calls: CSimDriver runs one opening Sync
before the first command (SimDriver.cpp:1150; beat 0's digest), then per beat the beat's
commands, its CMDST_Advance (Sim::AdvanceBeat) and a Sync (SimDriver.cpp:830-906,
Sim.cpp:5806). So a record that follows the k-th CMDST_Advance of the stream is decoded at
curBeat = k + 1, and once the user side has applied N sync packets (the opening one included)
the sim has decoded every record with curBeat < N. src/platform/posix/runner/ReplayChecks.cpp
implements the same model for the runner's checked=.

Exit status: 0 done, 1 corrupt found no record at that beat or synccheck failed, 2 usage or a
file it cannot read.
"""
import argparse
import base64
import json
import os
import re
import struct
import sys
import zlib

OP_ADVANCE = 0
OP_SET_COMMAND_SOURCE = 1
OP_COMMAND_SOURCE_TERMINATED = 2
OP_VERIFY_CHECKSUM = 3
OP_END_GAME = 23
VERIFY_SIZE = 23  # 3-byte message header + 16-byte digest + int32 beat
HISTORY_BEATS = 128  # Sim::mSimHashes ring (Sim.cpp:7947)
SIGNATURE = b"Replay v1.9\r\n"


class ReplayError(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ReplayError("the header ends early (offset %d, %d bytes wanted)" % (self.pos, n))
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def cstring(self):
        end = self.data.find(b"\0", self.pos)
        if end < 0:
            raise ReplayError("unterminated string at offset %d" % self.pos)
        out = self.data[self.pos:end]
        self.pos = end + 1
        return out

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def prefixed(self):
        return self.take(self.u32())


def parse(data):
    """Header and command stream of a .scfareplay (SessionStartup.cpp VCR_SetupReplaySession)."""
    r = Reader(data)
    info = {}
    info["version"] = r.cstring().decode("latin1")
    r.cstring()
    if r.take(len(SIGNATURE)) != SIGNATURE:
        raise ReplayError("not a Forged Alliance replay (no 'Replay v1.9' signature)")
    info["map"] = r.cstring().decode("latin1")
    r.cstring()
    info["mods_bytes"] = len(r.prefixed())
    scenario = r.prefixed()
    info["scenario_bytes"] = len(scenario)
    sources = []
    for _ in range(r.u8()):
        name = r.cstring().decode("utf-8", "replace")
        timeouts = struct.unpack("<i", r.take(4))[0]
        sources.append({"name": name, "timeouts": timeouts})
    info["sources"] = sources
    info["cheats"] = r.u8()
    armies = []
    for _ in range(r.u8()):
        # The army's launch settings, a serialized Lua table (string = 0x01 text NUL, bool = 0x03 byte).
        blob = r.prefixed()
        m = re.search(rb"\x01PlayerName\x00\x01([^\x00]*)\x00", blob)
        name = m.group(1).decode("utf-8", "replace") if m else "?"
        m = re.search(rb"\x01Human\x00\x03(.)", blob, re.S)
        human = bool(m and m.group(1) != b"\x00")
        unit_sources = []
        while True:
            b = r.u8()
            if b == 0xFF:
                break
            unit_sources.append(b)
        armies.append({"name": name, "human": human, "sources": unit_sources})
    info["armies"] = armies
    info["seed"] = r.u32()
    info["stream_offset"] = r.pos

    # The command stream: [op u8][size u16 LE, header included][payload].
    records = []  # (offset, recorded beat, source, decode beat, client-forwarded, sim source valid)
    advances = 0
    beats = 0
    odd_deltas = 0
    end_game = False
    allowed = False  # CReplayClient::mCurrentSourceAllowed starts false
    sim_source = None  # Sim::mCurCommandSource starts invalid
    source = None
    pos = r.pos
    truncated = None
    nsrc = len(sources)
    while pos + 3 <= len(data):
        op = data[pos]
        size = struct.unpack_from("<H", data, pos + 1)[0]
        if size < 3:
            truncated = "message of size %d at offset %d (the engine stops reading there)" % (size, pos)
            break
        if pos + size > len(data):
            truncated = "last message at offset %d is cut short" % pos
            break
        if op == OP_SET_COMMAND_SOURCE:
            if size < 4:
                allowed = False
                source = None
            else:
                source = data[pos + 3]
                allowed = source < nsrc
                if allowed:
                    sim_source = source  # forwarded; Sim::SetCommandSource accepts < source count
        if op == OP_ADVANCE:
            delta = struct.unpack_from("<i", data, pos + 3)[0] if size >= 7 else 0
            if delta != 1:
                odd_deltas += 1
            advances += 1
            beats += delta
        elif op == OP_VERIFY_CHECKSUM and size == VERIFY_SIZE:
            beat = struct.unpack_from("<i", data, pos + 19)[0]
            records.append((pos, beat, source, advances + 1, allowed, allowed and sim_source is not None))
        elif op == OP_END_GAME and allowed:
            end_game = True
        pos += size
    if truncated is None and pos != len(data):
        truncated = "%d trailing bytes" % (len(data) - pos)
    info["advances"] = advances
    info["advance_beats"] = beats
    info["odd_advance_deltas"] = odd_deltas
    info["end_game_in_stream"] = end_game
    info["truncated"] = truncated
    info["records"] = records
    return info


def compared(record, applied_beats=None):
    """True when Sim::VerifyChecksum compares this record (decoded before `applied_beats`)."""
    _, beat, _, decode_beat, forwarded, sim_valid = record
    if not (forwarded and sim_valid):
        return False
    if applied_beats is not None and decode_beat >= applied_beats:
        return False
    return decode_beat - HISTORY_BEATS <= beat < decode_beat


def summary(info):
    records = info["records"]
    by_beat = {}
    for rec in records:
        by_beat.setdefault(rec[1], set()).add(rec[2])
    players = sorted({rec[2] for rec in records if rec[2] is not None})
    beats = sorted(by_beat)
    mid = beats[len(beats) // 2] if beats else None
    return {
        "version": info["version"],
        "game_version": info["version"].rsplit(".", 1)[-1],
        "map": info["map"],
        "sources": [s["name"] for s in info["sources"]],
        "armies": [a["name"] + ("" if a["human"] else " (not human)") for a in info["armies"]],
        "human_armies": sum(1 for a in info["armies"] if a["human"]),
        "players": len(players),  # sources that recorded checksums
        "recorded": len(records),
        "compared": sum(1 for rec in records if compared(rec)),
        "checksum_beats": len(beats),
        "first_checksum_beat": beats[0] if beats else None,
        "last_checksum_beat": beats[-1] if beats else None,
        "mid_checksum_beat": mid,
        "records_at_mid_beat": len([rec for rec in records if rec[1] == mid]) if mid is not None else 0,
        "stream_beats": info["advances"],
        "odd_advance_deltas": info["odd_advance_deltas"],
        "end_game_in_stream": info["end_game_in_stream"],
        "truncated": info["truncated"],
    }


def read_replay(path):
    try:
        data = open(path, "rb").read()
    except OSError as error:
        raise ReplayError("cannot read %s: %s" % (path, error.strerror))
    return data, parse(data)


def cmd_info(args):
    _, info = read_replay(args.replay)
    s = summary(info)
    if args.json:
        print(json.dumps(s, sort_keys=True))
        return 0
    print("replay      %s" % args.replay)
    print("version     %s" % s["version"])
    print("map         %s" % s["map"])
    print("sources     %d: %s" % (len(s["sources"]), ", ".join(s["sources"])))
    print("armies      %d: %s" % (len(s["armies"]), ", ".join(s["armies"])))
    print("stream      %d beats%s%s" % (
        s["stream_beats"],
        ", ends with CMDST_EndGame" if s["end_game_in_stream"] else "",
        ", %s" % s["truncated"] if s["truncated"] else ""))
    if s["odd_advance_deltas"]:
        print("            %d CMDST_Advance with a delta other than 1" % s["odd_advance_deltas"])
    print("checksums   %d recorded by %d players over %d beats (%s..%s); %d compared by the sim" % (
        s["recorded"], s["players"], s["checksum_beats"], s["first_checksum_beat"], s["last_checksum_beat"],
        s["compared"]))
    print("mid beat    %s (%d records)" % (s["mid_checksum_beat"], s["records_at_mid_beat"]))
    return 0


def cmd_checked(args):
    _, info = read_replay(args.replay)
    print(sum(1 for rec in info["records"] if compared(rec, args.applied_beats)))
    return 0


def cmd_convert(args):
    raw = open(args.src, "rb").read()
    newline = raw.find(b"\n")
    if newline < 0:
        raise ReplayError("%s: no JSON header line" % args.src)
    header = json.loads(raw[:newline])
    body = raw[newline + 1:]
    if header.get("compression") == "zstd" or body[:4] == b"\x28\xb5\x2f\xfd":
        # Version-2 replays (FAF's vault today): the body is one zstd frame of the raw replay.
        # The standard library has no zstd: zstd_decode.py next to this file decodes it and
        # checks the frame's content checksum. (python3 -I leaves this folder off sys.path.)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import zstd_decode
        try:
            data = zstd_decode.decompress(body)
        except zstd_decode.ZstdError as error:
            raise ReplayError("%s: zstd: %s" % (args.src, error))
    else:
        blob = base64.b64decode(body)
        expected = struct.unpack(">I", blob[:4])[0]
        data = zlib.decompress(blob[4:])
        if len(data) != expected:
            raise ReplayError("%s: %d bytes decompressed, the header says %d" % (args.src, len(data), expected))
    info = parse(data)  # refuses anything that is not a replay
    with open(args.dst, "wb") as out:
        out.write(data)
    if args.meta:
        with open(args.meta, "w") as out:
            json.dump(header, out, sort_keys=True)
            out.write("\n")
    print("%s: %s, map %s, featured mod %s, %d bytes" % (
        args.dst, info["version"], header.get("mapname"), header.get("featured_mod"), len(data)))
    return 0


def digest_of(data, rec):
    return bytes(data[rec[0] + 3:rec[0] + 19]).hex()


def cmd_corrupt(args):
    data, info = read_replay(args.src)
    data = bytearray(data)
    hits = [rec for rec in info["records"] if rec[1] == args.beat]
    if not 0 <= args.byte < 16:
        raise ReplayError("--byte must be 0..15")
    before = [digest_of(data, rec) for rec in hits]
    for rec in hits:
        data[rec[0] + 3 + args.byte] ^= 0x01
    with open(args.dst, "wb") as out:
        out.write(data)
    would = sum(1 for rec in hits if compared(rec))
    if args.json:
        names = [s["name"] for s in info["sources"]]
        print(json.dumps({
            "beat": args.beat, "flipped": len(hits), "compared": would, "out": args.dst,
            "records": [{"source": names[rec[2]] if rec[2] is not None and rec[2] < len(names) else None,
                         "digest": old, "flipped_digest": digest_of(data, rec)}
                        for rec, old in zip(hits, before)]}, sort_keys=True))
    else:
        print("%d records at beat %d flipped (%d of them compared by the sim): %s" % (
            len(hits), args.beat, would, args.dst))
    return 0 if hits else 1


def cmd_strip(args):
    data, info = read_replay(args.src)
    drop = sorted(rec[0] for rec in info["records"])
    out = bytearray()
    last = 0
    for offset in drop:
        size = struct.unpack_from("<H", data, offset + 1)[0]
        out += data[last:offset]
        last = offset + size
    out += data[last:]
    with open(args.dst, "wb") as f:
        f.write(out)
    print("%d checksum records removed: %s" % (len(drop), args.dst))
    return 0


def cmd_synccheck(args):
    data, info = read_replay(args.replay)
    sim = {}
    line_re = re.compile(r"beat (\d+) final checksum: ([0-9a-f]{32})")
    with open(args.checksums) as f:
        for line in f:
            m = line_re.search(line)
            if m:
                sim[int(m.group(1))] = m.group(2)
    equal = differ = missing = 0
    first_differ = None
    for rec in info["records"]:
        if not compared(rec):
            continue
        want = sim.get(rec[1])
        if want is None:
            missing += 1
        elif want == digest_of(data, rec):
            equal += 1
        else:
            differ += 1
            if first_differ is None:
                first_differ = rec[1]
    ignored = None
    if args.synclog:
        import os
        ignored = 0
        for base, _, files in os.walk(args.synclog):
            for name in files:
                if name.endswith(".log"):
                    with open(os.path.join(base, name), errors="replace") as f:
                        ignored += sum(1 for line in f if "Ignoring verify of beat" in line)
    total = sum(1 for rec in info["records"] if compared(rec))
    ok = differ == 0 and missing == 0 and equal == total and not ignored
    result = {"compared": total, "equal": equal, "differ": differ, "missing_sim_digest": missing,
              "first_differing_beat": first_differ, "sim_checksum_beats": len(sim), "ignored_by_sim": ignored,
              "ok": ok}
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print("synccheck: %d recorded digests compared with the sim's own: %d equal, %d differ%s, %d without a sim "
              "digest; sim digests for %d beats%s -> %s" % (
                  total, equal, differ, "" if first_differ is None else " (first at beat %d)" % first_differ, missing,
                  len(sim), "" if ignored is None else "; 'Ignoring verify' lines in the sim log: %d" % ignored,
                  "ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("info")
    p.add_argument("replay")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("checked")
    p.add_argument("replay")
    p.add_argument("applied_beats", type=int)
    p = sub.add_parser("convert")
    p.add_argument("src")
    p.add_argument("dst")
    p.add_argument("--meta")
    p = sub.add_parser("corrupt")
    p.add_argument("src")
    p.add_argument("dst")
    p.add_argument("beat", type=int)
    p.add_argument("--byte", type=int, default=0)
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("strip")
    p.add_argument("src")
    p.add_argument("dst")
    p = sub.add_parser("synccheck")
    p.add_argument("replay")
    p.add_argument("checksums")
    p.add_argument("--synclog")
    p.add_argument("--json", action="store_true")
    args = ap.parse_args()
    handlers = {"info": cmd_info, "checked": cmd_checked, "convert": cmd_convert, "corrupt": cmd_corrupt,
                "strip": cmd_strip, "synccheck": cmd_synccheck}
    if args.cmd not in handlers:
        ap.print_usage(sys.stderr)
        return 2
    try:
        return handlers[args.cmd](args)
    except (ReplayError, OSError, ValueError, zlib.error) as error:
        print("replay_tool: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
