#!/usr/bin/env python3
"""Aggregate the layout sweep.

The unit is one (func, cand, shape, period) point.  Within a layout: `min` over
--reps (removes scheduler noise).  Across layouts: **median** plus the full
[min..max] range, because identical machine code has been measured to move
-13%..+2% on `.text` placement alone (maintainer, #147), and this harness's own
layouts are verified with `nm -n` to actually move the code.

Ratios against C0 are computed PER LAYOUT and then summarised, so the reported
spread answers "does this win survive code placement?" rather than "how noisy was
one build?".

  aggregate.py <csv> [ns|ratio] [funcs] [periods]
"""
import csv
import statistics
import sys
from collections import defaultdict

SHAPE_ORDER = ["randwalk", "randwalk-lo", "randwalk-hi", "gbm",
               "trend-chop-0.5p", "trend-chop-1p", "trend-chop-2p", "trend-chop-4p",
               "mono-up", "mono-down", "constant"]
GROUP = {s: ("gate" if i < 4 else "trend" if i < 8 else "tail")
         for i, s in enumerate(SHAPE_ORDER)}
LABEL = {
    "C0": "baseline (cached extremum + rescan)",
    "C1": "monotonic deque",
    "C2": "Van Herk, block-batched",
    "C3": "Van Herk, per-sample",
    "C4": "two-stack queue",
    "C5": "rescan tie-break -> `>=`",
    "C6": "reverse rescan (codegen-eliminated)",
    "C7": "deque, pow-2 capacity",
    "C8": "no cache, full rescan/bar",
    "C9": "incoming arm `else if` -> `if`",
}


def load(path):
    d = defaultdict(dict)
    with open(path) as fh:
        for row in csv.reader(fh):
            if not row or row[0].startswith("#"):
                continue
            if len(row) != 6:
                continue          # partially-flushed line while a run is in flight
            lay, func, cand, shape, per, ns = row
            try:
                d[(func, cand, shape, int(per))][lay] = float(ns)
            except ValueError:
                continue
    return d


def med(v):
    return statistics.median(sorted(v))


def main(path, mode="ratio", funcs=None, periods=None):
    d = load(path)
    layouts = sorted({l for v in d.values() for l in v})
    allf = funcs or sorted({k[0] for k in d})
    allc = sorted({k[1] for k in d}, key=lambda c: int(c[1:]))
    shapes = [s for s in SHAPE_ORDER if any(k[2] == s for k in d)]
    print(f"{len(layouts)} layouts: {' '.join(layouts)}")
    print(f"aggregation: min over reps within a layout; median [min..max] across layouts\n")

    for func in allf:
        pers = periods or sorted({k[3] for k in d if k[0] == func})
        for per in pers:
            base = {s: d.get((func, "C0", s, per)) for s in shapes}
            if not any(base.values()):
                continue
            print(f"### {func.upper()}  period={per}")
            head = f"{'cand':<5}{'algorithm':<36}"
            for s in shapes:
                head += f"{s[:13]:>15}"
            print(head)
            print(f"{'':<5}{'group ->':<36}" + "".join(f"{GROUP[s]:>15}" for s in shapes))
            for c in allc:
                line = f"{c:<5}{LABEL.get(c, ''):<36}"
                for s in shapes:
                    e = d.get((func, c, s, per))
                    b = base[s]
                    if not e or not b:
                        line += f"{'-':>15}"
                        continue
                    if c == "C0" or mode == "ns":
                        line += f"{med(e.values()):>10.2f} ns"
                    else:
                        common = sorted(set(e) & set(b))
                        rs = sorted(e[l] / b[l] for l in common)
                        line += f"{med(rs):>7.2f}[{rs[0]:.2f}-{rs[-1]:.2f}]"
                print(line)
            print()


if __name__ == "__main__":
    p = sys.argv[1] if len(sys.argv) > 1 else "resA-layouts.csv"
    m = sys.argv[2] if len(sys.argv) > 2 else "ratio"
    f = sys.argv[3].split(",") if len(sys.argv) > 3 else None
    pr = [int(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 else None
    main(p, m, f, pr)
