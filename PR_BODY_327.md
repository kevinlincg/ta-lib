test(gate): the corpus-wide sweeps that fail against the fixtures, and the SYNTH13 defect behind one (#327)

Steps 1 and 3 of the order in #327, plus the question it left open on SYNTH13.
Step 2 is still yours, and step 4 (the `cargo test` leg in `synth_gate.py`) is
deliberately NOT here: two sweeps are still red, and a leg with an allowlist is
the thing the issue rightly calls worse than the gap.

Rebased onto `dev` `cc52aea9`. Everything below was measured on that base in this
run; no number is carried over from an earlier one.

## The issue says six sweeps fail. On this base it is five

`02ee797e` (TA_SUPERTREND) fixed `rust_batch_impl_orders_capacity_before_aliasing`
from the shipped side while this branch was out, and it is green on `dev` now.
An earlier revision of this branch fixed the same defect independently; **that
commit is dropped and dev's version taken verbatim**, because dev's is stronger.
Both taught the expected-guard builder the emitter's cross-type skip. Where this
branch answered `None` and then asserted nothing further about such a function,
dev scans it anyway and asserts the guard's **absence** — so a generator that
started comparing `*const f64` against `*const i32` would now be caught, where
this branch's version would have skipped past it.

The reason it is worth saying out loud: that is the second time a fixture-only
finding in #327 turned out to be reachable from the shipped corpus as soon as one
function of the right shape arrived. SUPERTREND is the first shipped mixed
real+integer output, and it landed on exactly the sweep the fixtures had been
failing.

## 1. SYNTH13: a real defect, and the gate's needle does not over-match

`an_opener_never_answers_the_code_its_sub_call_handed_back` names the shape
exactly. Leg C puts `savedRetCode = retCode;` between the `sma()` call and its
guard; `drop_answered_cross_call_guards` stops its forward scan at any statement
that MENTIONS the code, so the guard survives — and the surviving guard is
reachable only with `SUCCESS`, because the call site already answered the
rejection. What it returns is therefore `Err(RetCode::Success)` in Rust, and an
empty-range handle in Java and C#: rule S7's case, and the one #271 item 4 was
about.

The fix is at the root rather than at the return: the scan's rule was always
"the variable could have changed", and a read is not that. It now walks past a
statement that only reads the code and stops at the three ways one can write it
— assigned or declared under that name, address taken, incremented — with an
unclassified statement variant answering "stop", exactly as the old rule did for
every mention. The intervening statement is untouched; the guard folds away as
leg A's does.

`synth13.c`'s leg-C comment and `synth13.md` are updated with it: what the leg
covers is now "a read does not stop the fold", and the WRITE cases are pinned in
`backends::ir_cleanup`'s own tests, for the reason the fixture already gives for
control flow. The "coverage trap" bullet moves leg C from the guard-survives half
to the guard-folds half, which is the one thing in that bullet a reader could
otherwise take on trust and be wrong about.

**Cost:** the fixture no longer exercises the scan's refusal end-to-end through
the four emitters. Leg D still does (an `==` test is refused, and its guard is
emitted in every backend), so what is lost is the specific refusal REASON, not
the refused-guard rendering. A fixture cannot carry the write case with the guard
body these legs have: a surviving guard that returns the code is precisely what
S7 forbids here.

## 2. Two gate helpers that model the shipped corpus

- **`no_throwing_sub_call_follows_the_cur_capture_in_a_java_step`** pinned the
  number of multi-output handles driving a sub-stream as a literal — a corpus
  count, in a suite that also runs against an injected `input/`. It is now
  derived from the IR (a composed plan with at least one sub) and compared
  against the set the emitted Java carries, in both directions. That is stronger
  than the literal was: a plan with a sub whose call the step stopped emitting
  now fails, where before it only moved a number. The literal survives as a
  FLOOR (`>= 6`) rather than being dropped — the shipped six are the minimum and
  an injected corpus only adds — so the sweep cannot go vacuous either. It also
  no longer skips a multi-output step that captures no `cur_*`; that shape
  satisfied the order assertion vacuously.

- **`every_integer_output_carries_an_example_claim`** swept every directory under
  `input/`. Its subject is the PUBLISHED rustdoc example — the domain is
  hand-written per function in `rust_doc::integer_domain_claim`, and a fixture's
  example reaches no crates.io reader, which is the reason the doc comment there
  already gives for this being a test rather than a panic. So fixtures are
  subtracted, by reading `input_synth/` rather than by matching a name pattern,
  and the exact count of shipped integer-output functions stays pinned.

  This composes with, rather than replaces, the fix `02ee797e` made to the same
  sweep: that one decides WHICH claims count (naming the integer output's own
  variable), this one decides which functions are asked. Worth noting that the
  two interact — on this base the offender the sweep reports is `synth6`, where
  the issue reported `synth3`, because dev's stricter counting changed which
  fixture trips first.

  This is the one place I made a judgement call you may want to reverse: the
  alternative is a claim for the fixture in `integer_domain_claim`, which would
  be the first mention of a gate fixture in shipped generator source.

`gate_fixtures()` lands in `tests/common/mod.rs` rather than in one suite file:
since `backend_suite.rs` was split, the two sweeps that need it are in different
test binaries (`update_and_fill_suite` and `rust_render_statement_suite`).

## 3. `synth_gate.py`'s docstring

It said six sweeps fail on an injected tree. That was already wrong before this
branch (SUPERTREND made it five) and is wrong after it (two). It now names what
remains — the peek pair, on SYNTH3's integer state array — and why the leg is not
added yet, instead of carrying a number that goes stale every time either side
moves.

## What is still red

`no_java_peek_copies_the_handle` / `no_csharp_peek_copies_the_handle`, on this
base, with fixtures injected and regenerated:

```
synth3: clones the handle's ring: int[] ring = sp.ring.clone();
synth3: int[] ring = new int[sp.ring.Length];
```

which is the message the issue quotes. These gates forbid the copy outright —
there is no "bounded clone is acceptable" clause in either sweep on this base —
so the contract question the issue poses, (a) permit a bounded clone for a
literal-sized array and pin the set, versus (b) teach the emitter an N-slot
pending-write shadow, is genuinely unanswered in the code and is yours.

**One correction to an earlier revision of this PR body, which I am flagging
rather than quietly dropping.** It claimed the clone was already accepted as
bounded and that what failed was a second condition — "no accumulator store is
refusable". No such clause exists in either sweep on `cc52aea9`; I read both.
Treat that paragraph as withdrawn.

There is also an option (c) — offer integer state arrays a shadow on the same
terms as real ones, via the `int_elem` the `circ_storages` loop already passes.
It was tried against a much older base (`69284b1d5`) and the result then was:
Java's clone disappeared in favour of the `(pkSlot0, pkVal0)` pending pair, and
Rust did not compile — 10 errors, all `expected i32, found usize`, because the
ctx-aware integer typing casts a direct `ring[head]` read into the index domain
and does not see through the shadow's ternary. That would put the cost in
`rust_lang`'s i32/usize election, the machinery #159/#163/#165 were each about,
and it buys nothing shipped. **I did not repeat that experiment on this base**,
and given the correction above I would not act on those numbers without
re-running them. It is offered as a lead, not as data.

## Verification

**Shipped corpus, no fixtures** (this is what the PR gate sees):

- `cargo test --no-fail-fast`: green, 0 failed.
- `cargo run -- generate` then `git status`: **clean**, 178 functions. The
  `ir_cleanup` change is therefore byte-identical on every shipped function —
  no shipped function puts a read between a cross-call and its guard.

**Injected corpus** — all fourteen `input_synth/synth*` copied into `input/`,
regenerated, nothing exempted. Two trees differing only by this branch's two
commits:

| tree | passed | failed |
|---|---|---|
| `dev` `cc52aea9` | 880 | **5** |
| this branch | 885 | **2** |

The five on `dev` are `an_opener_never_answers_the_code_its_sub_call_handed_back`,
`every_integer_output_carries_an_example_claim`,
`no_throwing_sub_call_follows_the_cur_capture_in_a_java_step`, and the two peek
sweeps. The two left are the peek sweeps.

**The new assertion, watched red under a deliberate break.** The break was
applied to this branch's tree and reverted afterwards. Emitted-side sub-stream
needle mangled (`sp.sub` → `sp.subZZZ`):

```
assertion `left == right` failed: the Java steps that drive a sub-stream are not
the composed plans that have one
  left: []
 right: ["bbands", "kc", "macdext", "stoch", "stochf", "stochrsi"]
```

Direct evidence for the Category-1 claim, from the `dev` control run above:
`7 multi-output handles drive a sub-stream, expected 6` — SYNTH14 is the seventh,
and the derived set on the injected tree is the shipped six plus it.

**Stated rather than implied — things I did NOT do here:**

- **The `ta_regtest` cross-language legs were not run.** `scripts/build.py`
  refuses to run as root, which is what this environment is; I confirmed that
  this run rather than assuming it. So `--xlang-hash --function=SYNTH` and
  `--codegen --function=SYNTH` have not been run against this base by me.
- **The C# server was never built or run** — no .NET SDK in this environment
  (confirmed) — so the C# side rests on the generated text and on the nightly.
- `scripts/synth_gate.py` was not run end to end, nor `--fuzz-064`, nor any
  benchmark. There is no performance claim in this PR.
- I did not re-run the option (c) experiment, as stated above.

One operational note, since it cost me a diff to notice: injecting the fixtures
in-tree and regenerating advances the append-only counter in
`ta_codegen/input/internal_error_ids.yaml` (`next: 403` → `407`), because the
run assigns IDs to the fixtures' guards. `synth_gate.py` does this in a throwaway
worktree so CI never sees it; a local in-tree run leaves it modified and it has
to be reverted before committing. It is not part of this branch.
