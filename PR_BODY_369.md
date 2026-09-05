feat(percentrank): Percent Rank, the strict share of the window below the current bar (#369)

Closes #369.

Implements #369 as ruled: `TA_PERCENTRANK(inReal, n)` — of the `n` bars **before** the
current one, the percentage whose value is **strictly less** than it. Group
`Statistic Functions`, one `inReal` in, one `outReal` out, default period 100,
`flags: [stream]`, `TA_PERCENTRANK_Lookback() == optInTimePeriod`.

The lookback is `optInTimePeriod`, not `optInTimePeriod - 1`: the current bar is
excluded from its own window, so the first rankable bar is bar `n`.

## The ruling

`<`, strict, per the ruling on #369. `percentrank.md` states the divergence: Pine's
`ta.percentrank` ranks with `<=` and is a different function. The visible case is a flat
series — 0 here, 100 there — and the test asserts exactly that, categorically, rather
than hoping a price corpus carries enough ties to notice.

## Two rules that fail silently, and how each is pinned

**Operation order.** `(count/period)*100.0` and `100.0*count/period` are different
doubles. MEASURED here on the shipped 252-bar `TA_SREF_close_daily_ref_0_PRIV` closes:
they disagree on **90 of the 249** period-3 output bars (the same count #369's card
reports for ta4j), and on 7 of the 232 at period 20. The published ta4j values are the
divide-first ones — bar 58 at period 20 is `55.00000000000001`, where the other spelling
gives a flat `55.0`. All four backends emit `(double)count / (double)period * 100.0`,
which is that order in each language's precedence.

Seven of the golden rows separate the two spellings, and the whole test file compares
with `!=` rather than `checkExpectedValue`'s 0.01 window — a tolerance of any size hides
this difference completely.

**In-place aliasing (#130).** Every window read precedes the store. `startIdx` is clamped
to at least the period, so the write index never runs ahead of `today - period`, the
oldest slot the bar reads — and no later bar reads that slot again. Safe by exactly one
slot, which is why the alias leg sweeps every period rather than one.

## Streaming

`stream-census` derives **T3, state=0, lags=0**, and `batch == stream` verifies bitwise in
all four backends (driven by hand at periods 100, 3 and 2 on a 240-bar random walk):
`ok=1`, `step_ok=1`, `peek_ok=1`, `peek_rep_ok=1`, `peek_rejects=0`, `fill_ok=1`,
`ufill_ok=1`, `range_ok=1` with every declared range site fired, and C's `state_ok=1`
across 4 state legs.

One authoring note worth keeping: the count loop is a **descending** counter
(`for(i = period; i >= 1; i--)`). The streaming analyzer bounds that shape and the
ascending `for(i = 1; i <= period; i++)` one it does not — `collect_window_bounds`
recognises `v = 0; v < E` and `v = E; v >= B`, nothing else — so the ascending spelling
makes `generate` refuse the declared `stream` flag with "unbounded offset var". The two
count identically; only the analyzer can tell them apart.

## Verification

Every leg below is **bit-exact**; there is no tolerance anywhere in `test_percentrank.c`.
The function emits one of `period+1` exactly-determined doubles, so a tolerance would only
ever hide a wrong count or a wrong operation order.

1. **Golden values.** Periods 20 and 100 are ta4j 0.22.6's values for this series,
   transcribed from #369's own capture (its sample table, plus the bar-58 value from the
   operation-order measurement). **They were not re-captured in this tree — there is no
   ta4j arm here.** What is independent is that they were reproduced from the shipped
   corpus before a line of C was written, and that the same reproduction gives #369's
   "90 of 249" figure exactly. Period 3 is this file's own full-precision table, covering
   all four counts (0, 1, 2, 3) at the first, middle and last bar each occurs on; it
   exists because the ta4j rows are mostly exact ratios and only one of them separates the
   two operation orders.
2. **Tie rule** — flat series (0.0 on every bar), strictly rising (100.0), strictly
   falling (0.0), and an alternating `+0.0`/`-0.0` window, where `-0.0 < 0.0` is false and
   the two zeros must count as equal.
3. **Degenerate ranges** — fewer bars than the lookback (`TA_SUCCESS`, both out-params
   zeroed), exactly one output bar, the minimum period, an anchored call past the
   lookback, and period 1 rejected as `TA_BAD_PARAM`.
4. **In-place aliasing**, bitwise, every period 2..60 over the 252-bar corpus.
5. **Range sweep** at `TA_STABLE_EXACT` — PERCENTRANK joins the `exact[]` list in
   `test_codegen.c`: each bar is recomputed from its own window, so no `startIdx`/`endIdx`
   pair may move a value by a bit.
6. **Cross-language.** `server_verify` on legs 1–3 (all four servers, bitwise), and
   `--xlang-hash --function=PERCENTRANK,SMA`: **PASS**, 3886 cases per server against the
   in-process C library, 0 mismatches, Rust/Java/C# all bit-identical.

Coverage counters are literal, not floors: 19 golden, 140 tie, 5 edge, 13039 alias
comparisons, asserted at the end of the group.

### Sabotage controls — each mutation regenerated, confirmed present in the generated C, and watched fail

| mutation | what went red |
|---|---|
| `100.0 * count / period` (multiply first) | golden leg, period 3 bar 7: `33.333333333333336` vs `33.333333333333329` |
| `<=` instead of `<` | golden leg, period 100 bar 251: `37` vs `35` |
| `<=` with the golden leg bypassed | the tie leg itself: `constant series -> 0: out[0]=100, expected 0` |
| `return optInTimePeriod - 1` | the abstract suite's lookback check: `TA_GetLookback() != outBegIdx [99 != 100]` |
| a `outReal[outIdx] = 0.0;` store placed before the count loop | the #130 abstract alias gate: `out0 <- in0.real: output 0 wrong in 55/152 values` |

The last two reddened shared gates before reaching this file's own legs 5 and 6, so those
two legs are evidenced by their comparison counters rather than by an observed red of
their own.

### Gates run

`scripts/build.py regen-check` (the PR gate) · `format` · `check-source-lists` ·
`clippy` (both crates, `-D warnings`) · `cargo test` in the generator (all suites green) ·
`cargo test --doc` and `--lib` on the crate · `cargo doc --no-deps` warning-free ·
the full bare `./ta_regtest` · `scripts/regtest.py --no-perftest --function=PERCENTRANK`
against `ta_ref_serve` (structural legs on all four servers; the expected NO VALUE
COMPARISON banner, since the frozen reference predates the function — same as RMA and
VHF).

Then both whole-corpus gates **unfiltered**, to prove nothing else moved — the
`test_abstract.c` dataset change below touches every function's abstract legs:

- `./ta_regtest --xlang-hash` — **PASS, 184 functions**, 0 mismatches. Rust 304812 cases,
  Java 303521, C# 304704, C's own server 5309; 26888 tolerance-lane calls, everything else
  bitwise.
- `./ta_regtest --codegen` — **all 4 languages passed**, 161 value-compared functions each,
  0 failures, float leg 1165 acknowledged comparisons.

## Costs and things I did not check

- **A HARNESS CHANGE, which the contribute page says needs your explicit approval —
  `test_abstract.c`: the four short datasets grow from 100 to 160 bars.** They must
  outrun the largest lookback any function reaches at its **defaults**, or
  `callWithDefaults` produces nothing and the `outBegIdx == lookback` check compares two
  zeros. PERCENTRANK's default of 100 is the first function to reach it — the next
  largest default lookback in the corpus is 34. This is a strengthening, not a relaxation:
  at 100 bars the check was *inverted* for this function, reddening the correct lookback
  (0 != 100) and passing an off-by-one (99 == 99). The cost is that four datasets × 184
  functions now run on 60% more data in that leg, including over the server pipes under
  `--codegen`. The alternative — making the assertion conditional on `outNbElement > 0` —
  would have weakened a real invariant to fit one function's default, so I did not take
  it. Named constant `ABSTRACT_SHORT_NB`, and the failure message now names the cause.
- **`--xlang-hash`'s array-transport self-check skips PERCENTRANK.** That leg runs one
  default-parameter call at a fixed `LB_TIER_N = 50` bars; a default lookback of 100
  produces nothing, so the function is skipped there (MEASURED: 4 (server, function) pairs
  with and without it in the filter). It asserts a per-server *wire format*, not a
  per-function property, so 183 other functions supply it unfiltered — but it does mean
  a filtered `ta_regtest --xlang-hash --function=PERCENTRANK` aborts as vacuous. Pair it
  with another function. I did not raise `LB_TIER_N`: that would grow the tier-agreement
  legs 3.2× for every function, and the coverage bought is a wire-format property already
  covered. Your call if you would rather have it.
- I did **not** run `scripts/synth_gate.py` or `--fuzz-064` (the latter skips post-0.6.4
  functions by construction), and I did **not** build the autotools/dist path — only the
  CMake one. `Makefile.am` was updated on both source lists and
  `scripts/build.py check-source-lists` agrees.
- The ta4j numbers are transcribed from #369, not re-captured; see leg 1 above.
- The direct bench reports PERCENTRANK at 0.94x `ta_bench_cg`/`libta-lib.a`, which is the
  usual LTO-vs-separate-TU build difference, not an algorithm claim. No performance claim
  is made in this PR.
