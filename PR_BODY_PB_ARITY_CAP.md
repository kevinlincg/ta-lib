test(period-boundary): the sweep's output-arity cap is the enforced one

`src/tools/ta_regtest/ta_test_func/test_period_boundary.c` carried its own
`#define PB_MAX_OUTPUT 3` — a second hand-written output-arity literal beside
the `CODEGEN_MAX_OUTPUTS` that #352 (PR #354) made a startup guard. Three loops
in that file size and bind from it: the output binding, the finite/normal scan,
and the server-verify pointer table.

This points the define at `CODEGEN_MAX_OUTPUTS` and adds the include. One line
of code; no behavioural change at the current cap of 3.

## What the cap being separate actually costs

I checked the failure mode rather than assuming it, and it is **not** the silent
one. Lowering `PB_MAX_OUTPUT` to 2 on dev and running the suite:

```
Fail: sweep ACCBANDS.optInTimePeriod=2: retCode 11, expected TA_SUCCESS
```

A function wider than the cap never gets its last output bound, so `TA_CallFunc`
answers `TA_BAD_PARAM` and every strict case in the sweep fails. Loud — but the
message names a parameter value that is perfectly valid, says nothing about an
arity cap, and arrives deep inside `PERIOD1/BOUNDARY` rather than at startup.
That is the whole defect being fixed: a misleading diagnostic and a second place
to remember, not an unverified output.

With the define collapsed, the same drift is the #352 guard's message, before
any test runs:

```
FAIL - ACCBANDS has 3 outputs but CODEGEN_MAX_OUTPUTS is 2.
       Raise it in test_codegen.h; the harness buffers and
       clamped loops size from it.
```

## Verification

- Full `bin/ta_regtest` on this branch: all tests succeeded.
- **Control, run here and watched:** with the define collapsed, setting
  `CODEGEN_MAX_OUTPUTS` to 2 fails at startup with 8 `FAIL -` lines (every
  3-output function) and `rc=94`; restoring it to 3 and rebuilding from the same
  tree is green again.
- `regen-check` green (this file is hand-written, so the gate is a no-op here —
  run to confirm the include does not perturb generation).

## What I did not check

- No `--codegen`, `--xlang-hash` or cross-language run: this environment has no
  `ta_ref_serve` worktree and no .NET SDK. The change is one `#define` in a
  hand-written C test and touches no generated file, so nothing in those legs
  can see it — but I did not run them.
- The next function to raise the cap is HA (#373, four outputs), where this
  edit removes one of the three hand-written raises. I have not sequenced the
  two; whichever lands first, the other's raise becomes a no-op or a one-line
  conflict in this define.
