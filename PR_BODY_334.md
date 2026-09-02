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

## Still unreachable after #333 — measured, not assumed

Branched before #333 landed and merged onto `dev` afterwards, so the
interesting question got re-asked on the tree that carries the purge: **the
whole emitted tree is still byte-identical**. `git status` after a full
regeneration shows nothing, and `regen-check` is clean.

STOCH and STOCHF are the only composed producers carrying temps, one each, and
the purge does not orphan either — so the filter still has nothing to drop and
this stays a cleanup that arms a path rather than one that clears a warning.
The six warnings #328 was about are gone with #333; this adds none and clears
none.

Because the emitted tree is identical to `dev`'s, **the C build here IS `dev`'s
build**, and I did not re-run it to claim otherwise.

## Gates

No gate is added. `dev` grew the right one with #333 —
`no_peek_frame_declares_a_local_nothing_reads`, which sweeps every peek frame in
the corpus and reads the composed frames along with the rest. It is green
before this change and after it, for the corpus reason above, so it does not
discriminate the fix; what it does is make the path this change arms loud
instead of silent the day something orphans a composed producer temp.

I wrote a second sweep of my own before that merge — same corpus, keyed on a
local being NAMED rather than read — and dropped it rather than push it:
#333's is strictly stronger (never named implies never read), so it would have
been duplication whose only distinguishing case is a declaration with an
initializer, which #333 excludes deliberately.

Both arms of the merged gate were watched red, one sabotage at a time, on this
branch's tree:

| sabotage | reddens |
|---|---|
| `frame_temps`' `StepFrame::Peek` arm returns `temps.to_vec()` — the #334 shape, applied to both tiers | 26 locals: 14 candlesticks' `totIdx`, `CORREL`'s `trailingX`/`trailingY`, `HT_PHASOR`'s and `MAMA`'s `jI`/`jQ`, and the rest |
| a temp the composed peek frame never names, injected into the producer model, with the composed call to `frame_temps` bypassed | 2 locals: `STOCH`, `STOCHF` — and green with the filter in place, which is the composed arm's own control |

The second row is the honest form of "no shipped function exercises this": the
corpus cannot supply an orphaned composed temp today, so the control injects
one.

**What remains ungated: a THIRD site being added that decides for itself
again.** One decision point is the whole defence there, and no test in the tree
enforces that a future emitter routes through it.

I also did **not** add a `synth<n>` fixture with a composed producer that
orphans a temp, which is the one thing that would make this reachable end to
end. Per `input_synth/README.md`, the synth gate's legs are value and parity
gates and the generator's own static sweeps never see an injected tree (#327
carries that classification) — so a fixture would exercise the construct
without ever checking the property, and the C leg would emit the warning
without failing, since the build is deliberately not `-Werror`.

## Verified

On `dev` `19286097` (current tip, merged in — the branch is mergeable as is):

- `generate` then `git status`: nothing — the emitted tree is byte-identical
- `regen-check`: clean (source lists, S1/S7 return codes, input formatting, regeneration)
- generator suite: 29 suites, 0 failed, including the two new `frame_temps` unit tests
- `cargo clippy --all-targets`: clean

## NOT run

- **The C, Rust, Java and C# builds, and every cross-language leg.** The emitted
  tree is byte-identical to `dev`, so there is nothing for them to measure that
  `dev`'s own CI has not already measured. Stated rather than quietly skipped.
- **Every benchmark.** This changes no emitted code, so there is no performance
  claim to make.
