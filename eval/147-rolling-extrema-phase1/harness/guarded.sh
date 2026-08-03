#!/bin/bash
# Build + run the guarded-vs-unguarded probe against the library currently in the
# tree.  Usage: guarded.sh <tag>   (tag only labels the output rows)
set -euo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
R="$HD/../ta-lib"
TAG=${1:?usage: guarded.sh <tag>}
# NB: `ls a b | head -1` under `set -o pipefail` fails when a does not exist, which
# silently aborted this script the first time round.
LIB=""
for cand in "$R/cmake-build/libta-lib.a" "$R/cmake-build/lib/libta-lib.a"; do
  [ -f "$cand" ] && { LIB="$cand"; break; }
done
[ -n "$LIB" ] || { echo "no libta-lib.a — run scripts/build.py first" >&2; exit 1; }
cc -O3 -fno-lto -DNDEBUG -I"$R/include" -I"$R/src/tools/ta_bench" \
   "$HD/guarded.c" "$LIB" -lm -o "$HD/bin/guarded_$TAG"
"$HD/bin/guarded_$TAG" "$TAG" "${@:2}"
