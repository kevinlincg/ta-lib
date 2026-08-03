# Measurement method — #147 rolling min/max candidates

Everything here lives **outside** the ta-lib tree.  There is no second benchmark
tool in the released tree; the only thing borrowed from the tree is the corpus
header `src/tools/ta_bench/bench_corpus.h` (PR #153), which is `#include`d, so the
input classes are exactly the project's own and no bespoke input is invented.

## What is timed

Each candidate is a `ta_codegen/input/<name>/<name>.c` body — the source of truth
form, not a generated file — compiled into **its own translation unit** with the
function renamed to `<name>_<cand>` and called through a function-pointer table.
No LTO, no inlining across the boundary, so all candidates are measured on equal
terms and none of them can be specialised to the caller.

`C0` is a **byte-identical copy** of the upstream input file, and `C5` differs
from it by exactly two characters (the two rescan comparison operators).  A
`diff` proves both, so the reference the whole comparison is relative to cannot
have drifted.

Per-bar cost is `wall_ns / (iters * outNBElement)`, `clock_gettime(CLOCK_MONOTONIC)`.
Wall-clock is the primary and only metric here; no instruction counts are used
(the maintainer has been burned by Ir moving flat -8% while wall-clock ranged
-27%..+2%).

Iteration count is chosen from a pilot call to spend ~20 ms per rep, so a cheap
point (period 14, randwalk, ~4 ns/bar) is not dominated by clock resolution and
an expensive one (period 1000, constant, ~860 ns/bar) does not take minutes.
`min` over 3 reps within a layout; the layout axis is aggregated separately.

## Code-layout sweep

This is the part that is easy to get wrong, so it is explicit.  Twelve binaries
are built from the *same object files* wherever possible, differing only in
where the candidate code lands:

| family | layouts | what moves | machine code |
|---|---|---|---|
| `.text` padding | L0 L1 L2 L3 L4 L5 (pad 0/64/192/576/1728/5184 B) | a `.space N` blob linked into `__text` **ahead** of the candidate objects, shifting every candidate by N bytes | **identical** |
| link order | L6 L7 (order reversed, pad 0/576) | which candidate functions share cache sets / share a page | **identical** |
| function alignment | L8 L9 (`-falign-functions=32`, pad 0/576) | inter-function padding | changed (reported as its own family) |

`-falign-functions=4` was tried and dropped: clang floors function alignment at 16
on x86-64, so those binaries came out byte-identical to L0/L3 and would have
inflated the layout count without adding a layout.  Replaced by two further pad
points (2112, 9280).

**The sweep is verified to actually move code**, not assumed to: `nm -n` on the
twelve binaries shows `_max_C0` at 0x5c0 / 0x600 / 0x680 / 0x800 / 0xc80 / 0x1a00
/ 0xe00 / 0x2a00 across the pad family — deltas exactly equal to the pad sizes —
and at 0x53b0 with the link order reversed.  A "layout sweep" whose layouts are
identical binaries is worse than no sweep at all.

One limitation to be explicit about: padding `.text` and permuting link order move
where a function *starts*; they do not reshuffle basic blocks *inside* it.  So
this sweep separates inter-function placement effects from real algorithmic
differences, but it cannot absorb an intra-function codegen difference.  Where one
of those showed up (C5) it was root-caused by disassembly instead — see
PROGRESS.md FINDING 3.

Aggregation: within a layout, `min` over reps; across layouts, **median** and the
full `[min..max]` range.  Ratios against C0 are additionally reported as the
per-layout ratio spread `[min..max]`, computed layout-by-layout — that is the
number that says whether a claimed win survives code placement rather than being
an artifact of one build.

## Input classes, in the maintainer's priority order

1. **gate** — `randwalk`, `randwalk-lo`, `randwalk-hi`, `gbm`.  A candidate that
   regresses here at any period is not interesting, whatever it does elsewhere.
2. **trend** — `trend-chop-0.5p/1p/2p/4p`.  The case that would justify a change.
3. **tail** — `mono-up`, `mono-down`, `constant`.  Reported, not led with.

Periods 14, 30, 64, 200, 1000.  14 and 30 are the family's defaults; 1000 is the
tail, deliberately not the headline.

## Function shapes

* single-extremum: **MAX**
* dual-extremum:   **MIDPOINT** (two channels over the same array — the shape
  the maintainer named alongside WILLR)

## Guarded vs unguarded

The bodies here are the unguarded core.  The generated guarded wrapper adds a
one-time parameter-validation prologue *outside* the loop, so it cannot appear in
a per-bar number at n=100000; the two differ by a constant that this harness
cannot resolve.  Guarded/unguarded are therefore reported separately from the
**in-tree** gate run (`ta_regtest --no-guarded` / `--no-unguarded`) rather than
from this harness, and that is stated wherever numbers are quoted.

## Correctness before performance

`--verify` compares every candidate against C0 **bitwise** (`memcmp` on the
doubles, not a tolerance) over all 11 shapes, 22 periods (2,3,5,14,15,20,21,29,
30,31,63,64,65,100,128,200,255,256,257,500,1000,4096), three ways:

* plain call
* **in-place** (`outReal == inReal`, issue #130)
* non-zero `startIdx` (exercises the priming path separately)

and additionally checks C0 itself against an **independent naive O(n·period)
reference written from the definition**, so a mis-transcribed baseline cannot
make every candidate look correct.

The gate is mutation-validated: injecting a `1.0000001 *` factor at all 28
`outReal[outIdx++] =` sites produced **48/48** distinct detections
(3 gate arms × 2 functions × 8 candidates) across all 11 shapes.  A gate that
passes without that check is not evidence of anything.

Nothing is timed for a candidate that fails verification.

## Small-range runs (allocation overhead)

C1..C4 and C7 `malloc` O(period) scratch per call (2 arrays for MAX, 4 for
MIDPOINT). At n=100000 that cost is amortised into nothing; on a short range it is
not, and a per-call `malloc` in MAX/MIDPOINT would be new behaviour for these
functions even though `ta_APO.c` sets a precedent. So the sweep is repeated at
n = 256 / 1024 / 8192 as well as 100000, and the short-range numbers are reported
next to the long ones rather than folded into them.
