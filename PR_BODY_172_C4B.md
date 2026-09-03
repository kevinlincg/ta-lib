test(stream): the managed peek sweeps never read the composed tiers' commit path

Two generator sweeps, one per managed emitter: a Java `peek` / C# `Peek` never
drives a sub-handle's committing verb.

Comes out of #172 **C4b**, and the first thing to report is that **C4b's premise
does not hold** — details at the end, because it changes what is worth doing
there and this PR is not it.

## The gap

`no_java_peek_copies_the_handle` and `no_csharp_peek_copies_the_handle` sweep
every peek frame for a write to a name the frame did not declare. On 167
functions that is the whole story. On the other 13 — the composed and dispatch
tiers — the rule has nothing to look at: their peek writes no field of its own
handle, every value it produces comes back from a sub-handle. Measured, with
peek localization disabled so that every frame steps the live handle: that sweep
names 167 functions and **not one of these 13**.

So their only way to commit is one call, `sp.sub0.update(...)` where
`sp.sub0.peek(...)` belongs. The sub-handle is a reference `clone()` shares, so
it corrupts the fork's original too.

C has gated this since the peek tier landed
(`no_peek_entry_point_commits_a_sub_stream`, reading the C source). Rust cannot
express it: `peek(&self)` borrows immutably. Java and C# can, and nothing read
for it — with both managed emitters made to call the committing verb from a peek
frame, **the other 901 generator tests stay green** and only these two go red.

## What the sweeps do

Keyed on the **verb**, not the receiver. The tiers reach a sub-handle three ways
— `sp.sub0`, a cast `((SmaStream) sp.sub)`, an index `sp.bank[slot]` — and a
rule naming receivers would miss the next spelling somebody adds.

Each one counts what it protects, in both directions:

* 13 peek frames drive a sub-handle at all (`>= 13`, so the sweep cannot go
  quiet by ceasing to find the tiers it exists for);
* the committing verb occurs outside the peek body (`> 0`, so a corpus that
  never calls `update` on a sub-handle cannot satisfy the rule vacuously).

**The defect assertion runs before both tripwires, and the order is
load-bearing.** Turning one peek's sub-call into a commit also drops that peek
out of the `>= 13` count, and the corpus carries exactly 13 — so with the
tripwire first, every single-site defect reported `only 12 Java peek entry
point(s) drive a sub-handle` instead of naming the offending line. Both twins
asserted in that order until this branch; the fix is the assertion order
only.

## The cost, and the honest limit on what this buys

**It buys detection time, not coverage.** The jar's `StreamSmokeTest` names the
same sabotage at run time, so this is not a hole in the tree — it is a hole in
*when* the tree notices:

| sabotage | named by | where that runs today |
|---|---|---|
| composed pipeline verb -> `update` | StreamSmokeTest, 22 failures / 11 functions | dev-nightly `cross-language` (JDK + Maven jar) |
| dispatch cast + MAVP bank -> `update` | StreamSmokeTest, 19 failures / 11 functions incl. MA and MAVP | same |
| either | these two sweeps | `cargo test` — the PR gate |

The two sweeps need no JDK, no Maven and no .NET SDK, so the PR gate carries
them. That is the same argument #211 made for `regen-check` and #326's fallout
made for `java-compiles`: caught after merge is caught one branch too late. It
is also the parity argument — C gates this, Rust cannot fail it, the two managed
backends were the ones with nothing.

Against that: **178 lines of test code, in two suites, for a defect the nightly
already catches.** Not factored into one helper on purpose — each reads its own
emitter's output, and both the verb and the receiver spellings differ — but that
is a judgement call, and if the answer is "the nightly is soon enough", this
should be dropped rather than trimmed.

## Verification

Re-run on this branch after merging dev `7065d886`:

* `regen-check` clean — test-only, no generated file moves.
* generator suite **903 / 903** (901 before, +2 here).
* Controls, each broken and watched to fail, then reverted:
  * composed pipeline verb forced to `update` / `Update`, both emitters -> both
    new sweeps red, the other **901 green**; the Java sweep names 21 sites.
  * **one** sub-call of BBANDS' peek forced to `update` -> the Java sweep red
    naming `bbands: peek commits a sub-stream: cur_tempBuffer1 =
    sp.sub0.update(inReal);`. This is the control the assertion order above
    exists for: before the reorder the same sabotage reported a hollowed-out
    corpus and never named the site.

Carried over from the pre-merge base, **not** re-measured here:

* `clippy -D warnings` clean.
* dispatch cast and MAVP bank spelling forced to `update` -> the Java sweep
  red. These are the two tiers the first control does not reach, which is why
  there are two.
* Java runtime arm, on the jar, for the same two sabotages: baseline 7 suites
  green (StreamSmokeTest 4681 checks), sabotaged 22 and 19 failures — that is
  the table above.

**Not run:** the C# suites — no .NET SDK to hand — so
`no_csharp_peek_commits_a_sub_stream` is justified by parity with the C gate and
the Java twin, not by a measured runtime miss. Its doc comment says so. Also not
run: `--xlang-hash`, `synth_gate.py`, and any benchmark; this PR claims no
performance number and touches no shipped code.

## #172 C4b, which sent me here

C4b says the 14 candlestick handles the Java streaming sweep cannot separate are
"covered by nothing at all today", and proposes bars that fire the patterns.
Measured, both halves are wrong:

1. **The defect class is covered, corpus-independently.** Disabling peek
   localization so every Java peek steps the live handle turns
   `no_java_peek_copies_the_handle` red on **167 functions, all 61 candlesticks
   among them**. A structural sweep does not care whether the pattern fires.
2. **The corpus is not empty for them either.** Over exactly the shapes
   `stream_verify` drives for a one-vector function — shape 0 at seed 1234, then
   shapes 1..8 at seeds 1237..1244, 240 bars — **all 61 candlesticks fire at
   least once**. It is thin: 23 fire on four shapes or more, while 24 fire on
   exactly one shape, several of those exactly once in 240 bars. So "no bars
   fire the pattern" is not the problem; the *value-based* comparison in the
   hand-written Java sweep is, and it feeds its own sinusoid rather than these
   shapes.
3. And C's runtime leg never needed the values: `stream_verify`'s peek
   non-commit arm compares the whole handle struct against an untouched twin
   (`sv_steq_TA_<N>`), so a candlestick that answers 0 either way is beside the
   point there.

Which leaves C4b's residue as a *belt* on 14 handles that already have braces —
worth far less than the 13 tiers this PR covers. Recommend rewriting C4b to say
so, or closing it; either way it should not be closed by building a candlestick
corpus for that sweep.

_Head re-verified after merging dev `af4cdede` (which landed #338, then DONCHIAN): `regen-check` green, exit 0, 179 functions; generator suite 904 passed / 0 failed. The net diff against dev is unchanged by that merge — the same 2 files._
