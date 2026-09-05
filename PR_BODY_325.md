perf(java): a composed update reads the sub-handle it just committed (#325)

Part of #325, and deliberately not all of it — see "What this leaves" below.

## The change

`update` commits, so after `sp.sub.update(bar, sink)` the callee's own `cur_*`
fields already hold what the sink was given. The sink was allocated per bar to
carry values that were sitting in the handle — the point the emitter comment
already made, blocked only by there being no `update` that takes no sink.

So: a package-private `void update( bars )` on every multi-output handle, the
commit with nothing to publish. The public sink overload delegates to it, so the
two cannot drift on which bars they reject or count, and the composed commit
paths call it and read `cur_*`:

- MA's MAMA dispatch arm — `MA(MAType.MAMA)` streaming update allocated one
  `MamaOut` per bar.
- STOCHRSI's composed pipeline — one `StochfOut` per bar.
- KDJ's composed pipeline — one `StochOut` per bar. KDJ landed on dev after this
  branch was first written and picks the change up by regeneration; nothing in
  the emitter is special-cased for it.

All three now allocate nothing on the commit path.

## What this leaves

The per-call sinks still in the generated Java are the three peek frames, which
is what #325 was filed about before the commit paths joined it: a peek commits
nothing, so its sink is the only carrier, and only inlining the sub-frame
removes it. This PR takes the allocation sites from 6 to 3 and does not touch
that half, so #325 stays open.

## What the numbers on the issue say this buys — and what they say it does not

The issue thread carries the measurement this branch does not, and it is the one
a reviewer will reach for first: against `389515aa1` with
`ThreadMXBean.getThreadAllocatedBytes`, min of 7 passes of 2M calls, JDK 21,
`-XX:+UseSerialGC`, **both composed `update` sites measured then already read
0 B/bar.** `StochfStream.update` and `MamaStream.update` are ~94 B of bytecode,
under `FreqInlineSize`, so C2 opens the call and escape analysis
scalar-replaces the `<N>Out`. It is only `peek`'s callees that are over the
inlining budget. That measurement predates KDJ and was **not** re-run on this
head — the third site is unmeasured, and I am not claiming a number for it.

Stated plainly, and not special-cased away: **on a warmed C2 this PR removes
allocations the JIT was already removing, and buys 0 B/bar.** What it buys
instead is narrower than "6 sites to 3" sounds:

- the generated source stops constructing a carrier for values the handle already
  holds, so the sites are free by construction rather than free because escape
  analysis happens to fire;
- anything not a warmed C2 — the interpreter, C1, tiered warm-up, or a future
  callee that grows past `FreqInlineSize` — pays the allocation that is written
  down, and stops paying it here.

If a source-level-only improvement is not worth 23 package-private methods, this
is the change to decline, and the issue's own conclusion — that closing #325 reads
better with the numbers than doing the frame-emitter work — is not contradicted by
anything in this PR. This PR is deliberately **not** that frame-emitter work.

It is instead the other option the thread names: a sink-less `update` that "removes
the two `update` sites on its own, with no emitter surgery", declined there only
because it "widens the public surface, and that is your call". The call taken here
is to add it **package-private**, which is the same primitive without the public
surface — so the stated objection does not apply, and no caller is offered
"commit and discard the outputs".

One number to reconcile rather than smooth over: the thread costs the public
variant at 15 methods. On this head it is **23**, counted as the generated Java
fragments carrying the sink-less overload — ACCBANDS, AROON, BBANDS, DONCHIAN,
ERI, FRACTAL, HA, HT_PHASOR, HT_SINE, KC, KDJ, MACD, MACDEXT, MACDFIX, MAMA,
MINMAX, MINMAXINDEX, SMI, STOCH, STOCHF, STOCHRSI, SUPERTREND, VORTEX. The count
is one per multi-output stream class, so it tracks the corpus and has grown with
it (it was 18 when this branch was first written, before DONCHIAN, ERI, FRACTAL,
HA, KDJ and VORTEX landed). Against the thread's 15 I can account for the ones
that landed after it was written; **I have not reconstructed the thread's own
count of 15**, so I cannot say the residual is only corpus growth.

## The cost, stated rather than special-cased

Not public, deliberately: "commit and discard the outputs" is not an API a
caller should be offered, and the alternative — a reusable `Out` field on the
composing handle — is an object per handle that exists only to satisfy a
signature, since these arms read `cur_*` and never the sink.

The cost is **23 package-private methods**, one per multi-output stream class,
and the two verbs now emit different shapes where the composed emitter had one
for both. If you would rather keep one shape, this is the change to decline.

## Gates

- `the_composed_sub_handle_sinks_are_exactly_the_costed_six` becomes
  `..._costed_three` — one site per composed function, the peek frame's. A
  second on any of them is a commit path that started allocating again.
- The U3 sweep anchors Java's multi-output `update` on the overload that
  commits; the public frame carries no finite test to read the rule off. That is
  `entry_sig`'s `multi` arm, and it is the only part of this branch's edit to
  `out_range_advance_suite.rs` that survives the #382 merge — see below.
- `test_java_ma_dispatch` pins the new arm and that it takes no sink.

Controls, run and watched to go red:

- On the merged head this round: setting the sink-count pin's expected KDJ entry
  to 2 turns `the_composed_sub_handle_sinks_are_exactly_the_costed_three` red,
  reporting `left: {"kdj": 1, "ma": 1, "stochrsi": 1}` against
  `right: {"kdj": 2, ...}`. The pin is reading the generated Java, not an empty
  sweep, and 1-per-function is the measured value rather than an assumed one.
- From the original tree, not re-run on this head: dropping the advance from the
  new overload turns the U3 sweep red (accbands/java); making the composed commit
  path allocate a sink again turns the sink-count gate red at stochrsi 2.

## Merged with dev `710765c6` (#382 removed UpdateAndFill)

Dev moved under this branch again, and this time not only around it. `0b36decc`
(#382) deleted the `UpdateAndFill` tier, which rewrote both suites this branch
edits, and `43ab73ae`, `8c0fedbc`, `0234625a` and `710765c6` moved the corpus and
KAMA. The head is now a **merge** of dev into the branch, not a rebase, and the
two suites were re-pointed rather than textually merged:

- `out_range_advance_suite.rs`: the `UpdateAndFill` legs and the
  `the_hand_rolled_tiers_advance_at_every_bar_loop` test are **dev's deletions**
  (both came from dev's `8e908bf8` and went out with #382), and they stay
  deleted. What survives from this branch is `entry_sig`'s `multi` arm.
- `java_stream_suite.rs`: dev's pin had grown to six sites over three functions
  once KDJ landed; this branch takes each to one, so the pin is
  `..._costed_three` at 1/1/1.
- `BuildStamp.java` and `TaCodegenServe.java` collided on the Java gencode digest
  only, and the resolution is one `generate` run rather than a pick from either
  side. This branch writes `7df91028357daa9e`; dev `710765c6` carries
  `d27fe1889b396250`.

Nothing else conflicted, and no generator source outside `java_stream.rs`
changed.

## Verified

On the merged head (`8167e571`), Linux x86-64, JDK 21 through the committed
Maven wrapper:

- `regen-check`: green — "ta_codegen output matches the committed source",
  201 functions, and its Cargo.lock, source-list and stream-retcode legs pass.
- `cargo test --release` in `ta_codegen/generator`: **935 passed / 0 failed**.
- `cargo clippy --release --all-targets -- -D warnings`: clean, exit 0.
- `ta_codegen build --backend=java`: the jar builds and all seven Java suites
  pass on that jar — StreamSmokeTest 4815 checks (including the U3 advance gate:
  24 rejections counted once, 66 untouched values, 24 resumed bars, 48 peeks that
  moved nothing), NoPhantomIoTest 4651, MetadataTest 1966, DivZeroTest 91,
  BatchApiTest 160, CoreApiTest 66, SMathOverflowTest 4.

This is the first round in which a JDK leg was actually executed on a merged
head; the previous two bodies said no JDK leg had been run, and this one no
longer needs to.

**Not run on this head, and not claimed:** `regtest.py` against the pinned-tag
oracle (`ta_ref_serve` is not built in this environment, so the cross-language
comparison did not run at all), `--language=rust,csharp` (no .NET SDK), and any
benchmark. The allocation claim in this PR is an allocation-site count in the
generated source, not a time and not a re-measured byte count.

`TA_SUPERTREND`, `TA_DONCHIAN` and the streams that landed since pick up the
same pair by regeneration; the public sink overload keeps `requireArgument`
ahead of the commit, so the null-sink rejection added in `91b76002` is not
reopened by the split.
