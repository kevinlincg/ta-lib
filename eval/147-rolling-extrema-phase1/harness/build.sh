#!/bin/bash
# Build the #147 measurement driver in several CODE LAYOUTS.
#
# Why: the maintainer measured IDENTICAL machine code move -13%..+2% purely on
# .text placement, and will not accept a single-build number.  Each layout below
# leaves every candidate's machine code alone (same TU, same flags for the
# candidate objects in the pad/order layouts) and only moves it:
#
#   pad=N     a `.space N` blob linked into __text AHEAD of the candidate
#             objects, so every candidate function shifts by N bytes
#   order     link order of the candidate objects (A = declaration order,
#             R = reversed), which changes which functions share cache sets
#   align=N   -falign-functions=N on the candidate objects: this one DOES
#             change the code (padding between functions), so it is reported
#             as a separate layout family, not mixed into the pad family
#
# run.sh takes the median and the range over all layouts.
set -euo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
REPO="$HD/../ta-lib"
CORPUS="$REPO/src/tools/ta_bench"
GEN="$HD/gen"
OBJ="$HD/obj"
BIN="$HD/bin"
CC=${CC:-cc}
BASEFLAGS=${BASEFLAGS:--O3 -fno-lto -DNDEBUG}

rm -rf "$GEN" "$OBJ" "$BIN"
mkdir -p "$GEN" "$OBJ" "$BIN"

FUNCS="max midpoint"
CANDS="C0 C1 C2 C3 C4 C5 C6 C7 C8 C9"

# --- one TU per candidate, function renamed -------------------------------
for f in $FUNCS; do
  for k in $CANDS; do
    cat > "$GEN/${f}_${k}.c" <<EOF
#include "shim.h"
#define ${f}_lookback ${f}_${k}_lookback
#define ${f} ${f}_${k}
#include "../variants/${f}.${k}.c"
EOF
  done
done

# --- pad blobs ------------------------------------------------------------
mkpad () { # $1 = bytes
  local n=$1
  if [ "$n" -eq 0 ]; then
    printf '.section __TEXT,__text\n' > "$GEN/pad_$n.s"
  else
    printf '.section __TEXT,__text\n.p2align 4\n_h147_pad_%s:\n.space %s\n' "$n" "$n" \
      > "$GEN/pad_$n.s"
  fi
  $CC -c "$GEN/pad_$n.s" -o "$OBJ/pad_$n.o"
}
for n in 0 64 192 576 1728 5184 2112 9280; do mkpad $n; done

# --- candidate objects, one set per alignment ----------------------------
for al in default 32; do
  af=""
  [ "$al" != default ] && af="-falign-functions=$al"
  for f in $FUNCS; do
    for k in $CANDS; do
      $CC $BASEFLAGS $af -I"$HD" -c "$GEN/${f}_${k}.c" -o "$OBJ/${f}_${k}.$al.o"
    done
  done
done

# --- driver ---------------------------------------------------------------
$CC $BASEFLAGS -I"$HD" -I"$CORPUS" -c "$HD/driver.c" -o "$OBJ/driver.o"

objs_order () { # $1 = align set, $2 = A|R
  local al=$1 dir=$2 list=()
  for f in $FUNCS; do for k in $CANDS; do list+=("$OBJ/${f}_${k}.$al.o"); done; done
  if [ "$dir" = R ]; then
    local rev=() i
    for (( i=${#list[@]}-1 ; i>=0 ; i-- )); do rev+=("${list[$i]}"); done
    printf '%s\n' "${rev[@]}"
  else
    printf '%s\n' "${list[@]}"
  fi
}

link () { # $1 tag  $2 pad  $3 align  $4 order
  local tag=$1 pad=$2 al=$3 dir=$4
  mapfile -t objs < <(objs_order "$al" "$dir")
  $CC $BASEFLAGS -o "$BIN/bench_$tag" "$OBJ/pad_$pad.o" "${objs[@]}" "$OBJ/driver.o" -lm
  echo "$tag pad=$pad align=$al order=$dir" >> "$BIN/layouts.txt"
}

: > "$BIN/layouts.txt"
# pad family (identical machine code, different .text placement)
link L0    0     default A
link L1    64    default A
link L2    192   default A
link L3    576   default A
link L4    1728  default A
link L5    5184  default A
# link-order family (identical machine code, different neighbours)
link L6    0     default R
link L7    576   default R
# alignment family (padding between functions changes)
link L8    0     32      A
link L9    576   32      A
link L10   2112  default A
link L11   9280  default A

echo "built $(ls "$BIN" | grep -c '^bench_') layouts"
cat "$BIN/layouts.txt"
