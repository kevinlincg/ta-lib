feat(ha): HA — Heikin-Ashi Candles, and the harness's first four-output function (#373)

Implements #373 as ruled: `TA_HA(inPriceOHLC) -> outHAOpen, outHAHigh, outHALow, outHAClose`, group `Price Transform`, flags `[overlap, unstable_period, stream]`, lookback `TA_GetUnstablePeriod(TA_FUNC_UNST_HA)` with a default of 0, so every input bar produces an output bar.

The close is the bar's four-price average, the open the midpoint of the previous candle's own open and close (the only recursive term), the high and low the elementwise extrema of the raw bar against that pair. Two carried doubles, no ring — `stream-census` derives T2, `state=2`.

## The flags: `unstable_period`, and deliberately not `path_dependent`

Worth stating up front, because an earlier revision of this branch shipped both and the difference is a gate rather than a preference.

`path_dependent` is *true* in the narrow sense: at the default knob of 0 the recursion re-seeds at the anchor, so `TA_HA(3, 7, ...)` starts its open at `(open[3]+close[3])/2` rather than warming up from bar 0, and a sub-range call therefore differs from the full-history one. But declaring it costs exactly what the card predicted:

- With `unstable_period` alone plus the `UNSTABLE_MAP` row, `stability_class()` answers `TA_STABLE_CONVERGING` and the range sweep **value-compares all four outputs** across every start/end pair. MEASURED — deleting only the `UNSTABLE_MAP` row, so the class falls back to `EPSILON`, fails it: `doRangeTestFixSize diff data for idx=0 (9.315750e+01, 9.200000e+01) ... For output #1 of 4`. That is the gate proving it is live, on this function, on all four outputs.
- With `path_dependent` set, that same gate maps to `TA_STABLE_SKIP` and compares nothing.

So the pair trades a live four-output value gate for a leg asserting the flag is earned. The convergence the knob buys is what makes `CONVERGING` the truthful class.

## The cap

`CODEGEN_MAX_OUTPUTS` goes 3 → 4. Every reference to it is symbolic, so the clamped loops and sized buffers scale with the define. What needed editing is the other places the number 3 is written by hand:

- `V_MAX_OUTPUT` (`test_variants.c`) — this one announced itself: the `TA_S_`/VARIANT gate **failed loudly** on HA rather than clamping.
- `PB_MAX_OUTPUT` (`test_period_boundary.c`) — this one **clamps silently** (`o < PB_MAX_OUTPUT`), so a wider function loses its extra outputs there with nothing to say so. Raised and commented. Whether that loop should fail like the other two rather than clamp is a separate call, and I have not made it here.

Neither the raise nor the guard is taken on trust:

- Perturbing **only the fourth output** in the generated Rust by `1e-9` is caught by `server_verify` as a BITWISE mismatch — so the raised cap really does compare output 4, rather than passing with it unread.
- Left at 3, the #352 startup guard refuses to run the suite at all, naming HA. The silent case is unreachable in both directions.

`TA_FUNC_UNST_HA` appends as value 25. Beyond `enums.yaml` that is the Rust template's `FuncUnstId` variant and `COUNT`, three regtest unstable tables, and two generator inventories (`abstract_rows_suite`'s id table, `stability_suite`'s self-declaring count).

## Numerics

The summation order `((o+h)+l)+c` and the two exact power-of-two divisions are the whole contract, and all four backends render that association. It is **not** `TA_AVGPRICE`'s `(h+l+c+o)/4`: same four terms, different order, and floating-point addition does not associate. The test asserts the difference rather than commenting it — differing on at least one bar AND agreeing to within rounding on every bar, so it fails both if the two are ever unified and if they drift for a real reason. MEASURED: 17 of 252 bars differ, all by one ulp.

## Tests

`test_ha.c`, registered in `ta_regtest.c`, `ta_test_func.h`, `CMakeLists.txt` and `Makefile.am`.

- **1008 bit-exact comparisons** — all four outputs, all 252 SREF bars, against a pandas-ta-classic 0.6.52 capture (run here, not quoted), compared with `memcmp` so a signed-zero divergence cannot pass as equal.
- **The seed, on a corpus that can see it.** SREF bar 0 satisfies `open + close == high + low` (`92.5 + 91.5 == 93.25 + 90.75`), which is exactly the condition under which the published seed and ta4j's raw-bar seed agree from bar 1 on — a golden frozen on SREF pins the seed at bar 0 and nowhere else. S12 is a 12-bar corpus violating that identity, from the same capture, and the leg carries its own control: an in-test raw-bar seed must disagree at every bar from 1 on, or the corpus has drifted degenerate and is pinning nothing.
- **The unstable period as a warm-up, not a different answer.** MEASURED here, `k = 54` is the smallest warm-up reproducing the full history bit-for-bit at every bar it can be asked for; `k = 10` does not, which is the control that keeps the `k = 54` leg from passing for free.
- **Edges**: the single-bar seed, an all-flat window (exact, no epsilon), and all four in-place calls, each output in turn handed the input it would clobber.

## Gates

Full `./ta_regtest` green; `regtest.py --function=HA --language=c,rust,java` rc 0 including `stream_verify` against the frozen oracle; `--xlang-hash` 506 cases per server, zero mismatches, bit-identical on Rust and Java across all shapes and the unstable-period axis; `regen-check`, `check-source-lists` and `clippy` clean; generator suite, `cargo test --doc/--tests`, warning-free `cargo doc`.

Two more things verified by breaking them and watching them fail: swapping the sum to AVGPRICE's order fails the golden at bar 1 (`93.164999999999992` vs `93.165000000000006`), and reading `inHigh[i]`/`inLow[i]` after the first store reddens the #130 in-place alias gate on 195/252 values — which is what the body's `tempHigh`/`tempLow` exist for.

## What I did not check

- **The C# leg.** No .NET SDK in this environment, so its server was never built or run. CI's "Generated C# compiles" and the nightly `--xlang-hash` cover it.
- **`±0`.** `max`/`min` over `{high, HA_open, HA_close}` can tie only on a signed zero, where C's comparison macro and Java's `Math.max` need not agree. No divergence appeared across `--xlang-hash`'s shapes on C, Rust and Java; I did not construct a targeted ±0 corpus, and C# is unverified as above.
- **The bench row.** `ta_bench` reports a near-zero reference time for HA, as it does for CUMSUM and every other post-cutover function — the reference arm has no such function. No performance claim is made here.

## Note on this branch's history

The branch carries two commits by design. The first is a complete earlier implementation; the second supersedes its flag choice with the measurement above, keeps its two strongest legs (the asserted AVGPRICE difference and the four-way aliasing), and adds the seed-discriminating corpus. One leg of the first commit is **not** carried over and is worth a look if you want it: a malformed-bar corpus reaching the `HA_high`/`HA_low` clamp arms that ordinary bars cannot — on a well-formed bar the average is already inside `[low, high]`, so two clamp arms are unreachable without it.

## Refreshed onto dev (dev at aebff428, ERI #361)

The branch was cut before VORTEX (#349) and ERI (#361) landed and has been merged
forward onto both. Every conflict either merge raised was in a generated tier —
`ta_func_api.c`, `BuildStamp.java`, `FunctionDescription.java`, the Java server —
so all were resolved by taking dev's side and regenerating; no hand-edited
artifact, and no number in this body moved. HA allocates no
`internal_error_ids.yaml` site, so it is clear of the id collisions the other
in-flight indicator branches have been hitting.

Re-run after the ERI merge: `build.py regen-check` green (regeneration is
idempotent over the merged tree), and the full C reference suite over a fresh
CMake build — all tests succeeded. **Not** re-run after this merge: `--codegen`,
`--xlang-hash`, `clippy`, the Rust doctests, or the Java and C# builds.

The function corpus goes 197 -> 198 against dev, adding exactly `HA`.
