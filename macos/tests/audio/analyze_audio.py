# analyze_audio.py: levels of a raw capture from SDL3's disk audio driver (32-bit float,
# interleaved; the audio package's runs use 48 kHz stereo), and WAV excerpts of it.
#
#   python3 -I analyze_audio.py <capture.f32> [--rate 48000] [--channels 2]
#       [--from S] [--to S] [--wav out.wav --wav-from S --wav-seconds N] [--quiet]
#
# Prints one line per second (RMS and peak in dBFS, clipped samples, loud fraction) and a
# summary: duration, overall RMS/peak, clipped samples (|x| >= 1.0), silent stretches
# (seconds whose peak is below -80 dBFS), and the share of seconds with sound. With --wav
# it writes that stretch as a 16-bit PCM WAV (clamped to +-1.0) for listening.
# Stdlib only (run with python3 -I). Game-derived output: keep it under build/.
import argparse
import array
import math
import os
import struct
import sys
import wave

SILENCE = 1.0e-4  # -80 dBFS


def db(x):
    return 20.0 * math.log10(x) if x > 0 else -999.0


def read_seconds(path, rate, channels, start, stop):
    """Yields (second index, array of floats) for each whole second in [start, stop)."""
    frame_bytes = 4 * channels
    total_frames = os.path.getsize(path) // frame_bytes
    total_seconds = total_frames // rate
    stop = total_seconds if stop is None else min(stop, total_seconds)
    with open(path, 'rb') as f:
        f.seek(start * rate * frame_bytes)
        for second in range(start, stop):
            data = array.array('f')
            data.frombytes(f.read(rate * frame_bytes))
            if sys.byteorder != 'little':
                data.byteswap()
            yield second, data


def write_wav(path, capture, rate, channels, start, seconds):
    frame_bytes = 4 * channels
    with open(capture, 'rb') as f:
        f.seek(int(start * rate) * frame_bytes)
        data = array.array('f')
        data.frombytes(f.read(int(seconds * rate) * frame_bytes))
    if sys.byteorder != 'little':
        data.byteswap()
    pcm = array.array('h', (int(max(-1.0, min(1.0, x)) * 32767.0) for x in data))
    if sys.byteorder != 'little':
        pcm.byteswap()
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--rate', type=int, default=48000)
    ap.add_argument('--channels', type=int, default=2)
    ap.add_argument('--from', dest='start', type=int, default=0)
    ap.add_argument('--to', dest='stop', type=int, default=None)
    ap.add_argument('--wav')
    ap.add_argument('--wav-from', type=float, default=0.0)
    ap.add_argument('--wav-seconds', type=float, default=25.0)
    ap.add_argument('--quiet', action='store_true', help='summary only')
    a = ap.parse_args()

    n_total = 0
    sumsq_total = 0.0
    peak_total = 0.0
    clipped_total = 0
    sounding = 0
    seconds = 0
    silent_runs = []
    run_start = None
    channel_sumsq = [0.0] * a.channels
    for second, data in read_seconds(a.capture, a.rate, a.channels, a.start, a.stop):
        seconds += 1
        sumsq = 0.0
        peak = 0.0
        clipped = 0
        loud = 0
        for i, x in enumerate(data):
            m = -x if x < 0 else x
            if m > peak:
                peak = m
            if m >= 1.0:
                clipped += 1
            if m > SILENCE:
                loud += 1
            sumsq += x * x
            channel_sumsq[i % a.channels] += x * x
        n = len(data)
        rms = math.sqrt(sumsq / n) if n else 0.0
        n_total += n
        sumsq_total += sumsq
        peak_total = max(peak_total, peak)
        clipped_total += clipped
        if peak > SILENCE:
            sounding += 1
            if run_start is not None:
                silent_runs.append((run_start, second))
                run_start = None
        elif run_start is None:
            run_start = second
        if not a.quiet:
            print('%5d s  rms %7.1f dBFS  peak %7.1f dBFS  clipped %6d  loud %5.1f%%'
                  % (second, db(rms), db(peak), clipped, 100.0 * loud / max(n, 1)))
    if run_start is not None:
        silent_runs.append((run_start, a.start + seconds))
    rms_total = math.sqrt(sumsq_total / n_total) if n_total else 0.0
    print('summary: %d s (from %d s), rms %.1f dBFS, peak %.1f dBFS, clipped samples %d, '
          'seconds with sound %d (%.0f%%)'
          % (seconds, a.start, db(rms_total), db(peak_total), clipped_total, sounding,
             100.0 * sounding / max(seconds, 1)))
    per_channel = ', '.join('%.1f' % db(math.sqrt(s / max(n_total // a.channels, 1))) for s in channel_sumsq)
    print('summary: rms per channel (dBFS): %s' % per_channel)
    long_runs = [(s, e) for s, e in silent_runs if e - s >= 2]
    print('summary: silent stretches >= 2 s: %s' % (', '.join('%d-%d s' % r for r in long_runs) or 'none'))
    if a.wav:
        write_wav(a.wav, a.capture, a.rate, a.channels, a.wav_from, a.wav_seconds)
        print('wrote %s (%.1f s from %.1f s)' % (a.wav, a.wav_seconds, a.wav_from))


if __name__ == '__main__':
    main()
