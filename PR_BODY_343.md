refactor(streaming): the managed peek frames stop seeding output locals they overwrite (#343)

#343 asks for the seed removal, a gate, and — before any of it — a measurement,
with its own closing rule attached: *"If it folds on both runtimes, close this
rather than do it: the source-level argument alone is thin."*

**The measurement was taken and it folds on Java.** So the honest framing of this
PR is: the perf premise is dead, what is left is one dead load per output per
peek call removed from the source, plus a gate that pins the property. Whether
that clears the bar is your call, and #343's own rule points at closing rather
than merging. The branch is here either way.

## The measurement

`scripts/stream_ab.py --lang=java --call=peek --base=<dev>`, 6 interleaved
rounds, 500,000 iterations per round, all 179 streaming functions, Temurin
OpenJDK 21.0.10, Intel Xeon @ 2.80GHz, 4 cores.

The tool's usual control — the untouched functions — **does not exist for this
change**: 174 of 175 peek frames are touched, so there is nothing unchanged left
in the peek run to calibrate against. Two controls were built instead:

| run | what is timed | n | median | sd | range |
|---|---|---:|---:|---:|---|
| `--call=peek` | the changed frames | 179 | **+0.06%** | 0.535 | −1.86% .. +1.57% |
| `--call=update` | **byte-identical in both arms** | 179 | −0.10% | 0.645 | −2.02% .. +2.25% |

The second row is the byte-identical control group: the same two library arms,
the same harness, timing `update` frames that this change does not touch at all
(verified, not assumed — see below). **It spreads wider than the changed run
does.** The layout noise floor of the method is larger than the entire effect
distribution, so there is no effect to report on HotSpot. `IMI`, the one peek
frame that keeps its seed and is therefore byte-identical inside the peek run
itself, reads +1.17% — larger than all but two of the 178 rows that did change.

That the change *cannot* be slower (it removes a field load and adds nothing) is
an argument from the source, not from the numbers, and it is the same thin
argument #343 says is not enough on its own.

## What I did not measure

- **C#.** Not measured, and not measurable with what is in the tree: neither
  `ta_bench_stream` nor `stream_ab.py` has a C# lane (`stream_ab.py` contains no
  C# code path at all), and the `ta-bench` skill says adding one needs a control
  arm reproducing a known effect first. There was also no .NET SDK on the box
  this round. So #343's "folds on **both** runtimes" test is half-answered: Java
  folds, C# is unknown. The C# emitter change is Java's predicate shipped
  unchanged, which is the existing convention for the peek election, not new
  license taken here.
- **No other JVM, no other machine, no other JIT tier.** One box, one JDK.

## What the change is

`streaming::peek_seed_is_dead` — a conservative write-before-read scan of the
peek transition IR. A seed is dropped only when every path that mentions the
local writes it before any read; a mention inside a loop or a switch, a compound
assignment, or an `if` whose arms disagree all answer "keep". A wrong answer can
therefore only leave the one load this exists to drop, never mis-render.

174 of 175 seeds go. `IMI` keeps its one: its sole store sits inside the period
loop, and the IR cannot prove a loop body runs.

The seed is safe to drop because a peek commits nothing, so the previous bar's
output is never an input to the transition. That is a property of the peek tier,
not of any one indicator, which is why the C and Rust peeks never paid it (C
writes through an out-param, Rust rebinds).

## The gate

`no_managed_peek_seeds_a_dead_output_local`, sweeping both managed backends:

- non-vacuity floors — `swept > 150` (the needle still matches peek frames) and
  `seedless > 150` (the emitter has not re-grown them all);
- the kept set pinned to exactly `{imi}`, so it fails in **both** directions: a
  frame that grows a seed back fails, and so does a change that silently drops
  the one seed the analysis deliberately keeps.

**Sabotage-proved, both directions, this round — I broke it and watched it fail:**

| sabotage | result |
|---|---|
| force `peek_seed_is_dead` → `false` (emitter re-grows every seed) | red at the `seedless > 150` floor |
| force `peek_seed_is_dead` → `true` (drops IMI's kept seed) | red at the exact set: `left: {}` / `right: {"imi"}` |

Both reverted; the tree pushed is the unsabotaged one.

## Verification

- `scripts/build.py regen-check` — clean, "ta_codegen output matches the
  committed source."
- `cargo test` in `ta_codegen/generator` — 417 + 20 suites, 0 failed.
- Every changed line in the generated Java and C# is a seed swap and nothing
  else: `git diff` over `ta_codegen/output/{java,csharp}` classifies to exactly
  `cur_<out> = sp.cur_<out>;` → `cur_<out> = 0.0;` (or `0`), 592 lines, no other
  shape.
- The `--call=update` control is only a control if update frames really are
  identical: all 174 changed lines in `Core.java` were mapped to their enclosing
  method, and all 174 are inside `peek`.
- **`--xlang-hash` was NOT re-run this round** — it needs the .NET SDK, which is
  not on this box. Deliverable 3 of the issue (179 functions, 0 mismatches) is
  therefore unconfirmed here. The change is a declaration initialiser the body
  overwrites before reading, so it cannot move a value; that is an argument, not
  a run, and I am flagging it as unrun rather than implying otherwise.

## Note on the commit label

The commit says `refactor(streaming)`, not `perf(streaming)`. It was written as a
perf change; the numbers above do not support that word, so it lost it.
