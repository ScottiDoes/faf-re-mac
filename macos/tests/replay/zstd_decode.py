"""Zstandard decompression in pure Python (RFC 8878), for FAF's version-2 replays.

FAF's replay vault serves replays as a JSON header line followed by a zstd frame
("compression": "zstd"); the Python standard library has no zstd and this machine has no
zstd tool, so replay_tool.py decodes them with this. Small and plain over fast: a replay
body is a few hundred KB. Checks the frame's content checksum (XXH64) when it has one.

Usage as a tool: python3 -I zstd_decode.py <in.zst> <out>
"""

import struct
import sys

MASK64 = (1 << 64) - 1


class ZstdError(Exception):
    pass


# --- XXH64 (content checksum) -------------------------------------------------------------
P1, P2, P3, P4, P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                      0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & MASK64


def _round(acc, lane):
    acc = (acc + lane * P2) & MASK64
    return (_rotl(acc, 31) * P1) & MASK64


def _merge(acc, val):
    acc ^= _round(0, val)
    return (acc * P1 + P4) & MASK64


def xxh64(data, seed=0):
    n = len(data)
    i = 0
    if n >= 32:
        v1 = (seed + P1 + P2) & MASK64
        v2 = (seed + P2) & MASK64
        v3 = seed
        v4 = (seed - P1) & MASK64
        limit = n - 32
        while i <= limit:
            a, b, c, d = struct.unpack_from("<4Q", data, i)
            v1, v2, v3, v4 = _round(v1, a), _round(v2, b), _round(v3, c), _round(v4, d)
            i += 32
        h = (_rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18)) & MASK64
        for v in (v1, v2, v3, v4):
            h = _merge(h, v)
    else:
        h = (seed + P5) & MASK64
    h = (h + n) & MASK64
    while i + 8 <= n:
        (k,) = struct.unpack_from("<Q", data, i)
        h ^= _round(0, k)
        h = (_rotl(h, 27) * P1 + P4) & MASK64
        i += 8
    if i + 4 <= n:
        (k,) = struct.unpack_from("<I", data, i)
        h ^= (k * P1) & MASK64
        h = (_rotl(h, 23) * P2 + P3) & MASK64
        i += 4
    while i < n:
        h ^= (data[i] * P5) & MASK64
        h = (_rotl(h, 11) * P1) & MASK64
        i += 1
    h ^= h >> 33
    h = (h * P2) & MASK64
    h ^= h >> 29
    h = (h * P3) & MASK64
    h ^= h >> 32
    return h


# --- Bit readers ------------------------------------------------------------------------------
class BackwardBits:
    """Reads a zstd backward bitstream: from the end, past the final byte's marker bit."""

    def __init__(self, data):
        if not data or data[-1] == 0:
            raise ZstdError("bitstream without its end marker")
        self.data = bytes(data)
        self.pos = len(data) * 8 - (8 - (data[-1].bit_length() - 1))  # bits left to read

    def read(self, n):
        if n == 0:
            return 0
        self.pos -= n
        p = self.pos
        if p >= 0:
            lo, hi = p >> 3, (p + n - 1) >> 3
            return (int.from_bytes(self.data[lo:hi + 1], "little") >> (p & 7)) & ((1 << n) - 1)
        avail = n + p  # bits past the start read as zeros
        if avail <= 0:
            return 0
        return (int.from_bytes(self.data[0:((avail - 1) >> 3) + 1], "little") & ((1 << avail) - 1)) << (-p)

    def peek(self, n):
        saved = self.pos
        v = self.read(n)
        self.pos = saved
        return v

    def overflowed(self):
        return self.pos < 0

    def finished(self):
        return self.pos == 0


# --- FSE --------------------------------------------------------------------------------------
LL_DEFAULT = [4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
              -1, -1, -1, -1]
ML_DEFAULT = [1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
              1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1]
OF_DEFAULT = [1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1]

LL_BASE = list(range(16)) + [16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
                             16384, 32768, 65536]
LL_BITS = [0] * 16 + [1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
ML_BASE = list(range(3, 35)) + [35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099,
                                8195, 16387, 32771, 65539]
ML_BITS = [0] * 32 + [1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]


def build_fse(probs, accuracy):
    """Decoding table [(symbol, nbBits, baseline)] for normalized counts (-1: 'less than 1')."""
    size = 1 << accuracy
    symbols = [0] * size
    high = size - 1
    nxt = [0] * len(probs)
    for s, p in enumerate(probs):
        if p == -1:
            symbols[high] = s
            high -= 1
            nxt[s] = 1
        else:
            nxt[s] = p
    step = (size >> 1) + (size >> 3) + 3
    mask = size - 1
    pos = 0
    for s, p in enumerate(probs):
        for _ in range(max(p, 0)):
            symbols[pos] = s
            pos = (pos + step) & mask
            while pos > high:
                pos = (pos + step) & mask
    if pos != 0:
        raise ZstdError("bad FSE distribution")
    table = []
    for cell in range(size):
        s = symbols[cell]
        x = nxt[s]
        nxt[s] += 1
        nb = accuracy - (x.bit_length() - 1)
        table.append((s, nb, (x << nb) - size))
    return table


def read_ncount(data, pos, max_symbol, max_accuracy):
    """FSE table description at data[pos:] -> (probs, accuracy, bytes used)."""
    bitpos = 0
    view = data[pos:pos + 512]

    def bits(n):
        lo = bitpos >> 3
        return (int.from_bytes(view[lo:lo + 4], "little") >> (bitpos & 7)) & ((1 << n) - 1)

    accuracy = bits(4) + 5
    bitpos += 4
    if accuracy > max_accuracy:
        raise ZstdError("FSE accuracy %d too high" % accuracy)
    remaining = (1 << accuracy) + 1
    threshold = 1 << accuracy
    nb = accuracy + 1
    probs = []
    previous0 = False
    while remaining > 1 and len(probs) <= max_symbol:
        if previous0:
            while True:
                rep = bits(2)
                bitpos += 2
                probs.extend([0] * rep)
                if rep != 3:
                    break
        top = (2 * threshold - 1) - remaining
        if (bits(nb) & (threshold - 1)) < top:
            count = bits(nb) & (threshold - 1)
            bitpos += nb - 1
        else:
            count = bits(nb) & (2 * threshold - 1)
            if count >= threshold:
                count -= top
            bitpos += nb
        count -= 1
        remaining -= abs(count)
        probs.append(count)
        previous0 = count == 0
        while remaining < threshold:
            nb -= 1
            threshold >>= 1
    if remaining != 1:
        raise ZstdError("bad FSE table description")
    return probs, accuracy, (bitpos + 7) >> 3


# --- Huffman ----------------------------------------------------------------------------------
def build_huffman(weights):
    """Literal weights (last one implied) -> (decode table indexed by maxBits bits, maxBits)."""
    total = sum((1 << (w - 1)) for w in weights if w > 0)
    if total == 0:
        raise ZstdError("empty Huffman weights")
    max_bits = total.bit_length()
    left = (1 << max_bits) - total
    if left & (left - 1):
        raise ZstdError("bad Huffman weights")
    weights = weights + [left.bit_length()]
    rank_count = [0] * (max_bits + 2)
    for w in weights:
        rank_count[w] += 1
    rank_start = [0] * (max_bits + 2)
    nxt = 0
    for w in range(1, max_bits + 1):
        rank_start[w] = nxt
        nxt += rank_count[w] << (w - 1)
    table = [None] * (1 << max_bits)
    for sym, w in enumerate(weights):
        if w == 0:
            continue
        length = 1 << (w - 1)
        start = rank_start[w]
        entry = (sym, max_bits + 1 - w)
        for k in range(start, start + length):
            table[k] = entry
        rank_start[w] += length
    return table, max_bits


def read_huffman_tree(data, pos):
    header = data[pos]
    pos += 1
    if header < 128:
        end = pos + header
        probs, accuracy, used = read_ncount(data, pos, 255, 6)
        fse = build_fse(probs, accuracy)
        bits = BackwardBits(data[pos + used:end])
        s1 = bits.read(accuracy)
        s2 = bits.read(accuracy)
        weights = []
        while True:
            sym, nb, base = fse[s1]
            weights.append(sym)
            s1 = base + bits.read(nb)
            if bits.overflowed():
                weights.append(fse[s2][0])
                break
            sym, nb, base = fse[s2]
            weights.append(sym)
            s2 = base + bits.read(nb)
            if bits.overflowed():
                weights.append(fse[s1][0])
                break
            if len(weights) > 255:
                raise ZstdError("too many Huffman weights")
        return build_huffman(weights), end
    count = header - 127
    nbytes = (count + 1) // 2
    weights = []
    for i in range(count):
        b = data[pos + i // 2]
        weights.append(b >> 4 if i % 2 == 0 else b & 15)
    return build_huffman(weights), pos + nbytes


def huffman_stream(table, max_bits, data, count, out):
    bits = BackwardBits(data)
    for _ in range(count):
        sym, nb = table[bits.peek(max_bits)]
        out.append(sym)
        bits.pos -= nb
    if bits.pos != 0:
        raise ZstdError("Huffman stream not fully consumed")


# --- Frames and blocks ------------------------------------------------------------------------
class _Frame:
    def __init__(self):
        self.huffman = None
        self.tables = {"ll": None, "of": None, "ml": None}
        self.rep = [1, 4, 8]


def _literals(data, pos, frame):
    h = data[pos]
    kind = h & 3
    fmt = (h >> 2) & 3
    if kind in (0, 1):
        if fmt in (0, 2):
            size, hsize = h >> 3, 1
        elif fmt == 1:
            size, hsize = (h >> 4) | (data[pos + 1] << 4), 2
        else:
            size, hsize = (h >> 4) | (data[pos + 1] << 4) | (data[pos + 2] << 12), 3
        pos += hsize
        if kind == 0:
            return bytes(data[pos:pos + size]), pos + size
        return bytes([data[pos]]) * size, pos + 1
    if fmt == 0 or fmt == 1:
        v = int.from_bytes(data[pos:pos + 3], "little")
        regen, comp, hsize, streams = (v >> 4) & 0x3FF, (v >> 14) & 0x3FF, 3, (1 if fmt == 0 else 4)
    elif fmt == 2:
        v = int.from_bytes(data[pos:pos + 4], "little")
        regen, comp, hsize, streams = (v >> 4) & 0x3FFF, (v >> 18) & 0x3FFF, 4, 4
    else:
        v = int.from_bytes(data[pos:pos + 5], "little")
        regen, comp, hsize, streams = (v >> 4) & 0x3FFFF, (v >> 22) & 0x3FFFF, 5, 4
    pos += hsize
    end = pos + comp
    if kind == 2:
        (frame.huffman, pos) = read_huffman_tree(data, pos)
    elif frame.huffman is None:
        raise ZstdError("treeless literals without a previous Huffman table")
    table, max_bits = frame.huffman
    out = bytearray()
    if streams == 1:
        huffman_stream(table, max_bits, data[pos:end], regen, out)
    else:
        s1, s2, s3 = struct.unpack_from("<3H", data, pos)
        pos += 6
        part = (regen + 3) // 4
        sizes = [s1, s2, s3, end - pos - s1 - s2 - s3]
        counts = [part, part, part, regen - 3 * part]
        for size, count in zip(sizes, counts):
            huffman_stream(table, max_bits, data[pos:pos + size], count, out)
            pos += size
    return bytes(out), end


def _sequences(data, pos, end, frame):
    n = data[pos]
    if n == 0:
        return [], end
    if n < 128:
        pos += 1
    elif n < 255:
        n = ((n - 128) << 8) + data[pos + 1]
        pos += 2
    else:
        n = data[pos + 1] + (data[pos + 2] << 8) + 0x7F00
        pos += 3
    modes = data[pos]
    pos += 1
    spec = (("ll", modes >> 6, LL_DEFAULT, 6, 35, 9), ("of", (modes >> 4) & 3, OF_DEFAULT, 5, 31, 8),
            ("ml", (modes >> 2) & 3, ML_DEFAULT, 6, 52, 9))
    for name, mode, default, default_acc, max_sym, max_acc in spec:
        if mode == 0:
            frame.tables[name] = (build_fse(default, default_acc), default_acc)
        elif mode == 1:
            frame.tables[name] = ([(data[pos], 0, 0)], 0)
            pos += 1
        elif mode == 2:
            probs, acc, used = read_ncount(data, pos, max_sym, max_acc)
            frame.tables[name] = (build_fse(probs, acc), acc)
            pos += used
        elif frame.tables[name] is None:
            raise ZstdError("repeat mode without a previous table")
    (ll_t, ll_a), (of_t, of_a), (ml_t, ml_a) = frame.tables["ll"], frame.tables["of"], frame.tables["ml"]
    bits = BackwardBits(data[pos:end])
    ll_s, of_s, ml_s = bits.read(ll_a), bits.read(of_a), bits.read(ml_a)
    seqs = []
    for i in range(n):
        of_code = of_t[of_s][0]
        ml_code = ml_t[ml_s][0]
        ll_code = ll_t[ll_s][0]
        offset_value = (1 << of_code) + bits.read(of_code)
        ml = ML_BASE[ml_code] + bits.read(ML_BITS[ml_code])
        ll = LL_BASE[ll_code] + bits.read(LL_BITS[ll_code])
        seqs.append((ll, offset_value, ml))
        if i != n - 1:
            _, nb, base = ll_t[ll_s]
            ll_s = base + bits.read(nb)
            _, nb, base = ml_t[ml_s]
            ml_s = base + bits.read(nb)
            _, nb, base = of_t[of_s]
            of_s = base + bits.read(nb)
    if not bits.finished():
        raise ZstdError("sequence bitstream not fully consumed")
    return seqs, end


def _execute(out, literals, seqs, frame):
    lit = 0
    rep = frame.rep
    for ll, offset_value, ml in seqs:
        out += literals[lit:lit + ll]
        lit += ll
        if offset_value > 3:
            offset = offset_value - 3
            rep[:] = [offset, rep[0], rep[1]]
        else:
            idx = offset_value - 1 if ll != 0 else offset_value  # LL == 0 shifts the repeat codes
            if idx == 0:
                offset = rep[0]
            elif idx == 3:
                offset = rep[0] - 1
                if offset == 0:
                    raise ZstdError("zero offset")
                rep[:] = [offset, rep[0], rep[1]]
            else:
                offset = rep[idx]
                if idx == 1:
                    rep[:] = [offset, rep[0], rep[2]]
                else:
                    rep[:] = [offset, rep[0], rep[1]]
        if offset > len(out):
            raise ZstdError("offset beyond the decoded data")
        start = len(out) - offset
        if offset >= ml:
            out += out[start:start + ml]
        else:
            for k in range(ml):
                out.append(out[start + k])
    out += literals[lit:]


def decompress(data):
    data = bytes(data)
    pos = 0
    result = bytearray()
    while pos < len(data):
        (magic,) = struct.unpack_from("<I", data, pos)
        if 0x184D2A50 <= magic <= 0x184D2A5F:  # skippable frame
            (size,) = struct.unpack_from("<I", data, pos + 4)
            pos += 8 + size
            continue
        if magic != 0xFD2FB528:
            raise ZstdError("not a zstd frame (magic 0x%08x)" % magic)
        pos += 4
        desc = data[pos]
        pos += 1
        fcs_flag, single, checksum, dict_flag = desc >> 6, (desc >> 5) & 1, (desc >> 2) & 1, desc & 3
        if not single:
            pos += 1  # window descriptor
        if dict_flag:
            raise ZstdError("dictionaries are not supported")
        fcs_size = (1 if single else 0, 2, 4, 8)[fcs_flag]
        pos += fcs_size
        frame = _Frame()
        out = bytearray()
        while True:
            bh = int.from_bytes(data[pos:pos + 3], "little")
            pos += 3
            last, btype, bsize = bh & 1, (bh >> 1) & 3, bh >> 3
            if btype == 0:
                out += data[pos:pos + bsize]
                pos += bsize
            elif btype == 1:
                out += bytes([data[pos]]) * bsize
                pos += 1
            elif btype == 2:
                end = pos + bsize
                literals, p = _literals(data, pos, frame)
                seqs, p = _sequences(data, p, end, frame)
                _execute(out, literals, seqs, frame)
                pos = end
            else:
                raise ZstdError("reserved block type")
            if last:
                break
        if checksum:
            (want,) = struct.unpack_from("<I", data, pos)
            pos += 4
            got = xxh64(bytes(out)) & 0xFFFFFFFF
            if got != want:
                raise ZstdError("content checksum mismatch (%08x != %08x)" % (got, want))
        result += out
    return bytes(result)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    raw = decompress(open(src, "rb").read())
    open(dst, "wb").write(raw)
    print("%s: %d bytes" % (dst, len(raw)))
