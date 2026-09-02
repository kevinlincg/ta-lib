fix(codegen): the composed C peek frame filters its producer temps too (#334)

Closes #334, exactly as specified there.

`emit_step_inner` filters the loop tier's peek declarations with `temps_used`,
and `temps_used`'s own doc says why: a peek frame DELETES statements, so a
declaration nothing then reads is `-Wunused-but-set-variable` under the C
build's `-Wall -Wextra` and CS0219 in the C# package, which builds with
`TreatWarningsAsErrors`. `emit_composed_frame_body` iterated `model.temps` raw,
for `StepFrame::Commit` and `StepFrame::Peek` alike. C was alone in it — Java,
C# and Rust each suppress their unfiltered block on a peek frame.

## The shape of the fix

The rule is now **one function**, `frame_temps`, and both C tiers take it.

That is the fix rather than an implementation detail of it: the defect was a
second site free to decide the same question for itself, so the repair is that
there is no second site — not a matching `match` copied into two places, which
is the same defect with a longer fuse.

The producer transition moves above the declarations it now filters, because
C89 puts declarations first and the filter needs the frame that will actually be
rendered. Its shadow locals are still pushed further down, at the point they
occupied before — and the byte-identical output below is what proves the
reordering did not disturb the emitted text.

## Unreachable on today's corpus — measured, not assumed

`generate` leaves **the whole tree byte-identical**: `git status` after a full
regeneration shows only `c_stream.rs`, and `scripts/build.py regen-check` is
clean. Not one emitted file moves.

STOCH and STOCHF are the only composed producers carrying temps, and every one
of theirs is read, so the filter has nothing to drop today. The path goes live
the moment a deletion pass orphans one — which is what #333 is about, so this
wants to land alongside or after it rather than being held for a corpus
difference it will not produce on its own.

Because the emitted tree is identical to `dev`'s, **the C build here IS `dev`'s
build**, and I did not re-run it to claim otherwise. `dev` currently carries the
six `-Wunused-but-set-variable` that #321's trim left and #333 removes; this
change neither adds to them nor clears any, and I verified those six are on
`dev` today by compiling `ta_CORREL.c`, `ta_HT_PHASOR.c` and `ta_MAMA.c` out of
a `dev` worktree with the CMake flags.

## Gates, and what is honestly not gated

**No corpus gate can discriminate this fix**, and I would rather say so than
add one that reads green either way. What the two unit tests pin is the RULE:

| test | asserts |
|---|---|
| `a_peek_frame_declares_only_the_temps_its_surviving_body_names` | a peek frame declares only what its surviving body names; a committing frame declares all of them |
| `a_temp_the_frame_only_writes_is_still_declared` | a store IS a mention, so retiring a write-only local is the store pass's job and not this rule's |

Each was watched red under its own sabotage, one at a time:

| sabotage | reddens |
|---|---|
| `StepFrame::Peek` arm returns `temps.to_vec()` (the #334 shape) | the first test |
| `StepFrame::Commit` arm filters too (the drift the rule forbids) | the first test |
| `temps_used`'s scan stops counting a plain `x = e` as a mention | the second test |

**What remains ungated: a THIRD site being added that decides for itself
again.** One decision point is the whole defence there, and no test in the tree
enforces that a future emitter routes through it.

I also did **not** add a `synth<n>` fixture with a composed producer that
orphans a temp, which is the one thing that would make this reachable end to
end. Per `input_synth/README.md`, the synth gate's three legs are value and
parity gates and the generator's own static sweeps never see an injected tree
(#327 carries that classification) — so a fixture would exercise this construct
without ever checking the property, and the C leg would emit the warning
without failing, since the build is deliberately not `-Werror`. A fixture is
worth having once that gap closes; as a gate for this PR it would be theatre.

## Verified

On `dev` `3c35cb24` (current tip; branched from it, no merge needed):

- `generate` then `git status`: only `c_stream.rs` — the emitted tree is byte-identical
- `scripts/build.py regen-check`: clean
- generator suite: **893 passed, 0 failed** (two of them new)
- `cargo clippy --release --all-targets -- -D warnings`: clean

## NOT run

- **The C, Rust, Java and C# builds, and every cross-language leg.** The emitted
  tree is byte-identical to `dev`, so there is nothing for them to measure that
  `dev`'s own CI has not already measured. Stated rather than quietly skipped.
- **Every benchmark.** This changes no emitted code, so there is no performance
  claim to make.
