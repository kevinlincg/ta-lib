---
title: "Zero-Lag Exponential Moving Average (ZLEMA)"
description: "Zero-Lag Exponential Moving Average: an exponential moving average taken over a de-lagged copy of the series, where each bar is extrapolated forward by…"
---

## Summary

Zero-Lag Exponential Moving Average: an exponential moving average taken over a de-lagged copy of the series, where each bar is extrapolated forward by the amount it moved over the last half-window. The extrapolation cancels the exponential average's own phase lag to first order, so the line turns closer to the price pivot than a plain EMA of the same period does — at the cost of overshooting when a move reverses, since the de-lagged series exaggerates whatever the last half-window did. Read it as a faster EMA: crossings arrive earlier and false ones arrive more often.

## Formula

```text
lag = (period - 1) / 2, truncated
d[t] = 2*P[t] - P[t-lag]
ZLEMA = EMA(d, period)
```

## Notes

- The published construction has no traceable primary source. It is universally attributed to John Ehlers and Ric Way, *Zero Lag (Well, Almost)* (2010), but that paper specifies a different filter — an error-correcting EMA with a per-bar gain search — and neither the de-lagged series nor the half-window lag appears anywhere in it. What ships here is the de-lagged-EMA construction that the surrounding ecosystem publishes under the "zero lag" name.
- The half-window lag truncates on even periods, which is what the implementations that publish golden values do.
- The de-lagged value is formed as `2*P[t] - P[t-lag]` rather than the algebraically equal `P[t] + (P[t] - P[t-lag])`: one rounding instead of two.
- The exponential average is seeded with a simple average of the first full window of de-lagged values, which is TA-Lib's own EMA convention. Implementations that seed from a single raw price instead differ through the whole warm-up, converging only well after it.
- The output is therefore an EMA of a derived series, and it converges as an EMA does: it reads the EMA unstable period rather than carrying one of its own.
- A period of 1 performs no smoothing: the output is a copy of the input.

## Inputs

- `inReal` — Source series (typically price)

## Outputs

- `outReal` — ZLEMA line

## Parameters

| Parameter | Type | Default | Accepted values | Description |
| --- | --- | --- | --- | --- |
| `optInTimePeriod` | integer | 30 | 1–100000 | Smoothing period, and the window whose half sets the de-lag distance |

## Properties

**Numerical Stability:** [Start-Independent](/functions/stability.md#start-independent)

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

TA-Lib Definition: [`zlema.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/zlema/zlema.c) · [`zlema.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/zlema/zlema.yaml)

| Native | File |
|--------|------|
| C | [`ta_ZLEMA.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_ZLEMA.c) |
| Rust | [`zlema.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/zlema.rs) |
| Java | [`Core_ZLEMA.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_ZLEMA.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Zero-Lag Exponential Moving Average, Zero Lag EMA, ZLMA

## See Also

[EMA](/functions/ema.md) · [DEMA](/functions/dema.md) · [TEMA](/functions/tema.md) · [HMA](/functions/hma.md) · [MA](/functions/ma.md)

## References

- John F. Ehlers and Ric Way, *Zero Lag (Well, Almost)*, Technical Analysis of Stocks & Commodities, V.28:11 (November 2010) — the paper the name comes from; it specifies a different filter, see Notes
- [Zero lag exponential moving average](https://en.wikipedia.org/wiki/Zero_lag_exponential_moving_average) — the de-lagged construction as commonly published
