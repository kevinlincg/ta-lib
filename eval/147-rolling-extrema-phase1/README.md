# #147 Phase 1 — rolling min/max evaluation artifacts

**This branch is not a merge candidate.** It exists so the Phase 1 material behind
[TA-Lib/ta-lib#147](https://github.com/TA-Lib/ta-lib/issues/147) can be read, re-run and
disagreed with as files instead of as comment attachments — the maintainer asked for
"harness, METHOD.md, the candidate inputs and the per-layout CSVs as a branch".

Nothing outside `eval/147-rolling-extrema-phase1/` is touched. No library source, no
`ta_codegen/input/`, no generated output, no build files. The candidate `.c` files here
are *copies* living under `eval/`, staged into `ta_codegen/input/` by `harness/gate.sh`
at run time and reverted afterwards.

Branch base: `dev` @ `0d8890eba` ("perf(rust): runtime FMA dispatch for the 26 fused
indicators (#156)").

---

## Read this first: the numbers were measured on a pre-`71bdfbd4c` corpus

Every measurement in `RESULTS-passA.txt`, `harness/res*.csv` and `harness/final.log` was
taken on `8fb6222d4` — the `#153` corpus branch **as originally submitted**, before the
maintainer's hardening commit `71bdfbd4c` ("fix(ta_bench): harden the #153 corpus — argv,
self-check, MAVP, regen gate") landed on `dev`. This was disclosed in the #147 Phase 1
comment and is repeated here so the branch does not read as more current than it is.

Consequences, per shape:

| shape | status on this corpus build |
|---|---|
| `randwalk` | **byte-identical** to current `dev` (shape index 0) |
| `mono-up`, `mono-down`, `constant` | deterministic, unaffected |
| `randwalk-lo`, `randwalk-hi`, `gbm`, `trend-chop-{0.5,1,2,4}p` | **different RNG streams** from current `dev` |

Each shape is still a valid instance of its class and every conclusion in the report is
stated per-shape, so the per-shape verdicts stand. Two things must **not** be read off
these numbers:

* **cross-regime ordering** among the four `trend-chop-*` series — that sweep varied path
  and regime length together, so it is not a controlled sweep across regime length;
* the **gbm-vs-randwalk "within 1%" control claim** past period ~200 (see PROGRESS.md /
  the #147 comment — the divergence there is on the pre-fix streams).

Phase 2 re-measures on the post-`71bdfbd4c` corpus.

---

## Layout

```
eval/147-rolling-extrema-phase1/
  README.md                this file
  issue147-phase1.md       the Phase 1 report as posted to #147, verbatim
  PROGRESS.md              working log: findings, dead ends, root causes, decisions
  STREAMING.md             the streaming / per-bar-latency answer, per candidate
  RESULTS-passA.txt        rendered result tables (median [min..max] over 12 layouts)
  harness/
    METHOD.md              the measurement method — read before the numbers
    driver.c               benchmark + correctness driver (incl. the +-0 / tie gate)
    shim.h  guarded.c      TU shim; guarded-vs-unguarded prologue probe
    mkvariants.py          emits variants/<f>.<cand>.c
    build.sh               builds 12 code layouts (pad / link-order / alignment)
    run.sh chain.sh final.sh queue.sh   drivers used for the passes
    aggregate.py           per-layout CSV -> median [min..max] tables
    gate.sh                in-tree 4-backend gate for one candidate
    verdict.py             gates/*.log -> one verdict line per candidate
    guarded.sh guarded_pass.sh          guarded-vs-unguarded pass
    variants/              THE CANDIDATE INPUTS: {max,midpoint}.C0..C9.c
    upstream/              byte-identical copies of the shipped input .c (the C0 source)
    probes/                earlier throwaway probes, kept for provenance
    gates/                 4-backend gate logs, per candidate
    resA-layouts.csv       per-layout raw: 12 layouts x cand x shape x period, n=100000
    resB-landscape.csv     period/shape landscape pass
    resC-shortrange.csv    n = 256 / 1024 / 8192 (malloc'd candidates' regression region)
    guarded.csv            guarded vs unguarded
    results-partial-L0L1.csv    first two layouts only (superseded by resA)
    final.log run.log chain.out guarded_pass.log   raw run output the CSVs derive from
```

Not committed: `harness/{obj,bin,gen}/` are build products of `build.sh`; earlier
`gates-pass1/` and `gates-pass2/` log sets were superseded by `gates/` and are omitted.

One mechanical edit was made to the raw logs before committing: the absolute path of the
measurement working directory was replaced with the literal `$WORKDIR` (and `$HOME` for
the home directory), by prefix substitution only. Nothing else in the logs was touched.

## The candidate ledger

`C0` is a **byte-identical copy** of the shipped `ta_codegen/input/{max,midpoint}/*.c`
(`diff` against `harness/upstream/` proves it), and `C5` differs from `C0` only in the
rescan comparison operators — one character in `max.c`, two in `midpoint.c`. The
eliminated candidates are kept deliberately: the cause of death is the useful part.

| id | algorithm | `generate` | scratch |
|---|---|---|---|
| C0 | baseline: cached extremum + rescan (byte-identical to shipped) | ok | none |
| C1 | monotonic deque, `malloc` period-sized | ok via fast-path-skip, **after an operand swap** (see below) | 2xO(p) |
| C2 | Van Herk / Gil-Werman, block-batched | ok via fast-path-skip | 2xO(p)/ch |
| C3 | Van Herk / Gil-Werman, per-sample | ok via fast-path-skip | O(p)/ch |
| C4 | two-stack queue | ok via fast-path-skip | 2xO(p)/ch |
| C5 | rescan tie-break `>` -> `>=` | ok (scalar) | none |
| C6 | reverse rescan, strict compare | **ELIMINATED** — `index variable 'i' leaks into the transition body` | none |
| C7 | deque over pow-2 capacity (C1 fairness check) | **ELIMINATED** — generator **panics** on `&` | <=4xO(p) |
| C8 | no cache, full rescan every bar | **ELIMINATED** — same T4 transition failure as C6 | none |
| C9 | incoming arm `else if` -> `if` | ok (scalar) | none |

All ten pass the correctness gate bitwise against C0 (`memcmp` on the doubles, not a
tolerance) over 11 shapes x 22 periods x {plain, in-place, non-zero `startIdx`}, modulo
the sign of IEEE zeros — the class the maintainer ruled benign in #147, and which C0
itself already exhibits against its own oracle on `TA_MIDPOINT`. The one exception is C8
on MAX, which is bit-identical *including* zero sign.

## Generator workarounds baked into these inputs

Three `ta_codegen` issues were worked around in the candidate `.c` rather than fixed, and
the workarounds are still present in the files here. They are filed separately (they are
generator bugs, not #147 algorithm questions) and stay in place for Phase 2:

1. **`&` is not parsed** — the input-C parser has no bitwise-AND, and panics instead of
   diagnosing. Killed C7 outright; no workaround.
2. **`int += int-param` emits an f64 cast in the Rust backend** — worked around by doing
   all wrap arithmetic with `+ cap - 1` and `>= cap` so no index ever goes negative.
3. **subscripted-usize cast placement** — `if( dqI[hd] < trailingIdx )` renders as
   `if (dqI[hd]) as usize < trailingIdx {`, which does not parse. Worked around by
   swapping the operands (`trailingIdx > dqI[hd]`) — visible in `variants/max.C1.c` and
   `variants/max.C7.c`.

A fourth, `blockStart += optInTimePeriod;` dropping its `as usize`, eliminated C2 on the
first gate pass and was **fixed upstream** in `fc7ab50dd` + `58dca3b6c`; re-verified
fixed, so that workaround is no longer needed.

## Re-running it

The harness is deliberately **out of tree** — it borrows only
`src/tools/ta_bench/bench_corpus.h` (#153) by `#include`, so the input classes are the
project's own and no bespoke input is invented. It was run from a sibling directory
layout and `build.sh` / `gate.sh` still expect that:

* `$harness/../ta-lib` — a ta-lib checkout to build against and to stage candidates into.
  From in-tree that means: `ln -s ../../.. eval/147-rolling-extrema-phase1/ta-lib`
  (or copy the `harness/` directory next to a checkout).
* `$harness/../jdk/jdk-17.0.20+8/...` and `$harness/../dotnet/sdk/dotnet` — optional; only
  `gate.sh` uses them, and only because the system JDK/.NET on the measurement box were
  too old for the Java (`--release 17`) and .NET (`net10.0`) backends.

Then:

```sh
cd eval/147-rolling-extrema-phase1/harness
./build.sh                 # 12 layouts; verify with `nm -n` that code actually moved
./run.sh                   # -> resA-layouts.csv
python3 aggregate.py       # -> the RESULTS tables
./gate.sh C2               # in-tree 4-backend gate for one candidate
python3 verdict.py         # gates/*.log -> one verdict line per candidate
```

`verdict.py` judges each candidate as a **delta against the C0 control**, not as an
absolute "0 mismatches", because the pristine tree already fails two of these gates on
this box for reasons outside this family (14 `TA_HT_TRENDMODE` C-vs-Java transcendental
tolerance mismatches under `--xlang-hash`; `bin/ta_064_serve` not built). The reasoning
is in the module docstring.

## Machine

iMac20,1 — Intel i7-10700K, x86-64, Apple clang 21.0.0. Single platform. GCC x86-64,
MSVC x86-64 and Apple-silicon arm64 are Phase 2. Per the maintainer's own note, a result
from one machine is not transferable, so nothing here is a recommendation.
