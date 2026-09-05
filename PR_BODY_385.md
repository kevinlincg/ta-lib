fix(kama): test the efficiency ratio's denominator for zero (#385)

Answers #385 with the first of its two options: KAMA gains the exact
`sumROC1 <= 0.0` test, the `ANNOTATED` row comes out, and the removal is itself
gated. If you prefer the other option — re-annotate as a clearance — the first
section below is the evidence to weigh, and only the first commit hunk needs to
go.

## The division is reachable, and the input is already in the tree

Not argued from the shape: reproduced against the shipped `libta-lib.a`.

The series is the one `test_composite2.c:1448` already carries for ER's
zero-denominator leg (`1e16, 0, -1, -2, -3, -4, -4, …`). At period 5, on `dev`:

```
TA_KAMA rc=TA_SUCCESS  begIdx=5  nbElement=59   →  58 of 59 outputs non-finite
first bad bar: -inf, every bar after it: NaN
```

Every period 2..12 reproduces it (51–58 non-finite outputs each). The corpus was
one `TA_KAMA` call away from finding this: the input that demonstrates ER's guard
demonstrates KAMA's missing one, and nothing called it.

Why one bar becomes 58: KAMA divides by its own carried state. `sumROC1` feeds
the ratio, the ratio feeds the smoothing constant, and that feeds `prevKAMA` —
so a single `+Inf` poisons the recursion and never leaves. That is the contrast
`divisor_guard_suite.rs` already draws for VWMA, whose unguarded division is
cleared *because* VWMA carries nothing across bars. The same reasoning does not
reach KAMA, which is what makes this a fix rather than a second `ANNOTATED` row.

The mechanism is ER's, unchanged: `sumROC1` is maintained subtract-then-add, so a
term absorbed on the way in is subtracted later at full precision and the sum
reaches exactly 0.0 with live terms still in the window. The `nullRun` purge
answers only the *flat* window. The clamp cannot answer at all — it compares
against the SIGNED numerator, so on a down move `0.0 <= negative` is false for
every value of the denominator, zero included.

## The change

One clause, at all three sites (priming, unstable-skip loop, steady loop):

```c
-   if( sumROC1 <= periodROC )
+   if( sumROC1 <= 0.0 || sumROC1 <= periodROC )
```

Written at the priming site too, where it is unreachable — a priming sum only
ever has non-negative terms added, so reaching 0.0 means every term was 0.0 and
then `periodROC` is 0.0 and the clamp answers first. `er.c` made the same choice
for the same reason: all sites read as one rule.

This is `ta_codegen/input/kama/kama.c`, so all four backends and every tier pick
it up from the one body — including C's streaming `Peek`, which carries
`sumROC1` in the handle (`sp->sumROC1`).

## What it costs

**Nothing on ordinary data — measured, not asserted.** Two full builds, `dev` at
`0234625a` and the same tree with this patch. `TA_KAMA` and `TA_ER` over a
deterministic 4096-bar random walk (with a flat run and a gap), periods 2..60,
FNV-hashing every output byte plus each `outBegIdx`/`outNBElement`:

| arm | corpus hash | values | non-finite |
|---|---|---|---|
| `dev` | `e2b8ee8785a3eddd` | 479,670 | 0 |
| patched | `e2b8ee8785a3eddd` | 479,670 | 0 |

Byte-identical. The same harness on the zero-denominator series is what makes
that discriminating rather than vacuous — it separates the arms exactly where it
should:

| arm | hash | values | non-finite |
|---|---|---|---|
| `dev` | `ab8a887aab50cc24` | 1,254 | **610** |
| patched | `03af22b0a5b69e6c` | 1,254 | **0** |

**Where output does change**, it changes from garbage to 1.0, on two paths:

- `sumROC1 == 0.0`: currently `±Inf` then permanent NaN. Strictly a fix.
- `sumROC1 < 0.0`: the sum goes *negative* when the subtract removes a term the
  add never really deposited. The old code then divides by it quite happily and
  gets a **finite** answer — so this is the one path where the patch replaces a
  finite number with a different finite number.

  This one I isolated rather than argued, because it is the only reason to
  hesitate. Search found a 45-bar series whose first dividing bar has
  `sumROC1 = -1.0`, `periodROC = -3.0`; on the shipped library:

  | bar | `dev` | patched |
  |---|---|---|
  | 19 | -0.960007751632178 | -0.960007751632178 |
  | **20** | **-8.1010420112672996** | **-1.8666709731289874** |
  | 21 | 13.255779347633002 | -1.9259283184049929 |
  | … | 22 of 35 outputs non-finite | 0 of 35 |

  So `dev`'s bar 20 is finite, and it is `fabs(-3.0 / -1.0)` = an efficiency
  ratio of **3.0** — a quantity defined on [0,1] — squared into the smoothing
  constant. I read that as garbage rather than signal, and the same series is
  NaN from bar 22 onward on `dev` anyway. But it is a behavior change on a
  currently-finite output, so it is your call, not mine.

**A cost I am not claiming to have measured:** I did not benchmark the added
comparison. It is one predictable `ucomisd` on a branch that already exists, but
`ta_bench` was not run for this, so I have not shown it is free.

## Gates

`divisor_guard_suite.rs`:

- KAMA's `OPEN` row is removed. `loop_accumulated_divisors_are_guarded_on_themselves`
  now covers KAMA on the merits.
- `the_sweep_detects_a_reintroduced_kama_defect` — the sabotage control #381 asked
  for, mirroring ER's. Kept as its own test rather than folded into a loop over
  the two: the point of #385 is that these are two bodies, and a parameterised
  test that silently found only one would read as covering both.
- `every_annotation_still_describes_a_reported_division` — new, and the reason it
  exists is this PR. KAMA's row had to be removed *by hand*, and nothing would
  have noticed if it had been left behind: it would have gone on suppressing a
  finding that no longer existed, and then suppressed the next one to appear on
  that divisor. This is the table's own stated argument ("a bare allowlist states
  nothing and silently keeps holding once its reason expires") finally asserted.
  It is scope beyond the one-clause fix; say the word and it comes out.

`test_composite2.c` gains leg (5), `test_kama_zero_denominator`, on the ER
series. Two claims, the second stronger than the first:

- every KAMA output is finite;
- **reconstructing KAMA from `TA_ER` is still bit-exact here.** Both bodies must
  answer 1.0 on the same bars for that to hold, so it checks the guards against
  each other, not just the arithmetic around them. `test_er_kama_reconstruction`
  could not see this: it runs on `history->close`, where the denominator never
  reaches zero.

Every gate here was watched failing, not assumed:

| control | result |
|---|---|
| revert the clause, run the suite | `loop_accumulated_divisors_are_guarded_on_themselves` **and** `the_sweep_detects_a_reintroduced_kama_defect` fail |
| re-add KAMA's row with the fix in place | `every_annotation_still_describes_a_reported_division` fails |
| revert the clause, rebuild, run `ta_regtest` | `KAMA zero-denominator Fail bar 6: -inf` |

## Verification

- `regen-check`: clean.
- `cargo test` in `ta_codegen/generator`: all suites pass.
- `ta_regtest` (full C suite): passes, hardcoded expected values unchanged.
- `ta_regtest --codegen --language=c,rust,java --function=KAMA`: all three agree
  with the frozen pre-cutover oracle. KAMA is inside the reference subset; ER is
  post-reference and is skipped there by design.
- **Not checked: C#.** No .NET SDK on the machine I ran this on, so
  `Core_KAMA.cs` was regenerated and read but never compiled or executed. It is
  the same one-clause rendering from the same IR as the three backends that were
  exercised, but I have not run it.
- Also not run: `scripts/synth_gate.py`, and `ta_bench` (see the cost section).

## Follow-on, not done here

The sweep is silent on the ratio's *upper* end. On a sustained decline the raw
`fabs` ratio can exceed 1.0 by a few ULP — `er.c` documents this and declines to
clamp it, because clamping with `fabs` would change TA_KAMA's output. That is a
separate question from the denominator and is not touched.
