Phase 1 is done: ten candidates built as `ta_codegen/input/<name>/<name>.c` bodies,
run through the real generator and all four language backends, and measured on the
#153 corpus with a 12-layout sweep. One machine only — Intel i7-10700K (iMac20,1),
Apple clang 21.0.0, x86-64. That is deliberately the same CPU as the table in the
issue body, and it is exactly *one* of the four slots you asked for, so nothing
below is transferable yet.

**Two things are blocked on a decision only you can make.** They are first because
Phase 2 (the other three toolchains) is not worth starting until they are settled —
one of them can invalidate most of it.

---

## Decision 1 — signed zero. Nothing is bit-identical in your sense.

`+0.0` and `-0.0` compare equal, so *which* of two numerically-equal window members
supplies the emitted value decides the **sign** of a zero output. Every candidate
changes that choice somewhere.

Our out-of-tree bitwise gate had a hole: every `bench_corpus.h` shape holds
`low > 0`, so no corpus series contains a zero at all. It was your own frozen-oracle
gate that found it, not ours — `--fuzz-064`, on the **control**:

```
[C0] BENIGN TA_MIDPOINT: 28 signed-zero case(s) (numerically equal, +0.0 vs -0.0)
[C0] comparisons: 171230  matches: 148119  benign(signed-zero): 30
     cci-tolerated: 8434  fma-tolerated: 14647  failures: 0
[C0] PASS
```

C0 is a byte-identical copy of the shipped input. So **shipped `TA_MIDPOINT` already
differs from v0.6.4 in 28 signed-zero cases today**, `--fuzz-064` classifies that
class `BENIGN`, and it passes.

Per candidate, `--fuzz-064` benign signed-zero totals (all with `failures: 0`):

| | C0 | C1 | C2 | C3 | C4 | C5 | C9 |
|---|---|---|---|---|---|---|---|
| benign(signed-zero) | 30 | 80 | 76 | 76 | 45 | 80 | 78 |
| of which `TA_MAX` | 0 | 43 | 38 | 38 | 32 | 43 | 41 |
| of which `TA_MIDPOINT` | 28 | 35 | 36 | 36 | 11 | 35 | 35 |

We then reproduced it out of tree so each candidate could be characterised rather
than inferred: a deterministic `+0.0`/`-0.0`/tie mix (4 variants) added to
`--verify`, over 22 periods × {plain, in-place, non-zero `startIdx`}. At n=20000,
`MISMATCH` count against C0:

```
max       C1=40 C2=40 C3=40 C4=36 C5=40 C6=40 C7=40 C8=0  C9=40
midpoint  C1=40 C2=40 C3=40 C4=40 C5=40 C6=40 C7=40 C8=40 C9=40   (676 total)
```

Across all 676 there are exactly **two distinct `(ref, got)` pairs**: `ref=-0 got=0`
and `ref=0 got=-0`. No difference of magnitude anywhere, at any period, in any arm.
n=50000 reproduces 676 identically; n=5000 gives 634.

Two more data points, because they cut in opposite directions:

* The independent naive O(n·period) reference we wrote from the definition also
  differs from shipped C0 — 40 cases, `TA_MIDPOINT` only, all `ref=0 naive=-0`.
  The *definition* does not pin the sign either.
* The only cell that is bit-identical including zero sign is **C8 (naive full
  rescan) on MAX** — and C8 is the slowest candidate by a wide margin (16.6× C0 at
  MAX/p200, 56–88× at p1000). So this is a real trade, not a free choice.

You wrote "bit-identical, not close". Literally, **nothing here clears that** — but
neither does the shipped code against its own oracle, and the repo already has a
sanctioned bucket for exactly this. We are not going to decide which of those two
facts governs. Practical exposure is narrow (OHLC prices are positive, so ±0.0 does
not arise from market data) but real: `MIN`/`MAX`/`MIDPOINT` take a general
`inReal`.

---

## Decision 2 — `analyze_fastpath_skip` dialled to `<= 100000`.

Every in-scope function is `stream`-flagged, and `ta_codegen` does not have a
hand-written stream per function — it **synthesizes** `TA_S_*` by transcribing the
batch body. `classify_locals` (`streaming.rs`, the `NonScalarState` arm at
`streaming.rs:4222` on `dev`) admits as carried stream state only scalars and
**literal-fixed-size** arrays. A malloc'd, period-sized deque or Van Herk scratch is
a pointer, so it is `StreamError::NonScalarState`, and `generate` exits 1
(`streaming.rs:3452`). Reproduced on `dev` @ `3ee7157fb`:

```
$ cargo run -- generate --func=MAX      # body = plain monotonic deque
error: MAX: YAML declares `streaming: true` but the function is not streamable
       at stage 1: non-scalar loop state `dqIdx`
```

So: O(1)-amortized sliding extrema needs O(period) state; the stream synthesizer
forbids runtime-sized O(period) state. Those two are incompatible as the generator
stands. This is a *build* failure, not a numeric one.

The escape hatch we used is an existing, sanctioned one — `analyze_fastpath_skip`,
which recognises `<prologue> if (<param> <= <lit>) { fast } else { general }
<epilogue>` as a bit-identical batch-only split, streams the `else` arm "for EVERY
param", and lets `stream_verify` enforce bit-exactness across the threshold.
`midprice.c:97` already ships this at `<= 20`. We set the literal to `100000`, the
top of `optInTimePeriod`'s declared range:

```c
   if( optInTimePeriod <= 100000 )
   {
      /* Van Herk / Gil-Werman block scan ... */
   }
   else
   {
      /* the current cached-extremum automaton, unchanged */
   }
```

This works, today, with zero generator changes: `generate` succeeds, the four
backends build, and `TA_MAX_StepInternal` contains the automaton, not the deque.

**But dialled to the full range the `else` arm is dead code in batch and alive only
as the stream source.** That is legal and it is arguably the correct structure for
C2 (see the streaming section) — but it is a design call, not a mechanical one, and
it is yours. If you reject it, C1–C4 need a new stream tier in the generator that
can carry period-sized scratch, and that is real work whose cost should be weighed
against the numbers below rather than assumed.

---

## Candidate ledger

Scope: value-only, `MAX` (single-extremum) and `MIDPOINT` (dual-extremum), per your
"a result on MAX alone won't settle it".

| id | algorithm | `generate` | vs C0 on the corpus | scratch |
|---|---|---|---|---|
| C0 | baseline, byte-identical copy of the shipped input | ok | reference | none |
| C1 | monotonic deque, malloc period-sized | ok *via fast-path-skip*, after an operand swap (below) | PASS | 2×O(p) |
| C2 | Van Herk / Gil-Werman, **block-batched** | ok *via fast-path-skip* | PASS | 2×O(p)/ch |
| C3 | Van Herk / Gil-Werman, **per-sample** | ok *via fast-path-skip* | PASS | O(p)/ch |
| C4 | two-stack queue | ok *via fast-path-skip* | PASS | 2×O(p)/ch |
| C5 | rescan tie-break `>` → `>=` | ok (scalar) | PASS | none |
| C9 | incoming arm `else if` → `if` | ok (scalar) | PASS | none |
| C6 | reverse rescan, strict compare | **ELIMINATED** | PASS | none |
| C7 | deque over pow-2 capacity (C1 fairness check) | **ELIMINATED — generator panics** | PASS | ≤4×O(p) |
| C8 | no cache, full rescan every bar | **ELIMINATED** | PASS | none |

"PASS" = bit-identical to C0 by `memcmp` on the doubles (not a tolerance) over all
11 shapes × 22 periods × {plain, in-place per #130, non-zero `startIdx`} — with the
signed-zero caveat above, which applies to every row. The gate is
mutation-validated: a `1.0000001 *` factor injected at all 28 `outReal[outIdx++] =`
sites produced **48/48** distinct detections (3 arms × 2 functions × 8 candidates).
C0 itself is additionally checked against the independent naive reference.

In-tree, for every non-eliminated candidate, on all four backends:

* 4/4 language servers build
* `--xlang-hash`: `Rust: 211162 cases, 0 mismatch(es)`, `Java: 211108 cases, 14
  mismatch(es)` — the 14 are `TA_HT_TRENDMODE` C-vs-Java and are **pre-existing on
  the pristine tree**, i.e. the control fails `--xlang-hash` too. Every candidate is
  therefore judged as a *delta vs the C0 control*, not as "0 mismatches". No
  candidate adds a mismatch, and none names a rolling-extrema function.
* `--codegen`: `Stream verify: 168 functions, 15596 legs bit-exact vs batch,
  0 expected-reject probes, 0 without a stream` — identical to control. That
  `0 without a stream` is the load-bearing line for Decision 2: under the
  fast-path-skip factoring MAX/MIDPOINT keep their `TA_S_*` API **and** stay
  bit-exact with the batch.
* default `ta_regtest`: `Variant parity: 168 functions x 31392 vectors, 31392
  four-way comparisons (26832 with output), bit-identical` — the #130 aliasing gate.
* `--fuzz-064`: 0 real failures, benign signed-zero as tabulated above.

Two environment gaps had to be closed first, or the gate would have silently covered
2 of 4 backends: the system JDK is 11 and the Java backend needs `--release 17`, and
the system .NET SDK is 7.0.200 against a `net10.0` project. `ta_064_serve` also had
to be built from the `v0.6.4` tag before `--fuzz-064` could run at all.

---

## Measurements

Method first, since you said it matters as much as the result. Wall-clock only, no
instruction counts as a metric (we took your warning). Per-bar cost is `wall_ns / (iters * outNBElement)`,
`CLOCK_MONOTONIC`, iteration count sized from a pilot to ~20 ms per rep, `min` over
3 reps within a layout, **median and full `[min..max]` across 12 layouts**. n=100000.
Each candidate is its own translation unit, called through a function-pointer table
— no LTO, no cross-boundary inlining, so no candidate can be specialised to the
caller. C0 is a byte-identical copy of the shipped input and C5 differs from it by
exactly two characters; `diff` proves both, so the reference cannot have drifted.

Ratio vs C0, lower is faster. `randwalk-hi` omitted for width — it tracks `randwalk`
within 4% in every cell (worst: MAX / C3 / p1000, 0.155 vs 0.161).

#### MAX

| period | | randwalk | gbm | trend-chop-1p | trend-chop-4p | constant |
|---|---|---|---|---|---|---|
| **14** | C0 ns/bar | 4.05 | 4.05 | 4.36 | 4.43 | 7.05 |
| | C1 deque | 2.46 [2.43-2.51] | 2.49 [2.46-2.53] | 2.26 [2.23-2.33] | 2.12 [2.07-2.20] | 0.71 [0.61-0.71] |
| | C2 VH-batched | **0.28** [0.27-0.29] | 0.28 [0.27-0.29] | 0.26 [0.26-0.27] | 0.25 [0.25-0.27] | 0.16 |
| | C3 VH-per-sample | 0.87 [0.86-0.90] | 0.87 [0.86-0.90] | 0.80 [0.80-0.83] | 0.79 [0.78-0.86] | 0.21 |
| | C4 two-stack | 0.93 [0.92-0.93] | 0.93 [0.92-0.94] | 0.86 [0.85-0.88] | 0.83 [0.81-0.85] | 0.24 |
| | C5 `>=` | 1.10 [1.09-1.13] | 1.09 [1.09-1.14] | 1.09 [1.07-1.13] | 1.10 [1.08-1.14] | 0.15 |
| | C9 `else if`→`if` | 0.92 [0.91-0.94] | 0.92 [0.90-0.94] | 0.91 [0.89-0.93] | 0.90 [0.89-0.94] | 0.19 |
| **30** | C0 ns/bar | 4.39 | 4.29 | 4.79 | 5.66 | 18.14 |
| | C1 deque | 2.31 [2.25-2.34] | 2.40 [2.36-2.43] | 2.11 [2.09-2.15] | 1.67 [1.65-1.71] | 0.28 |
| | C2 VH-batched | **0.26** | 0.27 | 0.24 [0.24-0.25] | 0.20 [0.20-0.21] | 0.06 |
| | C3 VH-per-sample | 0.66 [0.66-0.68] | 0.68 [0.68-0.69] | 0.62 [0.61-0.64] | 0.52 [0.51-0.53] | 0.10 |
| | C4 two-stack | 0.72 [0.71-0.73] | 0.74 [0.73-0.76] | 0.67 [0.66-0.68] | 0.57 [0.56-0.58] | 0.12 |
| | C5 `>=` | 1.19 [1.19-1.20] | 1.19 [1.18-1.20] | 1.20 [1.18-1.21] | 1.21 [1.18-1.23] | 0.06 |
| | C9 `else if`→`if` | 0.96 [0.95-0.97] | 0.96 [0.95-0.98] | 0.96 [0.95-0.98] | 0.95 [0.93-0.97] | 0.07 |
| **200** | C0 ns/bar | 7.65 | 6.97 | 6.71 | 5.96 | 163.01 |
| | C1 deque | 1.34 [1.31-1.37] | 1.50 [1.47-1.51] | 1.53 [1.52-1.54] | 1.70 [1.68-1.71] | 0.03 |
| | C2 VH-batched | **0.23** [0.23-0.24] | 0.26 [0.25-0.26] | 0.27 [0.26-0.27] | 0.30 | 0.01 |
| | C3 VH-per-sample | 0.32 | 0.35 [0.35-0.36] | 0.35 [0.35-0.36] | 0.42 [0.41-0.43] | 0.01 |
| | C4 two-stack | 0.36 [0.35-0.36] | 0.39 [0.39-0.40] | 0.39 [0.39-0.40] | 0.47 | 0.01 |
| | C5 `>=` | 1.30 [1.29-1.32] | 1.30 [1.26-1.31] | 1.30 [1.29-1.31] | 1.29 [1.28-1.30] | 0.01 |
| | C9 `else if`→`if` | 1.02 [1.02-1.03] | 1.02 [1.02-1.03] | 1.04 [1.02-1.04] | 1.04 [1.03-1.05] | 0.01 |
| **1000** | C0 ns/bar | 14.34 | 9.25 | 12.49 | 12.08 | 849.56 |
| | C1 deque | 0.72 [0.70-0.72] | 1.14 [1.11-1.15] | 0.83 [0.82-0.84] | 0.85 [0.84-0.86] | 0.01 |
| | C2 VH-batched | **0.13** [0.13-0.14] | 0.21 | 0.15 [0.15-0.16] | 0.16 | 0.0023 |
| | C3 VH-per-sample | 0.16 [0.15-0.16] | 0.24 [0.24-0.25] | 0.17 [0.17-0.18] | 0.18 [0.18-0.19] | 0.0023 |
| | C4 two-stack | 0.18 [0.17-0.18] | 0.28 [0.27-0.29] | 0.20 | 0.21 | 0.0028 |
| | C5 `>=` | 1.34 [1.33-1.36] | 1.33 [1.31-1.33] | 1.34 [1.33-1.38] | 1.34 [1.34-1.36] | 0.0013 |
| | C9 `else if`→`if` | 1.02 [1.01-1.03] | 1.03 [1.01-1.04] | 1.03 [1.02-1.04] | 1.03 [1.01-1.04] | 0.0016 |

#### MIDPOINT

| period | | randwalk | gbm | trend-chop-1p | trend-chop-4p | constant |
|---|---|---|---|---|---|---|
| **14** | C0 ns/bar | 7.42 | 7.44 | 7.90 | 8.03 | 13.61 |
| | C1 deque | 2.20 [2.19-2.24] | 2.23 [2.18-2.27] | 2.04 [2.01-2.05] | 1.93 [1.92-1.96] | 0.67 |
| | C2 VH-batched | **0.27** [0.27-0.28] | 0.27 [0.27-0.28] | 0.26 [0.25-0.26] | 0.25 | 0.15 |
| | C3 VH-per-sample | 0.57 [0.56-0.57] | 0.57 [0.55-0.57] | 0.53 [0.52-0.54] | 0.53 [0.51-0.53] | 0.17 |
| | C4 two-stack | 0.93 [0.92-0.94] | 0.93 [0.93-0.94] | 0.88 [0.87-0.90] | 0.87 [0.85-0.88] | 0.23 |
| | C5 `>=` | 1.12 [1.11-1.13] | 1.12 [1.11-1.13] | 1.12 [1.11-1.13] | 1.12 [1.11-1.13] | 0.13 |
| | C9 `else if`→`if` | 0.88 [0.88-0.89] | 0.88 [0.88-0.89] | 0.88 [0.88-0.89] | 0.89 [0.88-0.89] | 0.14 |
| **30** | C0 ns/bar | 8.37 | 8.32 | 9.08 | 10.66 | 35.84 |
| | C1 deque | 2.01 [1.98-2.03] | 2.06 [2.01-2.08] | 1.85 [1.82-1.89] | 1.49 [1.48-1.51] | 0.25 |
| | C2 VH-batched | **0.26** | 0.26 [0.26-0.27] | 0.24 | 0.20 [0.20-0.21] | 0.06 |
| | C3 VH-per-sample | 0.44 [0.44-0.46] | 0.45 [0.44-0.45] | 0.41 [0.41-0.42] | 0.35 [0.35-0.36] | 0.07 |
| | C4 two-stack | 0.72 [0.71-0.73] | 0.72 [0.72-0.74] | 0.67 [0.67-0.68] | 0.58 [0.57-0.58] | 0.10 |
| | C5 `>=` | 1.19 [1.19-1.21] | 1.19 [1.19-1.21] | 1.20 [1.19-1.21] | 1.21 [1.19-1.22] | 0.05 |
| | C9 `else if`→`if` | 0.94 [0.92-0.95] | 0.94 [0.93-0.97] | 0.94 [0.93-0.95] | 0.94 [0.93-0.95] | 0.05 |
| **200** | C0 ns/bar | 16.17 | 15.84 | 12.83 | 11.42 | 325.55 |
| | C1 deque | 1.07 [1.05-1.09] | 1.12 [1.10-1.13] | 1.38 [1.36-1.46] | 1.53 [1.52-1.55] | 0.03 |
| | C2 VH-batched | **0.21** | 0.22 [0.21-0.22] | 0.27 [0.27-0.28] | 0.30 [0.30-0.31] | 0.01 |
| | C3 VH-per-sample | 0.22 [0.22-0.23] | 0.23 | 0.28 [0.28-0.29] | 0.33 [0.32-0.34] | 0.01 |
| | C4 two-stack | 0.32 [0.32-0.33] | 0.33 | 0.40 [0.40-0.41] | 0.48 [0.47-0.48] | 0.01 |
| | C5 `>=` | 1.32 [1.28-1.33] | 1.31 [1.30-1.32] | 1.31 [1.31-1.34] | 1.31 [1.30-1.32] | 0.01 |
| | C9 `else if`→`if` | 1.00 [0.97-1.00] | 1.00 [0.98-1.02] | 1.00 [0.99-1.02] | 1.01 [0.99-1.02] | 0.01 |
| **1000** | C0 ns/bar | 34.97 | 30.43 | 24.14 | 23.42 | 1700.26 |
| | C1 deque | 0.50 [0.49-0.51] | 0.59 [0.59-0.60] | 0.74 [0.73-0.75] | 0.76 [0.75-0.79] | 0.01 |
| | C2 VH-batched | 0.11 | 0.12 | 0.16 [0.15-0.16] | 0.16 | 0.0022 |
| | C3 VH-per-sample | **0.10** | 0.12 [0.11-0.12] | 0.15 [0.14-0.15] | 0.15 | 0.0020 |
| | C4 two-stack | 0.14 | 0.16 | 0.20 | 0.21 [0.20-0.21] | 0.0026 |
| | C5 `>=` | 1.35 [1.34-1.36] | 1.35 [1.34-1.37] | 1.34 [1.32-1.35] | 1.35 [1.32-1.35] | 0.0010 |
| | C9 `else if`→`if` | 1.00 [0.99-1.01] | 1.00 [0.99-1.01] | 1.01 [0.98-1.01] | 1.01 [0.97-1.01] | 0.0011 |

Cells with no bracket have a `[min..max]` that rounds to the median at the printed
precision; the `constant` column at p1000 is given to four decimals because two
would round every candidate to 0.00.
Layout sensitivity is small here but not nil. Over the 504 ratio cells in the full
pass, the per-cell spread `[min..max]` around the median is: median 1.7%, 84% within
±3%, 95% within ±5%, 98% within ±10%. The widest single outlier is
MIDPOINT / C3 / randwalk-hi / p30 at 0.44 [0.44-0.67], where one build was 50%
worse. That one cell is exactly why the sweep exists, and it does not touch any
conclusion — but it is also why no single-build number in this family should be
trusted to better than a few percent.

### How the layout sweep was actually done

Twelve binaries from the same object files wherever possible:

| family | layouts | what moves | machine code |
|---|---|---|---|
| `.text` padding | L0–L5 (pad 0/64/192/576/1728/5184 B) + 2112/9280 | a `.space N` blob linked into `__text` **ahead** of the candidate objects | **identical** |
| link order | L6 L7 (reversed, pad 0/576) | which candidates share cache sets / a page | **identical** |
| function alignment | L8 L9 (`-falign-functions=32`, pad 0/576) | inter-function padding | changed (own family) |

**Verified to move code, not assumed to.** `nm -n` on the twelve binaries puts
`_max_C0` at 0x5c0 / 0x600 / 0x680 / 0x800 / 0xc80 / 0x1a00 / 0xe00 / 0x2a00 across
the pad family — deltas exactly equal to the pad sizes — and at 0x53b0 with the link
order reversed.

`-falign-functions=4` was tried and **dropped**: clang floors function alignment at
16 on x86-64, so those binaries came out byte-identical to L0/L3. Keeping them would
have inflated the layout count with non-layouts. Replaced by two further pad points.

Limitation to be explicit about: padding `.text` and permuting link order move where
a function *starts*; they do not reshuffle basic blocks *inside* it. The sweep
separates inter-function placement from algorithmic difference, but it cannot absorb
an intra-function codegen difference. Where one showed up (C5) it was root-caused by
disassembly instead.

### Guarded vs unguarded, from the built library

`libta-lib.a` linked directly, `TA_MAX` vs `TA_MAX_Unguarded`, ns/bar, randwalk:

| func | period | C0 g | C0 u | C2 g | C2 u | C9 g | C9 u |
|---|---|---|---|---|---|---|---|
| MAX | 14 | 3.70 | 4.04 | 1.19 | 1.19 | 3.61 | 3.80 |
| MAX | 30 | 4.09 | 4.42 | 1.28 | 1.28 | 4.16 | 4.30 |
| MAX | 200 | 7.36 | 7.65 | 1.80 | 1.80 | 7.68 | 7.85 |
| MAX | 1000 | 14.06 | 14.35 | 1.95 | 1.93 | 14.48 | 14.65 |
| MIDPOINT | 14 | 7.10 | 7.67 | 2.13 | 2.12 | 6.30 | 6.57 |
| MIDPOINT | 1000 | 34.23 | 34.61 | 3.76 | 3.76 | 34.69 | 35.06 |

The guarded entry point is consistently 1–8% *faster* here. It is a separate full
copy of the body, so that is placement, not validation cost, and it sits inside the
layout-sensitivity band — not a real effect to reason from. **The ratios are the
same in both variants** (C2: 0.32/0.30 at p14, 0.24/0.24 at p200, 0.14/0.13 at
p1000), so no conclusion depends on which is quoted. It also cross-validates the
harness through a different build path: C0 MAX p14 3.70–4.04 (in-tree cmake library)
vs 4.05 (harness TU); MIDPOINT p1000 34.2–34.6 vs 34.97.

### Where the O(period)-scratch candidates lose

**(a) Short ranges.** C1–C4 `malloc` per call. At n=100000 that amortises to
nothing; when the requested range is comparable to the period it does not.
3 layouts, randwalk, ratio vs C0:

| func | period | n=256 | n=1024 | n=8192 |
|---|---|---|---|---|
| MAX | 14 | C2 0.87 / C3 1.07 / C4 1.29 | C2 0.66 | C2 0.32 |
| MAX | 200 | **C2 1.41 / C3 1.18 / C4 2.29** | C2 0.65 | C2 0.26 |
| MIDPOINT | 200 | **C2 1.53 / C3 1.23 / C4 2.13** | C2 0.19 | C2 0.26 |

n=256 at period 200 is 57 output bars — the malloc and the first block's setup have
nothing to amortise against. In absolute terms it is ~90 ns for the whole call, so
it may simply be acceptable, but it is a real regression region and it is the
natural place for a threshold if one is wanted. C9, having no scratch, is 1.04–1.24
at n=256 and 1.00–1.19 at n=1024 (the first window always rescans, so its one extra
comparison is proportionally more visible).

**(b) `mono-up` on MAX, and only there.** From the full 11-shape landscape (L0 only):

| MAX, mono-up | C0 ns/bar | C1 | C2 | C3 | C4 | C8 | C9 |
|---|---|---|---|---|---|---|---|
| period 200 | **1.10** | 3.96 | 1.64 | 1.92 | 2.17 | 115 | 1.20 |
| period 4096 | **1.13** | 4.48 | 1.79 | 1.97 | 2.25 | 3060 | 1.20 |

`mono-up` is MAX's perfect case: the maximum is the newest bar on every bar, the
rescan never fires, C0 costs one comparison, and nothing can beat that — every
O(1)-amortized candidate pays bookkeeping for nothing. Note this is
**single-extremum only**: on MIDPOINT, `mono-up` pins the low, and C2 goes
0.27 → 0.02 → 0.00 as the period goes 14 → 200 → 4096. Note also that **C9 regresses
here too** (1.20–1.24 on MAX/mono-up), which is the one place its "free" label does
not hold.

You told us not to optimise for monotone input. We are reporting this as the tail it
is, in the direction that is inconvenient for us.

Period 4096 was included specifically to look for a top-end crossover (C2's scratch
at p4096 is 2 × 4096 doubles = 64 KB, past L1). There is none on this machine: at p4096
C2 is 0.12–0.17 (gate) / 0.07–0.13 (trend) on MAX, and 0.06 / 0.08–0.09 on MIDPOINT.
That matters because a crossover would have fitted the `<= <literal>` predicate
perfectly, and it is not needed.

---

## Three things you will probably care about

### 1. The deque loses at the default periods. Your "unverified" claim is true there, and false for long windows.

C1 (monotonic deque) is **2.20–2.49× worse than C0 at period 14** and **2.01–2.40×
at period 30** on the gate shapes — the two family defaults. It is still worse at
200 on every gate shape (1.07–1.50). C7 (pow-2 capacity, mask instead of wrap
branches) recovers 8–13%, nowhere near enough.

Measured crossover, gate shapes (L0 landscape, periods 14/30/64/200/1000/4096):

| C1 vs C0 | 14 | 30 | 64 | 200 | 1000 | 4096 |
|---|---|---|---|---|---|---|
| MAX randwalk | 2.46 | 2.33 | 1.96 | 1.35 | 0.73 | 0.67 |
| MAX gbm | 2.54 | 2.40 | 2.03 | 1.51 | **1.15** | 0.94 |
| MIDPOINT randwalk | 2.21 | 2.01 | 1.63 | 1.07 | 0.51 | 0.30 |
| MIDPOINT gbm | 2.25 | 2.06 | 1.68 | 1.13 | 0.60 | 0.26 |

So it crosses 1.0 between 200 and 1000 for three of the four, and only between 1000
and 4096 for MAX/gbm. The reason is not exotic: per-bar deque bookkeeping loses to a
rescan that usually does not fire.

That is the suggestion in the issue body — ours — measuring worst. On this
toolchain, on the periods that actually ship as defaults, whoever told you the deque
had been ruled out was right, even without the benchmark.

### 2. `>=` is a trap. Do not reach for it after reading #152.

The obvious reading of your #152 note is "flip the rescan's strict `>` to `>=` and
the flat case fixes itself". It does — and it costs 9–35% everywhere else, for a
reason that has nothing to do with the algorithm. `>` lets clang recognise the max
idiom and emit `maxsd`; `>=` does not, and it falls back to `cmpnlesd` + `blendvpd`
+ extra `movapd`. Disassembly of the same TU, `otool -tV`, nop padding excluded:

| | insns | `maxsd` | `cmpnlesd` | `blendvpd` |
|---|---|---|---|---|
| C0 (`>`) | 147 | 5 | 0 | 0 |
| C5 (`>=`) | 159 | **0** | 5 | 5 |
| C9 (`else if`→`if`) | 146 | 5 | 1 | 1 |
| C6 (reverse rescan, `>` kept) | **113** | 5 | 0 | 0 |
| C8 (no cache) | 102 | 5 | 0 | 0 |

C5 is 1.09–1.35 on every gate and trend cell in the tables above (and up to 1.38 on
`mono-down`) — for that reason alone.

**C9 gets the same tie-break with a two-token change and does not pay it.** Turn the
incoming-bar arm from `else if` into `if`, so it also runs on a rescan bar:

```c
   if( highestIdx < trailingIdx ) { /* forward rescan, strict `>` unchanged */ }
   if( tmp >= highest )        /* was: else if */
   { highestIdx = today; highest = tmp; }
```

On a tie the cached index then moves to `today` instead of parking on
`trailingIdx` — the exact mechanism you described — and after the first rescan a
flat stretch stops rescanning at all. `tmp` is correct in both paths: the forward
rescan's last iteration is `i == today`, so it leaves `tmp == in[today]`, the same
value the top-of-loop read put there. It keeps all 5 `maxsd` and is one instruction
shorter than C0; it does pick up one `cmpnlesd`+`blendvpd` pair, against C5's five.

Result: 0.88–1.04 on every gate and trend cell at every period, and the flat worst
case goes from 1.00 to 0.0016 (MAX) / 0.0011 (MIDPOINT) at p1000. No scratch, no
codegen concession, no streaming question. Its one regression is MAX/`mono-up` at
1.20–1.24, and short ranges (1.04–1.24 at n=256).

C6 (rescan the window **backwards** from `today` with the comparison left strict —
newest-first means an equal value never displaces the newer one already held) gets
the same tie-break, produces the leanest code of any variant, and measures 0.88–0.98
on every gate and trend cell (never above 1.00 on any shape). It is **eliminated by
the generator**, see below. C9 is the cheaper route to the same property.

### 3. Van Herk: the two forms differ by up to 3.1×, and the ordering inverts.

You said they are not the same code and may not measure the same. Both directions
are confirmed:

* MAX / p14 / randwalk: C2 **0.28** vs C3 **0.87** — the batched form is 3.1× faster.
* MIDPOINT / p1000 / randwalk: C3 **0.10** vs C2 **0.11** — the per-sample form wins.

If only one had been measured, the answer would have been wrong at one end or the
other.

Overall, on this machine C2 (block-batched) wins on every gate shape, every trend
shape and every period measured: 3.6×–7.7× on MAX and 3.7×–9.1× on MIDPOINT against
the acceptance shapes, with the advantage *growing* with period and a cost that is
nearly input-independent. Its regression surface on this corpus is exactly the two
regions in (a) and (b) above.

---

## Streaming path and per-bar latency

You asked specifically what our approach does here. Two separate answers, and they
came out differently than expected.

**Bit-exactness is not the binding constraint; expressibility is.** For value-only
functions a deque and the current automaton produce identical bits. The blocker is
that the stream synthesizer cannot carry runtime-sized state (Decision 2). This is a
build failure, not a numeric one.

**Under the fast-path-skip factoring, streaming latency does not change at all** —
the batch gets the new algorithm, `TA_S_*` keeps the cached-extremum automaton it
already has, and `stream_verify` proves the two agree bit-for-bit (15596 legs, 0
without a stream). No new spikes, no rebuild, no new state. That is the honest
answer for C1–C4 and C7: *they do not change streaming latency, because they are not
in the stream.*

For completeness, if a candidate **were** put in the stream:

| candidate | amortized / bar | worst case on a single bar | periodic O(period) rebuild? |
|---|---|---|---|
| C0 baseline | O(period) on trend and flat input | O(period), on **every** bar | no |
| C1 / C7 deque | O(1) | O(period) — one bar can pop the whole deque | no |
| C2 VH block-batched | O(1) | **not a per-bar algorithm** — emits `period` outputs per block iteration | inherent |
| C3 VH per-sample | O(1) | O(period), **1 bar in `period`** (suffix materialisation) | yes |
| C4 two-stack | O(1) | O(period), **1 bar in `period`** (drain) | yes |
| C5 / C9 rescan fix | improved (rescans fire far less on tie-heavy input) | O(period), on every bar — **unchanged** | no |

Two points worth stating plainly:

* **C3 and C4 would strictly improve worst-case per-bar latency, not worsen it.**
  Today's worst case is a full O(period) rescan on *every* bar for flat or trending
  input; theirs is O(period) on one bar in `period`. Your "periodic rebuild changes
  per-bar latency" concern is real in general but points the favourable way here.
* **C2 is inherently batch-only.** It produces outputs in bursts of `period` and
  needs the older block complete before it emits any of them. For C2 the
  fast-path-skip factoring is not a workaround, it is the correct structure — the
  batch and the stream really are different algorithms, and they are bit-identical
  because both are exact selections over the same window. Which is precisely why
  Decision 2 is a design call and not a hack.

**In-place aliasing (#130) shaped every implementation.** `outReal[outIdx]` can land
exactly on `inReal[trailingIdx]`, so every candidate keeps **copies of values** in
its scratch and never re-reads `inReal[i]` for an `i` the loop has already written.
(An index-only deque that dereferences `inReal[dq[head]]` happens to be safe too —
no dereferenced index is ever `< trailingIdx` — but that is fragile, so the
candidates do not rely on it.) Checked, not argued: the harness runs every candidate
with `outReal == inReal` and compares bitwise, and the in-tree four-way variant
parity gate agrees.

---

## Generator obstacles found on the way

All re-verified against `dev` @ `3ee7157fb` today, not just on our base.

**Three candidates died in `generate`:**

```
C6 (reverse rescan)   error: MAX: streamable by analysis but the transition cannot be built:
                             MAX: index variable `i` leaks into the transition body
C8 (naive rescan)     same
C7 (pow-2 deque)      thread 'main' panicked at src/parser/c_source.rs:2039:22:
                             Expected identifier, got Ampersand
```

C6/C8: the T4 transition builder drops the rescan's index bookkeeping and then
checks that no dropped variable survives; a backwards `while( i > trailingIdx )
{ i--; ... }` puts `i` on the wrong side of that split. Instructive that `midprice`
ships a naive-rescan arm and is fine — but only *behind* the `<= 20` predicate, so
the stream never transcribes it. A bare naive rescan as the only arm is not
streamable today.

C7: **the input-`.c` parser has no support for the bitwise `&` operator at all**, and
it panics rather than reporting. That is worth a one-line fix regardless of this
issue — an unsupported operator in a source-of-truth input should be a diagnostic.

**Three C→Rust emitter issues.** All were worked around in our input `.c`, not
fixed, and all silently produce non-compiling Rust rather than a diagnostic:

1. **`if( k < 0 ) k += optInTimePeriod;`** on an `int k` emits
   `k += ((optInTimePeriod) as f64);` → `error[E0277]: cannot add-assign f64 to i32`.
   **Still reproduces on `dev` @ `3ee7157fb`.** Avoided by doing all wrap arithmetic
   with `+ cap - 1` and `>= cap`, never letting an index go negative.
2. **`blockStart += optInTimePeriod;`** with `blockStart: usize` and
   `optInTimePeriod: i32` dropped the `as usize` cast that the explicit form gets →
   `error[E0308]`. This eliminated C2 — the fastest candidate — on our first gate
   pass. **You have already fixed this**, in `fc7ab50dd` (widening the gate from the
   `is_likely_index_var` name heuristic to `ctx.index_vars`) plus `58dca3b6c`
   (excluding sentinels). Confirmed: on `dev` @ `3ee7157fb` the compound form now
   emits `blockStart += (optInTimePeriod) as usize;` and the Rust crate builds
   clean. Our workaround (`blockStart = blockStart + optInTimePeriod;`) is no longer
   needed. `blockStart` is exactly the case `fc7ab50dd` describes — usize by
   subscript inference, invisible to the name heuristic.
3. **`if( dqI[hd] < trailingIdx )`** emits `if (dqI[hd]) as usize < trailingIdx {` →
   ``error: `<` is interpreted as a start of generic arguments for `usize`, not a
   comparison``. **Still reproduces on `dev` @ `3ee7157fb`.** Swapping the operands
   (`trailingIdx > dqI[hd]`) puts the cast where nothing can follow it. That is the
   "operand swap" in the C1 ledger row.

None of (1) or (3) is a live bug — no shipped input hits either shape — but both are
traps for anyone writing a new `<name>.c`, which is the documented way to contribute
here.

---

## Where our numbers disagree with yours, and what we do not know

* **You are right that our P=14/P=30 tail rows do not transfer.** MIDPOINT
  constant/randwalk is 1.83× on this 12-layout sweep (13.61 / 7.42) against your
  1.25×, and 4.29× at P=30 (35.84 / 8.37) against your 2.72× — i.e. this sweep
  reproduces the 1.84× / 4.25× you quoted from the PR to within a percent, on the
  same box, and the disagreement is machine-to-machine, not run-to-run. We are not
  treating the small-period tail rows as transferable. As a working rule we regard
  **any P=14 ratio inside about 1.15× as undecided on one machine.**
* **Our corpus build predates `71bdfbd4c`.** Everything here was measured on
  `8fb6222d4` (the #153 branch as submitted). Your seed fix means `randwalk` is
  byte-identical to current `dev` (shape index 0) but `randwalk-lo`, `randwalk-hi`,
  `gbm` and all four `trend-chop-*` series are **different RNG streams** on our
  build, and — your point exactly — our four trend-chop regimes varied path and
  regime length together, so they are not a controlled sweep across regime length.
  Each shape is still a valid instance of its class and every conclusion above is
  per-shape, but the cross-regime ordering should not be read from our numbers.
  `mono-up` / `mono-down` / `constant` are deterministic and unaffected.
* **On our build the gate "controls" are not within 1% of `randwalk` past period
  ~200.** C0, gbm vs randwalk: MAX 0.0% at p14, −2.1% at p30, **−8.9% at p200,
  −35.5% at p1000**; MIDPOINT +0.3% / −0.5% / −2.0% / **−13.0%**. That matches your
  ≤1% finding at 14/30 and diverges above it. Our gbm stream is not the one current
  `dev` generates, so this may be path-specific — but the divergence is
  monotone in period rather than random, so if it survives on the post-`71bdfbd4c`
  series then "cannot move the rescan rate" may not extend past ~200.

Other limits, stated rather than buried:

* **One platform.** Apple clang 21.0.0, x86-64, i7-10700K. GCC x86-64, MSVC x86-64
  and Apple-silicon arm64 are all unmeasured. Per your own note, a result from one
  machine is not transferable — so nothing here is a recommendation.
* **Two functions.** MAX and MIDPOINT only. MIN/MINMAX/MIDPRICE/WILLR are the same
  shape and should follow; **STOCH/STOCHF are not verified at all.** They already
  `malloc` and are still `stream`-flagged, but that is no precedent: their buffer is
  classified by `analyze_composed` as a materialised intermediate series, not as loop
  state. Their body is `prologue + producer loop + ma() sub-call tail`, whereas
  `analyze_fastpath_skip` wants `prologue + if/else of steady loops + epilogue`.
  **Whether fast-path-skip composes with `ComposedPlan` is untested.** If it does
  not, the family splits: MIN/MAX/MINMAX/MIDPOINT/MIDPRICE/WILLR could take a new
  algorithm this way and STOCH/STOCHF could not.
* **`--xlang-hash` is not clean on the control**, so every verdict here is a delta
  vs C0, not an absolute "0 mismatches" (the 14 `TA_HT_TRENDMODE` C-vs-Java
  tolerance mismatches, outside this family).
* **Index-returning functions were not touched**, per your scope call.

One incidental result that is not ours to act on: at period 14 the naive full rescan
(C8) **beats** the cached-extremum baseline — 0.86 on MAX, 0.71 on MIDPOINT — and
the crossover sits between 14 and 30, which independently reproduces the ~20 that
`midprice.c` already documents for its own arm, and extends it to the
single-extremum shape. `midprice.c` is the only input in the tree with that arm —
yet MIDPOINT and WILLR carry the same `default: 14` and do not have it. Giving them
the `<= 20` arm MIDPRICE already ships would be ~15–30% at their default period with
zero new algorithm and zero new memory. It does *not* help MIN/MAX/MINMAX at their
`default: 30`, where C8 is 2.2× worse. That is independent of everything else here
and does not need either decision.

---

## Phase 2, and why we are not starting it yet

Prepared and ready: **GCC x86-64**, **MSVC 2022 x86-64**, **clang x86-64 on the same
box as GCC** (your "separates compiler from CPU" slot), and **Apple clang arm64**
(M5 Max — the machine we used for the #146 and #150 arm64 rounds). You named
MSVC/Windows as the least likely to vectorize a branch-free block scan well and the
one that matters most because a plain-C algorithm ships ungated; we have that
environment, so it will not be the gap.

Plan: **C2, C3, C4 and C9** on all four — not C2 alone. C2 wins on this box, but the
C2/C3 inversion above is exactly the kind of thing a different vectorizer reorders,
C4 is the branchier structure that could survive where the block scan does not, and
C9 is the no-scratch scalar control that tells you whether any of the machinery is
paying for itself. Same 12-layout method, same corpus (post-`71bdfbd4c` this time),
guarded and unguarded reported separately.

**We are not starting until you rule on Decision 1.** If the signed-zero delta is
not acceptable, C1–C5, C7 and C9 are all out and only C8-on-MAX survives — and C8 is
the slowest candidate, so most of a four-platform sweep would be wasted. Decision 2
is less destructive but decides whether the deliverable is a `.c` change or a
generator change, which changes the shape of the PR.

Everything above — the harness, `METHOD.md`, the ten candidate inputs, the gate logs
and the raw per-layout CSVs — can be posted as a branch or a PR if that is more
useful to you than a comment.
