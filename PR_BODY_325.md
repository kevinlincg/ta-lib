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

On the tree as pushed, merged with dev `7065d886`:

- `generate` then `git status`: clean — dev's #337 and #316 change no file this
  PR touches.
- The generator suite: 29 test binaries, 0 failed.

Not measured: this was not benchmarked. The claim is the allocation count in
the generated code, 4 sites to 2, not a time. `--language=rust,csharp` was not
run either — no .NET SDK on the machine — though no C#, Rust or C file is in
the diff.

`TA_SUPERTREND` is a multi-output stream and picks up the same pair, which is
where the 17th method comes from; the public sink overload keeps
`requireArgument` ahead of the commit, so the null-sink rejection added in
`91b76002` is not reopened by the split.
