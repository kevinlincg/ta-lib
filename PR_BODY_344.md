SUPERSEDED — do not open this PR (#344 landed on dev as ce5f5748)

> **STOP.** Do not open a pull request from `issue-344-dead-open-locals`.
>
> The maintainer landed his own fix for #344 directly on `dev` as `ce5f5748`
> ("refactor(codegen): a head declares what its body uses, not what every shape
> might", authored 2026-09-04 05:10 UTC), while this branch was sitting here
> waiting to be opened. A PR from this branch would be a duplicate.
>
> Verified against `dev` at `ce5f5748`:
>
> - `src/ta_func` carries **0** `(void)` suppressions, and `dummyBegIdx` /
>   `dummyNBElement` survive only where a body reads them — the same residue this
>   branch removed.
> - `ce5f5748`'s own message reports the same 342 locals and the same three
>   sites (`dummyBegIdx` and `dummyNBElement` in 166 Open heads, `subOpenDummy`
>   in 10 of the 11 composed ones), reached the same way: emit the head after the
>   body and declare against the finished text.
>
> **The one difference, and it is not worth a PR.** `ce5f5748` leaves `STOCH`'s
> `subOpenDummy` declared at function scope (`src/ta_func/ta_STOCH.c:739`), where
> this branch moved it into the sub-open's own block. `STOCH` is the one composed
> function with a non-fused sub-call, so the local is *live* either way — it is
> block scope versus function scope and nothing else. No warning, no instruction
> difference, no gate sees it.
>
> The branch tip is `15db3bf5` (kept as a ref, in case you want the block-scope
> spelling as a follow-up). Nothing here needs to reach upstream.

---

The body written before `ce5f5748` landed follows, unchanged, for reference only.

---

refactor(codegen): the Open head declares an out-meta local only where the body mentions it (#344)

`c_hygiene` (dev `46577145f`) deletes a `(void)x;` whose block reads `x`, and
stops there on purpose: a declaration it removes wrongly is a compile error, not
a warning. So where `emit_open_head` declared a local the body below it never
mentions, the cast correctly survived — and with it the dead local it was
suppressing. This is the other half.

## What the head could not know

An emitter writes declarations before it renders the body, so it declares every
local any shape might need. The fix is the one the Rust dual-mode peek frame
took in `46577145f`: render the arm into a `String` first, emit the head against
that text.

The predicate is **mentions**, not **reads**. A body that only *writes* an
out-meta local still needs it declared, and still needs the suppression it is
owed — `c_hygiene` is the phase that judges reads, and it keeps its own answer.
Getting this backwards would trade 342 dead locals for a fresh crop of
`-Wunused-but-set-variable`.

## The residue, gone

| local | declarations removed | where |
|---|---:|---|
| `dummyBegIdx` | 166 | `TA_<N>_OpenImpl` |
| `dummyNBElement` | 166 | same |
| `subOpenDummy` | 10 | composed opens whose every sub-call fuses |
| **total** | **342** | |

862 deleted lines, one inserted, and nothing else in 177 regenerated `.c` files.
The 10 composed opens that keep `dummyBegIdx` keep it because they read it
(`*outBegIdx = dummyBegIdx;` in a nested block, which is why `c_hygiene`'s
innermost-block scope could not see the read and left the cast standing).

## A third call site the issue does not name

The issue lists two (`emit_open_core_body`, the dual-mode open).
`subOpenDummy` is a third, in `emit_composed_open`, and it is not the same
shape: rather than gate the declaration, the throwaway a non-fused sub-open
hands the callee now lives **in that sub-open's own block**, declared at its use
site. One composed function (`STOCH`) still has a non-fused sub-call, so 11
function-scope declarations become one block-local — the other 10 disappear with
nothing to gate.

## Why `open_head_prerender` exists

The head has two counter-consuming parts of its own — the private-parameter
initializers and the identity fast path — and the shared inline-helper counter
names hoisted temporaries. Rendering the arms first *and* leaving those two in
place would renumber every temporary below them, burying this diff in unrelated
renames. They move into `open_head_prerender`, called exactly where they used to
render, so the emission order (and therefore the numbering) is unchanged. That
is why the generated diff is only the lines above.

## What it is worth: source hygiene, and the object code agrees

**179 of the 181 `ta_func` TUs have byte-identical `.text`** at the shipped `-O3`
— 362 of 366 objects across the static and PIC builds. A dead local plus a dead
store to it is eliminated at `-O1` and above, as the cast half predicted.

The two that move keep their **exact `.text` size and instruction count**:

- `ta_TRIX` — two spill slots swap (`0x18` ↔ `0x34`) and one compare reverses its
  operands (`cmp %r15d,%ebx; jg` → `cmp %ebx,%r15d; jl`). 32 differing
  disassembly lines, all of that shape.
- `ta_STOCH` — the `subOpenDummy` zero store moves from the prologue into the
  block the local now lives in (which is the source change), one `sub` reorders,
  and alignment padding repacks (`nopl`/`cs` → `nopw`). Everything else is stack
  offsets renumbering around the freed slot.

No speed is claimed and no benchmark is owed: byte-identical code cannot be
faster, and the two that moved did not change what they execute.

## Gates

Per the issue: `regen-check` plus the existing warning-free build. No new gate —
`c_hygiene_suite` still has the server's casts to judge and does not go vacuous,
and a gate over "the head declares no dead local" would only restate the
committed C that `regen-check` already pins.

The warning-free build is the check that every removal was dead, so I proved it
is live rather than asserting it: injecting `int sabotageDead;` and a store into
`ta_SMA.c` and rebuilding raises

```
src/ta_func/ta_SMA.c:271:8: warning: variable 'sabotageDead' set but not used
                                     [-Wunused-but-set-variable]
```

under the `-Wall -Wextra` the tree ships. Reverted; the branch build is clean
(the one warning in the tree, `get_nanotime` unused in `ta_bench.c`, is
pre-existing and untouched here).

Run on this branch: `regen-check` OK · `cargo test` 30 suites, 0 failures ·
CMake Release build of the library and C tools · `ta_regtest` all tests
succeeded.

**Not run:** `ta_regtest --codegen` (the cross-language leg). `generate` changed
no Rust, Java or C# source at all — the diff is 177 `src/ta_func/*.c` plus the
emitter — so the other three backends' servers are byte-identical to dev's and
have nothing new to compare. I did not rebuild them.
