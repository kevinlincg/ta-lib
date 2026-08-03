#!/bin/bash
# In-tree gate run for one candidate.
#   gate.sh <cand>          e.g. gate.sh C6      ('C0' = pristine tree, the control)
#
# Installs harness/variants/{max,midpoint}.<cand>.c into ta_codegen/input/, runs the
# real codegen, and runs the gates the maintainer named:
#   * generate                    must not fail (the streaming classifier lives here)
#   * only-intended-files check   nothing else in the tree may move
#   * scripts/regtest.py          project driver: build + servers + cross-language
#   * ta_regtest --xlang-hash     cross-language BITWISE parity, 4 backends (#115)
#   * ta_regtest --codegen        includes stream_verify (batch vs TA_S_* bitwise)
#   * ta_regtest (default)        includes the I/O aliasing test (#130)
#   * ta_regtest --fuzz-064
# Logs land in harness/gates/. Verdict lines are echoed with a [cand] prefix.
set -uo pipefail
HD="$(cd "$(dirname "$0")" && pwd)"
R="$HD/../ta-lib"
CAND=${1:?usage: gate.sh <cand>}
LOGD="$HD/gates"; mkdir -p "$LOGD"
LOG="$LOGD/$CAND.log"
: > "$LOG"

# JDK 17 is required by the Java backend (`--release 17`); the system JDK here is
# 11, so a local Temurin 17 is prepended for this process only.
if [ -d "$HD/../jdk/jdk-17.0.20+8/Contents/Home" ]; then
  export JAVA_HOME="$HD/../jdk/jdk-17.0.20+8/Contents/Home"
  export PATH="$JAVA_HOME/bin:$PATH"
fi

# The .NET server project targets net10.0; the system SDK here is 7.0.200, so a
# local .NET 10 SDK is prepended for this process only.
if [ -x "$HD/../dotnet/sdk/dotnet" ]; then
  export DOTNET_ROOT="$HD/../dotnet/sdk"
  export PATH="$DOTNET_ROOT:$PATH"
  export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
fi

say () { echo "[$CAND] $*" | tee -a "$LOG"; }

say "javac $(javac -version 2>&1)  dotnet $(dotnet --version 2>&1 | tail -1)"
say "restoring pristine tree"
git -C "$R" checkout -- . >>"$LOG" 2>&1

if [ "$CAND" != C0 ]; then
  for f in max midpoint; do
    cp "$HD/variants/$f.$CAND.c" "$R/ta_codegen/input/$f/$f.c"
  done
  say "installed max/midpoint = $CAND"
fi

say "generate"
( cd "$R/ta_codegen/generator" && cargo run --quiet -- generate ) >>"$LOG" 2>&1
if [ $? -ne 0 ]; then
  say "GENERATE FAILED — candidate ELIMINATED at the codegen gate"
  grep -m3 "^error" "$LOG" | sed "s/^/[$CAND] /"
  exit 10
fi
say "generate ok"

ALLOWED='ta_codegen/input/(max|midpoint)/(max|midpoint)\.c|src/ta_func/ta_(MAX|MIDPOINT)\.c|ta_codegen/output/(rust/library/src/ta_func/(max|midpoint)\.rs|java/library/.*|java/tools/TaCodegenServe\.java|dotnet/.*|c/.*)'
DIRTY=$(git -C "$R" status --porcelain | awk '{print $NF}' | grep -vE "$ALLOWED" || true)
if [ -n "$DIRTY" ]; then say "UNEXPECTED DIRTY FILES:"; echo "$DIRTY" | tee -a "$LOG"
else say "only intended files changed"; fi

say "regtest.py (build + servers + cross-language, all backends)"
( cd "$R" && python3 scripts/regtest.py --no-generate-indicators --no-perftest ) \
  >"$LOGD/$CAND.regtest.log" 2>&1
rc=$?
grep -iE "All [0-9]+ language|passed|FAILED|mismatch" "$LOGD/$CAND.regtest.log" | tail -6 \
  | sed "s/^/[$CAND] regtest.py: /" | tee -a "$LOG"
say "regtest.py rc=$rc"

# --fuzz-064 needs bin/ta_064_serve: the frozen v0.6.4 oracle behind the current
# JSON-RPC transport. Built once (it is candidate-independent) and reused.
if [ ! -x "$R/bin/ta_064_serve" ]; then
  say "building ta_064_serve (frozen v0.6.4 oracle)"
  ( cd "$R" && python3 scripts/build_064_serve.py ) >>"$LOGD/064_build.log" 2>&1 \
    && say "ta_064_serve ok" || say "ta_064_serve build FAILED (see gates/064_build.log)"
fi

cd "$R/bin" || { say "no bin/"; exit 13; }
for args in "--xlang-hash" "" "--fuzz-064"; do
  name=${args:-default}
  ./ta_regtest $args >"$LOGD/$CAND.$name.log" 2>&1
  rc=$?
  say "ta_regtest ${name} rc=$rc"
  grep -iE "mismatch|golden case|comparison|[0-9]+ failure|PASS|FAIL|succeeded" \
    "$LOGD/$CAND.$name.log" | tail -6 | sed "s/^/[$CAND] $name: /" | tee -a "$LOG"
done
say "done"
