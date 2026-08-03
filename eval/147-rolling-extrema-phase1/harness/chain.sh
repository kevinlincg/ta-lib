#!/bin/bash
# 1. wait for the running gate queue
# 2. re-gate C1 with the swapped comparison (its first run died on the
#    unparenthesised-cast translator bug, which is now worked around)
# 3. run the reportable measurement with nothing else on the machine
set -uo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
until grep -q "^queue done" "$HD/gates/SUMMARY.txt" 2>/dev/null; do sleep 30; done
echo "=== gate queue finished ==="
bash "$HD/queue.sh" C1 >>"$HD/gates/queue.out" 2>&1
echo "=== C1 re-gated ==="
sleep 10
bash "$HD/final.sh" >"$HD/final.log" 2>&1
echo "=== measurement finished ==="
