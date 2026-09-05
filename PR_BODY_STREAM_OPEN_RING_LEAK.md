fix(codegen): an Open capture-failure return strands the live batch ring

A capture block runs with two allocations live at once: the half-built stream handle, and the batch's own CIRCBUF storage, whose top-level destroy was deliberately withheld so the capture can read it. `c_stream::alloc_and_capture` knew that for exactly one of its failure returns and for none of the seven that follow.

```c
/* src/ta_func/ta_HMA.c — dRing is the batch's own ring, still live */
if( !sp ) { if( dRing != &local_dRing[0] ) { TA_Free( dRing ); } return TA_ALLOC_ERR; }   /* right */
...
if( !sp->win_jFull_inReal ) { TA_HMA_ReleaseImpl( sp ); return TA_ALLOC_ERR; }            /* strands dRing */
...
if( !sp->cb_dRing ) { if( dRing != &local_dRing[0] ) { TA_Free( dRing ); } TA_HMA_ReleaseImpl( sp ); return TA_ALLOC_ERR; }  /* right */
```

`emit_circ_capture`, which runs after `alloc_and_capture`, had it right all along — which is why the two spellings sit a few lines apart in the shipped C disagreeing with each other.

## Blast radius

**20 sites across 5 shipped functions**: `AC` (4), `HMA` (8), `HT_DCPHASE` (2), `HT_SINE` (2), `HT_TRENDMODE` (4). The regenerate touches those five `.c` files and nothing else.

Four of the seven emitters are the `TA_ALLOC_ERR` arms, so the leak is reachable rather than purely theoretical: the path that fires under memory pressure is the path that loses memory. The other three are `TA_INTERNAL_ERROR` guards, which are defensive and should never fire.

**What that means for severity, stated plainly:** on a healthy process none of these 20 sites is reachable, so no shipped call leaks today. This is a latent defect on the OOM path, not a leak users are hitting. It is worth fixing because the emitter is the thing that is wrong, and the next streaming function inherits it.

## The fix

One variable, the one the function already computes for `if( !sp )`. `sp_fail` is hoisted as `fail_pre` and used by every failure return below the handle allocation.

It stays gated on `with_state && !model.circs().is_empty()`, and that gate is the whole subtlety: **the failure returns before the batch prolog must NOT free.** `emit_circ_hoist` declares the storage pointer uninitialised (`double *dRing;`) and the prolog assigns it, so a free on a pre-prolog path would read an indeterminate pointer — trading a leak for undefined behaviour. `HMA`'s own short-history branch has 7 such sites; they are correctly left alone, and the diff shows the change confined to the capture block.

## The gate

A second sweep in `c_hygiene_suite`, in that suite's existing shape: it reads the **committed** C off disk rather than re-rendering, so an emitter that bypasses `fail_pre` fails here instead of shipping.

It keys off the `if( !sp )` line's own free as the ground truth for liveness, and scans to the block's own `free_batch_storages` release. That is what makes the pre-prolog sites correctly out of scope — the alternative, a list of function names, would go stale on the next indicator. Non-vacuity floor: the sweep asserts it found at least one capture block and at least one failure return inside one.

**Control (run, not assumed):** reverting `c_stream.rs` alone and regenerating reddens it, naming all 20 sites:

```
20 failure return(s) release the handle but strand the still-live batch ring
  .../src/ta_func/ta_AC.c:756: if( sp->ringCap_trailingFastIdx < 0 || ... ) { TA_AC_ReleaseImpl( sp ); return TA_INTERNAL_ERROR(182); }
  ... 19 more
```

## What is NOT covered

There is **no runtime proof**, and no attempt at one. `TA_Malloc` is `#define TA_Malloc(a) malloc(a)` with no hook, and the repo has no allocation-failure injection facility, so reaching these 20 sites at runtime would mean adding an interposer — a larger change than the fix. The gate is therefore static, and the claim it supports is "the emitted text balances its frees", not "a leak was observed and is gone". If you would rather have the runtime harness, say so and it can be a follow-up; it would also give the `TA_INTERNAL_ERROR` guards their first execution.

I did not check the Java, C# or Rust output for an analogous shape — Rust drops, Java and C# collect, so there is nothing of this kind to leak, but I did not audit them for a *different* cleanup asymmetry.

## Verification

- `regen-check` — clean (`ta_codegen output matches the committed source. OK`).
- Full generator suite (`cargo test` in `ta_codegen/generator`) — pass.
- `cmake --build … --target ta_regtest` — compiles with no new warnings.
- `./ta_regtest` — `All tests succeeded`, including the streaming finite-input gate and the five touched functions.
- Based on dev `98c451d2`.

I did NOT run `--codegen`, `--xlang-hash`, or the nightly sanitizer paths: the change emits C-only text on failure returns that no test executes, and the four language backends' generated output is byte-identical before and after (`git status` after regenerate lists only the five `.c` files).

## Interaction with #375

COPPOCK's `_OpenImpl` has the same shape, so once this lands, PR #375 needs a plain regenerate to stay a fixed point — 2 more sites, no hand edits. Whichever lands second pays it.

## Re-verified on dev `710765c6`

Dev moved past `98c451d2` — the September indicator batch, #382's
`UpdateAndFill` removal and the KAMA guard — so the head is merged with
`710765c6` and re-checked at the same three tiers:

- `regen-check`: green, exit 0, 201 functions.
- Full generator suite: 936 passed / 0 failed.
- CMake Release build (gcc) and `./ta_regtest`: *All tests succeeded*.

COPPOCK is now on dev, so the interaction noted below is already paid: the merge
regenerated its `_OpenImpl` with the rest, and `regen-check` is a fixed point on
the merged head. Still not run, as before: `--codegen`, `--xlang-hash` and the
nightly sanitizer paths.
