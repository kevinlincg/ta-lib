test(codegen): Clone duplicates exactly what Close disposes (#386 B)

Closes the **"`Clone` has no generator-side gate"** entry under **B. Test
coverage** in #386, for C — the backend where the property is transcribed by
hand rather than derived. Test-only: the generated output is byte-identical.

## The invariant, and where it was only a comment

A fork owns its own copy of everything the original owns. So the inventory
`Close` walks and the inventory `Clone` duplicates are one set — and they are
built by two functions that do not consult each other:

| side | built by |
|---|---|
| dispose | `release_free_lines`, and the per-plan arms of `emit_close` |
| duplicate | `clone_buffer_lines`, and the per-plan arms of `clone_owned_lines` |

`clone_buffer_lines` states the correspondence itself — *"Mirrors
`release_free_lines` field for field, and that is the invariant: a buffer freed
there and not duplicated here is a fork that shares state with its original."*
Nothing held it. A doc comment is not a gate, and this one sits on the half that
would be edited by someone adding a buffer to the other half.

Both directions are defects, and neither shows up on the corpus we ship:

- **Disposed, not duplicated.** The fork's field still points at the original's
  buffer. The two handles share state — an `Update` on one moves the other — and
  the second `Close` frees what the first already freed. `stream_verify`'s fork
  leg compares values *at the fork bar*, where the two agree by construction, so
  it sees nothing; the double free needs an allocator that notices.
- **Duplicated, not disposed.** A leak on every fork, and the original leaks the
  same buffer at `Close`.

## What the sweep does

It reads the **emitted C**, not the plan. The plan is the part the two sides
already agree about — they can only disagree in the transcription, and the
transcription is text.

For each of the 201 streaming functions it derives two sets of handle fields:

- **disposed** — the argument of every `TA_Free(...)` and every `TA_*_Close(...)`
  inside `TA_<N>_Close` and the `TA_<N>_ReleaseImpl` it may delegate to, reduced
  to the field it names. A cast, an index and the receiver's spelling are noise:
  `sp->x`, `stream->bank[k]` and `(TA_SMA_Stream *)stream->sub` each name one
  slot, and the slot is what the two inventories must agree on. `TA_Free( sp )`
  names no field and drops out.
- **duplicated** — every `sp-><field> = NULL;` disown in `TA_<N>_Clone`. The
  disown is emitted once per duplication and only for a duplication, so it is
  the duplication's fingerprint.

Then it asserts the two sets are equal, in both directions, and reports which
way round each mismatch is.

A second test pins the other half of the same invariant: **every disown runs
before the first duplication.** That ordering is what makes the failure path
safe — a duplication that fails mid-way calls `TA_<N>_Close( sp )` on the
half-built copy, and if a field had not been disowned yet, that call would free
the *source's* buffer. Today the emitter gets this right by emitting `disown`
and `dup` as two separate lists in that order; nothing said it had to.

### Two ratchets, because equality is vacuous on empty sets

A derivation that stopped seeing functions, or stopped seeing their fields,
agrees with itself perfectly. So the sweep also asserts it saw `>= 200`
streaming functions and compared `>= 250` owned fields (corpus today: **201**
and **256**).

The second number is the one with real slack in it — 6 fields. It is a ratchet
in the same sense as the existing emit ratchets, and it will need a deliberate
downward edit if a plan is ever restructured to own fewer buffers. That is the
intended cost: the alternative is a floor loose enough to be passed by a
derivation that has quietly stopped working, which is how `java_stream_suite`'s
emit ratchet went stale at 168 against 201 (noted in #386's own backlog).

## Controls

Four, one per class the gate claims to catch. Each was applied to the emitter,
watched red, and reverted:

| break | result |
|---|---|
| `clone_buffer_lines` stops duplicating the extrema arrays | **39 mismatches** — `AROON: Close disposes x_inHigh, Clone shares it with the original`, … |
| `release_free_lines` stops freeing the window buffers | **12 mismatches** — `HMA: Clone duplicates win_jFull_inReal, Close never frees it`, … |
| the composed arm forgets the last sub-stream | **12 mismatches** — `KC: Close disposes sub2, Clone shares it with the original`, … |
| `emit_clone` emits `dup` before `disown` | **256 late disowns** on the ordering test |

The third is the one worth calling out: a sub-stream is a plan-level owner, not
a buffer, so it is a class the `clone_buffer_lines` comment does not even cover.
The same break leaves the ordering test green, and the ordering break leaves the
equality test green — the two tests fail independently, which is what says they
are two properties and not one.

## Scope: C only, and why the other three are not the same question

#386 phrases the entry as "no generator-side gate in any backend". Checking each:

- **Rust** — the handle is `#[derive(Debug, Clone)]`. The deep copy is derived by
  the compiler from the field types, so `Vec<f64>` and `Box<T>` fields deep-copy
  by construction. There is nothing here for a generator gate to hold that the
  type system does not already hold, and I am not proposing one.
- **Java and C#** — a hand-emitted per-field copy constructor
  (`KcStream( KcStream other )`), so this *is* the same question, and the only
  assertion on it today is one hand-written line for SMA in
  `java_stream_suite.rs`. The derivation is analogous but different in shape —
  the mirror there is the handle class's own field list, not a `Close` — and
  wants its own change. **Not done here**, deliberately, rather than bundled in
  half-finished.

So this PR closes the entry for the backend where the hazard is manual
malloc/free, and narrows rather than closes it overall. Say the word if you would
rather it wait until the managed arm exists too.

## Verification

- `cargo test` in `ta_codegen/generator` — the full suite, **0 failures**.
- `cargo clippy --all-targets -- -D warnings` — clean.
- `cargo run --release -- generate`, then `git status` — the only entry is the
  new test file, so every backend's output is byte-identical.
- The four controls above, each observed red before being reverted.

**Not run, and why:**

- `scripts/build.py regen-check` / `check-source-lists` — `build.py` refuses to
  run as root, which is what this environment has. I ran the gate's substance by
  hand instead (`generate`, then `git status`), which is what `regen-check`
  checks; I did not run the script itself.
- `ta_regtest`, the cross-language sweep, and the C reference suite. The change
  adds no library code and the generated output is byte-identical, so there is
  nothing for them to see — not a skip I am asking you to trust, a no-op.
- Java and C# were not built. Nothing in this PR touches them.
