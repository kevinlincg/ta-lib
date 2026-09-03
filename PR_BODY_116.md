test(gate): one constant picks the released baseline, and the gate refuses one it was not measured against (#116 step 1)

#116 step 1 asks for the differential-oracle machinery to be parameterized on a
version string "so rolling to a new released baseline is a one-line change".

Today it is not one line. The release is spelled into the build script's name
and `REF_TAG`, the `../ta-lib-064` worktree, the `ta_064_serve` binary, the
`--fuzz-064` flag, the `build.py fuzz-064` target, the `fuzz-vs-064` nightly job
in both workflows, and ~120 comment mentions across Python, C and Rust.

## What moved

One constant decides which release gets built:

```python
# scripts/serve_version.py
RELEASE_TAG = "v0.6.4"
```

and the worktree path derives from it (`../ta-lib-v0.6.4`). Everything else is
now version-free:

| was | is |
|---|---|
| `scripts/build_064_serve.py` | `scripts/build_baseline_serve.py` |
| `bin/ta_064_serve` | `bin/ta_baseline_serve` |
| `ta_regtest --fuzz-064` | `ta_regtest --fuzz-baseline` |
| `build.py fuzz-064` | `build.py fuzz-baseline` |
| `fuzz-vs-064` (both nightlies) | `fuzz-vs-baseline` |

## One deviation from the issue's sketch, and it is the load-bearing one

The issue sketches `ta_064_serve` -> `ta_XXX_serve`, i.e. keeping the version in
the filename and adding `ta_081_serve` when 0.8.1 ships. I did the opposite: the
binary name carries no version, and the version travels **inside** it — the
build stamps `RELEASE_TAG` into the serve's own `list_functions` answer.

The reason is the goal itself. A versioned filename is precisely the literal
that forces a C-side edit on every roll (`argv_064[] = {"./ta_064_serve"}`), so
`ta_081_serve` would keep the roll at two languages instead of one. **If you
would rather keep the versioned filename, say so** — it is one line in
`serve_version.py` plus the driver learning the name from the environment, and I
will redo it that way.

## The roll is deliberately not one line, and that is the finding

`test_codegen.c` declares the same tag separately:

```c
#define FUZZ_BASELINE_TAG "v0.6.4"
```

The driver reads the oracle's stamp and **refuses to run** when the two
disagree, naming what has to be re-derived.

This is not belt-and-braces. `FUZZ_BASELINE_TOL`, the per-case skips (#98, #107,
#244, #253, #118, #242), `FROZEN_ORACLE_MATYPE_MAX` and
`FMA_TRANSITION_TOLERANCE` are all statements about **one release's** behaviour.
On today's run they absorb 11,620 manifest-tolerated cases, 15,973
FMA-rebaseline cases and 9,323 skips. Moving `REF_TAG` alone would carry every
one of them onto a different library and still print `PASS` — a gate tolerating
tens of thousands of cases for reasons that had stopped applying, with nothing
saying so.

So the roll is: `RELEASE_TAG`, then the work the refusal names, then
`FUZZ_BASELINE_TAG`. The build side is the one line the issue asked for; the
second declaration is the speed bump, and it cannot be skipped because the gate
will not run until it is moved. That is a deliberate reading of the ask rather
than the literal one — **your call if you would rather it be genuinely one
line**, in which case the manifest and the skips need a different home.

Refusal text:

```
FAILED: the oracle is frozen at v0.6.4, but every comparison rule here
        was measured against v0.8.1.
        Rolling the baseline is not a rename. FUZZ_BASELINE_TOL, the
        per-case skips (#98, #107, #244, #253, #118, #242),
        FROZEN_ORACLE_MATYPE_MAX and FMA_TRANSITION_TOLERANCE each
        describe v0.8.1 specifically — re-measure or delete every one of
        them, then move FUZZ_BASELINE_TAG.
```

## Two things that used to pass quietly

- **A list_functions answer the driver cannot use** was a `warning: ... subset
  gate disabled` and the run carried on. That does not merely lose coverage: it
  leaves every post-baseline function compared against a stub that answers
  `TA_BAD_PARAM`. Now a hard failure.
- **A worktree left at the previous tag** was reused silently, so a rolled
  `REF_TAG` would have diffed against the old release under the new name. The
  path now carries the tag, so a roll cannot land on one; a worktree moved off
  the tag by hand is refused with the command to remove it.

## #161: carried forward, not fixed

The issue's constraint — the generalized script must not *silently* inherit the
metadata circularity. It does inherit it, and the comment at the call site now
says so explicitly (the serve's `ta_abstract` compiles from the current tree, at
every release it is ever pointed at), as does the new paragraph in
`ta_regtest/CLAUDE.md`. Stripping the abstract layer is not attempted here.

## Cost

- **The frozen worktree is rebuilt once** on every dev machine and CI runner,
  because `../ta-lib-064` becomes `../ta-lib-v0.6.4`. ~40 s here. The old
  directory is left where it is (and stays registered in `git worktree list`)
  rather than removed by a script.
- **`--fuzz-064` is gone, not aliased.** Muscle memory and any local script
  break. Deliberate — a deprecated alias for a dev-only flag is the scaffolding
  this repo strips — but easy to add back if you would rather.
- **~120 comment mentions churned** across 26 files. The prose that describes
  *v0.6.4's specific behaviour* was left naming v0.6.4 on purpose; only the
  plumbing was genericized.

## Verified

All on this branch, dev `58a0ac54`, gcc 13.3.0 / glibc 2.39, x86-64.

| check | result |
|---|---|
| `build.py fuzz-baseline` | **PASS** — 166852 comparisons, 139064 matches, 195 benign, 11620 manifest-tolerated, 15973 FMA-rebaseline, **0 failures** |
| same, on `dev` before the change | byte-identical counters and every skip line identical; the only diff in 59 lines of gate output is the oracle's own name |
| `build.py regen-check` | OK — `ta_codegen output matches the committed source` (the `server_gen.rs` edits are comments; no generated file moved) |
| `build.py test` (full C suite) | `* All tests succeeded *` |
| `build.py check-source-lists` | OK — 56 files agree across CMake and autotools |
| `build.py xlang-hash --language=rust` | PASS — 284,595 Rust cases, 0 mismatches |

**Controls — each was broken and watched to fail, not asserted:**

| control | result |
|---|---|
| `FUZZ_BASELINE_TAG` moved to `v0.8.1`, oracle still v0.6.4 | **red**, exit 84, the refusal above |
| oracle built with the stamp omitted (i.e. a pre-#116 binary) | **red**, exit 84, `reports no baseline_tag` |
| oracle whose `list_functions` answer is unusable | **red**, exit 79 (was a warning + green before this PR) |
| `../ta-lib-v0.6.4` checked out at v0.6.3 by hand | **red**, build script refuses and names the removal command |
| clean tree, either side of every control | green, same counters |

**I did not check:** the Java and C# arms of `xlang-hash` (no .NET SDK on this
box; the Java arm was not run either), `regtest.py --codegen`, and the autotools
dist path. The workflow renames are a YAML edit only — no CI run has executed
them, and `fuzz-vs-064` is referenced by no `needs:` in either workflow, which I
checked by grep rather than by running.

_Head re-verified after merging dev `67936169` (which landed #338): `regen-check` green, exit 0; generator suite 902 passed / 0 failed. The net diff against dev is unchanged by that merge._
