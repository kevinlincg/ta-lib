perf(java): a multi-output stream writes a caller-owned Out (#310)

Closes #310.

**This replaces the PR description as it stood on `9502134`. That head did not
compile** — nothing Java had been built or run when it was pushed, exactly as
the note on this PR warned, and the PR gate never builds Java, so the green
ticks said nothing about it. The current head is the same design with the three
things that were missing. What was wrong, so it is on the record rather than
buried in a force-push:

- `Core.java` — the MA→MAMA dispatch arm calls `((MamaStream) sp.sub).update(inReal)`
  with no sink. The emitter's own comment says it does not need one, and it does
  not, but the sink-less overload did not exist. 1 compile error.
- `TaCodegenServe.java` — the verify/bench server still reads
  `Core.<N>Stream.Value` and the old returning `update`/`peek`/`value` at 176
  lines. ~290 compile errors.
- javadoc — the shared `update`, `updateAndFill` and opener docs link `{@link #value()}`,
  which no longer resolves on a multi-output class. `-Xdoclint` fails the Maven
  build over it, so this is not cosmetic. 18 errors.

`generate` was idempotent throughout: the generator was consistently producing
code that does not compile, and the PR gate cannot see that.

## What the change is

`update`/`peek`/`value` on a multi-output handle take an `<N>Out` the caller
allocates and reuses across bars, instead of returning a `Value` the handle
cached. The cache was a guaranteed heap allocation on every bar; a returned
record was one whenever escape analysis did not fire, which measurement showed
is not something a caller can rely on. A caller-owned object is zero,
unconditionally.

Shape follows the decisions on the issue: `<N>Out` at `Core` level, no
`equals`/`hashCode`, composed `peek` allocating one sink per call (#325 has the
only fix), composed `update` allocating nothing at all.

### The one addition beyond those decisions

A multi-output handle also gets a **sink-less `update`** — commit the bar, write
nothing, read it later with `value(out)`.

It is what the composed `update` above calls, and it is also an API regression
repaired: before this change `update` returned a `Value` a caller could ignore,
so advancing a handle you are not reading this bar cost nothing. Without the
overload it would cost an `Out` allocated only to be filled and dropped.

It repeats the bar check, the step and the count rather than delegating to the
sink-taking overload. That is deliberate: each entry point owning its own
argument check is what lets the U3 sweep read rule U3 off one body, and
`updateAndFill` already repeats the same prologue. Delegating instead made the
sweep go red, correctly.

**This is the part that most deserves your judgement.** It adds one public
method per multi-output stream class (16 of them). If you would rather the
dispatch arm hold a reusable `Out` field and the public surface not grow, say so
and I will take that instead — the cost there is an object per handle that
exists only to satisfy a signature, since the arm reads `cur_*`, not the sink.

## Test/gate changes that came with it

- **U3 sweep (`out_range_advance_suite`)** now reads *every* `update` overload,
  not the first, so the sink-less one is bound by the rule too. Second-overload
  sites are counted apart with their own floor, so (a) the extra sweep cannot
  silently stop happening and (b) they cannot inflate the floors calibrated on
  one entry point per function.
- **The server's `value()` identity probe is retired.** `if (st.value() != up)`
  asserted that `value()` hands back the very record `update` returned — a claim
  about the cache, which is what this PR removes. It is replaced by the compare
  the C and Rust legs already make: `value` reports the bar `update` just
  committed, line by line.
- **`StreamSmokeTest`**: the `Value` contract becomes the `Out` contract —
  mutable, overwritten on reuse, identity equality (a mutable key with value
  equality breaks any `HashSet` holding it), one public field per output,
  asserted per function against the registry rather than on MACD alone. Its
  peek/copy sweep now resolves `update`/`peek` **by arity**: with two overloads,
  `getMethods()` order is unspecified. That sweep was silently skipping all 16
  multi-output handles until the arity was right — its own count assertion
  (161/177) is what caught it.

## Verification

The head is a single commit rebased onto `dev` `2e4171ce`. Everything below was
run on it, on Linux x86-64, JDK 21 through the committed Maven wrapper
(`--release 17`):

- `cargo test` (generator): **884 passed, 0 failed**
- `cargo clippy --all-targets -- -D warnings`: clean
- `ta_codegen generate` then `git status`: **clean** (no drift). The rebase
  itself needed a regenerate: #322's `GENCODE_DIGEST` covers the generated
  `Core` method text, which this PR changes, so a merge that did not
  regenerate would have failed `regen-check` on `dev`.
- `ta_codegen build --backend=java`: server, jar, javadoc, doc examples and
  tests all OK. `StreamSmokeTest` **4132 checks, all pass**; `NoPhantomIoTest`
  4098; `MetadataTest` 1733; `BatchApiTest` 181; `CoreApiTest` 66;
  `DivZeroTest` 91; `SMathOverflowTest` 4.
- `scripts/regtest.py --language=c,rust,java` against the pinned-tag oracle:
  **C 161 passed / 0 failed, Rust 161 / 0, Java 161 / 0** (967 acknowledged
  float comparisons).
- Controls, run and watched to fail: dropping the bar check from the new
  sink-less overload turns the U3 sweep red on that overload; restricting the
  sweep back to the first body turns the new second-overload floor red at 0
  sites.
- The diff touches Java and the generator only — no C, Rust or C# file, and no
  `ta_codegen/input/` file, is in it.

**Not run here:** no .NET SDK on this machine, so C# was not compiled and
`--xlang-hash` was not run. The diff contains no C# file, but I did not check
that C# still builds. I also did not run `synth_gate.py`.
