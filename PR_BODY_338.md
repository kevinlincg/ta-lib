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

- `scripts/build.py xlang-hash`, unfiltered, on the merged tree: **PASS**, 178
  functions, every server bit-identical to the in-process C library — Rust
  284,595 cases / Java 283,304 / C# 284,487, **0 mismatches each**. This is the
  leg the merge with `7065d886` had not re-measured, and it is the one that would
  see a `TA_FMA_MULTIVERSION` line changing a value.
- A bare `bin/ta_regtest` (the whole C reference suite, LEGACY/064/FROZEN and the
  streaming gates included): all tests succeeded.
- The #340 question this PR widens, measured on **this** tree rather than assumed:
  the C# parity row with the FMA3 intrinsic disabled
  (`DOTNET_EnableAVX2=0 DOTNET_EnableFMA=0`, both knobs — the FMA one alone is a
  no-op) is 284,487 cases, 0 mismatches, identical to the control. Going from 29
  fused C# files to 32 does not move that answer on linux-x64 / glibc 2.39 /
  .NET 10.0.400. It says nothing about real non-FMA3 silicon, Windows, macOS or
  musl, which remain unmeasured.

From the earlier runs on this branch, before the merge with #337:

- The batch numbers above, with their controls.
- `ta_regtest --codegen` and `--fuzz-064`.

Still not re-run after the merge: `--fuzz-064` and `--codegen`. `--xlang-hash` is
the stronger of the three on the axis this merge touches (bitwise, zero
tolerance, against the same in-process C golden), so what remains unchecked there
is the 0.6.4 differential and the 1e-6 sweep, not fusion-site parity.
