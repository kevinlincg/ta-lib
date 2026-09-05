build(fma): admit clang on glibc/x86_64 to the FMA dispatch guard (#380)

Partial answer to #380, orthogonal to the three options there: it removes the
regression on **glibc/clang** without touching a single bit, and leaves the
macOS / musl / MSVC / i386 / armhf rows — and therefore the decision — exactly
where they are.

`TA_FMA_MULTIVERSION` carried `&& !defined(__clang__)`, so no clang build ever
got the dispatch. On glibc that exclusion is unnecessary: clang implements
`target_clones` on x86 from clang 14, and clang 18 emits the same
resolver + `.default` + `.fma` clone pair gcc does. Below 14 the attribute is a
hard compile error, so `__clang_major__ >= 14` is a build gate, not a tuning
choice. Apple clang numbers itself on its own scale and would misread that
floor — it never reaches the test, because Mach-O has no ifunc and macOS has no
`__GLIBC__`.

## Measurement

Host: Intel Xeon @ 2.80 GHz (avx2+fma), Ubuntu 24.04, glibc 2.39, clang 18.1.3,
x86_64, 4 vCPU. Shipped `libta-lib.a` (CMake Release, no `-march`), throwaway
harness, one tier per process, min over 21 rounds x 12 processes, 100k bars,
period 14. Arms are two full clean builds from two worktrees: `dev` at 8c0fedbc
and the same tree with this patch.

| tier | dev (clang) | patched | ratio | object |
|---|---|---|---|---|
| `TA_ATR` batch | 2.3538 | 1.3375 | **1.760x** | changed |
| `TA_EMA_Peek` | 4.1192 | 3.2355 | **1.273x** | changed |
| `TA_ATR_Peek` | 6.1773 | 5.2743 | **1.171x** | changed |
| `TA_EMA` batch | 2.3536 | 2.3537 | 1.000x | changed |
| `TA_ATR_Update` | 7.3537 | 7.6479 | 0.962x | changed file, identical code |
| `TA_EMA_Update` | 3.8368 | 3.8682 | 0.992x | changed file, identical code |
| `TA_SMA` batch | 2.3537 | 2.3535 | 1.000x | byte-identical |
| `TA_SMA_Peek` | 1.9225 | 1.9200 | 1.001x | byte-identical |
| `TA_SMA_Update` | 3.8320 | 3.8322 | 1.000x | byte-identical |
| `TA_RSI` batch | 6.9813 | 6.9044 | 1.011x | byte-identical |
| `TA_RSI_Peek` | 15.5476 | 15.5305 | 1.001x | byte-identical |
| `TA_MFI` batch | 13.3209 | 12.5951 | 1.058x | byte-identical |
| `TA_MFI_Peek` | 11.5662 | 11.7647 | 0.983x | byte-identical |

**Control group.** 162 of the 202 `src/ta_func` objects are byte-identical
between the two arms; the 40 that differ are exactly `FUSING_INVENTORY`. The
seven byte-identical rows above span 0.983x–1.058x, which is this host's floor
(binary layout moves, as the `ta-bench` skill warns). The two `_Update` rows
belong with them: `Update` is not marked (Peek-only rule, #337), and
`TA_ATR_Update` disassembles to the same 69 instructions in both arms, differing
only in relocation targets — so the honest band is **0.962x–1.058x**, and only
the three bolded rows clear it.

**Bits.** ATR, NATR, EMA, T3, KAMA and TRIX over periods 2..60, plus 99,800
interleaved `TA_ATR_Peek`/`TA_ATR_Update` calls — 284,610,840 bytes of raw
`double` — hash identically across the two arms (md5 `55858b13…`). That is
discriminating rather than vacuous precisely because the same library is 1.760x
faster on the ATR batch tier in one arm: it demonstrably ran the `vfmadd` clone
and still produced the same bits, which is the `-ffp-contract=off` invariant
doing its job. `ta_regtest` passes in full on both arms.

**gcc is untouched**: all 202 objects are byte-identical across the two arms
when built with gcc 13.3.

## The gate

`clang-glibc-build` is the other half of the existing `musl-build` job: musl
asserts the dispatch is *absent*, glibc/clang now asserts it is *present*, then
runs the C reference suite against the dispatched library. Nothing else in the
tree builds with clang on Linux — the only clang coverage is macOS, where the
guard is correctly inert — so the clone going missing again would read as a
green nightly.

I ran its assertion against both arms locally and watched it go red: `readelf`
counts **0** `TA_*` IFUNC symbols in the `dev` DSO and **480** in the patched
one.

## What it costs

- Clang/glibc/x86_64 binaries grow by the clone set: `.text` 1,600,340 →
  1,758,852 (+9.9%), `libta-lib.so` +234 KB, `libta-lib.a` +325 KB. This is the
  same cost gcc/glibc builds have carried since #96; it is new only for clang.
- One more nightly job, shaped like `musl-build` (a full CMake build plus the C
  reference suite).
- It does **not** resolve #380. macOS, musl, MSVC, i386 and armhf still take the
  libm call, so options 1–3 there are untouched and still yours to pick. If you
  choose option 1 (unfused everywhere), this patch becomes redundant rather than
  wrong — the guard would simply have nothing left to dispatch on for these
  sites.

## What I did not check

- **aarch64** — no hardware. Unchanged by this patch either way (`__x86_64__`).
- **musl** — no Docker in my environment. The `__GLIBC__` requirement is
  untouched, and the nightly `musl-build` job still covers it.
- **macOS / Apple clang** — unchanged; still excluded by `__GLIBC__`, so #380's
  original measurement stands as written.
- **clang < 14** — I have only clang 18. The `>= 14` floor comes from the
  release notes that introduce `target_clones` on x86, not from a build I ran.
- **The new nightly job on an Actions runner** — I ran its commands locally, not
  on GitHub.

## One lead I am not acting on here

`TA_ATR_Update` carries the fused arithmetic inlined (a bare `call fma@PLT` in
the object, on gcc and clang alike) and is not marked, per #337's Peek-only
rule. Marking it does produce a real `.fma` clone with `vfmadd` on both
compilers — the rule's stated reason (the clone comes out empty) does not hold
for it. My quick probe read 1.46x on gcc / 1.32x on clang for `TA_ATR_Update`,
but that probe marked *every* `Update` unconditionally, and its control
(`TA_SMA_Update`, which contains no `fma` at all) moved 1.154x on the same run —
so no number there is claimable and I am not proposing the change. It looks
worth its own issue and a clean experiment.
