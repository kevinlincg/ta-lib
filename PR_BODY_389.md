test(regtest): a rejected Open/OpenAndFill must not write the caller's buffer (#389)

Closes the coverage half of #389: the gate, corpus-wide, in the one backend
where the defect is reachable. It does not move RSI or CMO.

## What the sweep does

For every entry in the generated `TA_StreamTable` (201 functions), under both
compatibility modes, through both `Open` and `OpenAndFill`: arm the caller's
output buffers with a sentinel, call, and on any non-success return require
them back byte-identical.

Two rejection classes, deliberately both:

- **History** — `historyLen` ramps up from 0 and the leg stops at the first
  length that produces a value, so the last rejection it sees is the one at the
  boundary, whatever the boundary turns out to be. That is the point: under
  Metastock `TA_RSI_Lookback` reports 13 while `Open` needs 15 bars, so a probe
  computed from the lookback lands one bar away from the violating call.
  Nothing in the leg knows a lookback.
- **Parameter** — every optional parameter driven one step outside each of its
  declared bounds, on a history that otherwise produces. This is the class the
  public frame answers before the body runs, and having both is what makes
  "only the in-body class can write" a measurement rather than a claim.

## What it finds

`TA_RSI` and `TA_CMO`, under Metastock, on `OpenAndFill`, and nothing else:

```
OPEN-CONTRACT TA_CMO METASTOCK OpenAndFill: retCode=17 wrote the caller's buffer (outBegIdx=13 outNBElement=1)
     output 0 [0] = 99.1494
OPEN-CONTRACT TA_RSI METASTOCK OpenAndFill: retCode=17 wrote the caller's buffer (outBegIdx=13 outNBElement=1)
     output 0 [0] = 99.5747
```

(The values differ from #389's because the series does; the shape is identical
— `TA_INSUFFICIENT_HISTORY` with a plausible oscillator value already in
`outReal[0]` and `outNBElement` reporting one element.)

Both are listed in `ocKnownOpen` and **asserted to still violate**, so the run
is green today and a listed row that stops violating fails it. Same treatment
#383 gave PPO/PVO and STOCH/STOCHF: moving the write is a decision about a
shipped function, not something to slip in under a gate.

## The numbers

| | |
|---|---|
| functions swept | 201 (`TA_STREAM_TABLE_SIZE`), x 2 modes |
| rejections whose buffers were compared | 14 762 |
| of those, out-of-range parameter probes | 1 360, **none accepted** |
| per-function positive controls | 402 (201 x 2), all passed |
| rejections leaving `outNBElement` non-zero | **2** — the two rows above |
| added runtime | under 25 ms; the group runs faster than the smallest existing indicator group (`--function=CVI`) |

## Controls, each one broken and watched go red

Not asserted — run.

1. **`ocKnownOpen` emptied** → red, naming `TA_RSI` and `TA_CMO` under
   `METASTOCK` with the value each wrote. This is the live proof the sweep sees
   the real defect.
2. **A row that no longer violates** (added `{"SMA", 1}`) → red:
   `TA_SMA is listed as a known open write-on-rejection (#389) under Metastock,
   but the sweep no longer sees it.`
3. **The `outNBElement` ceiling** lowered by one → red, printing both offenders.
4. **The positive control**: output pointers aimed at a decoy buffer, so the
   functions write somewhere the comparison never looks → red at the first
   function (`TA_AC DEFAULT: OpenAndFill reported 1 elements at historyLen=38
   and wrote nothing`). Without this arm, "nothing was written" is satisfied by
   a fixture the function never writes through, over all 201 functions.
5. **An in-range parameter probe** (`min + 1` instead of `min - 1`) → red, 572
   probes accepted. This one was not hypothetical: with the obvious `min - 1`,
   **58 of the probes were no-ops**, because a real parameter's declared bounds
   are ±3e37 where adding one is the identity. `oc_out_of_range` scales the step
   for reals and keeps the exact ±1 for integers and enums.

## Two judgement calls worth naming

**`outBegIdx`/`outNBElement` are ratcheted, not asserted.** §3.4 of
`website/src/api/README.md` calls them *undefined* on a non-success return, and
most bodies legitimately zero them on the no-data path, so asserting them would
be inventing a rule. But a rejection leaving `outNBElement` non-zero is exactly
what turns an invisible write into a value a caller trusting the count reads —
and over 402 function-modes it happens twice, both of them the listed pair. So
it carries a ceiling equal to the known-open row count, which drops with the
list rather than being a second constant to forget. If you would rather this
gate said nothing at all about the indices, it is one `if` to delete.

**The parameter leg asserts that an out-of-range parameter is rejected.** That
is slightly more than #389 asked for. It is here because the leg is worthless
if the probe is not actually out of range (see control 5), and because
`test_abstract`'s equivalent drives `TA_CallFunc`, i.e. batch — the streaming
open tier had no such check. Zero acceptances today.

## Why C only, and why that is not a gap to close later

- **Rust cannot host it.** `Compatibility` is `pub(crate)`, pinned to
  `Default`, with `compatibility_is_pinned_to_default` asserting there is no
  setter. No Rust probe can reach the Metastock seeding path.
- **Java and C# could, but `codegen_lang_has_compatibility_api` already records
  the standing decision** that the ported backends expose no compatibility API
  and the Metastock legs are C-only, permanently.
- **The undersized-output class has no C expression**: `TA_<N>_Open` takes a
  bare output pointer and carries no length. It stays where it is, in the Rust
  crate's `stream_open_contract.rs`.

So the sweep is C-only for the same reason `stream_verify`'s state leg is, and
the emitter it exercises is shared: the four backends transcribe one body.

## The fix, and why it is not in this PR

#389 offers two directions and the choice is yours:

- **Structural** — have the open frame commit `outBegIdx`/`outNBElement` and
  the fill only on the success return. Free for the two scalars; **not** free
  for the fill, which would need a scratch buffer and a copy on every
  *successful* `OpenAndFill`, since the rejection is raised after the writes
  and there is nothing to un-write.
- **Reorder the seed** — the write at `rsi.c:164-171` happens before the
  `today > endIdx` test that follows it. Hoisting the test costs nothing at run
  time, but the generator does not derive "this body needs one more bar under
  Metastock" today; it would be new analysis in the open transform
  (`c_stream.rs`'s early-success mapping, which already names RSI/CMO in its
  comment).

And #388 may delete the compatibility mode outright, which removes this
instance without removing the class — which is why the gate is worth landing
either way.

## Verification

- `ta_regtest` full run green, with the new group; `--function=CONTRACT` green
  on its own.
- `cd ta_codegen/generator && cargo run -- generate` → **zero drift**;
  `git status` shows only the six files this PR touches, none of them
  generated. The change is test-side only: no generator, no input, no shipped
  library file.
- `scripts/build.py check-source-lists`: *"ta_regtest source lists agree across
  CMake and autotools (77 files)"*, after registering the new file in both.

**What I did not run.** `--codegen` and `--xlang-hash` (this machine has no
`bin/ta_ref_serve`, and its JDK/.NET are too old for the Java/C# servers), and
an actual autotools build — the Makefile.am entry is verified by
`check-source-lists`, not by a configure-and-make. None of the three can be
affected by a change that adds one `ta_regtest` translation unit, but I would
rather say so than imply otherwise.
