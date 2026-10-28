# Independent check of the POSIX GDI font metrics (WinCompatGdiFont.cpp): recomputes, in
# Python straight from the font files' tables, what GDI's TrueType driver reports for
# CreateFont(-size, ..., face) - the algorithm Wine uses to match Windows (FreeType 16.16
# scales and 26.6 rounding; VDMX for ascent/descent, OS/2 usWin* otherwise; hdmx advances,
# rounded hmtx advances otherwise; 'kern' format 0 pair count) - and compares it with
# fonts_test's metrics.tsv for every row whose face is one of the game's fonts.
#
#   python3 -I macos/tests/fonts/reference.py <fonts dir> <metrics.tsv>
#
# Exit status 1 on any difference. Python 3 stdlib only.
import os
import struct
import sys


def be16(d, o):
    return struct.unpack(">H", d[o:o + 2])[0]


def bes16(d, o):
    return struct.unpack(">h", d[o:o + 2])[0]


def be32(d, o):
    return struct.unpack(">I", d[o:o + 4])[0]


class Font:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        self.t = {}
        for i in range(be16(d, 4)):
            tag, _, off, ln = struct.unpack(">4sIII", d[12 + 16 * i: 28 + 16 * i])
            self.t[tag.decode("latin-1")] = (off, ln)
        h = self.t["head"][0]
        self.upem = be16(d, h + 18)
        self.xmin = bes16(d, h + 36)
        self.xmax = bes16(d, h + 40)
        hh = self.t["hhea"][0]
        self.hasc, self.hdesc, self.gap = bes16(d, hh + 4), bes16(d, hh + 6), bes16(d, hh + 8)
        self.nhm = be16(d, hh + 34)
        self.nglyphs = be16(d, self.t["maxp"][0] + 4)
        o = self.t["OS/2"][0]
        self.avg = bes16(d, o + 2)
        self.weight = be16(d, o + 4)
        self.wasc, self.wdesc = be16(d, o + 74), be16(d, o + 76)
        self.family = self.name(1)
        self.cmap = self.read_cmap()

    def name(self, nid):
        d = self.data
        o = self.t["name"][0]
        count, so = be16(d, o + 2), be16(d, o + 4)
        for i in range(count):
            pid, eid, lid, n, ln, off = struct.unpack(">HHHHHH", d[o + 6 + 12 * i: o + 18 + 12 * i])
            if pid == 3 and lid == 0x409 and n == nid:
                return d[o + so + off: o + so + off + ln].decode("utf-16-be")
        return None

    def read_cmap(self):
        d = self.data
        o = self.t["cmap"][0]
        sub = None
        for i in range(be16(d, o + 2)):
            pid, eid, off = struct.unpack(">HHI", d[o + 4 + 8 * i: o + 12 + 8 * i])
            if pid == 3 and eid in (1, 0):
                sub = o + off
                break
        m = {}
        if sub is None or be16(d, sub) != 4:
            return m
        segx2 = be16(d, sub + 6)
        ends = sub + 14
        starts = ends + segx2 + 2
        deltas = starts + segx2
        ranges = deltas + segx2
        for s in range(segx2 // 2):
            end, start = be16(d, ends + 2 * s), be16(d, starts + 2 * s)
            delta, ro = bes16(d, deltas + 2 * s), be16(d, ranges + 2 * s)
            for c in range(start, end + 1):
                if c == 0xFFFF:
                    continue
                if ro == 0:
                    g = (c + delta) & 0xFFFF
                else:
                    g = be16(d, ranges + 2 * s + ro + 2 * (c - start))
                    if g:
                        g = (g + delta) & 0xFFFF
                m[c] = g
        return m

    def hmtx_adv(self, g):
        o = self.t["hmtx"][0]
        return be16(self.data, o + 4 * min(g, self.nhm - 1))

    def hdmx_record(self, ppem):
        if "hdmx" not in self.t:
            return None
        d = self.data
        o = self.t["hdmx"][0]
        n, size = bes16(d, o + 2), be32(d, o + 4)
        for i in range(n):
            if d[o + 8 + size * i] == ppem:
                return o + 8 + size * i + 2
        return None

    def vdmx(self, ppem):
        if "VDMX" not in self.t:
            return None
        d = self.data
        o = self.t["VDMX"][0]
        nratios = be16(d, o + 4)
        group = None
        for i in range(nratios):
            _, x, ys, ye = struct.unpack(">BBBB", d[o + 6 + 4 * i: o + 10 + 4 * i])
            if (x == 0 and ys == 0 and ye == 0) or (x == 1 and ys <= 1 <= ye):
                group = o + be16(d, o + 6 + 4 * nratios + 2 * i)
                break
        if group is None:
            return None
        recs, start, end = be16(d, group), d[group + 2], d[group + 3]
        if ppem < start or ppem > end:
            return None
        for i in range(recs):
            pel, ymax, ymin = struct.unpack(">Hhh", d[group + 4 + 6 * i: group + 10 + 6 * i])
            if pel > ppem:
                return None
            if pel == ppem:
                return ymax, ymin
        return None

    def kern_pairs(self):
        if "kern" not in self.t:
            return 0
        d = self.data
        o = self.t["kern"][0]
        if be16(d, o) != 0:
            return 0
        total = 0
        at = o + 4
        for _ in range(be16(d, o + 2)):
            ln, cov = be16(d, at + 2), be16(d, at + 4)
            if (cov >> 8) == 0 and (cov & 1):
                total += be16(d, at + 6)
            at += ln
        return total


def mulfix(a, b):
    neg = (a < 0) != (b < 0)
    m = (abs(a) * abs(b) + 0x8000) >> 16
    return -m if neg else m


def divfix(a, b):
    neg = (a < 0) != (b < 0)
    m = ((abs(a) << 16) + abs(b) // 2) // abs(b)
    return -m if neg else m


def r26(v):
    return (v + 32) >> 6


def metrics(f, size):
    ppem = size
    ys = divfix(ppem << 6, f.upem)
    v = f.vdmx(ppem)
    asc, desc = f.wasc, f.wdesc
    if v:
        a, ds = v[0], -v[1]
        il = a + ds - ppem
    else:
        a, ds = r26(mulfix(asc, ys)), r26(mulfix(desc, ys))
        il = r26(mulfix(asc + desc - f.upem, ys))
    el = max(0, r26(mulfix(f.gap - ((asc + desc) - (f.hasc - f.hdesc)), ys)))
    avg = max(1, r26(mulfix(f.avg, ys)))
    mx = r26(mulfix(f.xmax - f.xmin, ys))
    rec = f.hdmx_record(ppem)
    advs = []
    for c in range(0x20, 0x7F):
        g = f.cmap.get(c, 0)
        if rec is not None and g < f.nglyphs:
            advs.append(f.data[rec + g])
        else:
            advs.append(r26(mulfix(f.hmtx_adv(g), ys)))
    return {
        "tmHeight": a + ds, "tmAscent": a, "tmDescent": ds, "tmInternalLeading": il, "tmExternalLeading": el,
        "tmAveCharWidth": avg, "tmMaxCharWidth": mx, "tmWeight": f.weight, "kerningPairs": f.kern_pairs(),
        "advances(0x20..0x7E)": ",".join(str(x) for x in advs),
    }


def main():
    fonts_dir, tsv = sys.argv[1], sys.argv[2]
    faces = {}
    for n in sorted(os.listdir(fonts_dir)):
        if n.lower().endswith(".ttf"):
            f = Font(os.path.join(fonts_dir, n))
            full = f.name(4)
            faces.setdefault(f.family.lower(), []).append(f)
            faces.setdefault(("full", full.lower()), []).append(f)
    lines = open(tsv).read().splitlines()
    header = lines[0].split("\t")
    checked = diffs = 0
    for line in lines[1:]:
        row = dict(zip(header, line.split("\t")))
        key = row["face"].lower()
        cands = faces.get(key)
        if cands:
            f = min(cands, key=lambda c: (c.name(2) not in ("Regular", "Normal"), abs(c.weight - 400)))
        elif faces.get(("full", key)):
            f = faces[("full", key)][0]
        else:
            continue  # not a game font: an installed or fallback face
        want = metrics(f, int(row["size"]))
        checked += 1
        for k, v in want.items():
            if str(v) != row[k]:
                diffs += 1
                print("DIFF %s %s %s: reference %s, fonts_test %s" % (row["face"], row["size"], k, v, row[k]))
    print("%s: %d rows checked against the font tables, %d difference(s)" % ("PASS" if diffs == 0 else "FAIL", checked, diffs))
    sys.exit(1 if diffs else 0)


main()
