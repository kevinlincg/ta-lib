build(gate): check the committed Cargo.lock before any cargo call repairs it (#179 E8f)

Closes E8f of #179 — "`Cargo.lock` is touched by no script and checked by no gate."
The rest of #179 is untouched.

## Why it stayed invisible

Both locks are committed (`ta_codegen/generator/`, `ta_codegen/output/rust/`)
and `grep -rn Cargo.lock .github/workflows scripts` matches nothing on dev, so
"checked by no gate" is literal. What makes it worse than an unchecked file is
that the gap actively erases itself: cargo repairs a stale lock in place and
says nothing, so the first cargo command in a job destroys the evidence.

In `regen-check` that command is `cargo run -- format --check`, which runs
*before* the `_git_dirty` baseline is sampled. A repaired lock therefore lands
in the "already modified" set and is excluded from the very comparison it
should have failed. In the PR gate's job the second lock is repaired later
still, by `cargo test --doc`, after the regeneration gate has already reported
green.

That ordering is the whole reason the check goes first in `regen-check` rather
than being appended to it.

## The change

One function, called first in `regen-check` and also exposed as
`scripts/build.py check-cargo-lock` for symmetry with the other `check-*`
targets. Both manifests are reported, not just the first to fail.

`cargo metadata --locked` resolves and compiles nothing. No workflow file
changes: the PR gate already runs `regen-check`, so the gate acquires the check
without a new step or a new job.

Note the manifests under `ta_codegen/output/rust/` are themselves generated, so
the realistic way to produce this state is a dependency edit in the generator:
`generate` rewrites the manifest, the lock beside it does not move, and
"regenerate, then `git status` is clean" still holds.

## Controls, broken and watched to fail

Anti-vacuity first: on clean dev both locks verify, so the passing result is
not the command silently doing nothing.

**Control 1 — a generator dependency bump.** Set `ta-lib-dispatch` to `0.1.3`
in `dispatch/Cargo.toml` and the pin in `library/Cargo.toml`, which is what a
generator bump emits. Check goes red, naming `output/rust/Cargo.lock`. The lock
was **not** rewritten by the failing run — `--locked` refuses — so a red gate
leaves the tree as the author committed it.

**Control 2 — a committed stale lock, both gates on the same tree.** Edited the
`ta-lib-dispatch` entry in the committed `output/rust/Cargo.lock` to `0.1.1` and
committed it, so the tree is clean the way CI checks out:

- `regen-check` **without** this change: `REGEN_CHECK_RC 0`, "ta_codegen output
  matches the committed source. OK." The lock was still stale afterwards
  (`git status` clean against the bad commit), so the run neither caught it nor
  touched it.
- `regen-check` **with** this change, same tree: `REGEN_CHECK_RC 1`, stopping
  at the first section.

**Control 3 — the silent-repair mechanism itself.** Deleted one `[[package]]`
entry (`hashbrown`) from `generator/Cargo.lock`, then ran only
`cargo run --release -- format --check`, i.e. regen-check's first cargo call on
dev. It exits 0 and `git status` comes back clean: cargo re-added the entry and
said nothing. Re-staling the same lock and running the new check reports it.

## Cost

- 0.11 s wall for both manifests with a warm registry cache.
- On a cold runner it needs the registry index. I did **not** measure a cold
  run. The cost is not new work, though: the next step in the same job
  (`format --check`) fetches the index anyway, so this moves that fetch a few
  seconds earlier rather than adding one.
- The real cost is a behaviour change: a dependency edit that leaves the lock
  behind now fails the PR gate instead of being repaired in the runner. That is
  the point, but it is a new way for a PR to go red, and if you would rather
  keep the lock advisory this is the change to decline.

## What I did not check

- I did not verify this against a cold GitHub runner; the timing above is local
  and warm.
- I did not add `--locked` to any of the build or test invocations in the
  workflows. It would be redundant once this check passes, and it would report a
  stale lock through six error messages that describe something else.
- No generated file is in this diff, so no backend was regenerated for it beyond
  the full `regen-check` run reported above. C#, Java and the cross-language
  regtest were not run — this machine has no .NET SDK and no `ta_ref_serve`
  build — and nothing in the diff reaches them.
