perf(ta_func): ATR, NATR and SUPERTREND smooth in the two-coefficient Wilder form (#338)

Closes #338.

## The change

The Wilder recursion carried a divide in the loop-carried dependency chain:

```c
prevATR *= optInTimePeriod - 1;
prevATR += greatest;
prevATR /= optInTimePeriod;
```

It becomes the two-coefficient form, with the reciprocal hoisted out of the loop,
so the chain is one fused multiply-add instead of a divide:

```c
wAlpha = 1.0 / (double)optInTimePeriod;
wBeta  = 1.0 - wAlpha;
...
prevATR = wAlpha * greatest + wBeta * prevATR;
```

Generator input only — no backend change. The shared FMA detector canonicalizes
the add so the accumulator product is the fused one, so all four backends emit
the identical site and stay bit-identical to each other.

SUPERTREND carries a transcribed copy of the same recursion under a comment
declaring bit-exactness with `TA_ATR`; `ta_regtest`'s SUPERTREND differential
test failed on the first run when only ATR changed, so the same form is applied
there. It is the only other copy in the tree.

## Numbers

Batch tier, shipped `libta-lib.a`, 100k bars, period 14, min of 60 interleaved
alternating-order rounds against dev `df0c6beb`:

| function | before (ns/bar) | after | ratio |
|---|---|---|---|
| ATR | 5.020 | 1.349 | 3.72x |
| NATR | 5.021 | 1.383 | 3.63x |
| SUPERTREND | 9.222 | 6.426 | 1.44x |

RSI, ADX and EMA were carried in the same run as byte-identical controls and
came out at 0.99–1.00x.

## Interaction with #337, now that it has landed

The fused Wilder step puts an `fma()` into ATR, NATR and SUPERTREND for the
first time. The Peek rule that landed in `7065d88` therefore marks their `Peek`
tier as well, which is what the second commit here regenerates. It is generated
output only; the generator is untouched by it.

That moves two censuses:

- C translation units carrying `TA_FMA_MULTIVERSION`: 30 -> 33.
- Generated C# files calling `Math.FusedMultiplyAdd`: 29 -> 32.

The second one is worth naming explicitly, because #340 is open against exactly
that set: `Math.FusedMultiplyAdd` misrounds on machines without FMA3, and this
PR widens the exposed surface by three functions. It does not create the
exposure and it does not change what #340 has to decide, but the count in that
issue is 29 before this merges and 32 after.

## Costs, stated rather than special-cased

- **Not bit-exact with the old form for periods >= 3.** LEGACY/064/FROZEN gets
  two measured rows — ATR 2e-14 (from 4.88e-15), NATR 2e-14 (from 4.00e-15) —
  following that table's "3x measured, one significant digit" rule.
- **The streaming handle grows 16 bytes** for the two hoisted coefficients
  (`TA_ATR_Stream` 48 -> 64). That is the same struct #316 set out to stop C's
  Peek from copying.
- **At very large periods the new form is further from a long-double reference**
  than the old one: 3.9e-12 vs 2.6e-14 at period 100000. Both sit well inside
  the 1e-9 contract, but the direction is a loss, not a wash.
- Three more functions in the `Math.FusedMultiplyAdd` set, as above.

## What was verified, and when

On the tree as pushed (merged with dev at `7065d886`):

- `cargo run --release -- generate` leaves the tree clean — the regeneration is
  committed, not pending.
- The generator's own suite is green: 404 + 41 + 79 + 54 + ... , 0 failed across
  all 29 test binaries.

From the earlier runs on this branch, before the merge with #337:

- The batch numbers above, with their controls.
- Cross-language parity: Rust and Java bit-identical to C for the changed
  functions.
- `ta_regtest --codegen` and `--fuzz-064`.

Not re-run after the merge with `7065d886`: `ta_regtest` and the cross-language
parity legs. The merge adds only `TA_FMA_MULTIVERSION` lines, which #337
establishes as numerically inert under `-ffp-contract=off`, but this branch has
not itself re-measured that.
