# sweep_report.py <sweep.csv>: summary of macos/tests/audio/run_sweep.sh's per-cue results.
# Per bank: cues, audible, silent, play errors, quiet (peak below -40 dBFS), clipped,
# capped (still playing at the cap: loops, music, long voice-overs). Then every cue that
# was silent, failed to play, clipped or was quiet. Stdlib only (python3 -I).
import collections
import csv
import math
import sys


def main():
    rows = list(csv.DictReader(open(sys.argv[1])))
    banks = collections.OrderedDict()
    for r in rows:
        banks.setdefault(r['bank'], []).append(r)
    total = collections.Counter()
    print('%-38s %5s %7s %6s %6s %5s %7s %6s' % ('bank', 'cues', 'audible', 'silent', 'errors', 'quiet', 'clipped', 'capped'))
    odd = []
    for bank, cues in banks.items():
        c = collections.Counter()
        for r in cues:
            c['cues'] += 1
            error = r['play_result'] != '0x00000000'
            loud = int(r['loud_frames'])
            peak = float(r['peak'])
            if error:
                c['errors'] += 1
                odd.append(('play error', r))
            elif loud == 0:
                c['silent'] += 1
                odd.append(('silent', r))
            else:
                c['audible'] += 1
                if peak < 0.01:
                    c['quiet'] += 1
                    odd.append(('quiet (peak %.1f dBFS)' % (20 * math.log10(peak)), r))
            if int(r['clipped']) > 0:
                c['clipped'] += 1
                odd.append(('clipped %s samples' % r['clipped'], r))
            if r['ended'] == 'capped':
                c['capped'] += 1
        total.update(c)
        print('%-38s %5d %7d %6d %6d %5d %7d %6d' % (bank, c['cues'], c['audible'], c['silent'], c['errors'],
                                                      c['quiet'], c['clipped'], c['capped']))
    print('%-38s %5d %7d %6d %6d %5d %7d %6d' % ('TOTAL', total['cues'], total['audible'], total['silent'],
                                                  total['errors'], total['quiet'], total['clipped'], total['capped']))
    print()
    for why, r in odd:
        print('%-26s %s cue %s %s (state %s, %s, %s ms, peak %s)' % (why, r['bank'], r['cue'], r['name'], r['end_state'],
                                                                     r['ended'], r['wall_ms'], r['peak']))


if __name__ == '__main__':
    main()
