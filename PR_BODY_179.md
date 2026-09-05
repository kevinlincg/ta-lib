docs(rust): 45 of the 61 candlestick examples proved nothing (#179 E8d)

Closes #179 E8d.

## The gap, reproduced

E8d records that 45 of the 61 candlestick doctests "still only prove in range".
I reproduced the count before changing anything: run every Pattern Recognition
function on the exact synthetic series the generated example uses, and collect
the distinct values.

**16 of 61 produce a non-zero value. The other 45 return all zeros**, and their
example asserted

```rust
assert!(out[..out_range.count].iter().all(|&v| (-200..=200).contains(&v)));
```

which an all-zero output satisfies. The example was green on a function that
found nothing, and would have stayed green if the pattern had stopped firing
entirely.

## What this does

Each candlestick's YAML now carries a `doc_example:` block — the literal OHLC
window its example runs on, and the value the last bar fires with:

```yaml
doc_example:
  fires: 100
  bars:
    - [100.0, 106.0, 99.0, 105.0]
    - [108.0, 114.0, 107.0, 113.0]
    - [110.0, 111.0, 101.0, 102.0]
```

and the generated example asserts both halves:

```rust
// the window above is a worked instance of the pattern: it fires on the
// last bar -- sign for direction, magnitude for strength -- and nowhere else
assert_eq!(out[out_range.count - 1], 100);
assert!(out[..out_range.count - 1].iter().all(|&v| v == 0));
```

All 61 windows fire on their last bar **and nowhere else**, so the claim is
unambiguous: a reader sees the bars, and `cargo test --doc` is red if the
function stops finding them.

## Where the windows come from

E8d names the source — `fuzz_cdl_catalog` / `FUZZ_CANDLE` in
`src/tools/ta_regtest/fuzz_data.h`. I dumped that shape from the header itself
(seeds 0–15, 4000 bars) rather than transcribing anything by hand, and searched
it for, per pattern, the **shortest** window whose last bar fires and whose
earlier bars are all 0. Every bar is rounded to two decimals *before* the
search, so the numbers that ship are exactly the numbers verified.

FUZZ_CANDLE alone covers 52 of 61. Nine patterns never fire anywhere in it —
`CDL3OUTSIDE`, `CDLABANDONEDBABY`, `CDLDRAGONFLYDOJI`, `CDLEVENINGDOJISTAR`,
`CDLEVENINGSTAR`, `CDLGRAVESTONEDOJI`, `CDLONNECK`, `CDLSEPARATINGLINES`,
`CDLTASUKIGAP` — and their windows come from the same `fuzz_gen`, other shapes
(`FUZZ_RANDWALK`, `FUZZ_TIE_HEAVY`, `FUZZ_WITH_ZEROS`). That is itself worth
recording: **FUZZ_CANDLE does not exercise those nine**, which is a gap in the
shape, not in this change. I did not widen FUZZ_CANDLE here.

Windows are 3–15 bars: a candlestick compares each bar against a running
average whose period is a candle *setting*, so the window has to carry the whole
lookback, not just the pattern.

## The cost, which is yours to weigh

**The windows are a second copy of data that already exists in C.** They are
literal bars in `ta_codegen/input/<name>/<name>.yaml`; `fuzz_data.h` keeps its
own hand-built catalog. Nothing makes the two agree, and nothing needs them to —
each is verified where it lives (the doctest asserts its own window; ta_regtest
sweeps its own corpus) — but it is duplication and I am not going to pretend
otherwise.

The single-source alternative is to move the catalog windows into
`ta_codegen/input/` and generate `fuzz_cdl_catalog` from them, which makes part
of a hand-written test header generated. That is an architecture call about a
file you own, so I did not take it. If you want it, this data file is the thing
it would be built from.

Two smaller things I did **not** do:

- The Java and C# docs, and the streaming examples, still use the synthetic
  series. Only the Rust batch example is changed — E8d is scoped to docs.rs.
- No candlestick's *code* changed, so no C, Java or C# file moves in this diff.

## Gates

New: `every_candlestick_example_runs_on_a_window_that_fires` sweeps the 61
patterns and fails if one arrives without a window, or if its example fell back
to the `(-200..=200)` bound. The existing
`every_integer_output_carries_an_example_claim` is widened to recognise the new
claim shape (still counted by the output's own variable, so the
SUPERTREND-shaped hole it was tightened against stays closed).

**Controls, each actually run and watched go red, then restored:**

1. Nudged one bar of `CDL3BLACKCROWS`'s window (last close 92.0 → 101.0): the
   third crow no longer closes lower, `assert_eq!(.., -100)` fails, doctest red.
   Restored → green.
2. Changed `CDLXSIDEGAP3METHODS`'s `fires: 100` → `-100`: doctest red. Restored
   → green.
3. Deleted `CDLDOJI`'s `doc_example` block: the new gate panics with
   `cdldoji: a candlestick needs a doc_example window its example fires on`.
   Restored → green.

**Clean:** `regen-check` (a full four-backend generate is a fixed point);
generator `cargo test --release` (all 31 suites, 0 failed); generator
`cargo clippy --all-targets -D warnings`; crate `cargo clippy --all-targets
-D warnings`; `cargo doc --no-deps` warning-free; `cargo test --doc` 595 passed;
`cargo test --tests -p ta-lib` (the crate's own value gates).

**Not run, and why:** the C reference suite, the cross-language sweeps and the
C# build. Zero C, Java and C# files change in this diff — `regen-check`'s full
generate confirms the only output that moves is `output/rust/` — and this
machine has no .NET SDK.

## Also in this diff

One rationale comment in `rust_doc.rs` appeared **twice, verbatim** (the
eight-line SUPERTREND note explaining why the trend flag gets a domain and not a
relation). Dropped the copy, since it is in the function this change edits.

## Re-verified on dev `710765c6`

Dev moved under this branch — #382 removed the `UpdateAndFill` tier and the
September indicator batch took the corpus to 201 — so the head is merged with
`710765c6` and re-checked:

- `regen-check`: green, exit 0, 201 functions. The merge needed no regeneration
  of this branch's own output, which is what says the new indicators are not
  candlesticks and the E8d treatment does not reach them.
- generator suite: 936 passed / 0 failed.
- `cargo test --doc -p ta-lib` on the generated crate: **613 passed / 0 failed**
  — the doctests this PR is about, run on the merged head.

Still not run, for the same reasons as above: the C reference suite, the
cross-language sweeps and the C# build.
