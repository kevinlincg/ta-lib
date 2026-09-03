perf(streaming): the fused streaming tiers were emitted without target_clones (#337)

Closes #337. Generator-side only; unblocks #338.

`TA_FMA_MULTIVERSION` reached `TA_<N>` and `TA_S_<N>` and nothing else, so every
streaming tier of the 29 fused TUs ran its `fma()` as a bare `call fma@PLT`,
once per bar on the hot path.

The cause is the shape of the predicate, not a missed site list. The batch
emitter tests its **own rendered text** for `fma(` — and `Update`,
`UpdateAndFill` and `Peek` carry no arithmetic of their own. They call
`<N>_StepImpl`, gcc folds that static into each of them, and the fused site
therefore lands in a caller the text predicate cannot see.

`fma::annotate_multiversion_callers` closes the predicate over the whole
streaming section: a tier is marked when its own body fuses, **or** when it
calls a `static` helper that does. It runs as one pass at the end of
`c_stream::generate` rather than a prepend inside each tier emitter, because
five stream plans reach that point and they share no tier list — a plan added
later would otherwise ship an unclonable `fma()` without being asked.

## The attribute goes on the callers, never on the static step

This is the one part of the issue's proposed path that measurement contradicted,
so it is worth stating plainly. `target_clones` makes a function an ifunc, and
an ifunc does not inline. Marking `<N>_StepImpl` — as the issue's "The path"
section suggests — stops it folding into the three per-bar entries and buys them
nothing. Measured on gcc 13.3, `ta_EMA.c` with the real build flags:

| attribute on | result |
|---|---|
| `TA_EMA_StepImpl` (static) | `StepImpl` gets a clone pair it cannot inline; **`TA_EMA_Peek` still on a bare `call fma@PLT`** |
| the five entry points | all five get clone pairs; the step inlines into each clone; no entry point left on `fma@PLT` |

The issue also predicts `_Peek` "inlines its own copy of the step rather than
calling `_StepImpl`". At the source level it does call it — but the conclusion
drawn from that (that `_Peek` needs the attribute in its own right) is correct,
and the first row above is why: even with the static multiversioned, gcc 13
re-materializes a copy of the step inside `Peek`.

The issue's site list is also one short. `OpenInternal` and
`OpenAndFillInternal` carried **two** `fma@PLT` relocations each — the
history-replay loop — which the "four more sites per file" framing misses. The
predicate finds them without being told.

## What moves

145 lines added, every one of them `TA_FMA_MULTIVERSION`, nothing removed and no
other line added, across the 29 fused TUs — five tiers each (`Update`,
`UpdateAndFill`, `Peek`, `OpenInternal`, `OpenAndFillInternal`), 2 batch sites
unchanged, 7 per TU:

```
$ git diff -U0 src/ta_func/ | grep '^+' | grep -v '^+++' | sort -u
+TA_FMA_MULTIVERSION
$ git diff --numstat src/ta_func/ | awk '{a+=$1; d+=$2} END{print a, d}'
145 0
```

**No arithmetic moves**, so the numerical contract is exactly the one PR #96
already ships and gates: `-ffp-contract=off` keeps the clones bit-exact with
each other and with Rust/Java/C#. Rust, Java and C# output is byte-identical
after a full `generate` — the attribute is C-only.

Objdump on the shipped separate-TU build:

- All 145 marked tiers now carry a `.default` / `.fma` clone pair (203 `.fma`
  clones in the archive, up from 58).
- **No public per-bar tier holds an unclonable `fma@PLT` any more** — the check
  the issue's verification bar asks for. Stated precisely rather than as "goes
  to 0": relocations still exist *inside the `.default` clones*, which is the
  intended no-hardware-FMA fallback, not a miss.

## Verification

Ran and green: full `ta_regtest` (twice, including the streaming finite-input
gate and the hardcoded expected values), `build.py regen-check`, the generator's
whole `cargo test`, and `cargo clippy --all-targets` clean.

New gate, `fma_suite::fma_multiversion_marks_the_stream_tiers_that_fuse_and_not_their_static_step`,
pins all three halves of the decision. Each was broken deliberately and watched
fail:

| break | fails with |
|---|---|
| remove the annotate pass | `TA_EMA_Update fuses through the step it inlines but carries no TA_FMA_MULTIVERSION` |
| also mark the static step | `TA_EMA_StepImpl is static and must NOT be multiversioned` |
| mark unconditionally | `SMA fuses no site; nothing in its streaming section may be multiversioned` |

### `--fuzz-064`: the whole report is byte-identical to `dev`'s

Since first writing this I had a container with the release tags, so the frozen
v0.6.4 oracle could be built. This branch and `dev` df0c6beb were built and run
the same way (gcc 13.3, x86-64 glibc, `CMAKE_BUILD_TYPE=Release`) against the same
oracle. Diffing the two reports line by line leaves **nothing** but the pid and the
build stamp: 166852 comparisons, 139064 bit-identical, 15973 in the PR #96 FMA
bucket at an unchanged max of 4.13e-11, 0 failures, and every skip class equal.
A full `ta_regtest` on this branch is green as well, streaming finite-input gate
included.

Two limits on what that buys, stated rather than implied. `--fuzz-064` drives the
**batch** tiers, which this PR does not annotate — so it is a regression guard,
not evidence for the streaming half; the streaming half rests on `ta_regtest`'s
own stream gate. And because this box has hardware FMA, what the resolver ran was
the `.fma` clone, so the identity above is about the clone that is actually
selected, not about the `.default` fallback (still unexercised — see below).

### Performance: what the numbers do and do not support

`ta_bench_stream`, two independent interleaved A/Bs (arms alternate order each
round, min-of-N, `taskset`-pinned), with the **149 functions whose emitted C is
byte-identical as the control group**:

| run | metric | control spread | changed median |
|---|---|---|---|
| min of 6, `--points=20000 --iters=50` | `update_ns` | −27.2% .. +33.1% | −2.59% |
| | `peek_ns` | −7.2% .. +4.9% | −1.80% |
| min of 5, `--points=8000 --iters=250` | `update_ns` | −17.6% .. +24.5% | −1.81% |
| | `peek_ns` | −24.2% .. +19.3% | −2.16% |

**The only per-function claim I will defend** is the one that reproduced across
both runs and sat outside the control's own excursion in both — `Update`:

| | run 1 | run 2 |
|---|---|---|
| MACDFIX | −46.2% | −50.5% |
| MACD | −46.7% | −47.0% |
| ADOSC | −38.0% | −38.6% |
| KAMA | −37.5% | −34.4% |

`peek_ns` improves in the same direction in both runs and several functions read
double digits (HT_PHASOR, HT_DCPERIOD, T3, SMI), but the magnitudes are not
stable between runs (HT_PHASOR −25% then −60%) and run 2's control reached
±24%, so I am **not** quoting per-function peek numbers.

`batch_last_ns` is the built-in sanity check — the batch tiers are untouched by
this change and read a changed-set median of +0.08% / −0.04% with a ±4% range.

This ran on a 4-core shared cloud container, not the issue's Zen 4 box. I did
not attempt to reproduce its cyc/bar figures and cannot.

## Costs, stated rather than special-cased

**Shipped `.text` grows 3.04%** — 2,503,255 → 2,579,360 bytes; the static
archive 3,788,424 → 3,970,406. That is the clones' second copy of each marked
tier, and it is the price of the change.

**486 `fma` PLT relocations remain**, all of them inside the two `static`
helpers gcc declines to inline: `_OpenImpl` (394) and `_StepImpl` (92), 45
symbols across 13 functions — HT_TRENDMODE, HT_TRENDLINE, HT_SINE, HT_DCPHASE,
HT_DCPERIOD, HT_PHASOR, MAMA, T3, SMI and four candlesticks. Both clones share
those helpers, so an attribute on the entry points cannot reach them; the
functions in that list are unchanged by this PR rather than fixed by it. Note
that the residual list comes from the shipped separate-TU build, while
`ta_bench_stream` is a single-TU `-flto` build that re-decides inlining — which
is why HT_PHASOR can improve in the bench and still appear here.

## If #338 lands too, the second of the two needs a `generate`

#338 (ATR/NATR/SUPERTREND in the fused Wilder form,
`kevinlincg:issue-338-atr-fused-wilder`) makes those three fuse in their *streaming*
tiers, which is exactly what the pass added here marks. The two branches do not
conflict textually and each passes the gate on its own, but the merge of the two does
not: neither branch can carry the result, because this one has no fusing ATR to mark
and #338 has no annotate pass to run. Measured with `regen-check`, not predicted:

| tree | `scripts/build.py regen-check` |
|---|---|
| `dev` df0c6beb | exit 0 — "output matches the committed source. OK." |
| this branch alone | exit 0 — same |
| `issue-338` alone | exit 0 — same |
| the two merged | **exit 1** — "regenerating changed the committed output" |

The drift is 15 lines, all `TA_FMA_MULTIVERSION`: five per TU on ATR, NATR and
SUPERTREND. C-only. So whichever merges second is a merge plus one `generate` — this
PR needs nothing if it goes first.

## Two things for you to decide

1. **The residual.** Covering those 13 means marking a large static: it trades
   one direct call per bar for one ifunc call, in exchange for the 10–44 `fma`
   sites inside it becoming `vfmadd`. That looks like a win for MAMA/T3/HT_*
   specifically, and it is a loss for anything gcc would have inlined — so it
   needs a size or site-count discriminator in the generator, and its own
   measurements. Deliberately not guessed at here.
2. **The musl / macOS / MSVC question the issue raises.** I did not act on it,
   and I think it is bigger than a C-side emission choice. The macro's guard is
   untouched, so those platforms expand it to nothing and gain no new sites from
   this PR — that part is a no-op. But emitting *unfused* C there would break
   the property `fma.rs` exists to hold: all four backends fuse the same sites,
   and Rust/Java/C# fuse unconditionally. So it is a cross-language bit-parity
   decision, not a platform tuning knob, and the 19.2x / 152x / 18.2x cliff the
   issue measured is an argument for taking it deliberately.

## What I did not check

- **The `.default` clone was never executed.** This box has hardware FMA, so the
  resolver always picks `.fma`. The software-`fma` path is unexercised here;
  what stands behind it is that no arithmetic changed (the diff above) plus
  PR #96's existing contract.
- The `musl-build` nightly. Not run.
- `--xlang-hash`, `--codegen` and `server_verify` as separate runs: no language
  servers built in this tree and no .NET SDK. Cross-language parity rests here on
  the generated Rust/Java/C# being byte-identical, not on a run of those gates.
  (`--fuzz-064` has since been run — see the differential below.)

## Re-based onto `dev` 58a0ac54, and re-measured there

`dev` moved after the body above was written: 58a0ac54 (#316, "the C peek frame
binds the handle instead of copying it") rewrote 439 lines of
`backends/c_stream.rs` and regenerated all of `src/ta_func/`. The branch now
carries a merge of that.

It merged with **no textual conflict**, which on its own proves nothing about a
7-line hook into an emitter that was just rewritten around it — so the pass was
re-verified on the new base rather than assumed:

| re-run on the merged tree | result |
|---|---|
| `build.py regen-check` | exit 0 — "output matches the committed source. OK." |
| `cargo test --test fma_suite` | 3 passed, 0 failed |
| the generator's whole `cargo test` | 902 passed, 0 failed |
| `build.py test` (full C `ta_regtest`) | all tests succeeded |
| `.fma` clones in the archive | 203, unchanged |

The "#338 needs a `generate`" finding was **re-measured on this base**, not
carried over: merging `issue-338-atr-fused-wilder` into this branch and running
`generate` still drifts by exactly 15 lines, `+15 −0`, all of them
`TA_FMA_MULTIVERSION`, five each on ATR, NATR and SUPERTREND
(`_OpenInternal`, `_OpenAndFillInternal`, `_Update`, `_Peek`, `_UpdateAndFill`).
That combined tree is green on the full C `ta_regtest`. So the guidance is
unchanged: whichever of the two merges second is a merge plus one `generate`.

Not re-run on the new base: `--fuzz-064`, the objdump `.text`/PLT census, and the
`ta_bench_stream` A/Bs. Those numbers are from the df0c6beb base and are left as
they were measured.
