feat: RMA — Wilder's Smoothed Moving Average, the ATR/RSI/ADX smoother on its own (#348)

Closes #348.

An EMA whose smoothing factor is `1/n` instead of `2/(n+1)`, SMA-seeded over the first
`n` bars: Wilder's 1978 smoothing, already embedded in `TA_ATR`, `TA_RSI`, `TA_ADX` and
`TA_PLUS_DM`, now available on its own. Overlap Studies, one `line` output, default 30,
`flags: [overlap, unstable_period, stream, period1_identity]`.

`rma.c` is written standalone rather than delegating to `ema_private`, as #348 asks:
delegation would inherit `ema.c`'s retired `TA_COMPATIBILITY_METASTOCK` arm and its
extra-`k` plumbing. It is spelled letter for letter as `atr.c` spells the same smoothing
— `wAlpha` derived FROM `wBeta`, the seed summed from `0.0` in input order then divided
by the period, the step one statement — because that is what makes the differential leg
below bitwise.

The generator needed no change. `stream-census` derives T2, state 3, lags 0.

## One deviation from the issue's YAML

#348 writes `flags: [overlap, stream, period1_identity]` while also giving RMA its own
`TA_FUNC_UNST_RMA`. Those two cannot both be right: `unstable_period` is the flag that
*declares* ownership of an id (`generator/src/stability.rs` derives the intrinsic set
from it, and `abstract_rows` turns it into `TA_FUNC_FLG_UNST_PER`). Shipped with the
flag, matching `ATR` and `EMA`. Without it, `abstract_rows_unstable_period_set_*` fails
on the flag-vs-id disagreement.

## Also in this PR — #348's "must land in the same PR" list

- `TA_FUNC_UNST_RMA` appended to `enums.yaml` at **24** (`ALL` stays pinned at 65535),
  with the matching rows in `test_internals.c`'s append-only pin table, the Rust
  `FuncUnstId` (`COUNT` 24 → 25) and `test_codegen.c`'s `UNSTABLE_MAP`.
- `"rma"` inserted in `fma.rs::FUSING_INVENTORY`, which is an exact-set gate.
- The "swapping them reddens nothing" comments in `atr.c` and `natr.c` rewritten. They
  became false the moment the differential leg landed.
- Two count-carrying inventories updated: `stability_suite`'s self-declaring count
  (20 → 21) and `abstract_rows_suite`'s unstable-period table. The latter's test name
  had the count baked in (`..._is_exactly_the_twenty`); renamed to
  `..._is_pinned_by_name_and_ordinal`, since a number in a test name goes stale on the
  next indicator.

Not done, and deliberately: `TA_MAType_RMA` stays a separate issue, as #348 decides.

## Verification — eight legs in `src/tools/ta_regtest/ta_test_func/test_rma.c`

1. **The differential, `memcmp`-exact.** `TA_RMA(TA_TRANGE(h,l,c), n) == TA_ATR(n)`,
   both unstable periods pinned 0, over the 252-bar reference corpus at **every period
   1..60 plus 100 and 200** — 14,282 values. The only gate that sees the coefficient
   spelling, the fusion and the fused operand order.
2. **Exact dyadic vectors**, periods 2 and 4, bitwise, hand-derived from the published
   formula: at those periods every coefficient, seed sum and product is dyadic, so the
   whole vector is computed without a single rounding. Deliberately spelling-blind, which
   is why leg 1 is the spelling gate and this one the formula gate.
3. **Reference-corpus samples** at periods 14 and 30, each compared twice: pinned
   **bitwise** against the shipped double, and checked against an **exact-rational**
   evaluation of the same recursion at rel `1e-15`. Worst deviation over the *full*
   output, not just the samples: 2.9e-16 at n=14, 3.5e-16 at n=30. Also replayed through
   `server_verify`.
4. **Published vectors**, absolute at half a unit of the last printed decimal. Achelis
   *Technical Analysis from A to Z* p.366 (Tulip `tests/atoz.txt:296`, the
   hand/spreadsheet-derived one) and Tulip `tests/untest.txt:483` (regenerated from
   Tulip's own output, so a cross-implementation check rather than an independent one —
   worth having anyway, since Tulip's `wilders.c` reaches the same values through the
   `(x-v)*alpha + v` form). Both reproduce exactly.
5. **Period-1 identity**: bitwise on a strictly positive series, by value on a
   sign-crossing one, plus a pin of the single input where the two differ — a `-0.0`
   comes back `+0.0`, because the step is `fma(0, prev, 1*x)` and `(+0.0) + (-0.0)` is
   `+0.0`. Pre-existing and shared with SMA/TRIMA; unreachable from
   `test_period_boundary.c`, whose three sweep series are strictly positive.
6. **Flat input**: never NaN or Inf (the only divisions are by the period, which is
   `>= 1`), exactly the constant at power-of-two levels, within 1e-15 elsewhere.
7. **In-place aliasing**, `outReal` over `inReal`, periods 1/2/14/30.
8. **Range independence** via `doRangeTestEx` in the CONVERGING class with RMA's own id.

### Two corrections to the issue, both measured

**The four differential periods are not enough.** #348 asks for `n ∈ {3,14,30,100}`.
That list cannot see a seed written `tempReal * (1.0/period)` instead of
`tempReal / period`: the two spellings disagree on ~35% of arbitrary operands, but they
happen to agree on **all eight** seeds those four periods produce over this corpus and
its true-range series, so the mutation reads green. There is exactly one seed value per
period, so only more periods make the arm live — hence the 1..60 sweep, where **period 7**
catches it.

**"All-flat input returns the constant exactly" is not true at every magnitude.** At
`1e-300` and period 30 the seed sum needs 58 mantissa bits, so it rounds and the output
comes back `1.0000000000000007e-300`. That is one ULP of the seed accumulator, not drift
in the recursion, and it is shared with `TA_SMA`. Leg 6 is therefore split: bitwise on
power-of-two levels (where `n * 2^k` is exact and the division by `n` is too), near
elsewhere.

### Controls — each broken, watched go red, then restored and watched go green

| mutation | reddens |
|---|---|
| alpha-first coefficients (`wAlpha = 1.0/period` first) | leg 1, period 3 |
| `fma` operands swapped, patched into the **generated** C | leg 1, period 3 |
| the step unfused (`wAlpha*x + wBeta*prev`, no `fma`) | leg 1, period 3 |
| reciprocal-multiply seed | leg 1, period 7 |
| lookback `n` instead of `n-1` | leg 1's shape check |
| seed anchor `+1` | leg 1's shape check |
| seed sums `period-1` values (leg 1 disabled) | leg 2, period 2 |
| `alpha = 2/(n+1)` (legs 1–2 disabled) | leg 3 |
| `alpha = 2/(n+1)` (legs 1–3 disabled) | leg 4 |
| coefficients not `(1,0)` at period 1 (legs 1–4 disabled) | leg 5 |
| Rust `mul_add` operands swapped in the generated crate | `server_verify` — `SV FAIL [RMA] (pipe 0, rust): BITWISE mismatch vs in-process C` |

The `fma`-operand row is worth a note: **the input tier cannot express it.** The
generator canonicalizes the sum and elects the accumulator as the fused multiplicand, so
writing the two products the other way round in `rma.c` emits byte-identical C. The
comment in `rma.c` says so, and says to patch the generated file to watch that leg bite.

**Legs 6 and 7 have no control**, and the file says so rather than implying they gate
something. Every mutation tried either reddens leg 1 first or leaves them green — the
alpha-first spelling and the reciprocal seed both leave leg 6's bitwise arm green,
because at a power-of-two level every candidate coefficient pair sums to within half an
ULP of 1 and rounds back. For leg 7 it is structural: the aliased and non-aliased calls
run the same indices, so a reordering that would corrupt the aliased case moves the
non-aliased output too and leg 1 catches it there. They are property pins.

## What ran

- `./ta_regtest` — full suite, green (includes `LEGACY,064,FROZEN`, the VARIANT
  `TA_S_` parity gate and `test_period_boundary`'s sweeps).
- `./ta_regtest --xlang-hash --function=RMA --language=c,rust,java` — **0 mismatches**,
  3649 cases per server, 1771 of them at unstable period 3.
- `./ta_regtest --codegen --language=c,rust,java`, unfiltered — all three languages
  passed, which is where the abstract-metadata parity sweep for the new function and its
  new `FuncUnstId` lives. Filtered to `--function=RMA` it prints the "NOT a pass, use
  --xlang-hash" banner instead: RMA is post-cutover, so the `ta_ref_serve` sweep skips it
  by design and `server_verify` inside `test_rma.c` is what compares — the control above
  shows that arm bites.
- Generator `cargo test`, `clippy --all-targets -D warnings` over **both** crates, the
  crate's doctests (550) and unit tests, `cargo doc --no-deps` warning-free.
- `check-source-lists`, `check-cargo-lock`, `check-stream-retcodes`.
- `generate` twice — the second run writes an identical tree (the `regen-check`
  property).

## What did NOT run, and is therefore not claimed

- **The C# backend.** No .NET SDK in the environment this was written in, so the
  generated C# was produced by `generate` and never compiled or executed. The PR gate's
  C# compile step and `--xlang-hash`'s C# arm are the first things to look at.
- **The three live external oracle arms #348 asks for** — pandas-ta `ta.rma`, ta4j
  `MMAIndicator`, trading-signals `WSMA`/`RMA`. None of those runtimes was available.
  Leg 3's exact-rational arm substitutes for their *value* role and is far tighter than a
  1e-12-class external arm, but it shares the coefficient spelling with the
  implementation, so it is **not** an independent reading of the definition the way
  another library would be. The independent readings in this PR are legs 1, 2 and 4.
  Wiring `pandas_serve`'s `ta.rma` arm (#348 estimates ~5 LOC) is the cheapest thing to
  add on a machine with that venv, and #348's note about driving it at *our* alpha rather
  than `1/n` still applies.
- **The optional `ta_wilder_beta`/`ta_wilder_alpha` helper** #348 offers. Not taken: it
  is a refactor of three shipped functions on top of a new one, and #348's own warning
  about `canonicalize_accumulator_add` silently re-fusing a helper-shaped step is reason
  enough to keep the two changes apart.
