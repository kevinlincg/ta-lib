# #147 Phase 1 — rolling min/max replacement algorithm evaluation

Workdir: `/private/tmp/.../scratchpad/talib-147-algo/`
- `ta-lib/` — clone (base = corpus PR branch `bench/trend-chop-corpus`, HEAD 8fb6222d4,
  = upstream dev + merged #152). Remotes: `upstream` (github), `corpuslocal`.
  Based on the corpus branch on purpose: PR #153's `--shape` corpus is the required input.
- `harness/` — out-of-tree measurement harness. `METHOD.md` is the method writeup.
  `mkvariants.py` -> `variants/<f>.<C>.c`; `build.sh` (12 layouts); `run.sh`; `aggregate.py`.
- Machine: iMac20,1, **Intel i7-10700K**, x86_64, **Apple clang 21.0.0**. (Same CPU as the
  issue's original table. This is the maintainer's "Clang x86-64" slot; GCC x86-64,
  MSVC x86-64 and Apple-silicon arm64 remain unmeasured -> that is Phase 2.)

Scope: value-only MIN MAX MINMAX MIDPOINT MIDPRICE WILLR STOCH STOCHF.
Index-returning excluded (maintainer: separate output-compat issue).

## READ (done)
- #147 maintainer reply 2026-07-29T16:26:46Z — full.
- #152 maintainer comment 2026-07-31T11:47:00Z — full (flat = `2*(period-1)`; the
  `else if` arms never run because the rescan's strict `>`/`<` parks the index at
  `trailingIdx` on ties).
- PR #153 (ours, OPEN) — shapes randwalk{,-lo,-hi}, gbm, trend-chop-{0.5,1,2,4}p,
  mono-up, mono-down, constant. Used as-is; no bespoke input invented.

## FINDING 1 (structural, settles the candidate list)

All 8 in-scope functions are `stream`-flagged. `ta_codegen` derives `TA_S_*` by
**transcribing the batch body**, and `generate` HARD-FAILS (`main.rs:551`, exit 1)
when a stream-flagged function's body stops classifying. Reproduced:

```
$ cp harness/probes/max.C1-deque.c ta_codegen/input/max/max.c
$ cargo run -- generate --func=MAX
error: MAX: YAML declares `streaming: true` but the function is not streamable
       at stage 1: non-scalar loop state `dqIdx`
```

`classify_locals` (streaming.rs:4216-4222) admits as carried stream state only
scalars and **literal-fixed-size** arrays. A malloc'd period-sized deque / Van Herk
scratch is `RealPointer` -> `NonScalarState` -> generate fails.
streaming.rs:160-168 explains the intent ("no deque can reproduce it") — that
reasoning is about the **index-returning** members; for value-only members a deque
is value-identical, so the blocker is mechanical, not numeric.

## FINDING 2 (the escape hatch, and it is an existing sanctioned one)

`streaming::analyze_fastpath_skip` (streaming.rs:469-489) recognises

    <prologue> if( <param> <= <literal> ) { fast } else { general } <epilogue>

as a **bit-identical batch-only split**: only the `else` arm is streamed, "for
EVERY param", and `stream_verify` enforces bit-exactness across the threshold.
MIDPRICE already uses it at `<= 20`. Setting the literal to `100000` (== the top of
`optInTimePeriod`'s legal range) therefore lets the batch always run a new
O(period)-scratch algorithm while the STREAM keeps the current automaton — legal
today, zero generator changes. Verified end to end: `generate --func=MAX` succeeds
and `TA_MAX_StepInternal` contains the automaton, not the deque.

Caveat to state to the maintainer, not to hide: dialled to the full range this
leaves the `else` arm dead in batch and alive only as the stream source. That is a
design call for him. It is defensible (a stream produces one bar at a time, where
the deque's O(period) rebuild-free property matters less than its bookkeeping), but
it is his call, and the honest alternative is FINDING 1's "add a stream tier".

## FINDING 3 (corrects nothing in the maintainer's account, but is new)

Flipping the rescan tie-break from `>` to `>=` — the obvious reading of his #152
note — is **not free**. `>` lets clang recognise the max idiom and emit `maxsd`;
`>=` does not, and it falls back to `cmpnlesd` + `blendvpd` + extra `movapd`:

| | insns | maxsd | blendvpd |
|---|---|---|---|
| C0 (`>`)  | 150 | 5 | 0 |
| C5 (`>=`) | 160 | 0 | 5 |
| C6 (reverse rescan, `>` kept) | **116** | 5 | 0 |

C5 costs 10-34% on tie-free input for that reason alone. **C6 gets the same
tie-break by scanning the rescan BACKWARDS from `today` with the comparison left
strict** — newest-first means an equal value never displaces the newer one already
held. Scalar-only, so it needs no codegen concession at all, and it measures
*faster* than C0 on the gate shapes as well as killing the flat worst case.

## Candidate ledger

| id | algorithm | codegen | bitwise vs C0 | scratch |
|---|---|---|---|---|
| C0 | baseline (byte-identical upstream copy) | ok | reference | none |
| C1 | monotonic deque, malloc period-sized | ok *via fast-path-skip* (needs operand swap) | PASS | 2xO(p) |
| C2 | Van Herk / Gil-Werman, **block-batched** | ok *via fast-path-skip* | PASS | 2xO(p)/ch |
| C3 | Van Herk / Gil-Werman, **per-sample** | ok *via fast-path-skip* | PASS | O(p)/ch |
| C4 | two-stack queue | ok *via fast-path-skip* | PASS | 2xO(p)/ch |
| C5 | rescan tie-break `>=`/`<=` | ok (scalar) | PASS | none |
| C6 | reverse rescan, strict compare | **ELIMINATED** | PASS | none |
| C9 | **`else if` -> `if` on the incoming arm** | ok (scalar) | PASS | none |
| C7 | deque over pow-2 capacity (C1 fairness check) | **ELIMINATED** (parser panics on `&`) | PASS | <=4xO(p) |
| C8 | no cache at all — full rescan every bar | **ELIMINATED** (same as C6) | PASS | none |

"bitwise vs C0" = out-of-tree gate: all 11 shapes x 22 periods x {plain, in-place
(#130), non-zero startIdx}, `memcmp` on doubles. PASS there means bit-identical on
every corpus shape; see FINDING 8 for the +-0.0 caveat that applies to all of them. C0 itself is additionally checked
against an independent naive O(n*p) reference written from the definition.
**Mutation-validated: 48/48 detections** (3 gate arms x 2 functions x 8 candidates)
when a `1.0000001 *` factor is injected at all 28 output sites.
In-tree `--xlang-hash` / `stream_verify` gates: RUNNING (harness/gates/).

Layout sweep validated with `nm -n`, not assumed: `_max_C0` sits at
0x5c0/0x600/0x680/0x800/0xc80/0x1a00/0xe00/0x2a00 across the pad family (deltas ==
pad sizes exactly) and at 0x53b0 with the link order reversed. `-falign-functions=4`
was dropped after measurement: clang floors function alignment at 16 on x86-64, so
those two binaries were byte-identical to L0/L3 and would have padded the layout
count with non-layouts.

## In-tree gate results (4 backends: C, Rust, Java, .NET)

Verdict rule (harness/verdict.py): generate succeeds; all four language servers
BUILD; no `--xlang-hash` mismatch naming a rolling-extrema function; nothing new
vs the C0 control; `ta_regtest` default all-succeeded incl. four-way variant
parity (this is the I/O aliasing gate, #130); `--fuzz-064` 0 real failures.

| cand | in-tree verdict | evidence / death cause |
|---|---|---|
| C0 | **PASS** (control) | 4/4 servers build; xlang rust=0 java=14 (HT_TRENDMODE only, pre-existing); Stream verify 168 funcs / 15596 legs bit-exact vs batch, 0 without a stream; four-way variant parity bit-identical; fuzz-064 0 real failures |
| C1 deque | **PASS** (after a generator work-around) | 1st attempt died on the Rust build: ``` `<` is interpreted as a start of generic arguments for `usize` ``` — `if( dqI[hd] < trailingIdx )` is emitted as `(dqI[hd]) as usize < trailingIdx`. Swapping the operands (`trailingIdx > dqI[hd]`) puts the cast where nothing can follow it; then 4/4 build, xlang rust=0 java=14, Stream verify 15596 legs bit-exact, variant parity bit-identical, fuzz-064 0 real failures |
| C2 VH batched | **PASS** | 4/4 build; xlang rust=0 java=14 (control-identical); Stream verify 15596 legs bit-exact, 0 without a stream; variant parity bit-identical; fuzz-064 0 real failures, 76 benign signed-zero |
| C3 VH per-sample | **PASS** | as C2 |
| C4 two-stack | **PASS** | as C2 |
| C5 `>=` | **PASS** | as C2; fuzz-064 80 benign signed-zero |
| C6 reverse rescan | **ELIMINATED** | `generate`: "MAX: streamable by analysis but the transition cannot be built: index variable `i` leaks into the transition body" |
| C7 pow-2 deque | **ELIMINATED** | `generate` **panics in the C parser**: `thread 'main' panicked at src/parser/c_source.rs:2039: Expected identifier, got Ampersand` — the input-`.c` parser has no support for the bitwise `&` operator at all, and it aborts rather than reporting |
| C8 naive rescan | **ELIMINATED** | same as C6. Instructive: MIDPRICE ships a naive-rescan arm, but only *behind* the `<= 20` fast-path predicate, so the stream never has to transcribe it. A bare naive rescan as the ONLY arm is not streamable today |
| C9 `else if`->`if` | **PASS** | 4/4 build; xlang rust=0 java=14; Stream verify 15596 legs bit-exact; fuzz-064 0 real failures, 76 benign signed-zero |

The `Stream verify: ... 0 without a stream` line is the important one for FINDING 2:
under the fast-path-skip factoring MAX/MIDPOINT keep their `TA_S_*` API **and** stay
bit-exact with the batch, which is exactly what the maintainer required.

## Measured results — 12 layouts, median [min..max] over layouts

Full tables: `RESULTS-passA.txt` (pass A, all 12 layouts, 7 shapes x 4 periods),
`harness/resB-landscape.csv` (all 11 shapes x 6 periods, L0 only),
`harness/resC-shortrange.csv` (n=256/1024/8192, 3 layouts).

Ratio vs C0 on the **acceptance shapes** (randwalk / randwalk-hi / gbm) — the
maintainer's gate. Lower is faster:

| func | period | C0 ns/bar | C1 deque | C2 VH-batched | C3 VH-per-sample | C4 two-stack | C5 `>=` | C8 naive | C9 `if` |
|---|---|---|---|---|---|---|---|---|---|
| MAX | 14 | 4.05 | 2.46-2.49 | **0.28** | 0.87 | 0.93 | 1.09-1.10 | 0.86 | 0.92 |
| MAX | 30 | 4.29-4.39 | 2.31-2.40 | **0.26-0.27** | 0.66-0.68 | 0.72-0.74 | 1.19 | 2.20-2.25 | 0.96 |
| MAX | 200 | 6.97-7.65 | 1.34-1.50 | **0.23-0.26** | 0.32-0.35 | 0.36-0.39 | 1.30 | 16.6-18.2 | 1.02 |
| MAX | 1000 | 9.25-14.34 | 0.72-1.14 | **0.13-0.21** | 0.16-0.24 | 0.18-0.28 | 1.33-1.34 | 56-88 | 1.02-1.03 |
| MIDPOINT | 14 | 7.42 | 2.20-2.23 | **0.27** | 0.57 | 0.93 | 1.12 | 0.71 | 0.88 |
| MIDPOINT | 30 | 8.32-8.37 | 2.01-2.06 | **0.26** | 0.44-0.45 | 0.72 | 1.19 | 1.50-1.52 | 0.94 |
| MIDPOINT | 200 | 15.84-16.17 | 1.07-1.12 | **0.21-0.22** | 0.22-0.23 | 0.32-0.33 | 1.31-1.32 | 9.4-9.6 | 1.00 |
| MIDPOINT | 1000 | 30.43-34.97 | 0.50-0.59 | 0.11-0.12 | **0.10-0.12** | 0.14-0.16 | 1.35 | 24-27 | 1.00 |

Trend shapes (`trend-chop-1p`/`-4p`), where the change would supposedly be justified:
C2 lands at 0.25-0.30 for MAX and 0.16-0.26 for MIDPOINT — i.e. it wins by a similar
factor there as on the acceptance shapes, so it does not need the trend case to clear
the bar.

Tail (`constant`): C2 0.00-0.16, C9 0.00-0.19, C6 0.00-0.09. C0's own cost there is
7.05 -> 849 ns/bar for MAX as the period goes 14 -> 1000 (13.6 -> 1700 for MIDPOINT,
i.e. the 2*(period-1) the maintainer measured).

**Layout sensitivity is small here.** Almost every ratio's [min..max] over 12 layouts
is within +-3%; the widest single outlier is MIDPOINT/C3/randwalk-hi/p30 at
0.44 [0.44-0.67], where one build was 50% worse. That one case is exactly why the
sweep exists, and it does not touch any conclusion.

Reads:
- **C2 (block-batched Van Herk) wins everywhere, on every shape and every period**,
  3.6x-7.7x on MAX and 3.7x-9x on MIDPOINT against the acceptance shapes. Its
  advantage GROWS with period, and unlike C0 its cost is nearly input-independent.
- **The batched and per-sample forms are genuinely different code** and the ordering
  even INVERTS: C2 beats C3 by 3.1x at MAX/p14, and C3 beats C2 at MIDPOINT/p1000
  (0.10 vs 0.11). The maintainer's warning was right in both directions.
- **The deque loses at the default periods** — 2.2-2.5x worse at 14, 2.0-2.4x at 30,
  still worse at 200, and only ahead from roughly period 500 up. C7 (pow-2 mask
  instead of wrap branches) recovers ~8-10%, not enough. So the unverified claim he
  asked us to re-measure looks TRUE for the periods that matter, and false for very
  long windows.
- **C9 is free**: 0.88-1.03 on gate and trend at every period, and it removes the
  flat worst case (0.19x at p14 down to 0.004x at p1000).
- **C5 is the trap**: 1.09-1.35x worse everywhere except flat, for the `maxsd`
  reason in FINDING 3. Anyone reading #152 and reaching for `>=` will land here.

## The one place C2 regresses on the corpus: `mono-up` (and short ranges)

From the full landscape (`resB-landscape.csv`, all 11 shapes), MAX:

| period | C0 on mono-up | C1 | C2 | C3 | C4 | C6 | C8 | C9 |
|---|---|---|---|---|---|---|---|---|
| 200 | **1.10 ns/bar** | 3.96 | **1.64** | 1.92 | 2.17 | 0.59 | 115 | 1.20 |
| 4096 | **1.13 ns/bar** | 4.48 | **1.79** | 1.97 | 2.25 | 0.66 | 3060 | 1.20 |

`mono-up` is MAX's perfect case: the maximum is the newest bar on every bar, the
rescan never fires, and C0 costs one comparison. Nothing can beat that, so every
O(1)-amortized candidate pays its bookkeeping for nothing. This is the honest
complete statement of C2's regression surface on this corpus: **`mono-up` and short
ranges, and nothing else** — no gate shape, no trend shape, no period.

It is also exactly the shape the maintainer told us not to optimise for, in the
opposite direction — so it should be reported as the tail it is, not hidden and not
led with.

Period 4096 was included specifically to look for a top-end crossover (C2's scratch
is 2 x 4096 doubles = 64KB, past L1). There is none on this machine: C2 is 0.12-0.17
on the gate shapes and 0.07-0.13 on trend at p4096. So a `<= <literal>` hybrid
threshold is not needed at the top of the range here — which matters, because that
predicate is the shape the generator sanctions (FINDING 2), and if a crossover HAD
existed it would have fitted the pattern perfectly.

## Guarded vs unguarded — measured from the BUILT library, not argued

`harness/guarded.c` links `libta-lib.a` and times `TA_MAX` / `TA_MAX_Unguarded`
directly, per repo convention. ns/bar, shape=randwalk:

| func | period | C0 g | C0 u | C2 g | C2 u | C9 g | C9 u |
|---|---|---|---|---|---|---|---|
| MAX | 14 | 3.70 | 4.04 | 1.19 | 1.19 | 3.61 | 3.80 |
| MAX | 30 | 4.09 | 4.42 | 1.28 | 1.28 | 4.16 | 4.30 |
| MAX | 200 | 7.36 | 7.65 | 1.80 | 1.80 | 7.68 | 7.85 |
| MAX | 1000 | 14.06 | 14.35 | 1.95 | 1.93 | 14.48 | 14.65 |
| MIDPOINT | 14 | 7.10 | 7.67 | 2.13 | 2.12 | 6.30 | 6.57 |
| MIDPOINT | 1000 | 34.23 | 34.61 | 3.76 | 3.76 | 34.69 | 35.06 |

Two things fall out:
1. The guarded entry point is consistently 1-8% **faster** than the unguarded one
   here. It is a separate full copy of the body, so this is placement, not
   validation cost — it sits inside the layout-sensitivity band and is not a real
   effect to reason from.
2. **The ratios are the same in both variants** (C2: 0.32/0.30 at p14, 0.24/0.24 at
   p200, 0.14/0.13 at p1000), so nothing in the conclusions depends on which one is
   quoted.

This also **cross-validates the out-of-tree harness through a different build path**:
C0 MAX p14 3.70-4.04 (in-tree cmake library) vs 4.05 (harness TU); MIDPOINT p1000
34.2-34.6 vs 34.97; C2 MAX p14 1.19 vs 1.13. Within a few percent, two independent
compilations and two independent timing loops.

## Short ranges — where a per-call malloc actually costs something

`resC-shortrange.csv`, 3 layouts, randwalk, ratio vs C0:

| func | period | n=256 | n=1024 | n=8192 |
|---|---|---|---|---|
| MAX | 14 | C2 0.87 / C3 1.07 / C4 1.29 | C2 0.66 | C2 0.32 |
| MAX | 200 | **C2 1.41 / C3 1.18 / C4 2.29** | C2 0.65 | C2 0.26 |
| MIDPOINT | 200 | **C2 1.53 / C3 1.23 / C4 2.13** | C2 0.19 | C2 0.26 |

So the O(period)-scratch candidates **lose when the requested range is comparable to
the period** (n=256 at period 200 is 57 output bars): the `malloc` and the first
block's setup have nothing to amortise against. In absolute terms it is ~90 ns for
the whole call, so it may simply be acceptable — but it is a real regression region
and it is the natural place for a threshold if one is wanted. C9, having no scratch,
is 1.04-1.24x at short ranges (the first window always rescans, so its one extra
comparison is proportionally more visible) and 0.88-1.03x at n=100000.

## Superseded preliminary numbers (L0 only, kept for the record)

MAX, ns/bar, ratio vs C0:

| shape | period | C0 | C1 | C2 | C3 | C4 | C5 | C6 | C7 |
|---|---|---|---|---|---|---|---|---|---|
| randwalk | 30 | 4.40 | 2.33x | **0.26x** | 0.66x | 0.73x | 1.19x | 0.91x | 2.10x |
| randwalk | 200 | 7.67 | 1.35x | **0.23x** | 0.32x | 0.35x | 1.30x | 0.96x | 1.19x |
| trend-chop-2p | 30 | 5.52 | 1.76x | **0.21x** | 0.54x | 0.58x | 1.19x | 0.91x | 1.59x |
| trend-chop-2p | 200 | 7.28 | 1.65x | **0.27x** | 0.34x | 0.40x | 1.16x | 0.88x | 1.42x |
| constant | 30 | 22.02 | 0.23x | 0.06x | 0.08x | 0.13x | 0.05x | **0.03x** | 0.19x |
| constant | 200 | 163.36 | 0.03x | 0.01x | 0.01x | 0.01x | 0.01x | **0.004x** | 0.02x |

Reads so far:
- **C2 (block-batched Van Herk) is ~1.15-1.94 ns/bar essentially independent of
  shape and period**, i.e. 3.5-4.5x faster than C0 on the ACCEPTANCE shapes, not
  just the trend ones. The batched and per-sample forms differ by ~3x — the
  maintainer's "they are not the same code and may not measure the same" is right.
- **The deque LOSES on random-walk input** (2.3x at p30), which independently
  reproduces the claim he asked us to treat as unverified. C7 (pow-2 mask instead
  of wrap branches) only recovers ~10% — still 2.1x. So the claim looks true on
  this toolchain, for a reason: per-bar deque bookkeeping beats a rescan that
  usually does not fire.
- **C6 is a free win**: faster than C0 everywhere measured AND removes the flat
  worst case, with no codegen change and no scratch memory.

## FINDING 5 — C6 is eliminated by the codegen gate, and C9 replaces it

C6 (reverse rescan) is bit-identical and produced the LEANEST machine code of any
scalar variant (116 insns vs C0's 150, `maxsd` retained), but `generate` refuses it
at stage 2:

```
error: MAX: streamable by analysis but the transition cannot be built:
       MAX: index variable `i` leaks into the transition body
```

The T4 transition builder drops the rescan's index bookkeeping and then checks that
no dropped variable survives (streaming.rs:4777-4786); the backwards `while( i >
trailingIdx ) { i--; ... }` shape puts `i` on the wrong side of that split. Recorded
as a death, not worked around: the fix would be generator work, and a cheaper route
exists.

**C9** gets the same tie-break with a two-token change to the upstream body: turn
the incoming-bar arm from `else if` into `if`, so it also runs on a rescan bar.

```c
   if( highestIdx < trailingIdx ) { ...forward rescan, strict `>` unchanged... }
   if( tmp >= highest )        /* was: else if */
   { highestIdx = today; highest = tmp; }
```

On a tie the cached index then moves to `today` instead of parking on `trailingIdx`
— exactly the mechanism the maintainer described in #152 — and after the first
rescan a flat stretch stops rescanning altogether. `tmp` is correct in both paths:
the forward rescan's last iteration is `i == today`, so it leaves
`tmp == in[today]`, the same value the top-of-loop read put there.

C9 compiles to the SAME instruction count and the same 5 `maxsd` as C0, so it does
not pay C5's penalty. Measured (L0, contended, indicative): randwalk p30 0.95x,
p200 1.01x, trend-chop-2p p30 0.95x, constant p30 **0.073x**, constant p200
**0.008x** (121x faster).

## FINDING 6 (CORRECTED by the full sweep) — the naive rescan wins at period 14, for
## BOTH shapes, and MIN/MAX/MINMAX/MIDPOINT do not have that fast path today

My first read of C8 came from a single contended spot-check at period 30 and said
"the naive form is not a competitive floor for MAX". **That was wrong.** The
12-layout sweep, gate shapes, median [min..max]:

| | period 14 | period 30 | period 200 |
|---|---|---|---|
| MAX, C8 vs C0 | **0.86 [0.85-0.89]** | 2.20 [2.15-2.23] | 16.6 [16.4-16.8] |
| MIDPOINT, C8 vs C0 | **0.71 [0.70-0.74]** | 1.50 [1.48-1.52] | 9.4 [9.2-9.4] |

So the crossover sits between 14 and 30 — which independently reproduces the ~19-20
crossover `midprice.c` already documents for its own `<= 20` arm, and extends it to
the SINGLE-extremum shape, where I had argued it should not apply (only one
comparison chain to interleave). It does apply.

Consequence worth separating from the algorithm question: **MIDPOINT's default period
is 14 and it has no naive fast path**, while MIDPRICE (same shape) has one. Giving
MIDPOINT/MIN/MAX/MINMAX the `<= 20` arm MIDPRICE already ships would be ~15-30% at
the default period with zero new algorithm and zero new memory. (It does not help
MIN/MAX/MINMAX at THEIR default of 30, where C8 is 2.2x worse.) Recorded as a
finding; C8 as written is still codegen-eliminated (see the ledger) because a bare
naive rescan with no fast-path predicate is not streamable.

## FINDING 8 — the value-only half is NOT free of output-compat questions either

This is the most important correctness result of Phase 1 and it was nearly missed.

The maintainer's split is "value-only = bit-identical, can move first" vs
"index-returning = an output-compat decision". That split holds for magnitudes but
**not for the sign of zero**. `+0.0` and `-0.0` compare EQUAL, so which of two
numerically-equal window members supplies the emitted value decides the sign of a
zero output — and every candidate changes that choice somewhere.

Found by ta-lib's own frozen-oracle gate, not by our harness:

```
control C0:  BENIGN TA_MIDPOINT: 28 signed-zero case(s) (numerically equal, +0.0 vs -0.0)
C9:          BENIGN TA_MAX:      41 signed-zero case(s)
             BENIGN TA_MIDPOINT: 35 signed-zero case(s)
```

Our out-of-tree bitwise gate had a **coverage hole**: every `bench_corpus.h` shape
holds `low > 0`, so no corpus series contains a zero at all. Closed by adding a
deterministic +0.0/-0.0/tie mix to `--verify`. With it:

| | max | midpoint |
|---|---|---|
| C1 deque | 37 | 37 |
| C2 VH batched | 37 | 37 |
| C3 VH per-sample | 37 | 37 |
| C4 two-stack | 33 | 37 |
| C5 `>=` | 37 | 37 |
| C6 reverse rescan | 37 | 37 |
| C7 pow-2 deque | 37 | 37 |
| C8 naive rescan | **0** | 37 |
| C9 `else if`->`if` | 37 | 37 |

and **every single difference is the sign of zero** — 2 distinct (ref,got) pairs
across 625 reported mismatches, `ref=-0 got=0` and `ref=0 got=-0`, no difference of
magnitude anywhere. The independent naive reference differs from the shipped C0 in
the same way (37 cases), i.e. the *definition* does not pin this either.

How to read it:
- ta-lib has already decided this class is benign: `--fuzz-064` has an explicit
  `BENIGN ... signed-zero` bucket and PASSES on it, and the SHIPPED MIDPOINT
  already carries 28 such cases against v0.6.4 today. So there is precedent, in
  this exact function, in the current release.
- But the maintainer wrote "bit-identical, not close", and this is not
  bit-identical. It has to be his call, stated plainly, not buried.
- Practical exposure is narrow but real: prices are positive, so ±0.0 does not
  arise from OHLC data — however MIN/MAX/MIDPOINT take a general `inReal`, and a
  caller can feed anything.
- The only candidate that is bit-identical INCLUDING signed zero is C8 (naive
  rescan) for MAX — and C8 is the slowest candidate by a wide margin, so this is a
  genuine trade rather than a free choice.

## FINDING 7 — two C-to-Rust translator bugs, both surfaced by NEW code shapes

Neither is in the upstream corpus today, so neither is a live bug — but both are
traps for anyone writing a new `<name>.c`, and both silently produce non-compiling
or wrong Rust rather than a clear diagnostic.

1. **`if( k < 0 ) k += period;`** emits `k += ((optInTimePeriod) as f64)` for an
   `i32` k (does not compile), and `k = (j - 1) as i32` underflows a
   usize-inferred `j`. Avoided by doing all wrap arithmetic with `+ cap - 1` and
   `>= cap`, never letting an index go negative.
2. **`blockStart += optInTimePeriod;`** emits `blockStart += optInTimePeriod` with
   `blockStart: usize` and `optInTimePeriod: i32` — the `as usize` cast that the
   translator DOES insert for the explicit form is dropped on compound assignment:

   ```
   error[E0308]: mismatched types --> library/src/ta_func/max.rs:225:35
   225 |    blockStart += optInTimePeriod;
       |                  ^^^^^^^^^^^^^^^ expected `usize`, found `i32`
   ```

   This eliminated C2 (the fastest candidate) on the first gate pass. Worked around
   in the input C by writing `blockStart = blockStart + optInTimePeriod;`; every
   candidate now avoids `X op= <non-literal>` and uses only `++`/`--` on counters.
   Recorded as a finding, not hidden: the workaround is in OUR code, the bug is in
   the generator.

## Environment gaps on this machine (all fixed locally, all worth reporting)

| gap | effect | fix used |
|---|---|---|
| system JDK is 11, backend needs `--release 17` | Java server + library would not build -> xlang gate would silently cover 2 of 4 backends | local Temurin 17 in `talib-147-algo/jdk/`, `JAVA_HOME` set per gate process |
| system .NET SDK 7.0.200, project targets `net10.0` | .NET server would not build (NETSDK1045) | local .NET 10.0.302 in `talib-147-algo/dotnet/sdk/` |
| `ta_064_serve` not built | `--fuzz-064` cannot run at all (`cannot start ta_064_serve`) | fixed: fetched the `v0.6.4` tag from upstream and built it via `scripts/build_064_serve.py`; gate.sh now builds it once and reuses it |
| pristine tree: 14 C-vs-Java `XLANG TOL MISMATCH` on `TA_HT_TRENDMODE` | `--xlang-hash` FAILs on the CONTROL | not a candidate fault; this is why every candidate is judged as a DELTA vs the C0 control, and why the pass criterion is "no mismatch naming a rolling-extrema function and nothing new vs control" |

`harness/verdict.py` implements exactly that delta rule.

With the JDK and .NET gaps closed, the **C0 control passes all four backends**:

```
control C0: builds = {'C': 'ok', 'Rust': 'ok', 'Java': 'ok', '.NET': 'ok'}
control C0: xlang non-family mismatches = ['HT_TRENDMODE'] | family = none
            | default all-succeeded = True
[C0] --fuzz-064: comparisons: 171230  matches: 148119  benign(signed-zero): 30
     cci-tolerated: 8434  fma-tolerated: 14647  failures: 0
[C0] default: Variant parity: 168 functions x 31392 vectors, 31392 four-way
     comparisons (26832 with output), bit-identical
```

so the only pre-existing anomaly a candidate has to be scored against is the 14
`TA_HT_TRENDMODE` C-vs-Java tolerance mismatches, which are outside this family.

## FINDING 4 (a Phase 2 risk, found while checking the family)

STOCH/STOCHF already `malloc` and are still `stream`-flagged, which looked at first
like a counter-example to FINDING 1. It is not: their buffer is classified by
`analyze_composed` as a **materialised intermediate series** (streaming.rs:1364,
"STOCH's `tempBuffer` standing in as the output the loop writes"), not as loop
state. So it is no precedent for period-sized deque state.

It does raise a real risk though: STOCH/STOCHF's body is
`prologue + producer loop + ma() sub-call tail`, and `analyze_fastpath_skip` wants
`prologue + if/else of steady loops + epilogue`. Whether the fast-path-skip
factoring composes with `ComposedPlan` is **unverified**. If it does not, the
family splits: MIN/MAX/MINMAX/MIDPOINT/MIDPRICE/WILLR could take a new algorithm
this way and STOCH/STOCHF could not.

## Status

- [x] clone, baseline build (rc=0)
- [x] codegen wall found + reproduced; fast-path-skip escape hatch verified
- [x] 8 candidates for MAX + MIDPOINT, out-of-tree bitwise gate PASS, mutation-validated
- [x] harness with 12-layout sweep, METHOD.md
- [x] C5 `maxsd` regression root-caused -> C6
- [ ] in-tree gates (generate / build / xlang-hash / stream_verify / aliasing)
- [ ] final uncontended 12-layout sweep + aggregate table
- [ ] extend the surviving candidate(s) to MIN/MINMAX/MIDPRICE/WILLR/STOCH/STOCHF
- [ ] guarded vs unguarded split from the in-tree run
- [ ] write the #147 comment draft
