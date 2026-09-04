refactor(streaming): a Rust peek frame drops the binds nothing reads (#353)

Closes #353.

## What was wrong

Localizing a state write turns *a store into the handle that nothing read* into
*a store to a local that nothing reads*, and the bind carrying it is then a dead
field load on every call. C already purges both halves in its peek frame
(`peek_localized` runs `purge_dead_temp_stores`, then drops the declaration whose
only store went with it). Rust never ran that fixpoint.

So ten frames — CMO, DEMA, EMA, KAMA, RSI, T3, TEMA, VWMA, WMA, ZLEMA — emitted

```rust
let mut cur_outReal = sp.cur_outReal;   // a field load
...
if sp.optInTimePeriod == 1 {
    (*outReal) = inReal;
    cur_outReal = (*outReal);           // the only store
    return Ok((*outReal));
}
```

and never read `cur_outReal`; the frame answers through `outReal`. Nothing could
say so: `lib.rs` blanket-allows `unused_variables`, `unused_assignments` and
`unused_mut`, so rustc is silent, and the stores are dead by definition, so no
value gate sees them either.

## The change

`peek_frame_arm` now runs the same two shared helpers C does, in C's order, over
the localized names: `purge_dead_temp_stores`, then a `temps_used` liveness
filter on the binds. The extrema rebase is emitted as text beside the frame, so
no statement shows the read that keeps its targets alive — those stay pinned, as
in C.

**The fixpoint has to reach the shadow pair, not just the local.** WMA's
`pkSlot0`/`pkVal0` are read by the one store that goes, so purging the local
alone would have left a fresh dead pair behind — the same defect, newly
introduced by the fix. C purges shadows and slot temps for exactly this reason,
and its `TA_WMA_Peek` carries neither.

## Why 13 files move, not the 10 the issue names

The purge is a fixpoint: each deleted store exposes the next. Every cascade lands
on something **C's own peek frame already drops**, which is the check I used to
decide each one was real rather than convenient:

| function | also dropped | in `TA_<N>_Peek`? |
|---|---|---|
| WMA | `periodSub`, `trailingValue` | absent |
| HMA | `periodSubSqrt`, `trailingSqrt` | absent |
| MAMA | `prev_jI_{Even,Odd}`, `prev_jQ_{Even,Odd}` + their `_input_` pairs | absent |
| HT_PHASOR | same eight | absent |

Rust's WMA peek local list is now C's exactly: `j`, `rw`, `tempReal`,
`barsSinceReseed`, `periodSum`, `pkSlot1`, `pkVal1` (C additionally hoists the
`win_j_inReal` pointer, which Rust has no need of).

`pt.body` is spent once the body is localized, so the two lists the render still
needs are destructured out of the `PeekTransition` rather than re-wrapped around
a stale body.

## The gate

`no_rust_peek_binds_a_dead_local` (`tests/rust_stream_suite.rs`) asserts every
local a Rust peek frame declares is read by that frame, over all 177 streaming
indicators.

- **Its own two counters**, so it cannot pass vacuously: `swept > 170` (a needle
  that stopped matching `peek` would zero it) and `declared > 400` — the real
  count is **1311** binds parsed, so the floor has room without being decorative.
- **Reads the rendered text, not the IR.** An emitter that computed the right
  live set and then printed the whole list anyway would satisfy a check over the
  analysis and fail this one.
- **Its read test follows `streaming::names_read`**: a compound store drops its
  *head* self-read only, so `x += y` with `x` read nowhere else retires, while
  `x += x * 2` still reads. That is what makes the gate see the accumulator shape
  (WMA `periodSub`, HMA `periodSubSqrt`) and not just the `cur_<output>` bind.
- **Control:** reverted against this tree, regenerated, the gate fails and names
  all 20 offenders — the 10 `cur_outReal` binds, the 8 Hilbert `prev_j*` locals,
  and WMA `periodSub` + HMA `periodSubSqrt`. I ran that revert and watched it go
  red; the offender list above is copied from that run.

## Verification

- `regen-check`: **"ta_codegen output matches the committed source. OK."**
- `cargo test` in the generator: 31 test binaries, **0 failures**.
- Generated crate: `cargo test --tests -p ta-lib` green; `cargo clippy
  --all-targets` clean; `cargo test --doc` **553 pass**, which includes the
  per-function `peek == update` bit-for-bit witness on every changed function.
- `ta_regtest --codegen --language=rust --function=CMO,DEMA,EMA,HMA,HT_PHASOR,
  KAMA,RSI,T3,TEMA,VWMA,WMA,ZLEMA,MAMA` against `ta_ref_serve` (built from the
  `reference-pre-cutover` worktree): **13/13 PASS, exit 0**.
- Full-corpus `ta_regtest --codegen --language=rust`: every function PASS except
  one pre-existing mismatch, below.
- **C, Java and C# outputs are byte-identical** — `git status` after a full
  `generate` shows only `rust_stream.rs`, the test file, and the 13 Rust
  indicator files.

### The one red line in the full run is not this PR's

`STREAM MISMATCH [TA_MACDEXT] vector=46` is **#355**, already filed, and I proved
it independent of this change rather than taking the issue's word: stashed the
change, regenerated `--backend=rust`, rebuilt the server, re-ran the issue's own
repro — the mismatch response comes back **byte-identical** to the one this tree
produces. Its `peek_ok` and `peek_rep_ok` are both `1`; the anomaly is the
aggregate `ok: 0`, which is what #355 is about.

## Cost, and what I did not check

- **No performance claim is made.** These are dead locals in a `#![forbid(unsafe_code)]`
  crate, and LLVM is expected to eliminate them regardless; the case for the
  change is hygiene and C parity. **I did not benchmark peek throughput**, so
  nothing here should be read as a speedup.
- **Rust-only.** The Java and C# frames keep their local and zero the seed
  (`peek_seed_is_dead`, #343) because there the local *is* read. C needed nothing.
  Rust is now the third backend to delete rather than re-seed.
- `cargo clippy --all-targets` on the generator still reports one
  `needless_pass_by_value` at `src/streaming.rs:10157` (the `dead(Vec<Statement>)`
  test helper). That is pre-existing on `dev` at `4c3a63db` and is what the
  already-open #356 fixes — it is not from this change, and the two PRs are
  independent.
- **I did not run** the Java, C# or C legs of `ta_regtest --codegen`, the
  `--xlang-hash` gate, or `stream_ab.py`. No .NET SDK is present in the
  environment I built this in, and no generated Java, C# or C byte changed, so
  those legs have nothing new to compare — but I am naming it rather than
  implying coverage I do not have.
