refactor(streaming): type the range accessors per function (#387)

Implements #387 as filed: `TA_StreamOutRange` and `TA_StreamAdvance` are removed,
and every streaming function gains

```c
TA_LIB_API TA_RetCode TA_<NAME>_OutRange( const TA_<NAME>_Stream *stream, int *outBegIdx, int *outNBElement );
TA_LIB_API TA_RetCode TA_<NAME>_Advance( TA_<NAME>_Stream *stream );
```

+402 exported symbols. No compatibility concern: the pair is unreleased.

They are emitted into the function's own translation unit, which is the whole
point — the struct is complete there, so `OutRange` reads two fields where the
generic pair had to `memcpy` a prefix out through `TA_StreamRangeHead`. That
type existed only to describe the prefix to a `void *` reader, so the private
header no longer declares it, and `ta_utility.c` no longer includes that header
or `<string.h>`. `Advance` keeps its saturation from `emit_range_head_advance`,
the emitter every committing step shares, so it cannot come to disagree with
`Update` about `TA_MAX_INDEX`.

## The one real caller of the generic accessor

The MA dispatch tier used it for exactly what it was for: after its `switch` has
run, `sp->sub` is a bare `void *` and the arm that gave it a type is gone. The
read therefore moves **into** each arm, where `sub` is still
`TA_<CALLEE>_Stream *` and the arm knows which callee it opened. The filling
modes never read it — they capture the range from their own out-params
afterwards — so they get no read, which is also why this is not simply a
mechanical substitution.

## Gates

- **`out_range_advance_suite`** checked the advance body in three backends and
  skipped C, because C had no per-function advance to look at. It now checks
  four, and its floor rises 600 → 800 so the widening cannot be undone silently.
- **`rust_doc_suite`**'s expected-symbol set gains `OutRange` and `Advance` per
  function. A Rust handle still aliasing a shared C name now fails rather than
  passing on a symbol nothing exports.
- **`c_stream_every_tier_leads_with_the_range_head`** still passes, but it pins
  something weaker now and the doc comment says so: a struct that led with
  something else used to *miscompute* through the `void *`; now it does not
  compile. What is left to pin is the uniformity the shared seed/capture/advance
  emitters rest on — worth keeping, but it is no longer a correctness gate, and
  you may prefer to retire it.
- **`test_stream_finite`**'s NULL arm was one call on one handle, justified by
  there being one hand-written function to probe. There are now 201, so one
  handle proves the emitter rather than the corpus. The probe follows the tiers
  instead: `SF_ADV_NULL` asserts both range calls reject NULL, once per tier
  (SMA, MINUS_DI, MA, MAVP, BBANDS, STOCH, CDLDOJI) — and it covers
  `OutRange(NULL)`, which nothing asserted before.

**Both gates were sabotage-proved, and I watched each fail:**

| sabotage | what went red |
|---|---|
| C `Advance` emitted without the `MAX_INDEX` guard | `only_an_accepted_bar_advances_the_range` — "ac: c advance does not move the count under the MAX_INDEX guard" |
| `OutRange` reporting `outRangeBegIdx + 1` | C reference suite, `DIV,DIVZERO` leg — "begIdx=1 count=12, expected 0/12" |

Both were reverted and the tree re-verified green afterwards.

## What was verified

On x86-64 Linux, gcc, CMake Release:

- `cargo test` in `ta_codegen/generator` — the whole suite green.
- `cargo clippy --all-targets` — clean.
- The C reference suite (`ta_regtest`) — *All tests succeeded*.
- Cross-language verification against the frozen `reference-pre-cutover` oracle:
  `ta_regtest --codegen --language=c,rust` — **both languages pass**, 161
  functions each, including the `stream_verify` range legs, which is where the C
  server's own range reads (now typed) are exercised.
- The crate: `cargo doc --no-deps` warning-free, `cargo test --doc` 613 passed.
- **Regeneration is idempotent** — `generate` a second time changes nothing
  further, checked with `git status` before and after.
- **Java and C# generated output is byte-identical** (`git status` on
  `ta_codegen/output/{java,csharp}` is empty). Rust's whole delta is 402 lines:
  the two doc aliases per function, now naming the typed C symbols.

## What I did not check

- **`scripts/build.py regen-check` and `check-source-lists` as such.** `build.py`
  refuses to run as root and this environment has no other usable account, so I
  ran the gate's content by hand instead (regenerate, then diff `git status`).
  No source file is added or removed by this change, so the two build-system
  lists have nothing new to disagree about — but that is a reading of the diff,
  not a run of the checker.
- **Java and C# end to end.** There is no .NET SDK here, so
  `scripts/regtest.py` aborts at the prerequisite check and I could not run
  `--codegen` for those two. Their generated output is byte-identical to dev, so
  there is nothing new for them to fail on, but I did not build or run them.
- **The nightly jobs on an Actions runner** — local runs only.
- **No performance claim.** Nothing here is a perf change; I measured nothing.

## Costs, stated rather than smoothed away

- 402 more exported symbols in the shipped `.so`. That is the trade the issue
  proposes, but it is a real one: the DSO's dynamic symbol table grows by ~2×
  the streaming corpus, and #386's section B item about verifying the Windows
  DLL export count will need its number updated.
- `TA_StreamRangeHead` is gone, and with it the reason the range head *had* to
  lead every struct. The layout gate now pins a convention, not a requirement
  (see above) — your call whether to keep it.
- Callers writing generic code over several handle types lose the one call that
  worked across them. Nothing in this tree did that except the MA dispatch tier,
  which is now typed per arm, but an external caller doing so has no
  replacement short of a switch. It seems worth saying out loud since the API is
  unreleased and this is the moment to disagree.
