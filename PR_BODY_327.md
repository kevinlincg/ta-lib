test(gate): four of the six corpus-wide sweeps that fail against the fixtures (#327)

Steps 1 and 3 of the order in #327, plus the SYNTH13 question it left open. Step
2 is still yours, and step 4 (the `cargo test` leg in `synth_gate.py`) is
deliberately NOT here: two sweeps are still red, and a leg with an allowlist is
the thing the issue rightly calls worse than the gap.

Reproduced first on `dev` (`950809b4b`) by the recipe in the issue — inject all
`input_synth/synth*`, `generate`, `cargo test --no-fail-fast`. The same six fail
there, and the two messages that differ from the issue's differ only because
that tree carries functions the measurement did not; one differs in substance,
see **What is still red**.

Note on the recipe: the `generate` step is load-bearing, not a convenience.
Skipping it adds a seventh failure — `xml_suggested_matches_the_declaration`,
`SYNTH6 missing from ta_func_api.xml` — because that sweep reads the COMMITTED
`ta_func_api.xml` rather than generating one. It is an artifact of the shortcut,
not a defect, and it is not touched here.

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

## 2. Three gate helpers that model the shipped corpus

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

- **`rust_batch_impl_orders_capacity_before_aliasing`** rebuilt the expected
  guard over every output PAIR, while the emitter skips cross-typed pairs
  (Appendix E, #262). On SYNTH12's `f64, i32, f64` it demanded a guard the
  emitter is right not to emit. The builder now mirrors the same-type rule and
  answers `None` when no pair survives; B5-before-B6 is asserted where a guard
  exists, with a floor on how many carried one so that half cannot go quiet.

- **`every_integer_output_carries_an_example_claim`** swept every directory under
  `input/`. Its subject is the PUBLISHED rustdoc example — the domain is
  hand-written per function in `rust_doc::integer_domain_claim`, and a fixture's
  example reaches no crates.io reader, which is the reason the doc comment there
  already gives for this being a test rather than a panic. So fixtures are
  subtracted, by reading `input_synth/` rather than by matching a name pattern,
  and the exact count of shipped integer-output functions stays pinned (65).
  This is the one place I made a judgement call you may want to reverse: the
  alternative is a claim for SYNTH6 in `integer_domain_claim`, which would be the
  first mention of a fixture in shipped generator source.

`gate_fixtures()` lands in `tests/common/mod.rs` rather than in one suite file:
since `backend_suite.rs` was split, the two sweeps that need it are in different
test binaries (`indicator_variants_suite` and `rust_render_statement_suite`).

## What is still red, and what I found trying to fix it

`no_java_peek_copies_the_handle` / `no_csharp_peek_copies_the_handle`. The
message is not the one in the issue:

```
synth3: clones {"ring"} but no accumulator store is refusable
```

The clone is already ACCEPTED as bounded — the gate permits `.clone()` for an
array the batch body declares with a literal size, which is option (a) of the
issue's contract question, already in place. What fails is the second condition:
a frame may only clone where a `validate_peekable` refusal justifies it, and
SYNTH3 has no refusable store. It has none because `validate_peekable` was never
asked: `transition_buffers_with_state_arrays` excludes integer arrays outright
("none exist in the corpus, so an integer shadow would be ungated"), so SYNTH3's
`int ring[4]` is never offered a shadow and the frame localizes it as a written
array field — which in Java and C# means cloning the reference. The computed
store index is not the obstacle.

That makes a third option available, and it was tried: offer integer state
arrays on the same terms, carrying the element type the `circ_storages` loop
already passes for an integer CIRCBUF (`int_elem`). Measured then:

- Java: the clone is gone, replaced by the `(pkSlot0, pkVal0)` pending pair, and
  every read of `ring[head]` becomes the ternary. The jar's own suites passed
  (`StreamSmokeTest` 4027 checks, with the fixtures injected).
- **Rust did not compile**: 10 errors, all of one kind —
  `synth3.rs:868: expected i32, found usize` on
  `(if (head as usize) != pkSlot0 { sp.ring[head] } else { pkVal0 }) + 1 <= sp.slot`.
  The ctx-aware integer typing casts a direct `ring[head]` read into the index
  domain and does not see through the shadow's ternary.

So option (c) costs a change in `rust_lang`'s i32/usize election — the machinery
#159/#163/#165 were each about — and it buys nothing shipped either. I stopped
there rather than push an unverified change into that predicate: the choice
between (a) with the gate's justification clause relaxed, (b), and (c) is yours,
and this is the data I have for it.

**That experiment was run against the previous `dev` (`69284b1d5`) and has NOT
been repeated on this base.** Nothing in this PR depends on it — it is reported
as the reason step 2 is left to you, not as a change.

## Verification

On this base, with all fourteen fixtures injected AND regenerated, nothing
exempted:

- `cargo test --no-fail-fast`: only the two peek sweeps above fail. The control
  is the same tree at `dev` `950809b4b` with the same fixtures injected and
  regenerated, where six fail — the four fixed here plus those two.
- `cargo test --no-fail-fast` with no fixtures: all suites green.
- `generate` with no fixtures leaves the tree clean, so the shipped corpus's
  emitted output is byte-identical: no shipped function puts a read between a
  cross-call and its guard.
- Change footprint, taken as a file-level diff of the two regenerated trees
  (control vs this branch, both with fixtures): `SYNTH13` only, in Rust, Java and
  C#, plus the two whole-corpus Java surfaces that embed its fragment. `src/`
  — the C library — is byte-identical, as expected: a C cross-call is cross-TU,
  so the guard is live there and nothing folds.

Stated rather than implied — things I did NOT do here:

- **The `ta_regtest` cross-language legs were not re-run on this base.**
  `--xlang-hash --function=SYNTH` and `--codegen --function=SYNTH` were run
  against the pre-rebase base (`69284b1d5`) and passed there; `dev` has moved 59
  commits since, so I am not carrying those numbers forward as current. This
  environment refuses `scripts/build.py` outright when it runs as root, which is
  what blocked re-running them.
- **The C# server was never built or run** — no .NET SDK here — so the C# side
  rests on the generated text and on the nightly.
- `scripts/synth_gate.py` was not run end to end, nor `--fuzz-064`, nor any
  benchmark. There is no performance claim in this PR.

## Controls

All of these were run in this environment on this base, not carried over.

**The four sweeps, red before and green after.** Two trees, identical except for
this branch's two commits, both with all fourteen fixtures injected and
regenerated:

| tree | passed | failed |
|---|---|---|
| `dev` `950809b4b` | 879 | **6** |
| this branch | 885 | **2** |

The six on `dev` are exactly the six #327 names —
`an_opener_never_answers_the_code_its_sub_call_handed_back`,
`every_integer_output_carries_an_example_claim`,
`no_throwing_sub_call_follows_the_cur_capture_in_a_java_step`,
`rust_batch_impl_orders_capacity_before_aliasing`, and the two peek sweeps. The
two left are the peek sweeps.

**The two NEW assertions, watched red under a deliberate break.** Each break was
applied to this branch's tree and reverted afterwards:

- emitted-side sub-stream needle mangled (`sp.sub` → `sp.subZZZ`): the derived
  and emitted sets disagree, `left: []` against
  `right: ["bbands", "kc", "macdext", "stoch", "stochf", "stochrsi", "synth14"]`.
  That right-hand side is also the direct evidence for the Category-1 claim —
  the shipped six, plus SYNTH14 and nothing else.
- guard builder forced to answer `None`: trips the new floor,
  `only 0 bodies carried an aliasing guard — B6 is barely covered`.
