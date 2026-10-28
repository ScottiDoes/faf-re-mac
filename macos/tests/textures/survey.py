#!/usr/bin/env python3 -I
"""Survey of every texture file the game can load: the FA install's gamedata/*.scd, the FAF
gamedata archives (*.nx2, *.faf), and map folders. Prints counts per container/format/mip
state, so the POSIX ImageCodec covers what really ships.

    python3 -I survey.py [--list] <source> ...      source = archive, directory, or .scmap
"""
import os
import struct
import sys
import zipfile
from collections import Counter

TEX_EXT = (".dds", ".tga", ".png", ".bmp", ".jpg", ".jpeg")


def dds_kind(data):
    if len(data) < 128 or data[:4] != b"DDS ":
        return "dds:bad-header"
    (size, flags, height, width, pitch, depth, mips) = struct.unpack_from("<7I", data, 4)
    pf_flags, fourcc, bits, rmask, gmask, bmask, amask = struct.unpack_from("<2I5I", data, 80)
    caps1, caps2 = struct.unpack_from("<2I", data, 108)
    if not (flags & 0x20000):
        mips = 1
    mips = max(mips, 1)
    full = 1
    s = max(width, height)
    while s > 1:
        s >>= 1
        full += 1
    if pf_flags & 0x4:
        fmt = fourcc.to_bytes(4, "little").decode("latin1")
        if fmt == "DX10":
            dxgi, dim, misc, arr, misc2 = struct.unpack_from("<5I", data, 128)
            fmt = "DX10:%d" % dxgi
        elif fourcc < 256:
            fmt = "D3DFMT:%d" % fourcc
    else:
        kind = []
        if pf_flags & 0x40:
            kind.append("RGB")
        if pf_flags & 0x20000:
            kind.append("L")
        if pf_flags & 0x2:
            kind.append("A")
        if pf_flags & 0x1:
            kind.append("a")
        fmt = "%s%d[%08x,%08x,%08x,%08x]" % ("".join(kind), bits, rmask, gmask, bmask, amask)
    shape = "2d"
    if caps2 & 0x200:
        shape = "cube"
    if caps2 & 0x200000:
        shape = "volume(d=%d)" % depth
    mipstate = "full" if mips == full else ("1" if mips == 1 else "partial")
    pow2 = (width & (width - 1)) == 0 and (height & (height - 1)) == 0
    return "dds %s %s mips=%s%s" % (fmt, shape, mipstate, "" if pow2 else " npot")


def kind_of(name, data):
    ext = os.path.splitext(name.lower())[1]
    if ext == ".dds":
        return dds_kind(data)
    if ext == ".tga":
        if len(data) < 18:
            return "tga:bad"
        idlen, cmtype, itype = data[0], data[1], data[2]
        w, h, bpp, desc = struct.unpack_from("<HHBB", data, 12)
        return "tga type=%d bpp=%d" % (itype, bpp)
    if ext == ".png":
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            return "png:bad"
        w, h, depth, ctype = struct.unpack_from(">IIBB", data, 16)
        return "png depth=%d color=%d" % (depth, ctype)
    return ext[1:]


def walk(source):
    if os.path.isdir(source):
        for root, dirs, files in os.walk(source):
            for f in files:
                p = os.path.join(root, f)
                if f.lower().endswith(TEX_EXT):
                    with open(p, "rb") as fh:
                        yield p, fh.read()
    elif zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z:
            for info in z.infolist():
                if info.filename.lower().endswith(TEX_EXT):
                    yield source + "!" + info.filename, z.read(info)


def main():
    listing = "--list" in sys.argv
    sources = [a for a in sys.argv[1:] if a != "--list"]
    counts = Counter()
    for source in sources:
        for name, data in walk(source):
            k = kind_of(name, data)
            counts[k] += 1
            if listing:
                print("%s\t%s" % (k, name))
    total = sum(counts.values())
    for k, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("%6d  %s" % (n, k))
    print("%6d  total" % total)


if __name__ == "__main__":
    main()
