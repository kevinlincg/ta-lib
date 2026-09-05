---
title: "Heikin-Ashi Candles (HA)"
description: "Heikin-Ashi Candles: an OHLC-to-OHLC smoothing transform."
---

## Summary

Heikin-Ashi Candles: an OHLC-to-OHLC smoothing transform. Each output bar is a synthetic candle whose close is the raw bar's average price, whose open is the midpoint of the previous synthetic candle, and whose extremes clamp the raw extremes against those two. Consecutive candles share a body edge by construction, which is what suppresses the single-bar noise a raw candle chart shows.

The transform is a *filter*, not an oscillator: it returns prices in the input's own units and is plotted in place of the raw candles.

## Formula

```
HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
HA_open[0]  = (open[0] + close[0]) / 2
HA_open[i]  = (HA_open[i-1] + HA_close[i-1]) / 2
HA_high[i]  = max(high[i], HA_open[i], HA_close[i])
HA_low[i]   = min(low[i],  HA_open[i], HA_close[i])
```

Every operation is an addition or a division by an exact power of two, so the transform is exact whenever its inputs are.

`HA_close` is **not** [`AVGPRICE`](/functions/avgprice.md). The four terms are the same, but the summation order is not, and floating-point addition is not associative: `AVGPRICE` sums `(H+L+C+O)`, this sums `(O+H+L+C)`, and the two differ in the last place on ordinary bars.

## Inputs

- `inOpen` — Open price of each bar
- `inHigh` — High price of each bar
- `inLow` — Low price of each bar
- `inClose` — Close price of each bar

## Outputs

- `outHAOpen` — Heikin-Ashi open, the previous synthetic candle's midpoint
- `outHAHigh` — Heikin-Ashi high, the raw high clamped up by the synthetic body
- `outHALow` — Heikin-Ashi low, the raw low clamped down by the synthetic body
- `outHAClose` — Heikin-Ashi close, the raw bar's average price

## Notes

**The open re-seeds at the anchor.** `HA(3, 7, ...)` starts its open at `(open[3]+close[3])/2`; it does not warm up from bar 0. The `path_dependent` flag declares this class, as it does for the shipped accumulators.

**The unstable period buys back start-independence.** The open's recursion carries a factor of 1/2 per bar, so a seed's influence halves every step and dies out within a few dozen bars. Raising `TA_FUNC_UNST_HA` spends that many input bars as warm-up and returns a series that no longer depends on where the caller anchored — measured on the regtest history, 56 warm-up bars make a call anchored at bar 100 bit-identical to the full-history one over its whole emitted range, and 48 still leave two bars differing. The default is 0, which spends nothing and emits one output bar per input bar.

**The two extremes carry no state.** They are an elementwise clamp of the raw bar against the two body edges, so `HA_high`/`HA_low` are recoverable from `HA_open`, `HA_close` and the raw bar. They ship as outputs anyway because recomputing them is the caller's error to make, not the library's to delegate.

## Properties

**Numerical Stability:** [Path-Dependent](/functions/stability.md#path-dependent)

<div class="flag-table">

|  |
| :-- |
| <span class="flag-box">✅</span> **Overlap Input** <span class="flag-tip" tabindex="0" role="note" aria-label="Output is on the same scale as the input price, so it is drawn over the price chart." data-tip="Output is on the same scale as the input price, so it is drawn over the price chart.">i</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Independent Y-Axis</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Candlestick</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Can Output NaN or ±Inf</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Identity at Period 1</span> |

</div>

## Implementation

TA-Lib Definition: [`ha.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/ha/ha.c) · [`ha.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/ha/ha.yaml)

| Native | File |
|--------|------|
| C | [`ta_HA.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_HA.c) |
| Rust | [`ha.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/ha.rs) |
| Java | [`Core_HA.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_HA.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Heikin-Ashi · Heiken-Ashi · Average Bar

## See Also

[AVGPRICE](/functions/avgprice.md) · [TYPPRICE](/functions/typprice.md) · [WCLPRICE](/functions/wclprice.md)
