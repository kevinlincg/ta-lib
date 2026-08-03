#!/bin/bash
# Run gate.sh over a list of candidates, sequentially, never stopping on failure —
# a candidate that dies is recorded with its cause and the queue moves on.
set -uo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HD/gates"
SUM="$HD/gates/SUMMARY.txt"
for c in "$@"; do
  s=$(date +%s)
  bash "$HD/gate.sh" "$c" >>"$HD/gates/$c.stdout" 2>&1
  rc=$?
  e=$(( $(date +%s) - s ))
  echo "$c rc=$rc ${e}s" | tee -a "$SUM"
done
echo "queue done" | tee -a "$SUM"
