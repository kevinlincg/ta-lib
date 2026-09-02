ci: compile the generated C# on the PR gate too

The comment next to `java-compiles` says C# has the identical hole, that a .NET
SDK on every PR is "a bigger bill than a JDK for a smaller surface — the C#
stream emitter has no server-side twin to disagree with", and leaves it to
"whoever wants to argue that cost". This argues it, and one half of the premise
does not hold.

## The surface is not smaller

`server_gen.rs` emits `ta_codegen/output/csharp/tools/TaCodegenServe.cs` — 3.9 MB,
generated, and a **different emitter** from the library's `csharp.rs` /
`csharp_stream.rs`. It calls the library at 2132 stream-verb sites (1129
`.Update(`, 647 `.UpdateAndFill(`, 356 `.Peek(`) plus the internal `_Impl` tier
per function. That is the same two-emitter disagreement the Java pair had in
#326, in the same shape, and nothing on the PR path compares them.

The C# library also carries a gate the Java one needs a flag for:
`TreatWarningsAsErrors` + `GenerateDocumentationFile` makes **CS1591 an error**,
so a generated public member with no XML doc fails `dotnet build` — the C#
analog of `-Xdoclint`, already switched on in the shipped csproj, and invisible
to this gate until now.

## What the job is

Three steps mirroring `java-compiles`, on the committed sources (no `generate`,
so no cargo), compile only — nothing is run:

1. `dotnet build ta_codegen/output/csharp/library/TALib.csproj -c Release`
2. `dotnet build ta_codegen/output/csharp/library/test/TALib.Test.csproj -c Release`
3. `dotnet build ta_codegen/output/csharp/tools/TaCodegenServe.csproj -c Release`

`-c Release` is what ships and what the nightly measures, and warnings are errors
in both configurations, so this is not a stricter build than a contributor's.

Two comments in the file were **deleted rather than re-synced**: the invitation
paragraph above (the job below it now states its own case) and the header's "the
whole .NET side ... stay[s] in the nightly", which this change makes false.

## Controls (each unit run separately, on `e638d8ed`)

Baseline: all three units build **0 warnings / 0 errors**.

| control (temporary hand-edit to a generated file, reverted) | library | suites | server |
|---|---|---|---|
| 1. delete the XML doc block above generated `Core.SMA_Lookback(int)` | **RED** `error CS1591 ... 'Core.SMA_Lookback(int)'` | GREEN | not run |
| 2. rename generated `SmaStream.Peek` → `PeekBar` | **RED** (`Core_MA.cs:687`) | **RED** (`StreamApiTest.cs:181`) | **RED** (`TaCodegenServe.cs:33542`) |
| 3. one identifier out of step inside the server's own output (`outNBElement` → `outNbElement`, `TaCodegenServe.cs:60124`) | GREEN | GREEN | **RED** `error CS0103` |

Control 1 is why the library step is not redundant: the other two units compile
the same sources **without** `GenerateDocumentationFile`, so only the library
step sees CS1591. Control 3 is why the server step is not redundant: a defect in
`server_gen.rs`'s own output is invisible to the other two.

Control 2 **did not discriminate**, against my prediction that the library would
stay green: `MA`'s dispatch calls `SmaStream.Peek`, so the library is red too. A
public rename is caught broadly, by all three.

**I did not build a control that isolates the suites step.** Every generated
stream type is already referenced by the hand-written suites, so I found no API
change that trips only them; that step rests on the same rationale the Java job
already accepts — the suites are hand-written, the generator preserves them, and
an API change reaches them only if someone carries it.

Controls were hand-edits to generated files, reverted (`git status` clean after
each); they stand in for emitter drift. **I did not break an emitter and
regenerate** — the generator was not built this run.

## Cost

* **SDK download**: the runner images ship an older SDK and the library's single
  TFM is `net10.0`, so `setup-dotnet` fetches `dotnet-sdk-10.0.400-linux-x64`
  — 240,133,692 bytes, 16s to download and extract *on this box*. **I did not
  measure `setup-dotnet` on a GitHub runner**; that number is this environment's,
  not CI's.
* **Compiling**: 13s + 7s + 9s = **29s** total (warm SDK, `bin/`+`obj/` removed
  first), for the exact commands in the job.
* **Wall-clock on the gate: none.** It is its own job, so it runs beside
  `regen-check`, which this file's own comment puts at ~4m. I did not re-measure
  `regen-check`.
* **Runner minutes**: one more `ubuntu-latest` job per PR, on the same reasoning
  the `clippy` job comment already states for this repo.

If that download is the part you do not want on every PR, the alternative that
keeps most of the value is to gate only step 1 + step 3 (the two emitters that
can disagree) — but the bill is the SDK, not the compiling, so it would save
~16s of a ~1m job and lose the suites.

## What this does not cover

Nothing is executed: the C# suites, `--codegen`, `--xlang-hash` and arm64 C#
parity stay in the nightly. x86-64 only.

## Verification

* The three commands above, exactly as the job runs them, from the repo root on
  `e638d8ed`: 0 warnings / 0 errors.
* The three controls above.
* `.github/workflows/pr-codegen-gate.yml` parses (`yaml.safe_load`), and the job
  and step list is as intended. `actions/setup-dotnet@v6` with `10.0.x` is the
  same action and version pin `dev-nightly-tests.yml` already uses.
* **`regen-check` not run.** The change is one workflow file; `generate` never
  writes under `.github/` (nothing in `ta_codegen/generator/src` references it),
  so it cannot move the generated tree.
* Not run: the C# suites themselves, `ta_regtest` / cross-language, the Java and
  Rust legs (untouched), and every benchmark. This change makes no performance
  claim.
