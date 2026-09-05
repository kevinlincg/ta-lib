# HA

## Summary

Heikin-Ashi candles: a per-bar OHLC-to-OHLC transform that redraws the chart from averaged prices instead of raw ones. Each candle closes at the average of its own four prices and opens at the midpoint of the previous Heikin-Ashi candle's body, so consecutive candles share a boundary and the gaps of the raw chart disappear. The result trades responsiveness for readability: runs of one colour last longer and are easier to read as trend, at the cost of a lagged open and a body that no longer shows where the market actually opened or closed. Read a long same-colour run with small opposite shadows as a trend holding, and a small body with shadows on both sides as the trend losing conviction.

## Formula

```
HA_close[i] = (Open[i] + High[i] + Low[i] + Close[i]) / 4

HA_open[0]  = (Open[0] + Close[0]) / 2
HA_open[i]  = (HA_open[i-1] + HA_close[i-1]) / 2

HA_high[i]  = max(High[i], HA_open[i], HA_close[i])
HA_low[i]   = min(Low[i],  HA_open[i], HA_close[i])
```

## Notes

- The first candle is seeded from its own bar — the open at `(Open + Close) / 2`, the close at the four-price average — which is the convention published by StockCharts and implemented by pandas-ta-classic and kand. ta4j instead emits the raw bar as its first Heikin-Ashi candle; the two conventions differ by a residue that halves every bar and is gone within roughly the first fifty.
- Because that residue decays rather than persisting, the function carries an unstable period instead of being path-dependent: raising it discards early bars and returns values closer to an infinite-history calculation.
- The close is the same quantity as `AVGPRICE`, but summed in the published `(O+H+L+C)` order rather than that function's `(H+L+C+O)`. The two agree to within one unit in the last place and are not bit-identical.
- Only the open is recursive. The high and the low are elementwise extrema of the current bar, so they add nothing to the state that carries between bars.

## Inputs

- `inOpen` — Open price of each bar
- `inHigh` — High price of each bar
- `inLow` — Low price of each bar
- `inClose` — Close price of each bar

## Outputs

- `outHAOpen` — Heikin-Ashi open: the midpoint of the previous candle's own open and close
- `outHAHigh` — Heikin-Ashi high: the highest of the raw high and this candle's open and close
- `outHALow` — Heikin-Ashi low: the lowest of the raw low and this candle's open and close
- `outHAClose` — Heikin-Ashi close: the average of the bar's four raw prices

## Implementation

TA-Lib Definition: [`ha.c`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/ha/ha.c) · [`ha.yaml`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/input/ha/ha.yaml)

| Native | File |
|--------|------|
| C | [`ta_HA.c`](https://github.com/TA-Lib/ta-lib/blob/main/src/ta_func/ta_HA.c) |
| Rust | [`ha.rs`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/rust/library/src/ta_func/ha.rs) |
| Java | [`Core_HA.java`](https://github.com/TA-Lib/ta-lib/blob/main/ta_codegen/output/java/fragments/Core_HA.java) |

TA-Lib is also available for Python, R and more using a [wrapper](/install/#wrappers).

## Aliases

Heikin-Ashi, Heiken-Ashi, Heikin Ashi Candles, HA Candles, Average Bar Candles

## See Also

AVGPRICE · MEDPRICE · TYPPRICE · WCLPRICE · EMA

## References

- [StockCharts ChartSchool — Heikin-Ashi Candlesticks](https://chartschool.stockcharts.com/table-of-contents/chart-analysis/chart-types/heikin-ashi-candlesticks) — the four formulas and the first-candle seeding.
- [pandas-ta-classic — `candles/ha.py`](https://github.com/xgboosted/pandas-ta-classic/blob/main/pandas_ta_classic/candles/ha.py) — the seeded recursion and the elementwise high/low.
- [kand — `ta/ohlcv/ha.rs`](https://github.com/kand-ta/kand/blob/main/kand/src/ta/ohlcv/ha.rs) — the same seed and recursion in Rust.
- [ta4j — `HeikinAshiBarBuilder`](https://github.com/ta4j/ta4j) — the same recursion with the raw first bar.
