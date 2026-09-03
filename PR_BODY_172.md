ci(main-nightly): the release branch never built the jar it publishes (#172 C5)

Closes #172 C5. The rest of #172 stays open — B1/B2/B3/B4/B6 are account work,
C4b needs a candle corpus this has nothing to do with.

`main` is the branch a jar is published from, and nothing on it has ever built
one. The three-jar assertions — every `ta_codegen/input/` function on the jar's
`Core` as exactly two public `OutRange` overloads, no test class in any of the
three archives, `META-INF/LICENSE`, the JPMS module name — live in
`ta_codegen build --backend=java`, which is reached only through `regtest.py`
in dev-nightly's `cross-language` job. `main-nightly-tests.yml` runs `test`,
`fuzz-vs-064`, `xlang-hash`, `rust` and `regen-check`. `xlang-hash` looks like
it covers this and does not: it builds the Java **server**, which carries its
own spliced `Core`, not the Maven artifact.

This is the mirror of the `rust` job added for the same reason on the Rust
half, and it covers the same window: the commits only `main` sees — the dist
bot's, hotfixes, and the release cut itself.

## The two choices in it

**Straight `cargo run -- build --backend=java`, not
`build.py servers --language=java`.** That target runs `generate-servers`
first, so the jar would be built from freshly generated sources rather than
from the committed ones a release is actually cut from. What ships is the tree
as committed; `regen-check`, already on this workflow, is what proves the two
agree. Keeping them separate means a drift between input and committed output
fails as a regen-check failure and not as a confusing packaging one.

**Not gated on `test`**, like `rust` and `regen-check` — no C build is
involved, so a dist-pool failure must not skip the release branch's only
packaging coverage.

No Maven install is added: the committed wrapper fetches and SHA-256-verifies
the pinned distribution itself. `unzip` is already on `ubuntu-latest`, and the
backend fails loudly rather than silently when it is missing.

## What was measured

Both directions, on `dev` `e638d8ed`, with the command the job runs
(`cargo run --release -- build --backend=java`):

| tree | exit | last line |
|---|---|---|
| as committed | 0 | `Checking Java jars ... OK (178 functions, 787 entries across three jars)`, then all 7 suites on the jar: NoPhantomIo 4121, BatchApi 181, CoreApi 66, DivZero 91, Metadata 1743, SMathOverflow 4, StreamSmoke 4681 checks |
| `Core.SMA(float[])` made package-private (one character deleted) | 1 | `Checking Java jars ... FAILED / not exactly two public OutRange overloads (double[] + float[]): ["SMA x1"]` |

The red run is the control: a defect visible only in the packaged artifact
turns the job red, and the tree passes again once it is reverted. That
sabotage is the one the other main-nightly jobs are blind to by construction —
`xlang-hash` builds the server, and nothing else on the workflow opens a jar.

## Cost

One more `ubuntu-latest` job on main-nightly, plus a JDK install. Measured
warm here (generator already built, Maven already in `~/.m2`): **24–29 s** for
the command itself. I did **not** time it cold on a hosted runner — add the
generator's release compile and the wrapper's first Maven download, so the
`rust` job's 45-minute timeout is what I copied rather than a measured
envelope.

It is deliberately duplicate coverage: dev-nightly already runs this exact
command on `dev`, and every commit that reaches `main` through a merge has been
through it. The value is only in the commits that never went through `dev`.

## Not checked

- The workflow was verified by YAML parse (the `java` job's steps and inputs
  resolve, and `.github/actions/apt-install` takes `packages`) and by running
  its command locally. I did **not** run it on GitHub Actions — my token cannot
  dispatch upstream workflows.
- I did not touch `dev-nightly-tests.yml`; the Java coverage there is unchanged.

_Head re-verified after merging dev `af4cdede` (which landed #338, then DONCHIAN): `regen-check` green, exit 0, 179 functions; generator suite not re-run (this branch changes one workflow file only). The net diff against dev is unchanged by that merge — the same 1 file._
