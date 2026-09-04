feat: FOSC — Forecast Oscillator, the close against the previous bar's TSF (#345)

Closes #345.

`FOSC[t] = 100 * (Close[t] - TSF[t-1]) / Close[t]`, lookback `optInTimePeriod`,
group Momentum Indicators, one `line` output, `flags: [stream]`, default period 5
(Chande's own suggested regression length, not the TSF/LINEARREG family's).

**The lag is the indicator.** Comparing `Close[t]` against `TSF[t]` measures fit
rather than forecast error and does not reproduce the author-sourced values. The
unlagged form that several vendors publish as "CFO" — and that pandas-ta-classic's
`momentum/fosc.py` computes — is not what ships here.

## Shape of the implementation

The regression is `TA_TSF`'s, **transcribed rather than called**: the window FOSC
needs trails the emitted bar by one, so a call would have to be re-primed per bar.
Every arithmetic line keeps TSF's source shape on purpose — the #254 reseed, the
#103 sliding recurrence, and the fused `b + m*(double)optInTimePeriod` (#96, which
the C emitter renders as `fma(m, (double)optInTimePeriod, b)`, the same shape as
`ta_TSF.c`). Leg 2 below holds the copy to the original bitwise, so tidying any of
it is a test failure, not a style change.

## Verification

`src/tools/ta_regtest/ta_test_func/test_fosc.c`, four legs:

| # | Leg | What it can catch |
|---|---|---|
| 1 | Two published golden vectors (Tulip 0.9.2 `tests/atoz.txt:118`, which transcribes Achelis p.147; `tests/untest.txt:199`), absolute tolerance | a wrong **formula** |
| 2 | Differential vs shipped `TA_TSF`, **bitwise** (`memcmp`), period 2..40 × 35 startIdx — 1092 calls, **169,976 values, 0 mismatches** | a wrong **fusion** |
| 3 | Zero close (`+0.0` and `-0.0`) → exactly `+0.0`, asserted bitwise; batch tier, every language server, and Open/Update/Peek/OpenAndFill | the #112 guard, in every tier |
| 4 | Range independence via `doRangeTestEx` at `TA_STABLE_EPSILON` (the class inherited from TSF), three periods | a startIdx-dependent value |

Leg 2's alignment is the trick and is easy to get subtly wrong: the reference must
call `TA_TSF` at `startIdx = fosc_outBegIdx - 1`, the bar FOSC primes at, because
TSF's sliding sums are primed at its own caller's adjusted startIdx. Get it wrong
and the mismatch is ~1e-13 and reads like a tolerance problem.

### Controls — each one broken on purpose and watched go red

- Store the output **after** the slide (i.e. the unlagged variant) → leg 1 fails,
  `|diff| = 1.522e+00` against a `5e-5` gate.
- Re-associate `SumXY = SumXY + SumY - weightedTrailing` to
  `SumXY - weightedTrailing + SumY` → leg 1 still **passes**; leg 2 catches it at
  the 13th digit. This is exactly the tidy-up the input comment warns against.
- Drop the zero guard → red (the abstract interface's zero-data sweep catches it
  first, with `-nan`).
- Make the guard write `-0.0` → the abstract sweep **passes** (the value is
  finite) and leg 3's bitwise check is what fires.

## Two corrections to the issue

**Tolerance.** #345 prescribes an absolute `1e-11` for the Tulip leg. That bound was
measured between two full-precision kernels and is right for a *live* `tulip_serve`
arm — but the vectors the issue also asks to freeze are the **file's printed text**,
at four and three decimals. Text cannot carry `1e-11`; asserting it would fail a
correct implementation. The legs use half a unit in the last printed place (`5e-5`
and `5e-4`); measured deviations are `4.557e-05` and `4.267e-04`, i.e. agreement to
the full precision the files carry. Still non-vacuous by four decades — the unlagged
variant misses the same goldens by up to `2.752e+00`.

**Return code.** #345 expects `startIdx > endIdx` to answer `0/0/TA_SUCCESS`. It does
not, for FOSC or anything else — that is `TA_OUT_OF_RANGE_END_INDEX` library-wide.
The leg asserts FOSC answers whatever `TA_TSF` answers rather than a literal;
`0/0/TA_SUCCESS` is the *insufficient bars* case, asserted separately.

**Streaming tier.** The issue predicted "T3 trailing-ring". `stream-census` derives
**T4, state 11** — identical to TSF and LINEARREG. The claim that mattered ("the tier
TSF already occupies") holds; the label did not.

## One generator change, and it is load-bearing

`backends/c_stream.rs`. FOSC is the first function whose peek frame needs nothing but
a carried scalar — the forecast was computed on the previous commit, so the trim
leaves a body reading no window state at all. It is therefore the first to trip
`no_peek_frame_declares_a_local_nothing_reads`, whose own message already names this
defect: *"the trim orphaned it and nothing purged it"*.

Cause: `peek_localized` pinned **every** extrema index var out of the purge, so the
rescan cursor `j` survived to be declared, seeded from the handle, and shifted by a
rebase — read by nothing. Now only the two the rebase text itself reads (the cursor
and the trailing index) are pinned, and a peek frame rebases only the index vars its
trimmed body still reads. A commit frame is untouched: it writes them all back.

**Provably contained.** That corpus gate is green today *with* index vars pinned,
which means every existing peek body reads every index var it carries — so filtering
can only affect FOSC. Confirmed: no other `src/ta_func/*.c` moves a byte.

`backends/fma.rs` also gains `"fosc"` in `FUSING_INVENTORY`: FOSC fuses in Rust as it
does in C, and that list is an assertion about which functions do.

## What this leaves — deliberately, for you to rule on

The same dead `j` **still sits in FOSC's Java and C# peek frames.** Those emitters have
no purge at all, so the equivalent fix is to drop unread index vars from the
localization — and that **does** change existing functions: measured, it removes
`highestIdx`, `lowestIdx`, `i` and `j` rebase lines from other peek frames. That makes
it a corpus-wide cleanup of the same family as #343, not part of adding an indicator,
so I left it rather than smuggling it in here. Rust's peek mutates a cloned handle
instead of localizing, so it carries the dead shift in a third shape; also left.

## Gates run

Full `ta_regtest` (no filter), `regen-check`, `check-source-lists`, the generator's own
suite (30 test binaries), the generated crate's lib + doc tests, `clippy -D warnings`
over both crates, warning-free `rustdoc`, and `--xlang-hash` at **zero tolerance** for
FOSC on C, Rust and Java — 1948 cases each, 0 mismatches, 96% non-vacuous.

**I did not check C#.** This machine has no .NET SDK, so the C# library was never
compiled and the C# server never ran. The C# source is generated from the same IR and
the PR gate compiles it, but that claim is untested on my side.

**I did not run the `ta_ref_serve` sweep** — no pinned-tag reference worktree in this
checkout. FOSC is post-0.6.4 so that oracle auto-skips it either way, but functions I
did not touch went unswept by it.

## Not done, from the issue's oracle plan

No `ta_trading_signals` arm was added (the issue notes it would be one line in
`trading_signals_serve/capture.mjs`); that repo is not in this checkout. Live
`tulip_serve` capture likewise did not run — the goldens here are the checked-in
printed vectors, which is what fixed the tolerance above.
