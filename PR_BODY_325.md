perf(java): a composed update reads the sub-handle it just committed (#325)

Closes nothing on its own — #325 stays open for the two peek frames, which is
what it was about before the commit paths joined its scope.

`update` commits, so after `sp.sub.update(bar, sink)` the callee's own `cur_*`
fields already hold what the sink was given. The sink was allocated per bar to
carry values that were sitting in the handle — the point the emitter comment
already made, blocked only by there being no `update` that takes no sink.

So: a package-private `void update( bars )` on every multi-output handle, the
commit with nothing to publish. The public sink overload delegates to it, so the
two cannot drift on which bars they reject or count, and the two composed
commit paths call it and read `cur_*`:

- MA's MAMA dispatch arm — `MA(MAType.MAMA)` streaming update allocated one
  `MamaOut` per bar.
- STOCHRSI's composed pipeline — one `StochfOut` per bar.

Both now allocate nothing on the commit path. The per-call sinks left in the
generated Java are the two peek frames, which is what #325 was about before the
commit paths joined it: a peek commits nothing, so its sink is the only carrier
and only inlining the sub-frame removes it.

Not public, deliberately: "commit and discard the outputs" is not an API a
caller should be offered, and the alternative — a reusable `Out` field on the
composing handle — is an object per handle that exists only to satisfy a
signature, since these arms read `cur_*` and never the sink. The cost is 16
package-private methods, one per multi-output stream class, and the two verbs
now emit different shapes where the composed emitter had one for both. If you
would rather keep one shape, this is the change to decline.

Gates:

- `the_composed_sub_handle_sinks_are_exactly_the_costed_four` becomes
  `..._costed_two` — one site per composed function, the peek frame's. A second
  on either is a commit path that started allocating again.
- The U3 sweep anchors Java's multi-output `update` on the overload that
  commits; the public frame carries no finite test to read the rule off.
- `test_java_ma_dispatch` pins the new arm and that it takes no sink.

Verified on Linux x86-64, JDK 21 through the committed Maven wrapper:

- `cargo test`: 885 pass, 0 fail. `cargo clippy --all-targets -- -D warnings`:
  clean. `generate` then `git status`: clean.
- `ta_codegen build --backend=java`: server, jar, javadoc jar, doc examples and
  all 7 suites OK (StreamSmokeTest 4132 checks).
- `regtest.py --language=c,java` against the pinned-tag oracle: C 161 passed /
  0 failed, Java 161 / 0, 967 acknowledged float comparisons.
- Controls, run and watched to fail: dropping the advance from the new overload
  turns the U3 sweep red (accbands/java); making the composed commit path
  allocate a sink again turns the sink-count gate red at stochrsi 2.

Not measured: I did not benchmark this. The claim here is the allocation count
in the generated code, 4 sites to 2, not a time. `--language=rust,csharp` was
not run either — no .NET SDK on this machine — though no C#, Rust or C file is
in the diff.

## Rebase onto dev `cc52aea9`

The branch was rebased. The only conflicts were two generated artifacts
(`BuildStamp.java`, `TaCodegenServe.java`); they were settled by regenerating,
not by hand.

TA_SUPERTREND (`02ee797e`) arrived in the meantime and is a multi-output stream,
so it picks up the same pair of methods: the cost line is **17** package-private
methods, not 16. Its public sink overload keeps `requireArgument("...", "out",
out)` ahead of the commit, so the null-sink rejection `91b76002` added is not
reopened by the split — I read the emitted fragment to check that specifically.

Re-run on the rebased tree:

- `cargo test --no-fail-fast`: 885 passed, 0 failed.
- `generate` then `git status`: clean.
- The PR gate's three javac steps, the ones `f2e1c637` added, run by hand with
  the same flags: the shipped library under `-Xdoclint:all,-missing`, the
  hand-written suites against it, and the JSON-RPC server. All three exit 0.

NOT re-run after the rebase, and I am not carrying the pre-rebase results
forward as current:

- `regtest.py --language=c,java` — `scripts/build.py` refuses to run as root,
  which is what this environment is.
- `ta_codegen build --backend=java` (the Maven path: jar, javadoc jar, doc
  examples, the 7 suites and StreamSmokeTest's 4132 checks). The three javac
  steps above cover compilation but execute nothing.
- `cargo clippy --all-targets -- -D warnings`.
- The two deliberate-break controls listed above.

Still not measured: there is no benchmark here and no time claim. The claim is
the allocation count in the generated code, 4 sites to 2.
