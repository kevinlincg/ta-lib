---
title: "Wilder's Smoothed Moving Average (RMA)"
description: "Wilder's smoothed moving average: an exponential average whose smoothing factor is 1/period rather than the usual 2/(period+1), seeded with a simple…"
---

## Summary

Wilder's smoothed moving average: an exponential average whose smoothing factor is `1/period` rather than the usual `2/(period+1)`, seeded with a simple average of the first `period` bars. This is the smoothing J. Welles Wilder Jr. used throughout *New Concepts in Technical Trading Systems* (1978) and the one already embedded inside `ATR`, `RSI`, `ADX` and `PLUS_DM`; here it is available on its own. It reacts about half as fast as an `EMA` of the same period, which is what makes it the smoother of choice for volatility and directional-movement work. Sold under several names for one object: RMA, SMMA, Wilder's Smoothing, WildersAverage, WilderMA.

## Formula

alpha = 1 / period; RMA_t = alpha * price_t + (1 - alpha) * RMA_{t-1}. Seed: RMA = SMA of the first `period` bars.

## Notes

- `RMA(TRANGE(high, low, close), period)` is `ATR(period)` bit for bit, provided both functions' unstable periods are set to the same value. Each function owns its own unstable-period knob, so that is the caller's to arrange.
- Wilder's own canonical period is 14, and pandas-ta uses 10; the default here is 30, following the rest of the TA-Lib moving-average family.
- The smoothing factor `1/period` equals an `EMA`'s `2/(period+1)` at `2*period-1`, which is why Wilder's 14-period smoothing is often described as a 27-day EMA. The *seed windows* differ, though — `period` bars against `2*period-1` — so `RMA(x, period)` is not `EMA(x, 2*period-1)`: the two start far apart and converge only slowly.
- A period of 1 performs no smoothing: alpha is exactly 1 and the output is a copy of the input.

## Inputs

- `inReal` — price/data series to smooth

## Outputs

- `outReal` — the Wilder-smoothed average

## Parameters

| Parameter | Type | Default | Accepted values | Description |
| --- | --- | --- | --- | --- |
| `optInTimePeriod` | integer | 30 | 1–100000 | Number of bars in the average; sets smoothing alpha = 1/period |

## Properties

**Numerical Stability:** [Initial Unstable Period](/functions/stability.md#initial-unstable-period)

<div class="flag-table">

|  |
| :-- |
| <span class="flag-box">✅</span> **Overlap Input** <span class="flag-tip" tabindex="0" role="note" aria-label="Output is on the same scale as the input price, so it is drawn over the price chart." data-tip="Output is on the same scale as the input price, so it is drawn over the price chart.">i</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Independent Y-Axis</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Candlestick</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Can Output NaN or ±Inf</span> |
| <span class="flag-box">✅</span> **Identity at Period 1** <span class="flag-tip" tabindex="0" role="note" aria-label="A period of 1 performs no smoothing: the lookback is 0 and every output value is a bit-exact copy of its input value." data-tip="A period of 1 performs no smoothing: the lookback is 0 and every output value is a bit-exact copy of its input value.">i</span> |

</div>

## Implementation

TA-Lib Definition: [`rma.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/rma/rma.c) · [`rma.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/rma/rma.yaml)

| Native | File |
|--------|------|
| C | [`ta_RMA.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_RMA.c) |
| Rust | [`rma.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/rma.rs) |
| Java | [`Core_RMA.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_RMA.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Wilder's Smoothed Moving Average · Smoothed Moving Average · SMMA · Wilder's Smoothing · WilderMA

## See Also

[EMA](/functions/ema.md) · [SMA](/functions/sma.md) · [ATR](/functions/atr.md) · [RSI](/functions/rsi.md) · [MA](/functions/ma.md)
