docs(xlang-hash): #340 step 1 measured — no divergence on linux-x64, and the knob the recipe needs

Closes nothing on its own. This is issue #340's **step 1** ("Measure it") carried
out, plus step 4's documented boundary. No gate and no backend change: the
measurement did not justify either, and the issue asks for them only if it had.

## The answer

`--xlang-hash`, C# row, measured on upstream `dev` at `7065d886`, with the FMA3
intrinsic disabled for the spawned server (the branch's head is now rebased onto
dev `ce5f5748`, which is `7065d886` plus two dist bumps, #338, DONCHIAN and the
three commits under "Rebased onto dev `ce5f5748`" at the end — see that section,
the note at the end of this one, and the last bullets under Verified, for what
that does and does not change about these rows):

| run | C# cases | mismatches |
|---|---:|---:|
| control — hardware FMA3, as CI runs it | 284,487 | **0** |
| FMA3 + AVX2 disabled in the server | 284,487 | **0** |
| same, with four EMA fusion sites hand-unfused | 284,487 | **5,893** across 9 functions |
| all hardware intrinsics off (`DOTNET_EnableHWIntrinsic=0`) | 284,487 | **0** |

The third row is the control that goes red. It is the same command and the same
disabled-ISA environment as the second, with
`prevMA = Math.FusedMultiplyAdd(inReal[today++] - prevMA, optInK_1, prevMA)`
rewritten to the unfused `(a - b) * k + b` in the generated `Core_EMA.cs` — a 1
ULP change, which is the size of the divergence this issue is about. The gate
sees it, in EMA and in the eight functions that compose it, so the green rows are
not vacuous. The sabotage was reverted before the second row was re-run.

Alongside, a direct differential of the primitive itself — .NET's
`Math.FusedMultiplyAdd` against glibc's `fma()` on identical operands, 2,000,000
random triples spread over subnormal / near-1.0 / general-normal classes:
**0 mismatches with the ISA disabled**, 0 with it enabled, and 15,200 / 200,000
when the reference is switched to the unfused `a*b+c` (again, the control that
goes red). The reporter's own operands from dotnet/runtime#98704 return the
correctly-rounded `1.5` on this platform, not the `1.5000000000000002` reported.

## The trap, which is the reason this is a doc change at all

**`DOTNET_EnableFMA=0` alone does nothing.** With only that knob set,
`Fma.IsSupported` is still `true` on .NET 10.0.400 and the run is the control
wearing a different name. `DOTNET_EnableAVX2=0` has to be set with it. A step-1
run done the way the issue proposes it would therefore have reported "no
divergence" without having measured anything — which is the failure mode worth
writing down, more than the result is.

The second half of the recipe: the knobs have to be on the **spawned** `dotnet`
child, not only on `ta_regtest`. They are, since the server is `execvp`'d and
inherits the environment; I confirmed it from `/proc/<server pid>/environ` on a
live run rather than assuming it.

## What this does not establish

Stated as limits, not hedges — each is something I did not check:

- **Not real non-FMA3 silicon.** Disabling the knob removes the *JIT intrinsic*.
  Whatever the runtime falls back to still resolves `fma` against this host's
  CPU, which has FMA3. A genuine pre-Haswell machine exercises a path this box
  cannot produce.
- **linux-x64 / glibc 2.39 / .NET 10.0.400 only.** Windows, macOS, musl and
  ARM64 are unmeasured. dotnet/runtime#98704's reporter did not state a platform
  I could match, and if the misrounding lives in a platform CRT rather than in
  managed code then Linux is exactly the platform where it would not reproduce.
  That is a plausible reading of this result, and I did not confirm it — I did
  not read the runtime's fallback source.
- **Not a regression guard.** Nothing here stops a future change from breaking
  non-FMA3 parity; the note tells the next person how to look, it does not look
  for them.

## Your call

Whether to also add the disabled-ISA run as a nightly leg. Arguments both ways:
it is ~90 s on top of an `xlang-hash` job that already builds everything it
needs, and the sabotage row proves it would be a real gate rather than a green
rubber stamp. Against: #340 asks for a gate only if step 1 showed divergence, and
it did not, so adding one now is scope the issue deliberately did not authorise.
I left it out for that reason. Say the word and it is a small follow-up.

Also worth knowing: #338 fuses ATR, NATR and SUPERTREND, which moves them from 29
`Math.FusedMultiplyAdd` files to 32. **That row has now been re-run against dev's
landed form**, `67936169`, which is the base this branch sits on: control 284,487
cases / 0 mismatches, and the disabled-ISA row the same. The earlier measurement
was taken on this branch's own spelling of the two-coefficient step, before dev
landed #338 with a different coefficient order; the two agree, so the widening
does not move the answer on this platform.

## The stronger knob, and why it changes the conclusion's shape

`DOTNET_EnableAVX2=0 DOTNET_EnableFMA=0` stops the JIT emitting `vfmadd213sd` in
the caller, but the value can still reach a hardware FMA underneath. Setting
`DOTNET_EnableHWIntrinsic=0` goes further — `Fma.IsSupported` is `false` and
`DOTNET_JitDisasm` shows a real `call System.Math:FusedMultiplyAdd` in place of
the instruction — and the result is still correctly rounded:

- dotnet/runtime#98704's own operands return `1.5` / `0x3FF8000000000000`, not
  the `1.5000000000000002` the report describes;
- 4,000,000 random triples, 1,333,334 of them constructed as near-cancellations
  of the same shape as the reporter's, hash bit-for-bit identically to the
  hardware-FMA run (FNV-1a `0xFE4D36BA5DAB5B74` both ways);
- the full C# `--xlang-hash` row under that knob is green at 284,487 cases.

So the honest statement is stronger than "no divergence measured": on linux-x64
**no process-level knob reaches a misrounding fallback at all**. A nightly leg
built on any of these knobs would go red for an unfused site — the sabotage row
above proves that much — but it could never go red for the defect #340 names.
That is an argument against step 2 as the issue frames it, and it is the reason
the note now says so in the file rather than leaving the next person to find it.

Worth flagging for the same reason: `DOTNET_EnableFMA=0` alone leaving
`vfmadd213sd` in the emitted code is now checked at the instruction level, not
just inferred from `Fma.IsSupported`.

## Verification

- `scripts/build.py xlang-hash --language=csharp` for the control, then
  `bin/ta_regtest --xlang-hash --language=csharp` under the two knobs for the
  other two rows — same binaries, one build.
- The changed file is `src/tools/ta_regtest/CLAUDE.md` and nothing else; no
  generated artifact and no generator input is touched.
- `scripts/build.py regen-check` clean.
- Re-run on the current head (dev `67936169` + this doc commit), .NET 10.0.400
  installed fresh on the box: `xlang-hash --language=csharp` control 284,487 / 0,
  and the same under `DOTNET_EnableHWIntrinsic=0`, 284,487 / 0.
- Not checked this round: I did not re-read `/proc/<server pid>/environ` for the
  `DOTNET_EnableHWIntrinsic=0` row — that leg rests on `execvp` environment
  inheritance, which the earlier row did confirm directly. The primitive-level
  numbers above were taken in a standalone probe, not through the server.
- Head re-verified after merging dev `af4cdede` (which landed DONCHIAN):
  `regen-check` green, exit 0, 179 functions. The net diff against dev is
  unchanged by that merge — still the one file. **The `--xlang-hash` rows above
  were not re-run on this merge: there is no .NET SDK on the machine this round,
  so every C# number in this body is the one measured on `67936169`.** What did
  get checked instead is the only way DONCHIAN could have moved them: the count
  of generated C# files calling `Math.FusedMultiplyAdd` is still 32 on the merged
  head, and `Core_DONCHIAN.cs` contains no call — DONCHIAN is max/min/midpoint,
  so it adds no FMA site and cannot widen the exposure this body measures.

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

**Not re-run on the rebase:** every `--xlang-hash` row and the primitive-level
differential — there is still no .NET SDK on this box, so every C# number in
this body remains the one measured on `67936169`. The two structural checks that
are the only way the three new dev commits could have moved them were re-run on
the rebased head: 32 generated C# files still call `Math.FusedMultiplyAdd`, and
`Core_DONCHIAN.cs` still contains none. All three commits change generated C and
Rust; none of them writes a C# file.
