"""Analyses a raw capture of SDL's disk audio driver (package movies; notes:
macos/porting/movies.md). Python 3.9 stdlib only; run with python3 -I.

  analyse_audio.py RAW [--format s16|f32] [--rate 48000] [--channels 2] [--window 0.5]
                   [--wav OUT.wav] [--from SECONDS] [--to SECONDS] [--quiet]

SDL's disk driver writes the device's mixed output, in the device format it logs at
open ("Writing to file [...], format=S16LE channels=2 freq=48000"). Prints, per window:
RMS and peak (dBFS), the zero-crossing frequency of the first channel and left/right
correlation; then totals: duration, peak, clipped samples, silent stretches, and the
longest non-silent stretch. --wav writes [from, to) as a 16-bit WAV file to listen to.
"""
import argparse
import array
import math
import sys
import wave


def load(path, fmt):
    data = open(path, "rb").read()
    if fmt == "s16":
        a = array.array("h")
        a.frombytes(data[: len(data) - len(data) % 2])
        return [x / 32768.0 for x in a]
    a = array.array("f")
    a.frombytes(data[: len(data) - len(data) % 4])
    return list(a)


def db(x):
    return 20 * math.log10(x) if x > 0 else -999.0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("raw")
    p.add_argument("--format", default="s16", choices=["s16", "f32"])
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--channels", type=int, default=2)
    p.add_argument("--window", type=float, default=0.5)
    p.add_argument("--wav")
    p.add_argument("--from", dest="start", type=float, default=0.0)
    p.add_argument("--to", dest="end", type=float, default=None)
    p.add_argument("--quiet", action="store_true", help="totals only")
    p.add_argument("--silence-db", type=float, default=-60.0)
    a = p.parse_args()

    samples = load(a.raw, a.format)
    ch = a.channels
    frames = len(samples) // ch
    seconds = frames / float(a.rate)
    win = max(1, int(a.window * a.rate))
    peak_all = 0.0
    clipped = 0
    silent_runs = []
    loud_runs = []
    run_start = None
    loud_start = None
    rows = []
    for w0 in range(0, frames, win):
        w1 = min(frames, w0 + win)
        n = w1 - w0
        acc = 0.0
        peak = 0.0
        lr = ll = rr = 0.0
        crossings = 0
        prev = samples[w0 * ch]
        for f in range(w0, w1):
            base = f * ch
            for c in range(ch):
                v = samples[base + c]
                acc += v * v
                av = abs(v)
                if av > peak:
                    peak = av
                if av >= 0.9999:
                    clipped += 1
            left = samples[base]
            right = samples[base + 1] if ch > 1 else left
            lr += left * right
            ll += left * left
            rr += right * right
            if (left >= 0) != (prev >= 0):
                crossings += 1
            prev = left
        rms = math.sqrt(acc / (n * ch)) if n else 0.0
        peak_all = max(peak_all, peak)
        corr = lr / math.sqrt(ll * rr) if ll > 0 and rr > 0 else 0.0
        freq = crossings / 2.0 / (n / float(a.rate)) if n else 0.0
        t0 = w0 / float(a.rate)
        rows.append((t0, rms, peak, freq, corr))
        quiet = db(rms) < a.silence_db
        if quiet and run_start is None:
            run_start = t0
        if not quiet and run_start is not None:
            silent_runs.append((run_start, t0))
            run_start = None
        if not quiet and loud_start is None:
            loud_start = t0
        if quiet and loud_start is not None:
            loud_runs.append((loud_start, t0))
            loud_start = None
    end_t = frames / float(a.rate)
    if run_start is not None:
        silent_runs.append((run_start, end_t))
    if loud_start is not None:
        loud_runs.append((loud_start, end_t))

    if not a.quiet:
        print("%8s %9s %9s %9s %6s" % ("t(s)", "rms dBFS", "peak dBFS", "zc Hz", "L/R"))
        for t0, rms, peak, freq, corr in rows:
            print("%8.2f %9.1f %9.1f %9.0f %6.2f" % (t0, db(rms), db(peak), freq, corr))
    sound = sum(b - a_ for a_, b in loud_runs)
    print("file=%s duration=%.2fs rate=%d channels=%d format=%s" % (a.raw, seconds, a.rate, ch, a.format))
    print("peak=%.1f dBFS clipped_samples=%d non_silent=%.2fs (windows above %.0f dBFS)"
          % (db(peak_all), clipped, sound, a.silence_db))
    print("sound stretches: " + ", ".join("%.2f-%.2f" % r for r in loud_runs[:20])
          + (" ..." if len(loud_runs) > 20 else ""))

    if a.wav:
        f0 = int(a.start * a.rate)
        f1 = frames if a.end is None else min(frames, int(a.end * a.rate))
        out = array.array("h", (max(-32768, min(32767, int(round(v * 32767))))
                                for v in samples[f0 * ch: f1 * ch]))
        with wave.open(a.wav, "wb") as w:
            w.setnchannels(ch)
            w.setsampwidth(2)
            w.setframerate(a.rate)
            w.writeframes(out.tobytes())
        print("wrote %s (%.2f-%.2f s)" % (a.wav, f0 / float(a.rate), f1 / float(a.rate)))


if __name__ == "__main__":
    sys.exit(main())
