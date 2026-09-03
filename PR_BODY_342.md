fix(codegen): a function without the `stream` flag is refused, not emitted (#342)

Dropping `stream` from a function's YAML is the one authoring mistake nothing
answered. `generate` accepted the definition and emitted every backend; the
failure surfaced four steps later, as four broken server builds. Each backend
reported a missing symbol and none reported a missing flag.

Measured on dev (`af4cdede`) with one flagless definition injected into
`ta_codegen/input/`, via `scripts/regtest.py`'s server build:

```
C:    ta_codegen_serve.c:75209: unknown type name 'TA_SYNTH15_Stream';
                                did you mean 'TA_SYNTH5_Stream'?
Rust: E0599: no method named `synth15_open` found for mutable reference `&mut Core`
      E0599: no method named `synth15_open_and_fill` found for `&mut Core`
Java: TaCodegenServe.java:202110: cannot find symbol (3 errors)
C#:   CS1061: 'Core' does not contain a definition for 'Synth15Open'
      CS0426: the type name 'Synth15Stream' does not exist in the type 'Core'
      CS1061: 'Core' does not contain a definition for 'Synth15OpenAndFill'
Error: 4 server build step(s) FAILED
```

The four servers reference `<N>_Open` / `_OpenAndFill` / `<N>Stream` for every
function they dispatch, so a flagless definition cannot compile anywhere — and
the diagnostic names a generated symbol in a 200k-line file rather than the
one-word cause in the YAML.

## What this changes

`generate` now exits 1 naming the function, sitting next to the streamability
gate the flag arms in the other direction:

```
error: SMA: no `stream` flag, and the JSON-RPC servers assume every function streams
       Declare `flags: [stream]` (run `ta_codegen stream-census` for the
       derived tier), or restore the servers' batch-only emitter path (#342).
```

`streaming_suite.rs`'s whole-corpus gate asserts the same property as a
**total** over `input/` instead of the floor over declared functions it
asserted before. That floor is why the state was reachable at all: it can only
see functions that already carry the flag, so the case it must catch is
invisible to it by construction.

The message states the tree's current answer rather than inventing one. The
batch-only emitter path was retired in `221fcd63` once the last batch-only
function gained the flag, so shipping a batch-only function now means restoring
that path first — which is a decision worth making deliberately rather than
discovering from a C# compiler error.

## What it costs

`flags: [stream]` becomes mandatory to generate at all. The "ship batch-only
first, add the flag as a one-line follow-up" sequencing (the PVO precedent, and
what #342 itself planned for DONCHIAN) is no longer available without restoring
the emitter path. That is the tree's state since `221fcd63`, not a new
restriction — but this PR is what makes it explicit, so it is yours to rule on.

The authoring flow is unchanged: `stream-census` still classifies a flagless
definition, which is how the flag gets authored in the first place. Verified —
with `stream` removed from `sma.yaml` it reports `candidate SMA T3 state=1
lags=0 outs=1`, and only `generate` refuses.

**The alternative, if you would rather keep batch-only shippable:** restore
`6e018348`'s `if func.streaming` guards and give them a permanent user — a
batch-only `input_synth` fixture, which is what they never had and why they
looked like dead code. That is a larger change in the emitters you just
simplified, and it reverses `221fcd63`, so it is not what this PR does.

## Verification

- Controls, both watched red. With `stream` removed from `sma.yaml`:
  `all_declared_functions_are_streamable` fails naming SMA, and
  `generate --func=SMA` exits 1 with the new message. Restored, both pass.
- `cargo test` green (404 + 8 suites), `cargo clippy --all-targets` clean.
- `scripts/build.py regen-check` reports the output byte-identical: no emitted
  file changes, since every shipped function declares the flag. The new error
  path is unreachable for the current corpus, which is the point.

**Not checked:** I did not re-run `scripts/synth_gate.py` after the final edit.
It ran on this dev base during the investigation (that is where the four
compile errors above came from), all 14 fixtures declare `stream`, and
regen-check reports no output change, so nothing in its path moves — but I did
not confirm that by running it. I also did not run the nightly legs.

## Also

`.claude/skills/new-ta-func/SKILL.md`'s `stream` guidance said "Nothing catches
that" and described a `StreamSmokeTest` failure mode that `6e018348` had
already replaced. Corrected to the refusal.
