---
title: "Williams Fractal (swing pivot detector) (FRACTAL)"
description: "Williams Fractal: a bounded swing-pivot detector."
---

## Summary

Williams Fractal: a bounded swing-pivot detector. A bar is a swing high when its high stands strictly above the highs of a fixed number of bars on each side of it, and a swing low when its low stands strictly below the lows of the same neighbourhood. Bill Williams named the five-bar case a "fractal" in *Trading Chaos* (1995); charting platforms generalise it to independent left and right arms.

Each output is a 0/100 flag, and the flag for a candidate bar is reported at the bar that confirms it — the candidate plus its right arm — because that is the first bar at which the answer is knowable. Reading a `100` therefore means "the bar that sits `optInRightBars` back was a pivot", which is where a swing-based rule (a stop, a trendline anchor, a structure break) should place it. The two flags are independent: an outside bar can raise both, and most bars raise neither.

## Formula

Candidate = the bar optInLeftBars + optInRightBars back from the window end, i.e. optInRightBars back from the reported bar

outSwingHigh = 100 iff High[Candidate] > High[j] for every other bar j of the window
outSwingLow  = 100 iff Low [Candidate] < Low [j] for every other bar j of the window

Window = the optInLeftBars bars before the candidate, the candidate, and the optInRightBars bars after it

## Notes

- Both arms compare strictly. A candidate that merely ties a neighbour is not a pivot, so a flat top or a flat bottom raises no flag at all. Some charting documentation describes an asymmetric rule — strict against the left arm, non-strict against the right — which instead flags the last bar of a flat top; ta4j and trading-signals both use the strict rule implemented here.
- The flag is reported at the confirmation bar, not at the pivot. A caller that wants the pivot's own index subtracts `optInRightBars`.
- Nothing is smoothed, accumulated or divided: the outputs are exact from the first bar that has a full window, and a call over a sub-range agrees bar for bar with a call over the whole history.
- The left and right arms are independent, so an asymmetric pivot rule (a long left arm for structure, a short right arm for latency) is expressed directly rather than by post-processing a symmetric one.

## Inputs

- `inHigh` — High price of each bar
- `inLow` — Low price of each bar

## Outputs

- `outSwingHigh` — 100 when the candidate bar is a strict swing high, 0 otherwise
- `outSwingLow` — 100 when the candidate bar is a strict swing low, 0 otherwise

## Parameters

| Parameter | Type | Default | Accepted values | Description |
| --- | --- | --- | --- | --- |
| `optInLeftBars` | integer | 2 | 1–100000 | Bars that must be strictly exceeded on the candidate's left |
| `optInRightBars` | integer | 2 | 1–100000 | Bars that must be strictly exceeded on the candidate's right, and the delay before the verdict is reported |

## Properties

**Numerical Stability:** [Start-Independent](/functions/stability.md#start-independent)

<div class="flag-table">

|  |
| :-- |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Overlap Input</span> |
| <span class="flag-box">✅</span> **Independent Y-Axis** <span class="flag-tip" tabindex="0" role="note" aria-label="Output is on its own scale, drawn in a separate pane below the price chart." data-tip="Output is on its own scale, drawn in a separate pane below the price chart.">i</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Candlestick</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Can Output NaN or ±Inf</span> |
| <span class="flag-box">☐</span> <span style="opacity:0.5">Identity at Period 1</span> |

</div>

## Implementation

TA-Lib Definition: [`fractal.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/fractal/fractal.c) · [`fractal.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/fractal/fractal.yaml)

| Native | File |
|--------|------|
| C | [`ta_FRACTAL.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_FRACTAL.c) |
| Rust | [`fractal.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/fractal.rs) |
| Java | [`Core_FRACTAL.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_FRACTAL.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Williams Fractal, Swing High Swing Low, Pivot High Pivot Low

## See Also

[AROON](/functions/aroon.md) · [MAX](/functions/max.md) · [MIN](/functions/min.md) · [MINMAXINDEX](/functions/minmaxindex.md)

## References

- Bill Williams, *Trading Chaos*, John Wiley & Sons (1995)
- [ta4j `FractalHighIndicator` / `FractalLowIndicator`](https://github.com/ta4j/ta4j)
