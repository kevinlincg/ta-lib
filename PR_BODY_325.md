perf(java): a composed update reads the sub-handle it just committed (#325)

Part of #325, and deliberately not all of it — see "What this leaves" below.

## The change

`update` commits, so after `sp.sub.update(bar, sink)` the callee's own `cur_*`
fields already hold what the sink was given. The sink was allocated per bar to
carry values that were sitting in the handle — the point the emitter comment
already made, blocked only by there being no `update` that takes no sink.

So: a package-private `void update( bars )` on every multi-output handle, the
commit with nothing to publish. The public sink overload delegates to it, so the
two cannot drift on which bars they reject or count, and the two composed commit
paths call it and read `cur_*`:

- MA's MAMA dispatch arm — `MA(MAType.MAMA)` streaming update allocated one
  `MamaOut` per bar.
- STOCHRSI's composed pipeline — one `StochfOut` per bar.

Both now allocate nothing on the commit path.

## What this leaves

The per-call sinks still in the generated Java are the two peek frames, which is
what #325 was filed about before the commit paths joined it: a peek commits
nothing, so its sink is the only carrier, and only inlining the sub-frame
removes it. This PR takes the allocation sites from 4 to 2 and does not touch
that half, so #325 stays open.

## What the numbers on the issue say this buys — and what they say it does not

The issue thread carries the measurement this branch does not, and it is the one
a reviewer will reach for first: against `389515aa1` with
`ThreadMXBean.getThreadAllocatedBytes`, min of 7 passes of 2M calls, JDK 21,
`-XX:+UseSerialGC`, **both composed `update` sites already read 0 B/bar.**
`StochfStream.update` and `MamaStream.update` are ~94 B of bytecode, under
`FreqInlineSize`, so C2 opens the call and escape analysis scalar-replaces the
`<N>Out`. It is only `peek`'s callees that are over the inlining budget.

Stated plainly, and not special-cased away: **on a warmed C2 this PR removes two
allocations the JIT was already removing, and buys 0 B/bar.** What it buys instead
is narrower than "4 sites to 2" sounds:

- the generated source stops constructing a carrier for values the handle already
  holds, so the two sites are free by construction rather than free because escape
  analysis happens to fire;
- anything not a warmed C2 — the interpreter, C1, tiered warm-up, or a future
  callee that grows past `FreqInlineSize` — pays the allocation that is written
  down, and stops paying it here.

If a source-level-only improvement is not worth 17 package-private methods, this
is the change to decline, and the issue's own conclusion — that closing #325 reads
better with the numbers than doing the frame-emitter work — is not contradicted by
anything in this PR. This PR is deliberately **not** that frame-emitter work.

It is instead the other option the thread names: a sink-less `update` that "removes
the two `update` sites on its own, with no emitter surgery", declined there only
because it "widens the public surface, and that is your call". The call taken here
is to add it **package-private**, which is the same primitive without the public
surface — so the stated objection does not apply, and no caller is offered
"commit and discard the outputs". One number to reconcile rather than smooth over:
the thread costs the public variant at 15 methods and this is 18 (counted on the
merged head as the number of generated Java fragments carrying the sink-less
overload, all package-private). Two of the three above the thread's count are
accounted for: `SUPERTREND`, per the note below, and `DONCHIAN`, which landed on
dev after that comment was written and picks up the pair like any other
multi-output stream. **I have not accounted for the remaining one.**

## The cost, stated rather than special-cased

Not public, deliberately: "commit and discard the outputs" is not an API a
caller should be offered, and the alternative — a reusable `Out` field on the
composing handle — is an object per handle that exists only to satisfy a
signature, since these arms read `cur_*` and never the sink.

The cost is **18 package-private methods**, one per multi-output stream class,
and the two verbs now emit different shapes where the composed emitter had one
for both. If you would rather keep one shape, this is the change to decline.

## Gates

- `the_composed_sub_handle_sinks_are_exactly_the_costed_four` becomes
  `..._costed_two` — one site per composed function, the peek frame's. A second
  on either is a commit path that started allocating again.
- The U3 sweep anchors Java's multi-output `update` on the overload that
  commits; the public frame carries no finite test to read the rule off.
- `test_java_ma_dispatch` pins the new arm and that it takes no sink.

Controls, run and watched to go red:

- Dropping the advance from the new overload turns the U3 sweep red
  (accbands/java).
- Making the composed commit path allocate a sink again turns the sink-count
  gate red at stochrsi 2.

## Verified

On the original tree, Linux x86-64, JDK 21 through the committed Maven wrapper:

- `cargo test`: 885 pass, 0 fail. `cargo clippy --all-targets -- -D warnings`:
  clean. `generate` then `git status`: clean.
- `ta_codegen build --backend=java`: server, jar, javadoc jar, doc examples and
  all 7 suites OK (StreamSmokeTest 4132 checks).
- `regtest.py --language=c,java` against the pinned-tag oracle: C 161 passed /
  0 failed, Java 161 / 0, 967 acknowledged float comparisons.

On the earlier head, merged with dev `af4cdede`:

- `generate` then `git status`: clean (`regen-check` exit 0, 179 functions).
- The generator suite: 902 passed / 0 failed.
  `cargo clippy --release --all-targets -- -D warnings`: clean.

Re-verified against dev `af4cdede` only at the two tiers above. The Java jar,
javadoc and `regtest.py` legs in the previous block were **not** re-run on this
merge — no JDK leg was executed this round, so those three lines describe the
earlier tree, not the pushed head.

Not measured: this was not benchmarked. The claim is the allocation count in
the generated code, 4 sites to 2, not a time. `--language=rust,csharp` was not
run either — no .NET SDK on the machine — though no C#, Rust or C file is in
the diff.

`TA_SUPERTREND` is a multi-output stream and picks up the same pair, and so does
`TA_DONCHIAN` now that it is on dev; the public sink overload keeps
`requireArgument` ahead of the commit, so the null-sink rejection added in
`91b76002` is not reopened by the split.

## The generated-Java digest collision is already resolved in this branch

Anything that changes generated Java recomputes the Java gencode digest, so this
branch collides with dev on exactly the two lines carrying it:
`BuildStamp.GENCODE_DIGEST` and its spliced copy
`TaCodegenServe.SPLICED_GENCODE_DIGEST`. Dev landed first — #338 as `67936169`,
then DONCHIAN through `af4cdede` — so the collision is this branch's to resolve,
and the branch's own commit does it. (The head is rebased onto dev
rather than merged — see "Rebased onto dev `ce5f5748`" at the end — so there is
no merge commit; the digest below is what the single commit carries.)

Neither side's value is correct for the combined tree, so the resolution is one
`generate` run, not a pick from either side. Measured on the merged head against
dev `af4cdede`:

- taking dev's digest (`c6beffa2c163b194`) by hand and committing it leaves
  `regen-check` **red**, exit 1, and the diff it prints is exactly those two
  files — this is a deliberate control, re-run against `af4cdede` and watched
  to fail;
- re-running `generate` writes the combined value `fe336d7433975c86` and the
  gate is **green**, exit 0, generator suite 902 passed / 0 failed.

DONCHIAN also makes the merge more than a digest pick: it is a multi-output
stream, so the regeneration gives it the sink-less overload as well — that is
the 18th method, and the only non-digest content this branch adds beyond the
`update` split itself. Every other file in the two diffs auto-merged and
regenerated identically.

## Rebased onto dev `ce5f5748`

Dev moved after the verification above was taken: `46577145` (c_hygiene, the
post-emission `(void)` sweep), `b128cbf5` (a short `--function` token names a
whole component) and `ce5f5748` (#344, the Open head that declares only what its
body uses). This branch is rebased onto `ce5f5748` with no conflicts, and
`git patch-id` says its net diff against dev is byte-identical to the one this
body describes — nothing about the change itself moved.

Re-checked on the rebased head, at these tiers only:

- `scripts/build.py regen-check`: green, exit 0, 179 functions.
- `cargo test --release` in `ta_codegen/generator`: 916 passed / 0 failed.
- `cargo clippy --release --all-targets -- -D warnings`: clean.

Dev `ce5f5748` itself passes the same three commands, so that is a baseline for
the rebase and not a control that goes red.

**Not re-run on the rebase:** the Java jar, javadoc and `regtest.py` legs, and
`--language=rust,csharp` — no JDK and no .NET SDK leg was executed this round
either. What was checked instead is the one thing the rebase could have moved:
the Java gencode digest. This branch still writes `fe336d7433975c86` and dev
`ce5f5748` still carries `c6beffa2c163b194`, so the collision section above
stands as written — `ce5f5748`, `b128cbf5` and `46577145` change generated C
and Rust, not generated Java.
