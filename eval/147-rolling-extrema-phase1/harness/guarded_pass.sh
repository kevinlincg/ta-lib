#!/bin/bash
# Guarded vs unguarded, from the real generated library, for the candidates given.
# Only `scripts/build.py` is needed (no language servers), so this is cheap.
# Also diffs the generated GUARDED prologue between candidates, to check the claim
# that the guarded delta is candidate-independent instead of asserting it.
set -uo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
R="$HD/../ta-lib"
OUT="$HD/guarded.csv"
: > "$OUT"
first=1
for c in "$@"; do
  git -C "$R" checkout -- . >/dev/null 2>&1
  if [ "$c" != C0 ]; then
    for f in max midpoint; do cp "$HD/variants/$f.$c.c" "$R/ta_codegen/input/$f/$f.c"; done
  fi
  ( cd "$R/ta_codegen/generator" && cargo run --quiet -- generate ) >/dev/null 2>&1 \
    || { echo "$c: generate failed" >&2; continue; }
  # snapshot the guarded entry point for the prologue comparison
  sed -n '/^TA_LIB_API TA_RetCode TA_MAX(/,/^}/p' "$R/src/ta_func/ta_MAX.c" > "$HD/gates/guardprol_$c.txt"
  ( cd "$R" && python3 scripts/build.py ) >/dev/null 2>&1 \
    || { echo "$c: build failed" >&2; continue; }
  if [ $first -eq 1 ]; then
    bash "$HD/guarded.sh" "$c" >> "$OUT"; first=0
  else
    bash "$HD/guarded.sh" "$c" | grep -v '^#' >> "$OUT"
  fi
  echo "$c done" >&2
done
echo "--- guarded prologue differences between candidates ---"
ls "$HD"/gates/guardprol_*.txt | while read -r f; do echo "$(basename "$f"): $(wc -l < "$f") lines"; done
git -C "$R" checkout -- . >/dev/null 2>&1
