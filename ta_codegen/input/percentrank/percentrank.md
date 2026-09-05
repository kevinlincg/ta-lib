# PERCENTRANK

## Summary

Percent Rank: the percentage of the values in the preceding window that are strictly below the current value.

It answers "where does today sit within its own recent history" on a 0–100 scale, and is the inverse question to a percentile: a percentile names the value at a rank, this names the rank of a value. A reading near 100 says the current value is above almost everything the window holds, near 0 that it is below almost everything, and a flat stretch of data reads 0 because nothing in the window is strictly below.

It is the third component of Connors RSI.

## Formula

PERCENTRANK[t] = (count of j in [t-N, t-1] with P[j] < P[t]) / N * 100

The current bar is compared against the N bars before it and is excluded from its own window, so the count is divided by the window width and the ratio is scaled to a percentage.

## Notes

- The comparison is **strict**: a window value equal to the current one is not counted. TradingView's Pine `ta.percentrank` ranks with "less than or equal" instead, so on data carrying ties it computes a different function — most visibly on a constant series, where this function is 0 and Pine is 100.
- Inputs are expected to be finite. A NaN in the window compares false against everything, so it silently fails to count rather than propagating.

## Inputs

- `inReal` — Source price/value series

## Outputs

- `outReal` — Percentage of the preceding window that is strictly below the current value (0–100)

## Parameters

- `optInTimePeriod` — Number of preceding bars ranked against

## Implementation

TA-Lib Definition: [`percentrank.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/percentrank/percentrank.c) · [`percentrank.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/percentrank/percentrank.yaml)

| Native | File |
|--------|------|
| C | [`ta_PERCENTRANK.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_PERCENTRANK.c) |
| Rust | [`percentrank.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/percentrank.rs) |
| Java | [`Core_PERCENTRANK.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_PERCENTRANK.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Percent Rank, Percentile Rank

## See Also

MIN · MAX · STDDEV

## References

- Connors Research, *An Introduction to ConnorsRSI* (2012), page 8
