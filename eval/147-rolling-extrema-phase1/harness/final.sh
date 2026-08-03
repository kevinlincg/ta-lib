#!/bin/bash
# The reportable measurement run.  Must be run with NOTHING else on the machine:
# the gate queue rebuilds Rust/Java/.NET and would contaminate every number.
#
# Two passes, because a full 12-layout sweep of the whole grid does not fit in a
# sensible wall time and the layout axis matters most where the conclusions are:
#
#   A  headline grid, ALL 12 LAYOUTS   -> the median-of-layouts + range numbers
#      7 shapes x 4 periods x every candidate
#   B  landscape, ONE layout (L0)      -> the full shape/period picture
#      all 11 shapes x 6 periods, single build, clearly labelled as such
set -euo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
cd "$HD"

echo "== correctness first (nothing is timed for a candidate that fails) ==" >&2
./bin/bench_L0 --verify --points=20000
./bin/bench_L0 --verify --points=5000  --seed=13
./bin/bench_L0 --verify --points=50000 --seed=99 --trend-strength=1.0

SHAPES_A=randwalk,randwalk-hi,gbm,trend-chop-1p,trend-chop-4p,mono-down,constant
echo "== pass A: headline grid across all 12 layouts ==" >&2
bash run.sh "$HD/resA-layouts.csv" --points=100000 --budget-ms=15 --reps=3 \
  --shapes=$SHAPES_A --periods=14,30,200,1000

echo "== pass B: full landscape, single layout L0 ==" >&2
./bin/bench_L0 --tag=L0 --points=100000 --budget-ms=15 --reps=3 > "$HD/resB-landscape.csv"

echo "== pass C: short ranges (exposes the per-call malloc in C1..C4/C7) ==" >&2
: > "$HD/resC-shortrange.csv"
first=1
for n in 256 1024 8192; do
  for b in bin/bench_L0 bin/bench_L3 bin/bench_L6; do
    tag="$(basename $b | sed 's/^bench_//')n$n"
    if [ $first -eq 1 ]; then
      "$b" --tag="$tag" --points=$n --budget-ms=15 --reps=3 \
        --shapes=randwalk,trend-chop-1p --periods=14,30,200 >> "$HD/resC-shortrange.csv"
      first=0
    else
      "$b" --tag="$tag" --points=$n --budget-ms=15 --reps=3 \
        --shapes=randwalk,trend-chop-1p --periods=14,30,200 | grep -v '^#' \
        >> "$HD/resC-shortrange.csv"
    fi
  done
done

echo "done" >&2
