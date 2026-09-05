feat(ha): Heikin-Ashi Candles, and the harness cap that blocked a 4th output (#373)

Implements #373. HA is the first shipped function with four outputs, so it lands together with the hand-written-harness widening the issue identifies as the substantial part of the work.

## The cap

`CODEGEN_MAX_OUTPUTS` goes 3 → 4. The issue counts 39 sites; all 39 turned out to be *symbolic* references to the define, so the clamped comparison loops and the sized buffer arrays scale with it and needed no per-site edit. What did need editing is the other three places the number 3 is written down by hand, none of which the startup guard from #352 covers:

- `TestBuffer` gains `out3`, and `TA_NB_OUT` goes 3 → 4, so the classic per-function harness can hold a fourth output plane (`buf` grows one plane per global buffer — 5 × 1 × 480 doubles, ~19 KB).
- `test_variants.c`'s `V_MAX_OUTPUT` 3 → 4. Without it the variant gate refuses HA at startup: `4 inputs / 4 outputs / 0 optIn exceeds the gate's V_MAX_* bounds`.
- `ta_test_legacy.c`'s `nbOutput > 3` guard is deliberately **not** raised. It is per-frozen-case, and a post-cutover function has no v0.6.4 case, so it can never see HA. Raising it would widen a gate that has nothing to gate.

`internal_error_ids.yaml`, the `FuncUnstId` enum and the generated side all took HA without hand-holding; the enum row is appended at 25, never inserted.

## The function

```
HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
HA_open[0]  = (open[0] + close[0]) / 2
HA_open[i]  = (HA_open[i-1] + HA_close[i-1]) / 2
HA_high[i]  = max(high[i], HA_open[i], HA_close[i])
HA_low[i]   = min(low[i],  HA_open[i], HA_close[i])
```

Four outputs as ruled. Every operation is one addition or one division by a power of two, so the transform is exact whenever its inputs are, and the goldens are frozen at **tolerance zero** rather than a relative band.

The seed is carried as a *virtual previous candle* — the pair initialised to the anchor bar's own open and close — rather than as a special first iteration. The uniform recursion then produces `(open+close)/2` at that bar, one loop body serves the anchor and every bar after it, and the same pair is the streaming tier's initial state. It is also what keeps the steady loop free of `endIdx` references, which stage-1 streamability requires.

**Keep the summation order.** `TA_AVGPRICE` sums the same four terms as `(H+L+C+O)/4`. Floating-point addition is not associative, and on the 252-bar regtest history the two orders differ by one ulp on **17 of 252 bars**. So `HA_close` is not a composed `AVGPRICE` call, and leg 3 of the test asserts they disagree — otherwise the rule is unenforced prose and a future "simplification" would pass every other leg.

## One deviation from the issue, and its cost

The issue lists `flags: [overlap, unstable_period, stream]`. This ships `path_dependent` as well. They answer different questions:

- `unstable_period` is the ABI knob the issue specifies as the sole lookback.
- `path_dependent` is what is true at its **default of 0**: the open recursion re-seeds at the anchor, so `HA(3, 7, ...)` starts its open at `(open[3]+close[3])/2` rather than warming up from bar 0. A sub-range call therefore legitimately disagrees with a full-history one, and without the flag the range-stability leg value-compares them and fails.

The cost is real and worth stating: `path_dependent` maps to `TA_DO_NOT_COMPARE` → `TA_STABLE_SKIP`, so the generic range sweep value-compares **nothing** for HA. That is why `test_ha.c` carries the value coverage itself, and why the cross-language sweep reports HA as `0 value-compared, 1 path-dependent: coherency only` — its values are gated by `--xlang-hash` instead. If you would rather HA declared only `unstable_period` and left the range sweep to fail, say so and I will restructure rather than special-case it.

The knob does buy start-independence back, and the amount is measured, not asserted: on the regtest history a call anchored at bar 100 needs **56** warm-up bars to become bit-identical to the full-history call over its whole emitted range; **48** still leaves two bars differing, and **32** leaves fifteen. The test pins both ends — exact agreement at 64, disagreement at 0.

## Verification

Primary proof is an external capture. `pandas-ta-classic 0.6.52`'s `ha()` over the 252-bar regtest history, frozen as `ha_gold_open` / `ha_gold_close` and compared with `memcmp`. An independent re-derivation agrees with the oracle on **all 1008 values, bitwise** (all four columns), which is what lets the two clamped outputs ride on spot pins plus a recomputed clamp identity instead of two more full tables.

Seven legs, and **every one has a control that I broke and watched fail**:

| # | Leg | Control | Result |
|---|-----|---------|--------|
| 1 | External golden, 252 bars, tolerance 0 | swap the sum to AVGPRICE's order | red: `close bar 1: 93.164999999999992 != oracle 93.165000000000006` |
| 1 | " | seed the open recursion from the high | red: `open bar 0: 92.375 != oracle 92` |
| 2 | Clamp identity, all 252 bars | delete both `haOpen` clamp arms | red: `clamped pin bar 2: (96.375,94.25) != oracle (96.375,92.58250000000001)` |
| 3 | `HA_close` ≠ `AVGPRICE` | (17/252 bars differ — measured, non-vacuous) | — |
| 4 | `path_dependent` earned | drop the flag from the YAML | red: `TA_FUNC_FLG_PATH_DEP is not published` |
| 5 | Unstable-period convergence | (56/48/32-bar thresholds measured) | — |
| 6 | Edges + four-way in-place aliasing | — | — |
| 7 | Malformed bars | delete both `haClose` clamp arms | red: `malformed bar 1: (100,100,99,75) != (100,100,75,75)` |

**Leg 7 exists because of a control that stayed green.** Deleting both `HA_close` clamp arms left legs 1–6 passing on the whole 252-bar history. On a well-formed bar (`low ≤ open,close ≤ high`) `HA_close` is an average of the four and is therefore already inside `[low, high]`, so its two clamp arms are unreachable there — half the clamp was untested. Leg 7 adds four bars whose high sits below their own body, arranged so each of the four arms decides one output: high from `HA_open` (bar 1) and from `HA_close` (bar 2), low from `HA_close` (bar 1) and from `HA_open` (bar 3). All 16 expected values are exact in binary and the oracle reproduces them bit-identically.

Gates run locally, all green:

- `scripts/build.py regen-check` — regenerating changes nothing
- `scripts/build.py check-source-lists` — CMake and autotools agree (72 files)
- full C reference suite, including the abstract, variant and streaming gates
- the same suite under **ASan/UBSan** (`--sanitize`, `halt_on_error=1`)
- `scripts/regtest.py --function=HA` (C, Rust, Java) against the frozen `ta_ref_serve` oracle
- `ta_regtest --xlang-hash --function=HA` — **Rust and Java bit-identical to the in-process C library on 506 cases each, zero mismatches**, including at unstable period 3
- `cargo test` in the generator (all suites), `cargo clippy --all-targets -D warnings` on both the generator and the generated crate, `cargo doc --no-deps`, and the crate's 598 doctests

**What I did not run:** the **C# backend** — there is no .NET SDK on the machine I built this on, so `Core_HA.cs` is generated and committed but never compiled or value-checked. The PR gate's "Generated C# compiles" job and the nightly's C# parity job are the first things to watch. I also did not run `scripts/synth_gate.py` (nightly), nor `--fuzz-064` (HA is post-cutover, so it has no v0.6.4 arm).

Two generator-side hand-maintained lists needed HA appended, and both failed loudly first, which is the point of them: `tests/abstract_rows_suite.rs`'s unstable-id table and `tests/stability_suite.rs`'s self-declaring count (21 → 22).

## No performance claim

`scripts/regtest.py` printed a `HA … 40826.67x` row in the direct bench. That is vacuous — the reference arm has no HA to time, so the "6" it reports is timer quantisation against nothing. There is no performance claim in this PR.
