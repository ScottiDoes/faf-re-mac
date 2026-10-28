# pngdiff.py <a.png> <b.png> <out.png>: per-pixel absolute difference (x4, clamped) of two
# PNGs written by ppm2png/bmp2png (8-bit RGB, filter 0), plus the share of pixels that differ
# by more than 8 in any channel. Python stdlib only (python3 -I).
import struct, sys, zlib


def read_png(path):
    data = open(path, "rb").read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack_from(">II", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 3
    rows = [raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(height)]
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


w, h, a = read_png(sys.argv[1])
w2, h2, b = read_png(sys.argv[2])
assert (w, h) == (w2, h2)
out, differing = [], 0
for y in range(h):
    ra, rb = a[y], b[y]
    row = bytearray(w * 3)
    for x in range(w):
        d = 0
        for c in range(3):
            v = abs(ra[3 * x + c] - rb[3 * x + c])
            d = max(d, v)
            row[3 * x + c] = min(255, v * 4)
        if d > 8:
            differing += 1
    out.append(row)
write_png(sys.argv[3], w, h, out)
print("%s: %.2f%% of pixels differ by more than 8" % (sys.argv[3], 100.0 * differing / (w * h)))
