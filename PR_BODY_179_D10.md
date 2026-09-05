docs(changelog): 0.8.1 never mentions the crate it publishes (#179 D10)

Closes D10 of #179 — "the CHANGELOG's 0.8.1 section does not mention the crate."
The rest of #179 is untouched.

## What is missing

The 0.8.1 section lists the streaming API and the new indicators, and says
nothing about the Rust library going out on crates.io. A first publish is
user-facing by this repo's own CHANGELOG rule, and 0.8.1 is still unreleased,
so the entry is free to write now and an API break to add later.

## The version discontinuity, and why it is in the entry

`ta-lib` is not a new name on crates.io. Versions 0.1.0–0.1.2 (2021–2023) are an
unrelated third-party binding from `virtualritz/ta-lib-rs`. A reader who runs
`cargo add ta-lib` and gets 0.8.1, or who finds the old versions first, has
nothing in the CHANGELOG telling them which of the two projects they are looking
at — and the jump 0.1.2 → 0.8.1 reads as a mistake without it.

The entry states only that those versions are not this project's. It does **not**
presuppose B2 (whether they get yanked), which is account work and stays open.

## Scope

Three lines in `CHANGELOG.md`, under the existing `## [0.8.1] Not Released Yet`
→ `### Added`. No code, no generated output, no gate.

## What I checked

- `git diff` against `dev` is the three lines and nothing else.
- The crate-name and dependency claims (`ta-lib` depends on `ta-lib-dispatch`,
  versioned separately) are read off `ta_codegen/output/rust/library/Cargo.toml`
  in this tree.

## What I did not check

- **The crates.io history of the 0.1.x versions** is quoted from #179 D10 itself,
  not re-verified against crates.io from this session.
- Nothing is built or run by this change, so no gate covers it; a wrong sentence
  here fails no CI and only a reader catches it.
