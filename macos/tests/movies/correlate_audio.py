"""Finds a reference sound inside a capture by envelope correlation (package movies; notes:
macos/porting/movies.md). Python 3.9 stdlib only; run with python3 -I.

  correlate_audio.py CAPTURE.raw REFERENCE.wav [--format s16|f32] [--rate 44100]
                     [--channels 2] [--window 0.02]

CAPTURE is raw SDL disk-driver output; REFERENCE a 16-bit PCM WAV (for example the
movie_census --shot NAME@audio dump of a movie's decoded audio). Both become RMS envelopes
over WINDOW seconds; the lag that maximises the normalised correlation is where the
reference starts in the capture. Prints the lag, the correlation (1.0 = identical shape)
and the level of the capture relative to the reference over the matched stretch (0 dB =
played at the decoded level).
"""
import argparse
import array
import math
import sys
import wave


def envelope(samples, channels, rate, window):
    n = max(1, int(window * rate))
    out = []
    frames = len(samples) // channels
    for f0 in range(0, frames - n + 1, n):
        acc = 0.0
        for i in range(f0 * channels, (f0 + n) * channels):
            v = samples[i]
            acc += v * v
        out.append(math.sqrt(acc / (n * channels)))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("capture")
    p.add_argument("reference")
    p.add_argument("--format", default="s16", choices=["s16", "f32"])
    p.add_argument("--rate", type=int, default=44100)
    p.add_argument("--channels", type=int, default=2)
    p.add_argument("--window", type=float, default=0.02)
    a = p.parse_args()

    data = open(a.capture, "rb").read()
    if a.format == "s16":
        cap = array.array("h")
        cap.frombytes(data[: len(data) - len(data) % 2])
        cap = [x / 32768.0 for x in cap]
    else:
        cap = array.array("f")
        cap.frombytes(data[: len(data) - len(data) % 4])
        cap = list(cap)
    with wave.open(a.reference, "rb") as w:
        rch, rrate = w.getnchannels(), w.getframerate()
        ref = array.array("h")
        ref.frombytes(w.readframes(w.getnframes()))
        ref = [x / 32768.0 for x in ref]

    ec = envelope(cap, a.channels, a.rate, a.window)
    er = envelope(ref, rch, rrate, a.window)
    if len(er) > len(ec):
        print("reference longer than capture")
        return 1
    mr = sum(er) / len(er)
    dr = [x - mr for x in er]
    nr = math.sqrt(sum(x * x for x in dr))
    best = (-2.0, 0)
    for lag in range(0, len(ec) - len(er) + 1):
        seg = ec[lag: lag + len(er)]
        ms = sum(seg) / len(seg)
        num = 0.0
        ss = 0.0
        for x, y in zip(seg, dr):
            d = x - ms
            num += d * y
            ss += d * d
        c = num / math.sqrt(ss * nr * nr) if ss > 0 and nr > 0 else 0.0
        if c > best[0]:
            best = (c, lag)
    c, lag = best
    seg = ec[lag: lag + len(er)]
    pc = math.sqrt(sum(x * x for x in seg) / len(seg))
    pr = math.sqrt(sum(x * x for x in er) / len(er))
    gain = 20 * math.log10(pc / pr) if pc > 0 and pr > 0 else float("nan")
    print("reference %s (%.2f s) found at %.3f s in %s: correlation %.3f, level %+.1f dB vs decoded"
          % (a.reference, len(er) * a.window, lag * a.window, a.capture, c, gain))
    return 0


if __name__ == "__main__":
    sys.exit(main())
