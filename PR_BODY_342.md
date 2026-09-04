fix(codegen): a function without the `stream` flag is refused, not emitted (#342 follow-up)

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
- `scripts/synth_gate.py` on this branch: **PASS** — 14 synthetic functions,
  4 languages, stream and batch bitwise legs, 1520 elements compared across
  14 fixtures x 4 servers, and the generator suite green (902 tests) on the
  injected tree. The refusal is what a flagless `input_synth` fixture would now
  hit, so this is the gate that had to stay green, and it did.

**Not checked:** the nightly legs (`--xlang-hash`, `--fuzz-064`, the arm64 and
Windows/macOS jobs). Nothing in this change reaches an emitted file — regen-check
reports the output byte-identical — but I did not run them.

## Also

`.claude/skills/new-ta-func/SKILL.md`'s `stream` guidance said "Nothing catches
that" and described a `StreamSmokeTest` failure mode that `6e018348` had
already replaced. Corrected to the refusal.

## Rebased onto dev `ce5f5748`

Dev moved after the verification above was taken: `46577145` (c_hygiene, the
post-emission `(void)` sweep), `b128cbf5` (a short `--function` token names a
whole component) and `ce5f5748` (#344, the Open head that declares only what its
body uses). This branch is rebased onto `ce5f5748` with no conflicts, and
`git patch-id` says its net diff against dev is byte-identical to the one this
body describes — nothing about the change itself moved.

Re-checked on the rebased head, at these tiers only:

- `scripts/build.py regen-check`: green, exit 0, 179 functions.
- `cargo test --release` in `ta_codegen/generator`: 916 passed / 0 failed.
- `cargo clippy --release --all-targets -- -D warnings`: clean.

Dev `ce5f5748` itself passes the same three commands, so that is a baseline for
the rebase and not a control that goes red.

**Not re-run on the rebase:** `scripts/synth_gate.py`, the nightly legs, and the
two red controls — I did not re-remove `stream` from `sma.yaml` on the rebased
head. Those numbers are the ones measured on `af4cdede`.
