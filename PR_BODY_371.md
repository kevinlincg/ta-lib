feat(fractal): FRACTAL — Williams Fractal, strict on both arms (#371)

Implements #371 as ruled: `TA_FRACTAL`, two integer 0/100 outputs flagging
whether the bar `optInRightBars` back was a **strict** local high / low of its
`optInLeftBars + 1 + optInRightBars` window. Strict on both sides — a candidate
that ties a neighbour on either arm raises nothing.

## Shape

`ta_codegen/input/fractal/` — `fractal.yaml`, `fractal.c`, `fractal.md`. Nothing
hand-edited under `src/` or `ta_codegen/output/`; every other file in the diff is
`generate` output.

- Group `Momentum Indicators`, input bundle `inPriceHL`, flags `[stream]`.
- `optInLeftBars`, `optInRightBars`: `1..100000`, default 2 (Williams' five-bar
  rule), independent, so an asymmetric pivot needs no post-processing.
- `outSwingHigh`, `outSwingLow`: `type: integer`, 100 or 0. MINMAXINDEX is the
  precedent for two integer outputs; the corpus goes from 66 integer-output
  functions to 67, and from 67 integer outputs to 69.
- `fractal_lookback() == optInLeftBars + optInRightBars`, closed form.
- The verdict is reported at the confirmation bar `candidate + optInRightBars`,
  which is what makes the function causal and is where both oracle libraries
  report it.

The body rescans the window per bar. A cached running extremum cannot answer this
question: the candidate sits in the middle of the window, and a tie has to be
distinguished from a strict win. `stream-census` derives **T4, state=4, lags=0**
(the extrema automaton) — one paced top-level index, everything else derived from
it inside the loop, which is the shape the analyzer accepts.

## Verification

Run in this order; the first two carry a control I broke and watched fail.

1. **External oracle, bitwise.** The confirmation-bar flags frozen in #371,
   captured there from ta4j 0.22.6/0.24.1 and trading-signals 8.3.0 over bars
   4..14 of the 252-bar SREF corpus at (2,2). The leg first checks the history it
   was handed IS the data the goldens were captured on, then compares. This is
   the only leg that ties the two *conventions* — the confirmation-bar anchor and
   strictness — to something outside this repo.
2. **Differential over the whole corpus, bitwise, against shipped MAX/MIN**, at
   the eight (left,right) pairs #371 measured, including both asymmetric ones and
   both period-1 arms: a candidate is a swing high exactly when its high beats
   `MAX(high, left)` at bar `c-1` and `MAX(high, right)` at the confirmation bar;
   mirrored on MIN for the low. A period of 1 is outside TA_MAX's range and is
   read straight from the input. Floors on both the comparison count and the
   number of 100s raised, so an all-zero agreement cannot pass.
3. **Range coherency**: `FRACTAL(100, n-1)` bar-for-bar against `FRACTAL(0, n-1)`.
   Registered in `test_codegen.c`'s `exact[]` (`TA_STABLE_EXACT`): comparisons
   only, no accumulator, no unstable period.
4. **Hand-built discriminators**: a candidate that ties its right arm (this is the
   case, and the only one, that separates the ruled strict rule from the
   `>`-left / `>=`-right variant), an outside bar that raises both flags, all-flat,
   a monotone ramp, exact-fit and one-bar-short windows.
5. **Output-distinctness rejection** (#108). No in-place aliasing leg exists and
   none is possible: both outputs are `TA_Integer`, both inputs `TA_Real`.

Controls, each broken deliberately and observed red before reverting:

- Right arm relaxed to non-strict (`>=` → `>`): leg 2 fails at `L=1 R=1` bar 66,
  and leg 4's tie case fails with `got (100,100), expected (0,0)`.
- Everything green again after reverting, from the same build.

Gates run green: `ta_regtest --function=FRACTAL`; `ta_regtest --codegen
--language=c,rust,java --function=FRACTAL` (structural + stream legs; the value
sweep correctly reports no frozen-reference baseline for a post-cutover
function); `ta_regtest --xlang-hash --language=c,rust,java --function=FRACTAL`
(2055 cases per server, 0 mismatches, bit-identical); the full unfiltered
`ta_regtest`; `build.py check-source-lists`; `build.py regen-check`;
`build.py clippy`; the generator suite; `cargo test --doc/--lib/--tests -p
ta-lib`; `cargo doc --no-deps` warning-free.

## What this costs, and what I did not check

- **C# was not run here.** This environment has no .NET SDK, so `--xlang-hash`
  and `--codegen` covered C, Rust and Java only. The C# source is generated and
  is in the diff; the PR gate's "Generated C# compiles" is what covers it. I did
  not execute a single C# call.
- **The external oracle is the issue's capture, not a fresh one.** I have no
  network access to ta4j or trading-signals from here, so leg 1 freezes the
  confirmation-bar lists #371 already measured, over 11 bars at one parameter
  pair. Whole-corpus coverage comes from leg 2, which is an independent
  *decomposition* but shares this PR's strictness convention — so strictness
  rests on the ruling plus leg 4's tie case, not on a second capture.
- **I could not read the issue's comment thread.** The GitHub API is blocked from
  this environment and the rendered page did not carry the comments, so I worked
  from the issue body and its RULED banner. If a comment narrowed anything
  further, it is not reflected here.
- `TA_FuncId` in the Rust crate is dense and alphabetical, so FRACTAL landing
  between FLOOR and HMA renumbers the variants after it — as every new function
  does.
- One new `ta_regtest` error code (`TA_FRACTAL_ORACLE_VACUOUS = 1667`), one
  `internal_error_ids.yaml` append (`FRACTAL.extrema`; see the refresh section
  below for the number it currently holds).
- Cost per bar is `O(left + right)` comparisons, deliberately: the rescan is what
  makes the strict-vs-tie distinction cheap to state and impossible to get subtly
  wrong. At the default (2,2) that is four comparisons per output bar.
- The rustdoc example claim for the two integer outputs is a domain claim
  (`0 or 100`) rather than a relation: which bars carry the 100 is data-dependent,
  and on the doctest's synthetic series a strict rule may legitimately raise none.

## Refreshed onto dev (dev at aebff428, ERI #361)

The branch was cut before CVI/MASSI (#358, #359), COPPOCK (#362), CUMSUM (#372),
VORTEX (#349) and ERI (#361) landed, and has been merged forward onto each. Only
one number quoted above moved, and it moved twice, both times for the same
reason — a function that landed first took the id FRACTAL had been assigned:

- `FRACTAL.extrema` is now **426** (it was 420 when written, then 424, then 425).
  On the last merge, upstream ERI had taken **425**, so both keys read 425 and
  two different guards would have reported `5425`. The renumber is never
  hand-applied: dropping the stale ledger entry and regenerating hands out the
  next free id (`next: 427`), and the `TA_INTERNAL_ERROR(...)` in every generated
  tier follows.
- `TA_FRACTAL_ORACLE_VACUOUS = 1667` is **unchanged** and still free on dev.

Every conflict any of these merges raised was in a generated tier, so all were
resolved by taking dev's side and regenerating — no hand-edited artifact.

Re-run after the ERI merge:

- `build.py regen-check` — green, so regeneration is idempotent over the merged
  tree. It discriminates: the same command over the un-renumbered tree carries
  the duplicate id through unchanged, which is why the check below is the one
  that catches it.
- `one_id_names_one_guard` (generator suite) — I re-introduced the duplicate
  deliberately and watched it fail with `ids allocated to more than one guard:
  [(425, ["ERI.dualmode", "FRACTAL.extrema"])]`, then reverted. This is the gate
  that catches the collision class, and the PR gate runs it.
- the full C reference suite over a fresh CMake build — all tests succeeded,
  `FRACTAL` included.
- the function corpus goes 197 -> 198 against dev, adding exactly `FRACTAL` (the
  large `ta_func_api.c` diff is the packed byte array reflowing, not content
  loss).

**I did not re-run**, after this merge, any `--xlang-hash` leg, the
cross-language `ta_regtest --codegen` run, `clippy`, the Rust doctests, or the
Java and C# builds.

**Ordering note.** ER (#350, PR #378) still holds internal-error id **425** on
its own branch, the number ERI took when it merged. That collision is dev's to
resolve at merge time by the same mechanism used here; it is not visible on
#378's own tree, only in the merge result.


Closes #371.
