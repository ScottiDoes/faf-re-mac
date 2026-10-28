# ppm2png.py <file.ppm>...: converts the screenshot layer's binary PPMs (P6, 8-bit) to
# PNG next to them, and prints each image's size and the colours at a few points.
# Python stdlib only (run with python3 -I).
import struct, sys, zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError("%s: not an 8-bit P6 PPM" % path)
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos:pos + width * height * 3]


def write_png(path, width, height, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


for path in sys.argv[1:]:
    width, height, rgb = read_ppm(path)
    out = path[:-4] + ".png"
    write_png(out, width, height, rgb)

    def at(x, y):
        i = (y * width + x) * 3
        return "#%02x%02x%02x" % tuple(rgb[i:i + 3])
    print("%s: %dx%d  corner %s  centre %s  right-middle %s  bottom-right %s" % (
        out, width, height, at(4, 4), at(width // 2, height // 2), at(width - 4, height // 2), at(width - 4, height - 4)))
