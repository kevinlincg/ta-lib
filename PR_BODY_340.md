docs(xlang-hash): #340 step 1 measured — no divergence on linux-x64, and the knob the recipe needs

Closes nothing on its own. This is issue #340's **step 1** ("Measure it") carried
out, plus step 4's documented boundary. No gate and no backend change: the
measurement did not justify either, and the issue asks for them only if it had.

## The answer

`--xlang-hash`, C# row, measured on upstream `dev` at `7065d886`, with the FMA3
intrinsic disabled for the spawned server (the branch is rebased onto `67936169`,
which is `7065d886` plus two dist bumps and #338 — see the note at the end of
this section for what that does and does not change about these rows):

| run | C# cases | mismatches |
|---|---:|---:|
| control — hardware FMA3, as CI runs it | 284,487 | **0** |
| FMA3 + AVX2 disabled in the server | 284,487 | **0** |
| same, with four EMA fusion sites hand-unfused | 284,487 | **5,893** across 9 functions |

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
`Math.FusedMultiplyAdd` files to 32. I ran the same disabled-ISA row against that
widening — 284,487 cases, 0 mismatches, unchanged — so it does not move this
answer either, on this platform. One caveat on that row: I measured it on my own
branch's spelling of the two-coefficient step, before `67936169` landed #338 on
`dev` with a different coefficient order. The file count is the same either way,
but **I did not re-run the row against dev's landed form.**

## Verification

- `scripts/build.py xlang-hash --language=csharp` for the control, then
  `bin/ta_regtest --xlang-hash --language=csharp` under the two knobs for the
  other two rows — same binaries, one build.
- The changed file is `src/tools/ta_regtest/CLAUDE.md` and nothing else; no
  generated artifact and no generator input is touched.
- `scripts/build.py regen-check` clean.
