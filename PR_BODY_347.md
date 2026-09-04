feat: ZLEMA — Zero-Lag Exponential Moving Average, and TA_MAType_ZLEMA (#347)

Implements #347 as specified, with one departure (period 1, below) and one gate widening the function was the first to need.

An EMA over the de-lagged series `2*P[t] - P[t-lag]`, `lag = (n-1)/2` truncated, seeded with an SMA of the first full window of de-lagged values — TA-Lib's own EMA convention, not the single raw price Tulip seeds from. The body reads the lagged bar through a trailing index advanced in lockstep, so `stream-census` derives **T3, state=2, no generator change**, and the peek frame's ring holds exactly the `lag` bars it needs.

## The arithmetic contract, and the gate that holds it

The de-lag is spelled `2.0*x - trailing` (one rounding, not the two of `x + (x - trailing)`), and the step keeps `TA_EMA`'s own `((v - prev)*k) + prev`. Both are gated rather than asserted in a comment: `test_zlema.c` materialises the de-lagged series, calls the **shipped `TA_EMA`** on it, and asserts `memcmp` equality over periods 2..40 × 26 start indices.

The alignment that makes it bitwise is worth stating because it is easy to get subtly wrong: de-lagged index `j` is input bar `j+lag`, so the reference must call `TA_EMA` at `startIdx = zlemaBegIdx - lag`. Off by one there and the leg degrades to ~1e-15 and reads like a tolerance problem.

## Departure from the issue: period 1 is NOT the identity by itself

The issue says period 1 "degenerates cleanly … ZLEMA == input (verified exactly)". It does not. The de-lag *is* the identity there (lag 0, and `2.0*x - x` is exact), but `k` is exactly 1.0 and the step reduces to `(x - prev) + prev`, which returns `x` only while consecutive values stay within a factor of two of each other. That is `TA_EMA`'s own trap — the reason `ema.c` carries an explicit copy branch (`080926`). `zlema.c` carries the same branch, and `period1_identity` is declared on the strength of it.

Watched red: with the branch removed, bar 1 of a series alternating `1e-3` and `1e3` comes back `0.0019999999999527063` where `0.002` is asserted.

## TA_MAType_ZLEMA is 12, and the choice list reads oddly forever

Appended after the `DISABLED` (#93) and `DEFAULT` (#182) pseudo-members, because `MAType` is append-only by its own contract and `test_internals.c` pins `DEFAULT == 11`. **Cost, stated so nobody "fixes" it later:** the user-facing choice list now reads `… T3, HMA, DISABLED, DEFAULT, ZLEMA` — a real average after the two pseudo-members. Cosmetic, and the price of not renumbering an ABI.

Everything else is generated or delegates: one arm each in `ma_lookback` and `ma`, one row in the enum pin table, and the `ta_def_ui.c` choice list / Java / C# / Rust `MAType` mirrors regenerate. The generated Javadoc/XML-doc of every other `MAType` taker (APO, BBANDS, MACDEXT, MAVP, PPO, PVO, STOCH, STOCHF, STOCHRSI) gains `12=ZLEMA` — that is most of the diff's line count.

## One gate needed widening

ZLEMA is the first function whose **fused** site reads a **ring buffer**. `peek_suite.rs`'s peek/update multiply-add comparison masks buffer reads in the `sp->ring[...]` spelling only, while a peek frame binds the pointer first and subscripts the bound local (#316). The two frames are semantically identical there; the gate saw two different `fma(` texts and reported a divergence that is not there.

The mask now also accepts the bound spelling, **only on a line carrying an `fma(`**. Everywhere else the bound form stays compared as written: the unrestricted form would have erased 193 peek buffer reads corpus-wide, this one erases 1. A floor (`bound_masked >= 1`) pins the new branch alive, and was watched red at `>= 2`.

Two inventories move for the ordinary reason — the function has the property they enumerate: `fma.rs::FUSING_INVENTORY` gains `zlema`, and MA's dispatch-arm list goes 10 → 11.

## Controls (each broken by hand and watched fail)

| Sabotage | Leg that reddened |
|---|---|
| even-period lag rounded up (`period/2`) | the shape check — `nbElement` 238 where 239 is asserted (the lag is part of the lookback) |
| de-lag coefficient `1.9` instead of `2.0` | frozen goldens, 9.9e-2 relative at the first bar |
| step reordered to `v*k - prev*k + prev` | the `TA_EMA` differential, `memcmp` at period 2 |
| period-1 copy branch removed | the identity leg, `0.0019999999999527063` for `0.002` |
| MA's ZLEMA arm pointed at `ema` | the MAType parity leg (`beg 2/nb 250` vs `3/249`) |
| the new bound-form mask removed | the peek gate, naming ZLEMA — this is the failure that motivated it |

## Verification

Green: full `ta_regtest`; `--xlang-hash --function=ZLEMA` **bit-identical at zero tolerance, Rust 1948 / Java 1948 cases, 0 mismatches**; generator `cargo test` (30 suites); `regen-check`; `check-source-lists` (58 files); `clippy -D warnings` over both crates; `cargo doc --no-deps` warning-free; generated crate 73 lib + 550 doc tests (ZLEMA's three doctests included); Java jar built through `mvnw` with `StreamSmokeTest` 4711 checks.

**Not verified here, stated rather than smoothed over:**

- **C# was never compiled.** This machine has no .NET SDK. `Core_ZLEMA.cs` and the regenerated C# metadata/server are unexecuted, and the xlang-hash run above carries no C# arm.
- The `--codegen` value sweep **skips** ZLEMA by construction — a post-cutover function has no frozen `ta_ref_serve` baseline, and the runner says so explicitly. Its structural legs and the in-test `server_verify` did run against the C, Rust and Java servers.
- The frozen golden rows are the ones **recorded in #347** from pandas-ta-classic; this branch checks against them at 1e-12 relative. It does not re-measure the oracle — no pandas or Tulip arm was run here.
- The Tulip `TA_ZLMA` / pandas `TA_ZLMA` oracle-arm **rename** the issue asks for (decision 6) is not in this branch: those servers live outside this repo.
- No benchmark is claimed and none is owed; nothing on an existing path changed shape.
