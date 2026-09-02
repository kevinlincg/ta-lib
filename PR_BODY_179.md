docs(rust): the front page's three samples are authored once, not twice (#179 D5b, D8b)

## D5b — the two prose bodies are still authored separately

The crate has two front pages, `library/src/lib.rs`'s module docs and
`library/README.md`, and three Rust samples appear on both: the batch quick
start, the builder, and the streaming walk-through. They were typed out twice,
in two string literals ~170 lines apart in `main.rs`, and compiled on two
separate paths — `cargo test --doc` for the module docs, the `cfg(doctest)`
`ReadmeExamples` struct for the README.

Two compiled copies of one claim is the shape where a divergence is invisible:
both halves stay green while they say different things. That had already
happened — the builder sample asserts the setting took on the README and stops
at `build()?` in `lib.rs`, so the doctest proved only that the call compiles.

Each sample is now one `FrontPageExample` — the `use` items and the statements,
no wrapper — and each page's wrapper is added on the way out:

| page | wrapper |
|---|---|
| `lib.rs` | `//!` prefix, `use ta_lib::…;`, hidden `# Ok::<(), ta_lib::RetCode>(())` |
| `README.md` | fenced ` ```rust `, `use` line plus `RetCode`, pasteable `fn main() -> Result<(), RetCode>` |

The pages keep rendering differently, and should: one is rustdoc with intra-doc
links, the other has to stand on crates.io. What they can no longer do is differ
on the code.

**This is the executable half of D5b, not all of it.** The narrative around the
samples is still authored per page, because the two differ by more than markup:
`lib.rs` states the API shape as bullets carrying intra-doc links, the README as
prose with a different category list and an install section. Unifying that means
choosing one rendering for text that reads differently in the two places — a
call for the maintainer, not something a refactor should decide. Left open.

## D8b — one sentence left

`lib.rs` said the crate is `#![forbid(unsafe_code)]` and stopped there. The
sentence now also says where the one `unsafe` in the shipped dependency graph
is — `ta-lib-dispatch`, inside the `is_x86_feature_detected!("fma")` test that
proves it sound — and why `forbid` here does not see it (it expands from another
crate's macro).

## What the generated output does

Exactly two hunks, both in `lib.rs`. `README.md` is **byte-identical**, which is
the check that the renderers reproduce what was there rather than something that
merely compiles:

- the builder doctest gains the read-back the README already had, so it proves
  the setting took;
- the `forbid` sentence gains the D8b clause.

## Verification

- `scripts/build.py regen-check` — clean on the committed tree.
- `cargo clippy --all-targets --manifest-path ta_codegen/generator/Cargo.toml -- -D warnings` — clean.
- The generator's own suite — green.
- `RUSTDOCFLAGS=-D warnings cargo doc --no-deps -p ta-lib` — warning-free.
- `cargo test --doc -p ta-lib` — 544 passed, 0 failed.
- Control for the added assertion: with the expected period changed to `11` the
  `lib.rs` doctest fails (`left: 10, right: 11`); restored, it passes. The
  assertion discriminates.

Not run here: the Java and C# backends were not built (no .NET SDK in this
environment), and this change writes no Java or C# — `regen-check` covers that
their committed output did not move.
