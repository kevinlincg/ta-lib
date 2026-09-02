perf(ta_func): ATR, NATR and SUPERTREND smooth in the two-coefficient Wilder form (#338)

Closes #338. The three-statement recursion

```c
prevATR *= optInTimePeriod - 1;
prevATR += greatest;
prevATR /= optInTimePeriod;
```

becomes the two-coefficient form with the reciprocal hoisted:

```c
wAlpha = 1.0 / (double)optInTimePeriod;   /* loop-invariant */
wBeta  = 1.0 - wAlpha;
...
prevATR = wAlpha * greatest + wBeta * prevATR;
```

Generator-side only, in `ta_codegen/input/`. The shared FMA detector then does the
rest: `canonicalize_accumulator_add` puts the accumulator product on the left and
`fuse_operands` fuses it, so all four backends emit the same site —
`fma(wBeta, prevATR, wAlpha * greatest)`, `(wBeta).mul_add(prevATR, wAlpha * greatest)`,
`Math.fma(...)`, `Math.FusedMultiplyAdd(...)`. Source operand order does not decide
which product fuses; the canonicalizer does, which is why the input is written in the
issue's order and still fuses the accumulator.

## SUPERTREND is in the diff, and it is not scope creep

`supertrend.c` carries a transcribed copy of the ATR recursion under an explicit
comment declaring bit-exactness with `TA_ATR`. Changing ATR alone broke that
contract, and the SUPERTREND differential test in `ta_regtest` caught it on the
first run:

```
SUPERTREND differential Fail [gData 10 3] bar 42:
  got (111.26398735269693,-1) expected (111.26398735269694,-1) rebuilt over TA_ATR/TA_MEDPRICE
```

So the change is applied there too, and that test goes back to green. It is the only
other copy: `grep '\*= optInTimePeriod - 1' ta_codegen/input/*/*.c` matches nothing
else. ADX/DX/RSI/CMO keep their own recurrences untouched, as the issue asks.

## Numbers

Batch tier, the **shipped** separate-TU `libta-lib.a` (not `ta_bench_cg`), gcc 13.3.0
`-O3`, 100 000 bars, period 14, `TA_SUPERTREND` at period 10 / multiplier 3.0.
Min of 60 interleaved rounds per arm with **alternating arm order**; four control
functions whose emitted C is byte-identical across the two arms are timed in the same
process, in the same run.

| function | | dev `df0c6beb` ns/bar | this branch ns/bar | ratio |
|---|---|---:|---:|---:|
| `TA_ATR` | changed | 5.020 | 1.349 | **3.72x** |
| `TA_NATR` | changed | 5.021 | 1.383 | **3.63x** |
| `TA_SUPERTREND` | changed | 9.222 | 6.426 | **1.44x** |
| `TA_RSI` | control | 5.106 | 5.169 | 0.99x |
| `TA_ADX` | control | 8.061 | 8.047 | 1.00x |
| `TA_EMA` | control | 1.713 | 1.718 | 1.00x |
| `TA_TRANGE` | control | 0.937 | 0.884 | 1.06x |

Read the controls first. Three of them land on 0.99–1.00x, which is this run's real
floor; `TA_TRANGE` reads 1.06x because at 0.9 ns/bar it is near the harness's own
resolution, and it is the honest worst case for how much of these ratios is noise.
Nothing here is claimed below ~1.1x.

**This does not reproduce the issue's 4.73x.** On this host (Intel Xeon @ 2.10GHz, a
shared 4-vCPU cloud instance) the batch gain is 3.7x, not 4.7x. The gap is plausibly
divider latency and the noisier box rather than a disagreement about the mechanism —
but it is a different number on different hardware and is reported as measured, not
reconciled. An earlier ordering-biased sweep on the same box read 4.03x; alternating
the arm order moved it to 3.72x, which is why the ordering matters enough to state.

Streaming was **not** benchmarked here — see the ordering note at the bottom.

## Accuracy

Measured directly, both recursions run in `double` against the same recursion carried
in `long double`, 200 000 bars per period, seeded True Ranges in [0.5, 3.5]:

| period | max rel. diff, old vs new | rel. err vs long double, old | ... new |
|---:|---:|---:|---:|
| 1 | 0 (bit-identical) | 0 | 0 |
| 2 | 0 (bit-identical) | 2.4e-16 | 2.4e-16 |
| 14 | 1.4e-15 | 9.2e-16 | 1.0e-15 |
| 100 | 3.3e-15 | 2.2e-15 | 2.3e-15 |
| 1 000 | 8.4e-15 | 6.2e-15 | 5.6e-15 |
| 10 000 | 1.2e-13 | 2.0e-14 | 1.2e-13 |
| 100 000 | 3.9e-12 | 2.6e-14 | 3.9e-12 |

Two things this says that the issue does not:

- **n = 1 and n = 2 are bit-identical**, as the issue states. Confirmed.
- **At large periods the new form is genuinely less accurate**, not merely different.
  At the legal maximum period of 100 000 it sits 3.9e-12 from a long-double reference
  where the old form sits 2.6e-14 — a ~150x accuracy regression. That is still ~250x
  inside the documented 1e-9 relative contract, and periods that large are not what
  ATR is used at, but it is a cost and not a wash. It is stated here rather than
  buried, and it is the maintainer's call whether it is acceptable.

One claim in the issue does **not** reproduce: the **DC-gain bias is exactly zero**,
not `n * 2^-54`. Feeding a constant True Range of 1.0 for 20n bars, the fused form
converges to exactly 1.0 at every period tested. The reason is the fusion itself —
`fma(wBeta, 1, wAlpha)` rounds the exact sum `wBeta + wAlpha` once, and since
`wBeta = fl(1 - wAlpha)` is within 2^-54 of `1 - wAlpha`, that single rounding lands
back on 1.0. The bias the issue bounds is real for an *unfused* emission of this
form. It does not exist in the emission this branch produces, on any of the four
backends, because all four fuse.

## The two new tolerance rows

`LEGACY,064,FROZEN` compares against frozen v0.6.4 with per-function absolute
tolerances, and its rule is "the largest deviation the function actually shows across
its cases here, times ~3, rounded up to one significant digit". ATR failed it at
`diff 4.44e-16, tolerance 0`, which is the gate doing its job.

To measure rather than guess, the comparison was temporarily replaced with an
unconditional print of every non-zero deviation, and the maximum taken per function.
The check that the instrumentation was right: **the same run reproduced every existing
row's documented measurement exactly** — EMA 1.42e-14, MA 1.42e-14, MAVP 1.42e-14,
SAR 1.42e-14, ADOSC 7.45e-09, MACDFIX 1.33e-15, CORREL 3.15e-13, T3 1.28e-13,
TEMA 9.95e-14, LINEARREG_ANGLE 1.74e-11, and the rest. The instrumentation was then
reverted and only the two rows added:

```c
{ "ATR",  2e-14 },  /* #338  measured 4.88e-15 */
{ "NATR", 2e-14 },  /* #338  measured 4.00e-15 */
```

SUPERTREND needs no row: it postdates v0.6.4 and is not in the freeze.

## What this costs

- **The streaming handle grows 16 bytes.** The two coefficients are hoisted into the
  handle by the stream emitter (`sp->wAlpha`, `sp->wBeta`), which is what makes the
  streaming `Update` benefit too — but `TA_ATR_Stream` goes 48 -> 64 bytes, and
  #316 is currently trying to stop C's `Peek` from copying that struct. Same for
  NATR and SUPERTREND.
- **Accuracy at large periods**, quantified above.
- Two extra `double` locals in the batch bodies.

## Ordering: this wants #337 first

The issue is explicit that this should land after #337, and the generated C says why:
`TA_FMA_MULTIVERSION` sits on the batch tiers (`TA_ATR`, `TA_S_ATR`) and on nothing
else, so `TA_ATR_StepImpl` / `_Update` / `_Peek` each pay a `call fma@PLT` for the
site this change introduces. Merging this before #337 would speed the batch tier up
~3.7x and slow the peek tier down. The #337 implementation is on
`kevinlincg:issue-337-stream-fma-multiversion`, one commit on top of this same dev.

## Verified

- `cargo run -- generate` is clean and idempotent (a second run changes nothing).
- The generator's own suite: green. The FMA inventory gate is a live control here —
  it went red naming "FMA dispatch inventory drifted" when atr/natr started fusing,
  and again for supertrend, before each was registered in `FUSING_INVENTORY`.
- `bin/ta_regtest`: all tests succeed, including LEGACY/064/FROZEN, SUPERTREND, KC
  (which recomposes bands over `TA_ATR`), PERIOD1/BOUNDARY and the streaming
  finite-input gate.

## Not verified — stated rather than implied

- **C#**: not compiled or tested. No .NET SDK in the environment this was prepared in.
  The emitted C# was read and fuses the identical site, but nothing executed it.
- **Cross-language value gates**: `--codegen` / `--xlang-hash` were not run, so the
  claim that the four backends agree bitwise rests on the shared detector and on
  reading the four emissions, not on a measured hash.
- **`--fuzz-064`**: not run (needs the frozen-oracle worktree built).
- **Streaming benchmarks**: not run, and would be misleading before #337 anyway.
- **musl / MSVC / non-glibc libm**: not measured, same open question as #337.
