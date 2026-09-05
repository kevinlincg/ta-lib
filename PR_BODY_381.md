gate(codegen): a division by a running sum must name that sum (#381)

Closes the PR-gate half of #381 — the check in the title — and fixes the one
function in the tree it finds.

## The rule

A window sum is maintained by add-then-subtract, so its value is not a function
of the window's contents alone. Absorption drives it to exactly `0.0` while a
live term is still inside the window, and a later subtract at full precision
lands it on `0.0` or below on a window nothing calls flat. Every predicate that
reasons about the *window* instead — a flat-bar counter, a comparison against the
numerator, a period clamp — is therefore a proxy: correct in exact arithmetic,
false in floating point, and the division it gates emits `NaN`/`Inf` from a
function that does not declare `TA_FUNC_FLG_NAN_INF_OUT`.

`ta_codegen/generator/src/domain_guard.rs` states it as one sentence: **a
division whose divisor is a `double` local holding nothing but a running sum
must sit under a condition naming that same sum against zero.** Direction is not
checked — `> 0.0`, `<= 0.0`, `TA_IS_ZERO(s)` and `!TA_IS_ZERO(s)` all satisfy it,
because which arm divides is the author's business and which *variable* is
tested is not.

It runs at generate time beside the naming and docs gates, and for the same
reason: all four backends transcribe one body, so they agree bitwise on the same
`NaN` and no cross-language comparison can see it. Both halves are PR-gate
today — `regen-check` runs `generate`, and `cargo test` on the generator runs
the suite.

## What it found: TA_KAMA

`TA_KAMA`'s efficiency ratio was gated by `sumROC1 <= periodROC` alone. That is
the *ratio clamp* — it pins an up-move to exactly 1.0 where FP would give
1.0000000000000002 — and it compares against the **signed** numerator, so it is
false for every down move. The `nullRun` purge beside it fires on a flat window;
a sliding sum reaches `0.0` on windows that are not flat.

On `[0, 1e16, 1e-300, 0, 0, 0, 0, 0]` with `optInTimePeriod = 4`, `sumROC1` is
exactly `0.0` at bar 6 with `nullRun == 3`, and `periodROC == -1e-300`, so the
clamp is false and the division runs. Against the library built from `dev` at
`4ca038a`:

```
rc=0 begIdx=4 nb=4
  out[0] (bar 4) = 0
  out[1] (bar 5) = 0
  out[2] (bar 6) = -nan     <-- TA_SUCCESS, and KAMA declares finite output
  out[3] (bar 7) = -nan
```

With this PR, bars 6 and 7 are `0`. `kama.c` now carries the same
`sumROC1 <= 0.0` denominator test `er.c` already has (#378), which is what keeps
the two ratios bit-identical on the windows that reach it — `er.c`'s comment
claimed KAMA had no equivalent, and that is now stale and deleted rather than
re-synced.

**Say plainly what the reproducer costs:** it needs a ~1e16 dynamic range inside
one window, which is not a realistic price series. It is a legal `double` input
that a function declaring finite output answers `NaN` to, not a bug anyone is
hitting in the field. The *rule* is what the two shipped defects in #381 were
about; KAMA is the third instance of it, and it is in code that predates the
cutover.

## Why the analysis is shaped the way it is

Every hop only ever **excuses** a division, so widening makes the gate quieter
and a narrowing is what would make it wrong:

- **"Nothing but a running sum."** A name accumulated into somewhere and reloaded
  from an input array elsewhere is a scratch register, not a sum. Every
  Hilbert-transform function has one called `tempReal`; without this clause the
  gate reports four of them and gets turned off.
- **The copy hop.** `curTR = sTR; … curVMP / curTR` is the shape `VORTEX` uses,
  so a set that does not follow the copy analyzes nothing in exactly the
  functions this is for.
- **The alias hop.** `tempReal = fabs(imagPart); if( tempReal > 0.0 )` is a guard
  on `imagPart` (`HT_DCPHASE`, `HT_SINE`, `HT_TRENDMODE`).
- **`double` locals only.** A parameter is the caller's value and an `int` is a
  count; `MACD`'s period swap would otherwise read as an accumulation.

Not analyzed, deliberately: a divisor that is an expression or an array element,
and guard *polarity*. A gate that reports maybes is a gate people learn to skip.

## Controls

`tests/domain_guard_suite.rs` — six tests, every one driven by mutating a body
and asserting the answer changes, because a gate that only runs over a corpus it
already passes proves nothing:

- **`deleting_kamas_denominator_test_is_reported`** — reads the live `kama.c`,
  deletes `sumROC1 <= 0.0 || `, and requires the report to come back. This is the
  control that was watched red: before the fix in this PR, `generate` itself
  failed with three `KAMA:` findings.
- `a_guard_on_the_numerator_does_not_count_as_one_on_the_divisor` — the shape
  that shipped. It *is* a guard and it *does* enclose the division; separating
  this from "is the division inside an `if`" is the whole point.
- `a_copy_of_the_sum_is_still_the_sum`, `a_reloaded_scratch_register_is_not_a_running_sum`,
  `an_early_return_guards_what_follows_it` — each asserts both directions.
- `every_shipped_indicator_gates_its_running_sum_divisions` — the corpus sweep,
  with a floor on the indicator count so an empty discovery cannot pass it.

## Verified

- `cargo test` (generator): 432 + suites, all green, `domain_guard_suite` 6/6.
- `cargo clippy --all-targets -- -D warnings` on the generator and on the
  generated crate: clean.
- `cargo test --doc -p ta-lib` (613) and `cargo test --tests -p ta-lib` (7): green.
- `ta_regtest` (C reference, cmake build of this branch): **all tests succeeded**,
  including the KAMA-reconstruction differential in `test_composite2.c` and
  `LEGACY,064,FROZEN`. The reference corpus never reaches the guard, which is why
  the value change is invisible to it.
- `generate` twice over: fixpoint, 15 tracked files changed and nothing else.
- The `NaN` and its disappearance were measured against a real `libta-lib.so`
  built from this tree, before and after, not from a model of the recurrence.

**I did not run** the cross-language leg (`ta_regtest --codegen`) or
`scripts/regtest.py`: they need `bin/ta_ref_serve` from the pinned pre-cutover
worktree plus a JDK and the .NET SDK, and `scripts/build.py` refuses to run in
this container. All four backends transcribe the one changed body and the
generated diffs are the same three-site edit in each, but the four servers were
not driven against each other here.

## Not in this PR

The nightly **guard-mutation** leg proposed in #381's body — regenerate per guard
with `TA_IS_ZERO` ⇄ `TA_IS_ZERO_SCALED` substituted and require RED. That is the
half aimed at *tests that cannot fail*, it is a different cost profile
(regenerate-and-run per guard, nightly), and it belongs in `scripts/synth_gate.py`
shape rather than here. This PR is only the static check the issue title asks for.
