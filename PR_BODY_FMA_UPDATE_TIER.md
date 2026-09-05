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

## What the objects actually say

`dev` at `972c5cc9`, CMake Release (no `-march`), gcc 13.3.0, x86-64. Counting
functions whose own compiled body carries a `R_X86_64_PLT32 fma` relocation —
i.e. gcc inlined the step into them and the fused site is a libm call:

| tier | bodies carrying an inlined `fma` |
|---|---|
| `_Update` | 30 |
| `_UpdateAndFill` | 30 |
| `_OpenImpl` | 23 |
| `_OpenInternal` / `_OpenAndFillInternal` | 17 |
| surviving `_StepImpl` symbols carrying the fused site out of line | 10 |

The 10 that really do stay out-of-line are `HT_*`, three `CDL*`, `MAMA` — the
big steps. For the other 30 the premise does not hold.

So I marked `Update` and `UpdateAndFill` too and looked at the clones: **80 new
clones, 57 of which contain a `vfmadd`.** They are not empty. The 23 that do not
are mostly the functions whose step stayed out of line — six `HT_*`, three
`CDL*`, `MAMA` — plus `CDLPIERCING`, `SMI` and `T3`. Those clones are not empty
either: they are duplicates of the delegating body, still calling libm.

## And it is still a loss

Host: Intel Xeon @ 2.10 GHz (avx2+fma), 4 vCPU, Ubuntu 24.04, gcc 13.3.0,
CMake Release. Throwaway harness, one tier per process, 100k bars, period 14,
500-bar warm-up through `Open`, min over 21 rounds x 16 processes, arm order
alternated per process. Both arms are full clean builds of the same tree; the
only difference is the marking rule.

| tier | dev (ns/bar) | marked | ratio | object |
|---|---|---|---|---|
| `TA_ATR_Update` | 4.1827 | 3.4724 | **1.205x** | changed |
| `TA_EMA_Update` | 4.2545 | 4.1382 | 1.028x | changed |
| `TA_T3_Update` | 25.2107 | 26.3646 | **0.956x** | changed |
| `TA_TRIX_Update` | 9.7627 | 11.3875 | **0.857x** | changed |
| `TA_SMA_Update` | 3.4561 | 3.4536 | 1.001x | byte-identical |
| `TA_RSI_Update` | 9.6896 | 9.7158 | 0.997x | byte-identical |
| `TA_MFI_Update` | 6.6671 | 6.6980 | 0.995x | byte-identical |
| `TA_ATR_Peek` | 2.4782 | 2.4784 | 1.000x | changed file, marked in both arms |

**Control group.** 162 of the 202 `src/ta_func` objects are byte-identical
between the arms; the 40 that differ are exactly `FUSING_INVENTORY`. The three
byte-identical rows span 0.995x–1.001x, which is this host's floor.

**Single-object isolation.** Because a 40-file diff moves layout, I rebuilt two
more arms with the mark applied to one function each (`generate --func=` writes
one file), verified by `cmp` that exactly one object differs, and re-ran:

| arm | measured row | ratio | the same run's byte-identical rows |
|---|---|---|---|
| ATR only | `TA_ATR_Update` | **1.202x** | SMA 1.000x, RSI 0.986x, TRIX 1.008x |
| TRIX only | `TA_TRIX_Update` | **0.872x** | SMA 1.001x, ATR 1.003x, RSI 1.006x |

Both effects survive with one object changed, so neither is layout.

**Bits.** All eight tiers above produce byte-identical output across the arms
(99,500 doubles each, md5 per tier).

## Why "it has a vfmadd" is not the rule

`TA_TRIX_Update` on `dev` makes **three** libm `fma` calls per bar. Its `.fma`
clone has three `vfmadd` and no call at all, and is 49 instructions against the
baseline's 82 — and it is 0.87x. I verified at runtime, by comparing
`&TA_TRIX_Update` against the clone addresses, that the ifunc resolves to the
`.fma` clone (as it does for ATR, EMA and T3). **I did not root-cause the
regression** — I have no perf counters here, and I am not going to guess at a
microarchitectural story.

`T3` shows a second cost the old text does not mention: on `dev`
`TA_T3_Update` is 111 instructions with the step inlined; marked, its `.default`
clone is 34 instructions with the step called out of line. Attributing a
function can *cost* it the inlining it already had.

## What this leaves open

`TA_ATR_Update` alone is 1.202x with one object changed, and it is the tier
#338 just rewrote. Capturing it would need a per-function opt-in, which this
tree has no knob for and which I am not proposing — one function is not a rule.
If you want that knob, the number is real and reproduced; say so and I will
build it properly rather than bolt a name list onto the emitter.

## What it costs

Nothing. The change is a comment. The measurements above are the argument for
*not* making the code change they describe.

For the record, had it been made: `libta-lib.a` 4,246,486 → 4,339,534 bytes
(+2.19%).

## What I did not check

- **clang** — gcc 13.3 only. clang picks its own inlining, so the census (which
  bodies carry an inlined step) is a gcc number, not a portable one.
- **aarch64 / macOS / musl** — no hardware, and `TA_FMA_MULTIVERSION` is inert
  outside glibc/x86-64 anyway.
- **`-O3` / LTO / `-march=native`** — the shipped Release configuration only.
  The old text quotes `--param max-inline-insns-auto` at `-O3`; my census is at
  the flags the library actually ships with.
- **Why TRIX regresses** — measured and reproduced under isolation, not
  explained.
- **The batch tiers** — untouched by this rule and not re-measured.
- **A second host** — one box, one CPU model.
