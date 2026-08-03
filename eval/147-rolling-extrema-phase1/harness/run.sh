#!/bin/bash
# Sweep every layout built by build.sh.  One CSV, one line per
# (layout,func,cand,shape,period); aggregate.py takes the median and range over
# the layout axis.
set -euo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
OUT=${1:-$HD/results.csv}
shift || true
: > "$OUT"
first=1
for b in "$HD"/bin/bench_L*; do
  tag=$(basename "$b" | sed 's/^bench_//')
  echo "  layout $tag ..." >&2
  if [ $first -eq 1 ]; then
    "$b" --tag="$tag" "$@" >> "$OUT"
    first=0
  else
    "$b" --tag="$tag" "$@" | grep -v '^#' >> "$OUT"
  fi
done
echo "wrote $OUT ($(grep -vc '^#' "$OUT") rows)" >&2
