# Compares two metrics.tsv files (gdi_reference.exe on Windows/Wine vs fonts_test on the
# Mac): per face and size, every column that differs, and for the advances which
# characters. Python 3 stdlib only.
#
#   python3 -I macos/tests/fonts/compare_tsv.py metrics_gdi.tsv build/m2-fonts-tests/out/metrics.tsv
import sys


def load(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    header = lines[0].split("\t")
    rows = {}
    for line in lines[1:]:
        if line.strip():
            row = dict(zip(header, line.rstrip("\r").split("\t")))
            rows[(row["face"], row["size"])] = row
    return header, rows


def main():
    header, reference = load(sys.argv[1])
    _, ours = load(sys.argv[2])
    rows = differing = 0
    for key, want in reference.items():
        got = ours.get(key)
        if got is None:
            print("%s %s: missing from %s" % (key[0], key[1], sys.argv[2]))
            continue
        rows += 1
        notes = []
        for column in header[2:]:
            if column.startswith("advances"):
                a = want[column].split(",")
                b = got[column].split(",")
                chars = ["%r %s->%s" % (chr(0x20 + i), a[i], b[i]) for i in range(min(len(a), len(b))) if a[i] != b[i]]
                if chars:
                    notes.append("advances: " + ", ".join(chars))
            elif want.get(column) != got.get(column):
                notes.append("%s %s->%s" % (column, want.get(column), got.get(column)))
        if notes:
            differing += 1
            print("%s %s: %s" % (key[0], key[1], "; ".join(notes)))
    print("%d rows compared, %d differ (reference -> ours)" % (rows, differing))


main()
