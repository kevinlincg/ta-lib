Re-measured on Intel, and on two of the four no-clone targets — the clang row does not reproduce, and musl is a regression on every tier

Taking up the request to re-measure `67936169a` off the Zen 4 box. Exact one-commit
A/B: `2d8a2381` (immediately before) against `67936169a`, so the only difference
between the arms is this change.

**Host: Intel Xeon @ 2.10GHz, 4 vCPU, KVM/container, Linux 6.18, gcc 13.3.0,
clang 18.1.3, glibc 2.39, musl via `musl-gcc`.** This is a cloud vCPU, not bare
metal — absolute ns/bar run ~1.2-1.3x the Zen 4 laptop's and the per-round spread
is 30-90%. The ratios below are min-of-3-inner per process, min across 21 rounds
with the arm order alternating per round, one case per process, `taskset`-pinned,
linking the shipped `libta-lib.a` (separate TUs, no LTO), every peek result
`black_box`'d rather than accumulated. Ten untouched functions (RSI, SMA, EMA,
TRANGE, CCI, MFI, DX, ADX, STDDEV, WMA) ride through the same run as the control.

Three arms, each verified to be the code path it claims by `nm` on the archive:
gcc+glibc carries `TA_ATR.fma`/`TA_ATR_Peek.fma` plus resolvers; the clang and musl
archives carry none, so those two are real no-clone builds rather than a forced
`#else`.

---

## 1. Intel, gcc + glibc — every tier at least as good as Zen 4, four of them better

Controls read **0.947-1.010x** (min-based), so nothing inside that band is readable.

| tier | before | after | **this box** | Zen 4 (issue) |
| --- | ---: | ---: | ---: | ---: |
| `TA_ATR` batch | 4.795 | 1.070 | **4.48x** | 4.71x |
| `TA_NATR` batch | 4.789 | 1.223 | **3.92x** | 3.52x |
| `TA_SUPERTREND` | 8.843 | 5.943 | **1.49x** | 1.42x |
| `TA_KC` | 12.287 | 9.084 | **1.35x** | 1.30x |
| `TA_ATR_Update` | 6.405 | 2.813 | **2.28x** | 1.74x |
| `TA_ATR_Peek` | 2.284 | 1.894 | **1.21x** | 1.23x |
| ATR peek-then-commit | 6.492 | 4.870 | **1.33x** | 1.17x |
| `TA_ATR_UpdateAndFill` | 6.048 | 2.228 | **2.71x** | 2.13x |

### The chain probe, and the one prediction that does not hold

Both chains timed in the same process, 7 reads:

* `v *= n-1; v += x; v /= n` — **4.864-6.095 ns/op** (median 4.98)
* inline `vfmadd` — **0.972-1.207 ns/op** (median 0.99)
* chain ratio — **4.97-5.19x**

`TA_ATR` batch before is 4.795 against a 4.86 divide chain — **98.5%** of it, matching
the issue's 98% and confirming the Wilder step is the entire per-bar cost here too.
After is 1.070 against a 0.972 fma chain, **91%**.

The derivation in the request was that Intel should read *larger* than 4.71x because
`mulsd`/`addsd` are 4 cycles against 3. **The ceiling is indeed larger here — 5.0-5.2x
against Zen 4's 4.71x — but the realized batch speedup is smaller: 4.48x.** ATR captures
90% of the available ceiling on Zen 4 (4.71/4.71) and 87% of it here. So the prediction
holds for the chain and inverts for the tier that matters. On a 2.1 GHz vCPU I would not
read the 4.48-vs-4.71 gap as a property of Intel; the ceiling being higher is the solid
half.

## 2. clang + glibc, no clone — the modelled regression does not reproduce

This is the third question, built for real rather than by forcing the `#else`.
Controls **0.981-1.056x**.

| tier | before | after | **measured** | forced-`#else` model (issue) |
| --- | ---: | ---: | ---: | ---: |
| `TA_ATR` batch | 4.788 | 1.150 | **4.16x** | 2.13x |
| `TA_NATR` batch | 4.853 | 1.639 | **2.96x** | 1.65x |
| `TA_ATR_Peek` | 2.101 | 2.095 | **1.00x** | **0.73x** |
| `TA_ATR_Update` | 6.089 | 2.920 | **2.09x** | — |
| `TA_ATR_UpdateAndFill` | 6.140 | 2.361 | **2.60x** | — |
| `TA_SUPERTREND` | 9.021 | 6.857 | **1.32x** | — |
| `TA_KC` | 11.947 | 8.418 | **1.42x** | — |

**No regression anywhere, and `Peek` is flat rather than 0.73x.** The mechanism:

| | `fma()` | same chain as `b*v+c` | ratio |
| --- | ---: | ---: | ---: |
| glibc 2.39 | **0.982 ns/op** | 1.462 | **0.67** |
| musl | **11.416 ns/op** | 1.708 | 6.68 |

glibc's `fma` is ifunc-dispatched to a hardware FMA3 implementation, so it is *cheaper
than a multiply plus an add* even paying the call. Losing the clone therefore costs
about **7%** on the ATR batch tier (1.070 with it, 1.150 without), not 2.2x. Whatever
made the forced-`#else` arm read 1.877 on the Zen 4 box, it is not the absence of the
clone as such on a glibc target — **I cannot account for that gap and am not going to
guess at it.**

**This does not clear Intel macOS.** That target is clang + *libSystem*, and the row
above is clang + glibc. If Apple's `fma` is hardware-dispatched the way glibc's is, macOS
should look like this row; I have no Mac and did not measure it.

## 3. musl x86-64 — this is where the regression actually lives

Also on the request's list, and the arm nothing had measured. Controls **0.973-1.067x**;
every marked row below is far outside that band.

| tier | before | after | **measured** |
| --- | ---: | ---: | ---: |
| `TA_ATR` batch | 4.774 | 9.296 | **0.51x** |
| `TA_NATR` batch | 4.870 | 9.660 | **0.50x** |
| `TA_SUPERTREND` | 9.033 | 14.290 | **0.63x** |
| `TA_KC` | 24.174 | 29.101 | **0.83x** |
| `TA_ATR_Update` | 6.145 | 10.940 | **0.56x** |
| `TA_ATR_Peek` | 2.347 | 9.017 | **0.26x** |
| ATR peek-then-commit | 6.543 | 18.534 | **0.35x** |
| `TA_ATR_UpdateAndFill` | 6.009 | 10.806 | **0.56x** |

Every tier regresses, `Peek` by **3.8x**. The cause is the 11.416 ns/op above: musl's
`fma` is a correctly-rounded software implementation, 11.6x glibc's and 6.7x its own
`b*v+c`. The `before` column matches the gcc+glibc `before` column to within 1% on ATR,
and the ten controls are flat, so this is the fused step and not a slower toolchain.

**The clone cannot rescue this**, exactly as `ta_utility.h` already says. Confirmed
rather than taken on trust: `target_clones("default","fma")` compiles and *statically*
links and runs under `musl-gcc` here, but the dynamic link dies at load with
`Error relocating: unsupported relocation type 37` — musl's linker has no ifunc. A
musllinux wheel or Alpine package ships the shared object, so that is the shipped path.

**Inference, not a measurement:** an unfused two-coefficient form would evaluate through
musl's `b*v+c` at 1.708 ns/op and should turn this column into a win of roughly the same
size the other platforms see, rather than a loss. That would extend the issue's
"unfused has no regression anywhere measured" to musl as well. **I did not build the
unfused arm** — flipping it needs the per-site opt-out the generator does not have, as
the issue notes.

## What this changes in the trade as recorded

The issue's platform story is that fused wins big on gcc+glibc and wins *less* on the
no-clone targets ("slower than today on Peek and slower than the unfused form on
batch"). On these two no-clone targets it splits instead:

* **clang + glibc: no regression at all** — 4.16x batch, `Peek` flat. The no-clone
  penalty is ~7%, because the libm call is already hardware FMA.
* **musl: a regression on all eight tiers**, up to 3.8x on `Peek`, and unfixable by the
  clone mechanism.

So the dividing line is the **libm's `fma`**, not the compiler and not the clone. That
is a sharper criterion than `__GLIBC__ && !__clang__` describes, and it is the one that
would tell you which platforms the fused form is safe on.

## What I did not check

* **aarch64 / Apple Silicon — not measured at all.** No hardware. This is the row the
  request most wanted and the one I cannot supply.
* **Intel macOS** — the clang row here is glibc, not libSystem (see above).
* **i386 and MSVC** — not built.
* Bare-metal Intel. This is a 2.1 GHz cloud vCPU with 30-90% per-round spread; I trust
  the ratios against their control bands, not the absolute ns/bar.
* No correctness gate was re-run for this — it is a timing measurement on two builds of
  commits that already passed them. The `before`/`after` ATR and NATR last-bar values do
  differ in the last bit or two, which is the change being present in these binaries and
  nothing more.
* The unfused arm, on any platform.
