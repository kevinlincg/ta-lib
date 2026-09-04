test(stream_verify): the aggregate folded a leg that reported no flag (#355)

Closes the second, unresolved half of #355. The first half — the MACDEXT
reproducer itself — was a peek refusal, and `eea41c65` fixed it; the reproducer
no longer reproduces, and not because of anything here (`bbaae6c1`'s
`TA_MAType_RMA` shifted the `rand()`-driven vector the seed lands on, so that
exact command is no longer replayable at all).

What is left is the part the issue could not resolve from the response:

> Every `*_ok` field in the response reads 1 while the aggregate `ok` reads 0.
> Whatever sub-check flipped the aggregate is not one of the flags the response
> reports. Either a leg exists that has no reported flag, or the aggregation
> disagrees with its own components.

It is the first reading. The **base leg** — the `Open`/`Update` value comparison
against batch, plus the empty-batch open-reject precheck and the short-history
reject that share its verdict — is the one leg with no flag of its own. Every
other leg reports one (`fill_ok`, `ufill_ok`, `range_ok`, `value_ok`,
`state_ok`, `clone_ok`, `peek_ok`, `peek_rep_ok`); the base leg is `allOk` /
`all_ok`, which is reported only after the others have folded into it, as `ok`.
So when the base leg is what failed, the response says only that something did.

## The change

**Every response reports `base_ok`**, in all four servers, next to the `ok` it
feeds. In C the named legs fold into `allOk`, so the value is snapshotted at the
last point where `allOk` still means the base leg alone; in Rust, Java and C#
`allOk` is already that, and only the report was missing.

**The driver names it.** `STREAM BASE MISMATCH` is checked before the generic
flag, like every other leg.

**And the class is closed, not just this instance.** If `ok` reads 0 while every
component the servers fold reads 1, that is now its own failure —
`STREAM OK UNEXPLAINED`, saying that the aggregate folds a leg reporting no flag.
The component list lives beside the walk that reads it
(`STREAM_OK_COMPONENTS`), so a leg added to a server's fold without a flag lands
there instead of costing the next reader a source read.

Two adjacent defects, found while establishing the above and fixed here because
they are the same statement:

* Java's Value-accessor compare (`value()` must report the bar `update` just
  wrote) set `allOk = false` with **no diag and no flag** — the only remaining
  base-leg site in any backend that named nothing at all. It now sets a diag
  like every other one. C#'s equivalent already did; C and Rust report the leg
  as `value_ok`.
* Java and C# emitted **`peek_rejects` twice in one JSON object**. Harmless
  today only because `stream_flag` takes the first match.

## Gate, and its control

`sv_every_response_reports_the_base_leg` (generator, emitted-text) pins one
`base_ok` per `ok` and one `peek_rejects` per `peek_rep_ok`, in all four
backends. It has to be an emitted-text gate: a backend that stopped reporting
`base_ok` would go back to answering the anonymous aggregate, which is green
until something fails.

Both halves sabotage-proved:

* dropping `base_ok` from the C# response alone →
  `csharp: 372 stream_verify response(s) report ok, 191 report base_ok`
* reinstating the duplicate key on the Java response →
  `java: 242 response(s) report peek_rep_ok and 423 report peek_rejects`

## The driver messages, proved against a real failure

A `all_ok = false` injected into the Rust server's `sv_sma`, and the same
sabotaged server driven by three different drivers:

| driver | prints |
|---|---|
| dev, unmodified | `STREAM MISMATCH [TA_SMA]` — response `... value_ok:1, ok:0, peek_ok:1 ...`, i.e. #355's symptom, verbatim |
| this PR | `STREAM BASE MISMATCH [TA_SMA]` — `base_ok:0` |
| this PR, with the server additionally forced to report `base_ok:1` | `STREAM OK UNEXPLAINED [TA_SMA]` |

The middle row is the fix; the third is what makes the class closed rather than
this one instance patched.

## Verification

* `--codegen`, unfiltered: **161 passed / 0 failed** in each of C, Rust, Java.
* `regen-check` clean; `check-source-lists` clean; `build.py clippy` clean;
  generator `cargo test` 432+ green including the new gate; full C `ta_regtest`
  suite green.
* Response shape confirmed by hand against the running C, Rust and Java servers.

**What I did not check.** There is no .NET SDK on the machine I built this on, so
the **C# server was never run** — its change is compile-checked by the PR gate's
`Generated C# compiles` job and pinned by the new emitted-text gate, and that is
all I can say for it. I also did not run `--xlang-hash` (same reason: it needs
the .NET SDK) or `synth_gate.py`. Neither touches the diff's surface — nothing
here reaches an indicator body, `src/ta_func/` is untouched, and the generated
library sources are byte-identical — but I did not run them and am not claiming
they pass.

## Cost

`base_ok` is one more integer per `stream_verify` response, on a path that only
the test harness drives. Nothing in the shipped libraries changes.

## Adjacent, NOT fixed here — a separate hole worth its own issue

**Java and C# run the Value probe but report no `value_checked` / `value_legs` /
`value_ok`**, where C and Rust report all three. Two consequences: a Value
mismatch there reaches the driver only through the aggregate (which is why the
diag above is the fix available in this PR), and the driver's coverage floor —

```c
if( ctx.streamValueFunctions > 0 && ctx.streamValueFunctions != ctx.streamFunctions )
```

— is guarded on `> 0`, so for those two backends it never runs. The leg can go
absent in Java or C# and nothing would say so; that is exactly the failure mode
the fork leg's own counter exists to prevent. Reporting the three fields would
activate that floor for both, which is the right change and a bigger one than
this PR should carry — and I cannot run the C# half of it here. Happy to take it
as a follow-up if you want it.
