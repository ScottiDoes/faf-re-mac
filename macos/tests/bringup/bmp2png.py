# bmp2png.py <file.bmp>...: converts uncompressed 24/32-bit BMPs (the engine's dump_Frame
# output) to PNG next to them and prints size and mean colour. Python stdlib only (python3 -I).
import struct, sys, zlib


def read_bmp(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError("%s: not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, planes, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if compression not in (0, 3) or bpp not in (24, 32):
        raise ValueError("%s: unsupported BMP (bpp %d, compression %d)" % (path, bpp, compression))
    bottom_up = height > 0
    height = abs(height)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(height):
        src = (height - 1 - y) if bottom_up else y
        row = data[offset + src * stride: offset + src * stride + width * step]
        out = bytearray(width * 3)
        out[0::3] = row[2::step]
        out[1::3] = row[1::step]
        out[2::3] = row[0::step]
        rows.append(bytes(out))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


for path in sys.argv[1:]:
    width, height, rows = read_bmp(path)
    out = path[:-4] + ".png"
    write_png(out, width, height, rows)
    allpix = b"".join(rows)
    n = width * height
    mean = tuple(sum(allpix[c::3]) // n for c in range(3))
    print("%s: %dx%d mean #%02x%02x%02x" % (out, width, height, *mean))
