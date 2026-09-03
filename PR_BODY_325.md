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
the thread costs the public variant at 15 methods and this is 17 (verified by
counting the diff, all package-private); the 17th is `SUPERTREND`, per the note
below, and I have not accounted for the remaining one.

## The cost, stated rather than special-cased

Not public, deliberately: "commit and discard the outputs" is not an API a
caller should be offered, and the alternative — a reusable `Out` field on the
composing handle — is an object per handle that exists only to satisfy a
signature, since these arms read `cur_*` and never the sink.

The cost is **17 package-private methods**, one per multi-output stream class,
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

On the tree as pushed, merged with dev `67936169`:

- `generate` then `git status`: clean.
- The generator suite: 902 passed / 0 failed.

Not measured: this was not benchmarked. The claim is the allocation count in
the generated code, 4 sites to 2, not a time. `--language=rust,csharp` was not
run either — no .NET SDK on the machine — though no C#, Rust or C file is in
the diff.

`TA_SUPERTREND` is a multi-output stream and picks up the same pair, which is
where the 17th method comes from; the public sink overload keeps
`requireArgument` ahead of the commit, so the null-sink rejection added in
`91b76002` is not reopened by the split.

## The #338 digest collision is already resolved in this branch

Both this PR and #338 change generated Java, so both recompute the Java gencode
digest, and whichever landed second was going to collide on exactly the two
lines carrying it: `BuildStamp.GENCODE_DIGEST` and its spliced copy
`TaCodegenServe.SPLICED_GENCODE_DIGEST`. #338 landed first, as dev `67936169`,
so the collision is this branch's to resolve, and the head merge commit does it.

Neither side's value is correct for the combined tree, so the resolution is one
`generate` run, not a pick from either side. Measured on the merged head:

- taking dev's digest by hand and committing it leaves `regen-check` **red**,
  exit 1, and the diff it prints is exactly those two lines — this is a
  deliberate control, run and watched;
- re-running `generate` writes the combined value `ad1343133314e1b2` and the
  gate is **green**, exit 0, generator suite 902 passed / 0 failed.

Every other file in the two diffs auto-merged and regenerated identically, so
the digest is the whole interaction between them.
