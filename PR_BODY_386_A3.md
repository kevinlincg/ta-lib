fix(streaming): the fill's aliasing reject reads extents, not addresses (#386)

#386 **A. API shape** — "C's `OpenAndFill` aliasing guard is pointer-equality
only". C only; no other backend's generated source moves.

## The hole

`TA_<NAME>_OpenAndFill` writes the caller's arrays and then seeds the handle's
ring from the input tail. The guard tested first-element identity, so an output
one bar into an input compared unequal, was accepted, was filled, and the capture
then read a tail the fill had already overwritten — `TA_SUCCESS`, every later
`Update` wrong, and no error anywhere.

C was the only backend that could reach it: two Java arrays are identical or
disjoint, safe Rust cannot build the call at all, and C# already answers
`Span.Overlaps`.

## What it emits now

```c
TA_LIB_API TA_RetCode TA_SMA_OpenAndFill( TA_SMA_Stream **stream, const double inReal[],
                                          int historyLen, int optInTimePeriod,
                                          int *outBegIdx, int *outNBElement, double outReal[] )
{
   int fillNb;
   ...
   fillNb = TA_SMA_Lookback( optInTimePeriod );
   fillNb = ( fillNb >= 0 && fillNb < historyLen ) ? historyLen - fillNb : 1;
   if( TA_RANGES_OVERLAP( outReal, fillNb, inReal, historyLen ) ) return TA_BAD_PARAM;
```

Both extents were already at the guard. An input is read over `historyLen`
elements; an output is written over `historyLen - Lookback(...)`, which is the
same count the call reports as `outNBElement`, so the two cannot drift apart. The
macro takes ELEMENT counts and reads each operand's own `sizeof`, so an `int`
output against a `double` input is measured in its own units — something pointer
identity could not express at all. `TA_BytesOverlap` (`ta_utility.c`, not
exported) does the comparison through `uintptr_t`, and subtracts only from the
lower address, so no pointer into a different object is ordered and no range
arithmetic leaves its own object.

Measuring the destination over what is WRITTEN, not over `historyLen`, is
deliberate: a caller who allocated exactly the count the fill produces and placed
it against an input must not be refused for bytes nothing touches. Both halves
are pinned, in both directions, in the gates below.

## Strictly a widening

A call that writes nothing — history below the lookback, or a parameter out of
range, where `Lookback` answers -1 — measures the destination over ONE element
rather than none. A zero-length range overlaps nothing, so without that fallback
a destination that literally IS an input would stop being `TA_BAD_PARAM` and
start being whatever the body answers next. With it, every call this guard
rejects today is still rejected, with the same code; what is new is only the
overlapping ones.

## What this costs, and what it does not close

- **The batch tier is untouched, and now deliberately differs.** In-place
  (output == input, whole buffer) is *supported* there — rule N4, several bodies
  elect their scratch that way — and #225 settled identity-only detection on
  cost. The fill forbids in-place outright, so the stronger rule refuses it no
  legal call. `docs/error-handling-spec.md` Appendix E states the two rules
  separately rather than one; that split is a decision to keep or reject, and
  reverting this PR is what un-splits it.
- **The `uintptr_t` laundering** that Appendix E named as what a conforming check
  "would have to" do is now in the tree, once, out of line.
- **Cost not measured.** The guard adds one `Lookback` call and one range compare
  per output/input pair, once per `OpenAndFill`, ahead of a `TA_Malloc` and a
  whole-history replay. I did not benchmark it and make no performance claim
  either way.
- **One divergence left open.** For a call whose history is below the lookback,
  the other three backends measure the caller's declared buffer while C now
  measures one element, so a destination sitting *below* an input and reaching it
  only through the fill count is `TA_INSUFFICIENT_HISTORY` in C where C# says
  `BadParam`. Nothing is at risk — the call writes nothing — but the backends do
  not answer alike on it.
- Rejection **order** is unchanged: the guard sits exactly where it sat, ahead of
  parameter validation. A parameter out of range makes `Lookback` answer -1, and
  the fallback extent then rejects only what identity already rejected, so the
  parameter fault still answers on its own terms.

## Gates, and the controls that go red

- `testBatchArgumentContract` (`test_internals.c`) gains six cases with their own
  counters and floor, kept apart from S4's so a deleted one cannot hide behind
  it. Each rejected placement has an accepted twin one element away: a
  destination one bar into an input against one abutting it from above; a
  destination ending one element inside an input against one ending exactly where
  it starts; two outputs one element apart against two that abut. Neither half
  alone says anything — an address comparison passes every accepted case, and a
  destination measured over `historyLen` fails every accepted case — so the pairs
  are what pin the arithmetic. The sixth is the short-history pair that pins the
  widening: `outReal == inReal` with 5 bars stays `TA_BAD_PARAM`, while the
  disjoint call at 5 bars answers `TA_INSUFFICIENT_HISTORY`.
- `fill_aliasing_reject_measures_written_extent_against_history`
  (`open_core_suite.rs`) sweeps every streaming function and requires the
  destination to carry `fillNb` and the source `historyLen`, never the reverse,
  plus the `Lookback`-derived count and the fallback. Which calls a guard refuses
  is invisible to every value gate — no accepted call computes differently — so
  the extents have to be pinned on the emitted text.
- **Controls, both watched failing.** Reverting the emitter hunk and regenerating
  makes the runtime case fail with
  `TA_SMA_OpenAndFill(outReal = inReal + 1) returned 0, expected TA_BAD_PARAM (2)`
  — which is also the reproduction of the defect. Spelling the destination extent
  as `historyLen` makes the generator gate fail with
  `AC: output outReal is written over the fill count, not the whole history`.

## Verification

- `cargo test` in `ta_codegen/generator`: 936 passed, 0 failed. `cargo clippy
  --all-targets` clean.
- `./ta_regtest` green (the full hand-written C suite, including the streaming
  gates and the frozen v0.6.4 leg).
- `ta_regtest --codegen --language=c` green over the whole corpus (161 pass, 0
  fail), on this branch merged to dev tip `7625e259`, and `./ta_regtest` green
  again on that merge. An earlier run at `f0f89e1c` could reach no verdict at
  all: the short-history leg reported a false
  `open accepted a history shorter than one output` on every unstable-period
  function until `7625e259` fixed it.
- Regenerated: the diff is `src/ta_func/*.c` plus the emitter; `output/rust`,
  `output/java` and `output/csharp` regenerate byte-identical, and no generated
  server or bench file moves.
- **Not run here:** the cross-language `--codegen` legs for Rust/Java/C#, and the
  nightly `--xlang-hash` and `--fuzz-064`. No value path changes and the three
  ported backends' sources are byte-identical, but I did not exercise them.
