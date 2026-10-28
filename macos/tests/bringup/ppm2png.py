# ppm2png.py [--half] <file.ppm>...: converts the screenshot layer's binary PPMs (P6, 8-bit) to
# PNG next to them (--half: also <name>_half.png at half size, box-filtered, for viewing) and
# prints each image's size and mean colour. Python stdlib only (run with python3 -I).
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


def half(width, height, rgb):
    w2, h2 = width // 2, height // 2
    out = bytearray(w2 * h2 * 3)
    stride = width * 3
    for y in range(h2):
        r0 = (2 * y) * stride
        r1 = r0 + stride
        for x in range(w2):
            i = 6 * x
            o = (y * w2 + x) * 3
            for c in range(3):
                out[o + c] = (rgb[r0 + i + c] + rgb[r0 + i + 3 + c] + rgb[r1 + i + c] + rgb[r1 + i + 3 + c] + 2) >> 2
    return w2, h2, bytes(out)


args = sys.argv[1:]
do_half = "--half" in args
args = [a for a in args if a != "--half"]
for path in args:
    width, height, rgb = read_ppm(path)
    out = path[:-4] + ".png"
    write_png(out, width, height, rgb)
    n = width * height
    mean = tuple(sum(rgb[c::3]) // n for c in range(3))
    print("%s: %dx%d mean #%02x%02x%02x" % (out, width, height, *mean))
    if do_half:
        w2, h2, small = half(width, height, rgb)
        write_png(path[:-4] + "_half.png", w2, h2, small)
