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
signature, since these arms read `cur_*` and never the sink. The cost is 17
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

## Rebases, and where every number above comes from

Carried forward across four dev advances (`cc52aea9`, `19ede494`, `e638d8ed`,
`3c35cb24`). Every time the only conflicts were generated Java artifacts
(`BuildStamp.java`, `TaCodegenServe.java`), settled by regenerating, never by
hand. The per-rebase logs are dropped from this body deliberately — they were a
list of counts that go stale.

TA_SUPERTREND (`02ee797e`) arrived during that and is a multi-output stream, so
it picks up the same pair of methods: the cost line is **17** package-private
methods, not 16. Its public sink overload keeps `requireArgument("...", "out",
out)` ahead of the commit, so the null-sink rejection `91b76002` added is not
reopened by the split.

**Everything below was run on the merge with `3c35cb24` (the current dev tip),
in one session.** The merge is a merge commit, not a rebase.

- `regen-check` on a clean tree (committed merge, not a dirty one): clean.
- Generator suite: **891 passed, 0 failed**.
- `cargo clippy --all-targets -- -D warnings` on both the generator and the
  generated crate: clean.
- The PR gate's three javac steps (`f2e1c637`) — library under
  `-Xdoclint:all,-missing`, the hand-written suites against it, the JSON-RPC
  server: all pass, and pass identically on a dev `3c35cb24` control.
- The two counts this PR actually claims, read off the merged
  `Core.java`: **17** package-private sinkless `update` overloads, and
  **2** remaining `new *Out()` sites — `MamaOut` in `MA`'s `peek` and
  `StochfOut` in `STOCHRSI`'s `peek`. Both are peek frames, which is the
  #325 tail; no commit path allocates.

## Carried to the post-#333 dev (`19286097`)

The merge conflicted only in generated Java — `TaCodegenServe.java` and
`BuildStamp`'s `GENCODE_DIGEST`, the staleness stamp `#324` added, which by
construction can only be whatever the regenerated `Core` hashes to. Both were
resolved by regenerating, not by hand.

Re-measured on that merge, since `#333` rewrites peek frames and the two
remaining sinks are in peek frames:

- **Both counts are unchanged: 17 sinkless `update` overloads, 2 remaining
  `new *Out()` sites**, still `MamaOut` in `MA.peek` and `StochfOut` in
  `STOCHRSI.peek`.
- `generate` then `git status`: clean.
- Generator suite: 29 suites, 0 failed.
- `javac` 21 compiles the generated library and the generated JSON-RPC server
  clean.

## Carried again, to dev `a5ca7cc6`

Three further advances since: `#336` (`1c1fca43`, the composed peek frame's temp
filter — the C emitter only), `918a7b70` (the release scripts' Cargo.lock sync)
and `#331` (`a5ca7cc6`). None of them touches generated Java. The merge was
clean; nothing was hand-resolved.

Re-run on that merge, in one session:

- `regen-check`: **`ta_codegen output matches the committed source. OK.`**
- Generator suite: **29 suites, 899 passed, 0 failed.**
- The two counts, re-read off the merged `Core.java`: **17** package-private
  sinkless `update` overloads, and **2** remaining `new *Out()` sites —
  `MamaOut` at `Core.java:103880`, inside `public double peek(...)`, and
  `StochfOut` at `145893`, inside `public void peek(..., StochrsiOut out)`.
  Both peek frames; no commit path allocates.

NOT re-run on this last merge: `javac`, the Maven path, every cross-language
leg, and the deliberate-break controls. They are claimed for the `19286097`
merge above and nothing since touches Java.

NOT run on this merge, and so not claimed for it:

- `scripts/regtest.py` and every cross-language leg. `bin/ta_ref_serve` was not
  built from the pinned reference worktree in this session, and the oracle is
  what those legs compare against. They passed on the `e638d8ed` merge, two
  advances back; that is a different tree.
- `ta_codegen build --backend=java`, the Maven path (jar, javadoc jar, doc
  examples, the 7 suites, StreamSmokeTest), and the gate's `-Xdoclint` step. The
  javac runs above compile the library and the server and execute nothing.
- C#. No .NET SDK in this session — an earlier run had one, this one does not.
  No C# file is in the diff.
- **The two deliberate-break controls, and the sink-count gate's red run, were
  not re-executed on this merge.** They were run and watched to fail on an
  earlier base; I am not presenting them as current.
- Every benchmark. There is no time claim here — the claim is the allocation
  count in the generated code, 4 sites to 2.
