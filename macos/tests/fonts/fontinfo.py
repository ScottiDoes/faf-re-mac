# Dumps the tables of TrueType fonts that GDI's metrics depend on (head, hhea, OS/2,
# hdmx, VDMX, gasp, kern, LTSH, name): the reference data behind the POSIX GDI font
# emulation (src/platform/posix/WinCompatGdiFont.cpp). Python 3 stdlib only.
#
#   python3 -I macos/tests/fonts/fontinfo.py <font.ttf>... [--ppem 12,14]
import struct
import sys


def tables(data):
    num = struct.unpack(">H", data[4:6])[0]
    out = {}
    for i in range(num):
        tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i: 28 + 16 * i])
        out[tag.decode("latin-1")] = (off, length)
    return out


def names(data, t):
    off, _ = t["name"]
    _, count, strings = struct.unpack(">HHH", data[off: off + 6])
    res = []
    for i in range(count):
        pid, eid, lid, nid, ln, so = struct.unpack(">HHHHHH", data[off + 6 + 12 * i: off + 18 + 12 * i])
        raw = data[off + strings + so: off + strings + so + ln]
        if pid == 3 or pid == 0:
            s = raw.decode("utf-16-be", "replace")
        else:
            s = raw.decode("mac-roman", "replace")
        res.append((pid, eid, lid, nid, s))
    return res


def main():
    args = sys.argv[1:]
    ppems = [10, 12, 14, 16, 18, 20]
    if "--ppem" in args:
        i = args.index("--ppem")
        ppems = [int(x) for x in args[i + 1].split(",")]
        del args[i: i + 2]
    for path in args:
        data = open(path, "rb").read()
        t = tables(data)
        print("==", path)
        print("tables:", " ".join(sorted(t)))
        ho = t["head"][0]
        upem = struct.unpack(">H", data[ho + 18: ho + 20])[0]
        xmin, ymin, xmax, ymax = struct.unpack(">hhhh", data[ho + 36: ho + 44])
        print("head: upem", upem, "bbox", xmin, ymin, xmax, ymax)
        hh = t["hhea"][0]
        asc, desc, gap = struct.unpack(">hhh", data[hh + 4: hh + 10])
        print("hhea: ascender", asc, "descender", desc, "lineGap", gap)
        oo = t["OS/2"][0]
        ver, avg, weight = struct.unpack(">HhH", data[oo: oo + 6])
        tasc, tdesc, tgap, wasc, wdesc = struct.unpack(">hhhHH", data[oo + 68: oo + 78])
        fs = struct.unpack(">H", data[oo + 62: oo + 64])[0]
        first, last = struct.unpack(">HH", data[oo + 64: oo + 68])
        print("OS/2: v%d xAvgCharWidth %d weight %d fsSelection 0x%x first %04x last %04x typo %d %d %d win %d %d"
              % (ver, avg, weight, fs, first, last, tasc, tdesc, tgap, wasc, wdesc))
        for nid_want in (1, 2, 4, 6, 16, 17):
            seen = set()
            for pid, eid, lid, nid, s in names(data, t):
                if nid == nid_want and (s, lid) not in seen:
                    seen.add((s, lid))
            print("name %d:" % nid_want, sorted({(s) for s, _ in seen}), "langs", sorted({hex(l) for _, l in seen})[:20])
        if "gasp" in t:
            go = t["gasp"][0]
            _, n = struct.unpack(">HH", data[go: go + 4])
            print("gasp:", [struct.unpack(">HH", data[go + 4 + 4 * i: go + 8 + 4 * i]) for i in range(n)])
        if "hdmx" in t:
            xo = t["hdmx"][0]
            _, n, size = struct.unpack(">HhI", data[xo: xo + 8])
            print("hdmx ppems:", [data[xo + 8 + size * i] for i in range(n)])
        if "VDMX" in t:
            vo = t["VDMX"][0]
            _, nrecs, nratios = struct.unpack(">HHH", data[vo: vo + 6])
            ratios = [struct.unpack(">BBBB", data[vo + 6 + 4 * i: vo + 10 + 4 * i]) for i in range(nratios)]
            offs = [struct.unpack(">H", data[vo + 6 + 4 * nratios + 2 * i: vo + 8 + 4 * nratios + 2 * i])[0] for i in range(nratios)]
            print("VDMX ratios:", ratios)
            g = vo + offs[0]
            recs, startsz, endsz = struct.unpack(">HBB", data[g: g + 4])
            entries = {}
            for i in range(recs):
                h, ymx, ymn = struct.unpack(">Hhh", data[g + 4 + 6 * i: g + 10 + 6 * i])
                entries[h] = (ymx, ymn)
            print("VDMX group0: %d..%d" % (startsz, endsz), {p: entries.get(p) for p in ppems})
        if "kern" in t:
            ko = t["kern"][0]
            ver, n = struct.unpack(">HH", data[ko: ko + 4])
            sub = ko + 4
            _, slen, cov = struct.unpack(">HHH", data[sub: sub + 6])
            npairs = struct.unpack(">H", data[sub + 6: sub + 8])[0]
            print("kern: v%d tables %d first coverage 0x%x pairs %d" % (ver, n, cov, npairs))
        if "LTSH" in t:
            print("LTSH present")
        for ppem in ppems:
            sc = ppem / upem
            print("  ppem %d: winAsc*s %.3f winDesc*s %.3f avg*s %.3f gap*s %.3f" % (ppem, wasc * sc, wdesc * sc, avg * sc, gap * sc))


main()
