fix(csharp): a cross-typed output pair can alias, and only C rejected it (#386)

Closes the **C# SUPERTREND accepts cross-typed aliased outputs** entry under
**A. API shape** in #386.

## The defect

`SUPERTREND` is the corpus's one function with a `double` output beside an `int`
one. C compares the pair through `const void *` and answers `TA_BAD_PARAM`
(`ta_SUPERTREND.c:139`, `:368`, and the `OpenAndFill` guard at `:872`). C# emitted
no term for it anywhere — not in either batch overload, not in the streaming fill.
The recorded reason, in three places, was that the pair cannot alias:

> Cross-typed pairs are skipped: `Span<double>` and `Span<int>` cannot be laid
> over the same memory, and `Overlaps` is not defined across element types.

The second half is true and the first does not follow from it. `Overlaps` is
generic in the element type, which bounds what a span is *read as*, not what
memory it *covers* — and `MemoryMarshal.Cast` lays one type over the other in
**safe** code, no `unsafe` anywhere.

## Measured, against the pre-change library

A 40-bar series, period 10, one `double[30]` handed in as both outputs
(`Span<double>` directly, `Span<int>` via `MemoryMarshal.Cast`). Disjoint
reference run first, so "wrong" is measured and not asserted:

```
disjoint: begIdx=10 n=30 real[0]=99 int[0]=1
aliased spans: 30 doubles / 60 ints over one buffer

before:  ALIASED ACCEPTED: rc=Success begIdx=10 n=30
                           real[0]=2.1219957915E-314 (reference 99) wrongValues=15/30
after:   ALIASED REJECTED: TaLibArgumentException: TA_SUPERTREND: bad parameter
```

`2.12e-314` is a denormal assembled from the two `int` trend flags written over
the first `double`. Half the output is destroyed, `Success` is returned, and the
same call in C answers `TA_BAD_PARAM`.

## The change

One helper, `cross_typed_overlap`, used by both C# emitters: a cross-typed pair
is compared through its **byte projections**, `MemoryMarshal.AsBytes(a).Overlaps(
MemoryMarshal.AsBytes(b))`, which asks the question the typed call cannot. Fully
qualified, because the generated files carry `using System;` alone and this is the
only construct in the corpus that needs the interop namespace.

Two properties are deliberately preserved:

- **Empty operands stay accepted.** `Overlaps` is false when either side is empty
  and the byte projection of an empty span is empty, so Appendix D item 11 / #262
  is untouched.
- **Same-typed pairs keep the typed spelling.** The only generated text that moves
  is SUPERTREND's three guards; 43 same-typed pairs across the corpus are byte-
  identical.

Java and Rust still contribute no term — `double[] == int[]` is "incomparable
types" and `*const f64 == *const i32` is a type error — and neither language can
build the aliased pair either, so nothing is owed there.

## The gate, and its controls

`csharp_rejects_a_cross_typed_output_pair` (`indicator_variants_suite.rs`) sweeps
the corpus rather than pinning SUPERTREND, so the second mixed function cannot
arrive unguarded. It cuts each backend's streaming section off the file first —
`csharp::generate` and `c::generate` both splice it into the same text, and
without the cut either tier's term satisfies an assertion about the other. That
was not hypothetical: the first draft of this gate passed with the batch emitter
reverted.

Four controls, each applied and watched go red:

| control | result |
|---|---|
| revert the C# batch emitter to the skip | red — `C# batch must reject the cross-typed pair outReal/outInteger` |
| revert the C# stream emitter only | red — `the streaming fill ... must reject outReal/outInteger too` |
| widen the byte spelling to every pair | red — `accbands: outRealUpperBand/outRealMiddleBand are the same element type` |
| drop C's `const void *` comparison | red — `C is what C# is level with here` |

Floors are literal (`cross_pairs >= 1`, `stream_tiers >= 1`, `same_pairs >= 40`),
so the sweep cannot go vacuous by finding nothing.

## Verified

- `regen-check` green; the only generated file that moves is `Core_SUPERTREND.cs`.
- `cargo test` 935 green, `cargo clippy --all-targets` clean.
- `dotnet build TALib.csproj` — 0 warnings, 0 errors, under `TreatWarningsAsErrors`
  and `IsAotCompatible` (`MemoryMarshal.AsBytes` is trim/AOT-safe).
- The C# suite runner: all 7 suites pass (`BatchApiTest`, `CoreBuilderTest`,
  `DivZeroTest`, `MetadataTest`, `NoPhantomIoTest`, `SMathOverflowTest`,
  `StreamApiTest`).

**I did not run `ta_regtest`.** The C library, the four servers and the other
three backends are byte-identical under `regen-check`, so there is nothing there
for it to see — but that is an argument, not a run.

## Docs

`docs/error-handling-spec.md` Appendix E said "C compares them, the other three
cannot" and named C#'s `Overlaps` as one of the three. That premise is what this
PR refutes, so the paragraph now reads "C and C# compare them, Java and Rust
cannot", and the **Test coverage** note says which gate covers which
(`checkOutputAliasRejected` drives the C library; the render pin covers the rest).

Two further sites had drifted and are corrected rather than left to rot:
`docs/streaming-api-design.md` still described C#'s guard as `ReferenceEquals`
over `double[]`/`int[]`, which predates the Span migration, and the header comment
on `test_frames_index_outputs_by_declaration_position` asserted the skip happens
"in every backend", which C has never done.

## What this does not fix

Rule N8 is unchanged: two outputs of different types that **partially** overlap
are still undetected, in every backend. The byte projection would in fact see a
partial overlap — but detecting it here and nowhere else would make C# diverge
further, not less, and N8 is a decided question (#225). Left alone deliberately.
