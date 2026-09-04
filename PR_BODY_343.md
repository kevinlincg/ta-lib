refactor(streaming): the managed peek frames stop seeding output locals they overwrite (#343)

#343 asks for the seed removal, a gate, and — before any of it — a measurement,
with its own closing rule attached: *"If it folds on both runtimes, close this
rather than do it: the source-level argument alone is thin."*

**The measurement was taken and the perf premise died.** It folds. So this PR
does not claim the issue's bar was cleared — that rule is **superseded by the
explicit ruling in #343** ("turn it into a PR", Sep 4), on the stated grounds
that `46577145f` shipped the whole `c_hygiene` phase on zero instructions and
pure source hygiene, and that holding this to a stricter bar would be
inconsistent. Recorded here so nobody later reads the merge as evidence the
measurement came out positive. It did not.

What is bought is not fewer lines but a line that stops lying: `double
cur_outReal = sp.cur_outReal;` asserts the previous bar's output feeds the
transition, and a peek commits nothing, so it never does. `= 0.0` is true, and
character-for-character what Rust already emits. Three backends converge on one
story: C writes through an out-param, Rust rebinds at the type default, the
managed pair now agree with Rust.

## The measurement

`scripts/stream_ab.py --lang=java --call=peek --base=<dev>`, 6 interleaved
rounds, 500,000 iterations per round, all 179 streaming functions, Temurin
OpenJDK 21.0.10, Intel Xeon @ 2.80GHz, 4 cores.

The tool's usual control — the untouched functions — **barely exists for this
change**: every peek frame but one is touched (178 of the 179 on this head), so
there is next to nothing unchanged inside the peek run to calibrate against. Two
controls were built instead:

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

### The second run, and the control that actually settles it

A second measurement — the one posted in #343, on different hardware
(i7-10700K, Temurin 21.0.12.1 / .NET 10.0.400, `stream_ab.py --call=peek`,
defaults, **both managed lanes**) — reads:

| lane | reported rows' median | control |
|---|---|---|
| Java | −1.4% (min −3.3, max +6.0) | spread median **5.1%**, p90 7.3% — every row inside noise |
| C# | −1.4% (min −5.5, max +6.0) | spread median 1.1%, and see the tell below |

**The tell is the untouched functions.** The dispatch tier (`MA`, `MAVP`) never
carried a seed — its peek locals were already zero-initialised — yet both report
the same ≈−1.2% "improvement" as the touched rows, in **both** languages. That
is single-giant-class layout drift, which the harness header warns about, not the
load being removed. Whatever a seed costs after JIT, it is under the
discrimination floor of the method on both runtimes.

That control-function reading is the part of this work worth keeping regardless
of what happens to the change: a `stream_ab.py` row for a function the diff does
not touch is not a null, and reading one as an improvement would have produced a
number that discriminates nothing.

Neither of the two runs above was re-taken in the round that produced this head;
both are cited as recorded, not as re-measured. What *was* re-run on this head is
listed under Verification.

## What I did not measure

- **C# on the box that produced this head.** The C# lane does exist in the tree
  (`scripts/stream_ab.py --lang=csharp`, `emit_csharp`), and the second run above
  used it — so #343's "folds on **both** runtimes" question is answered, just not
  from here: this container has no .NET SDK, so nothing C# was compiled, timed or
  hashed while preparing this branch. The C# emitter change is Java's predicate
  shipped unchanged, which is the existing convention for the peek election, not
  new license taken here.
- **No other JVM, no other machine, no other JIT tier** than the two boxes named
  above.

## What the change is

`streaming::peek_seed_is_dead` — a conservative write-before-read scan of the
peek transition IR. A seed is dropped only when every path that mentions the
local writes it before any read; a mention inside a loop or a switch, a compound
assignment, or an `if` whose arms disagree all answer "keep". A wrong answer can
therefore only leave the one load this exists to drop, never mis-render.

On this head, 178 of the 179 peek frames in each managed backend come out
seedless. `IMI` keeps its one, in both languages: its sole store is the last
statement *inside* the `for( i = sp.optInTimePeriod - 1; ... )` body, and the IR
cannot prove a loop body runs. It does — period ≥ 2 — but that is range
knowledge this pass does not consume, so the seed is kept honestly rather than
exempted by name.

The pass is a read-only **query** over already-built IR, not a rewrite, which is
why it lives in `streaming.rs` and not in `ir_cleanup.rs` (length-preserving IR
passes). Its safety is one-sided: an unrecognised construct falls to
"read or unknown" → keep the seed → today's behaviour. A new IR construct cannot
break it; it can only make it miss an opportunity. So it adds no obligation to
IR construction.

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

**Sabotage-proved, both directions, on this head — broken by hand and watched to
fail:**

| sabotage | result |
|---|---|
| force `peek_seed_is_dead` → `false` (emitter re-grows every seed) | red at the floor: `java: only 20 seed-free frames` |
| force `peek_seed_is_dead` → `true` (drops IMI's kept seed) | red at the exact set: `left: {}` / `right: {"imi"}` |

Both reverted; `git status` clean; the gate green again on the pushed tree.

### What the gate cannot see

`seeded == {imi}` pins the pass's **output**, so it catches two things: a seed
growing back anywhere, and IMI's kept seed being dropped. It **cannot** catch a
*different* function being wrongly dropped — nothing in the assertion knows
whether dropping some other frame's seed was legitimate. That direction rests
entirely on the runtime legs that compare a peek against the committed update —
`ta_regtest --codegen`'s peek probes (`streamPeekProbes`, per language) and
Java's `StreamSmokeTest`. The exact-set assertion should not be read later as a
correctness proof; it is a hygiene pin.

## Verification

Re-run on the head being pushed, on dev `ce5f5748`:

- `cargo run -- generate` over the whole corpus, then `git status` — clean, so
  the committed tree is the generator's fixed point (the `regen-check` property;
  `scripts/build.py` refuses to run as root on this box, so the generator was
  driven directly).
- `cargo test --release` in `ta_codegen/generator` — **917 passed / 0 failed / 1
  ignored**, which is dev's 916 plus this gate.
- `cargo clippy --release --all-targets -- -D warnings` — clean.
- Both sabotage directions above, red, then green after revert.
- `git patch-id --stable` on this head and on the pre-rebase tip `4feeeefa`:
  both `03a32225…`, so the rebase moved nothing but the base.
- The whole non-generated diff is four files: `streaming.rs` +83,
  `java_stream.rs` +15, `csharp_stream.rs` +14, `update_and_fill_suite.rs` +51
  (161 insertions, 2 deletions). Every other changed path is under
  `ta_codegen/output/{java,csharp}` — no C, no Rust, no build-system file.
- The generated diff against dev is seed lines and nothing else. Every changed
  line under `ta_codegen/output/{java,csharp}` classifies to exactly two shapes:
  **428** `double cur_X = sp.cur_X;` → `= 0.0;` and **268** `int cur_X =
  sp.cur_X;` → `= 0;`, plus the two build-stamp digest lines
  (`GENCODE_DIGEST` / `SPLICED_GENCODE_DIGEST`, `c6beffa2c163b194` →
  `852b2c601d4dbc3e`). No third shape. (An earlier round counted 592 such lines
  over the smaller pre-rebase corpus; 696 is the count on this head.)
- Directly counted on the generated sources: **179** peek frames per managed
  backend, and exactly one surviving `= sp.cur_` — `Core.java`'s `IMI.peek` and
  `Core_IMI.cs`.

Not run here:

- **`--xlang-hash`** — it needs the .NET SDK, which this container does not have.
  Deliverable 3 of the issue (179 functions, 0 mismatches) is therefore
  unconfirmed from this box; it was reported green on the box that posted the
  measurement in #343 (same count, same function, before and after — the 28
  pre-existing HT_TRENDMODE tolerance-lane rows on macOS/Temurin). The change is
  a declaration initialiser the body overwrites before reading, so it cannot
  move a value; that is an argument, not a run.
- **The C suite / `ta_regtest`** — no C output changed (the diff above is Java
  and C# only), so it was not re-run on this head.
- **The `stream_ab.py` rows** in either measurement section — cited as recorded,
  not re-measured.

## Note on the commit label

The commit says `refactor(streaming)`, not `perf(streaming)`, per the ruling's
point 1: there is no measurable perf, and shipping a measured-null change under
`perf(` would misrepresent it in the changelog. The measurement is in the body
instead, as the reason the perf claim was dropped.

## Containment

- 83 lines of pure analysis in `streaming.rs`, ~14 per managed backend at the
  declaration site, and a 51-line gate in the existing cross-backend suite.
- No IR construction changed, and none has to: the pass is one-sided (see above).
- Nothing in C or Rust moved; the two backends that never carried the shape stay
  byte-identical.

## Rebase

Dev moved after the first round's verification was taken: `46577145`
(`c_hygiene`, the post-emission `(void)` sweep), `b128cbf5` (a short
`--function` token names a whole component) and `ce5f5748` (#344, the Open head
that declares only what its body uses). The branch is rebased onto `ce5f5748`,
which resolves the `BuildStamp.java` / `TaCodegenServe.java` conflicts the
ruling flags — resolved by rebase-and-regenerate, not by hand-editing either
generated file, and the digests above are what `generate` produced. `git
patch-id` reports the net diff against dev byte-identical to the pre-rebase
tip's, so nothing about the change itself moved; every number in the
Verification section was taken on this head, after the rebase.
