docs(fma): the Peek-only rule is a measured result, not a size argument

Comment-only change in `c_stream.rs`. `generate` is byte-identical, so the
shipped library, every binding and every gate are untouched. The reason to look
at it is that the sentence being replaced is load-bearing — it is what a reader
consults before deciding whether to extend `TA_FMA_MULTIVERSION` to the other
streaming tiers — and it is measurably false.

The old text: the delegating tiers "delegate to `static`
`_StepImpl`/`_OpenImpl` bodies that usually exceed `--param
max-inline-insns-auto` (30 at `-O3`), so the clone is emitted empty and costs
bytes for nothing".

Everything below is measured on `dev` at `52d0e839` (i.e. after #382 removed
`UpdateAndFill` and after #338 rewrote ATR/NATR), CMake Release, no `-march`,
gcc 13.3.0, x86-64.

## What the objects actually say

Counting functions whose own compiled body carries an `R_X86_64_PLT32 fma`
relocation — i.e. gcc inlined the step into them and the fused site is a libm
call:

| tier | bodies carrying an inlined `fma` |
|---|---:|
| `_Update` | **40** of 201 |
| `_Peek` | 38 (already marked — the count is over its `.default` clones) |
| `_OpenImpl` | 34 |
| `_OpenInternal` / `_OpenAndFillInternal` | 17 |
| out-of-line `_StepImpl` symbols | **0** |

No step survives out of line on this tree, so for the `_Update` tier the old
premise does not hold anywhere: the fused site is inside the function the
attribute would land on.

So I marked `Update` too (the one-line generator change below) and looked at the
clones. **40 new `.fma` clones; 27 of them contain a `vfmadd`.** The 13 that do
not are `MAMA`, `SMI`, `T3`, four `CDL*` and six `HT_*` — and they are not empty
either. They are the exact 13 functions that grow an out-of-line `_StepImpl`
symbol in the marked arm (0 before, 13 after): gcc answers the attribute by
*undoing* the inlining, so both clones call the step instead of carrying it.
`TA_T3_Update` is 113 instructions with 9 fused sites inlined on `dev`; marked,
each clone is ~36 instructions and calls out.

The experiment is one flag in `mark_fma_multiversion` — mark `update_signature`
as well as `peek_signature`, keyed on the function fusing anywhere rather than
on the `Update` text (the fused site is in the step, so `Update`'s own text never
spells `fma(`). It is not in this PR; the PR is the comment.

## And it does not point one way

Host: Intel Xeon @ 2.80 GHz (avx2+fma), 4 vCPU, Ubuntu 24.04, gcc 13.3.0, CMake
Release. Throwaway harness linking `libta-lib.a`, one function per process,
100k bars, period 14, 500-bar warm-up through `Open`, min over 24 processes x 31
in-process rounds, arm order alternated per process, pinned to one core.

**Single-object isolation.** A 40-object diff moves layout, so each row below is
its own pair of archives differing in exactly one member — arm A's
`libta-lib.a` with one `ta_<F>.c.o` swapped for the marked build's, verified by
`cmp` over the extracted archives (`objects_differing=1`). Every row carries the
same three unchanged control functions from that same run.

| marked function | dev (ns/bar) | marked | ratio | controls in the same run (SMA / RSI / MFI) |
|---|---:|---:|---:|---|
| `TA_KAMA_Update` | 10.734 | 7.656 | **1.402x** | 1.067 / 1.011 / 1.003 |
| `TA_TRIX_Update` | 9.229 | 10.472 | **0.881x** | 1.130 / 1.111 / 1.008 |
| `TA_NATR_Update` | 6.550 | 5.885 | **1.113x** | 1.000 / 1.003 / 1.004 |
| `TA_ATR_Update` | 5.591 | 5.001 | 1.118x | 1.067 / 1.001 / 1.017 |
| `TA_T3_Update` | 20.632 | 20.015 | 1.031x | 1.133 / 1.104 / 1.007 |
| `TA_EMA_Update` | 3.900 | 3.824 | 1.020x | 1.067 / 1.003 / 1.017 |

**Read the control column before the ratio.** This host moves an *unchanged,
byte-identical* function by up to 13% when one unrelated object changes size, so
the T3 and EMA rows say nothing — I am not claiming them. What survives its own
run's floor is KAMA (1.40x with controls at 1.07/1.01/1.00), TRIX (0.88x while
every control in that run moved the *other* way, 1.13/1.11/1.01), and NATR
(1.11x on the one run whose controls were flat, 1.000/1.003/1.004). ATR reads
the same 1.118x as NATR but against a 1.067x control, so it is suggestive rather
than resolved.

The ratios are stable, not noisy. The whole-diff arms (all 40 objects marked at
once) were measured separately at 16 and at 40 processes; across those two runs
and the isolated ones above, ATR read 1.118 / 1.118 / 1.118, TRIX 0.875 / 0.881
/ 0.881, KAMA 1.400 / 1.408 / 1.402 and the SMA control 1.067 every time. The
control's 1.067 is as repeatable as the effects, which is what says it is
alignment rather than noise.

**Bits.** Every function above produces a bit-identical output accumulator
across the arms.

## Why "it has a vfmadd" is not the rule

`TA_TRIX_Update` on `dev` makes three libm `fma` calls per bar. Marked, its
`.fma` clone has three `vfmadd`, no call at all, and 55 instructions against the
baseline's 87 — and it is the slower arm. I verified that the ifunc resolves to
that clone: built `-no-pie` and compared `&TA_TRIX_Update` (0x403aa0) against
`nm`'s `TA_TRIX_Update.fma` (0x403aa0), not `.default` (0x404450). **I did not
root-cause the regression** — no perf counters on this box, and I am not going
to guess at a microarchitectural story.

## What this leaves open

`TA_KAMA_Update` is 1.40x with one object changed. Capturing it would need a
per-function opt-in, which this tree has no knob for and which I am not
proposing — one function is not a rule, and the same knob would have to exclude
TRIX by name. If you want it, the number is real and reproduced; say so and I
will build it properly rather than bolt a name list onto the emitter.

## What it costs

Nothing. The change is a comment. The measurements are the argument for *not*
making the code change they describe.

For the record, had it been made: `libta-lib.a` 4,061,810 → 4,103,910 bytes
(+1.04%).

## What I did not check

- **clang** — gcc 13.3 only. clang picks its own inlining, so the census (which
  bodies carry an inlined step) is a gcc number, not a portable one.
- **aarch64 / macOS / musl** — no hardware, and `TA_FMA_MULTIVERSION` is inert
  outside glibc/x86-64 anyway (that inertness is #380's subject).
- **`-O3` / LTO / `-march=native`** — the shipped Release configuration only.
  The old text quotes `--param max-inline-insns-auto` at `-O3`; the census is at
  the flags the library actually ships with.
- **Why TRIX regresses** — measured and reproduced under isolation, not
  explained.
- **The other 34 marked functions** — six timed rows, chosen as the fused
  recursive tiers plus T3/EMA; the remaining marked functions were censused, not
  benchmarked.
- **The batch tiers** — untouched by this rule and not re-measured.
- **A second host** — one box, one CPU model. This is a different box from the
  one the superseded version of this PR body measured on (2.80 GHz vs 2.10 GHz),
  which is part of why the ATR ratio moved 1.20x → 1.12x; the tree changed too.
