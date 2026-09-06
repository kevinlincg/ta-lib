test(stream_verify): Java and C# report the Value leg, and its floor stops being dead (#386 B-2)

Closes the "Java/C# Value vacuity floor is dead code — never emits validation keys" item in #386's section B.

## What was wrong

`stream_verify`'s **Value leg** — the handle's own accessor read back against the bar the stream is on, once at `Open` and again after every `Update` — runs in all four servers. Only **C and Rust ever reported it**: they answer `value_checked` / `value_legs` / `value_ok`. Java and C# ran the identical compares and folded the result into `allOk`, emitting no key at all.

So `ctx.streamValueFunctions` was 0 for those two servers, and the floor

```c
if( ctx.streamValueFunctions > 0 && ctx.streamValueFunctions != ctx.streamFunctions )
```

skipped them on its own guard. Dead in the direction that matters: **a server that stopped emitting the leg entirely read exactly like one that never had it** — the same failure mode the peek probe had, and the reason the state and range legs are keyed off a declared capability rather than a count.

## What this changes

* The Java and C# emitters declare `valueChecked` / `valueOk` / `valueLegs` and set them at the two sites that were already comparing (`Open` anchor, `value` vs `update`), then emit the three keys in the response. **Each site keeps its existing `allOk = false`**, so `ok`, `step_ok` and every diag string are unchanged — `value_ok` is the narrower report, not a replacement, and no call that passes today can start failing.
* The floor reads `codegen_lang_has_value_probe(lang->name)` — all four servers — instead of `value_checked > 0`, and names the server in its message. `streamFunctions != 0` still guards it, so a `--function=` filter selecting no streaming function reads as nothing-to-check.

Coverage after the change: 201 of 201 streaming functions report the leg on Java, 402 sites (2 per function) in the generated C# — checked over the emitted text, so no function slipped through with the counters declared and never set.

## Verification

Branch is based on dev `51fb0968`; every run below was made at `69ffb01b`, the commit before it — the one commit between them touches `scripts/python-dev.py` and `scripts/README.md` only, so nothing here was re-measured on it. gcc 13.3 / Release, OpenJDK 21.

| what | result |
|---|---|
| `cargo test` (generator) | 935 pass, 0 fail |
| `cargo clippy --all-targets -- -D warnings` | clean |
| `cargo run -- generate`, then `git status` | only the four files in this diff |
| `./ta_regtest` (C reference suite) | all pass |
| `./ta_regtest --codegen --language=c,rust,java` against `bin/ta_ref_serve` (frozen `reference-pre-cutover`) | all 3 languages pass, float leg 1015 acknowledged comparisons |
| `cargo run -- build --backend=java` (mvnw jar + its seven suites) | all pass, incl. StreamSmokeTest 4872 checks |
| C and Rust server sources | byte-identical (`generate` produces no diff for them) |

### The control, run in both directions

The claim is that the old floor was dead, so both halves were measured on the same sabotage — the C emitter stops writing `valueChecked = 1` (`valueLegs++` left in place, so the leg still *runs* and still compares; only the report goes away):

* **with this change** — `./ta_regtest --codegen --language=c --function=SMA,RSI`
  → `STREAM VALUE VACUOUS: only 0 of 4 streaming functions probed their Value accessor on the c server`, run fails.
* **with the floor restored to `value_checked > 0`**, same sabotaged server, same command
  → `All 1 language(s) passed codegen verification` · `* All tests succeeded. *`

That second line is the defect: a server that had stopped reporting the leg passed. Both files were restored afterwards and the green runs in the table above are from the restored tree.

## What this costs

* Two extra fields per streaming response on two servers, and ~1200 added lines in each of the two generated server sources. Wire and repo noise, no runtime cost on the shipped library — nothing here is in `src/`.
* The floor is now an **obligation**: any backend that runs the Value compares must also report them, or the gate is red until it does. That is the point, but it is a new requirement on a fifth backend, and on any future server that legitimately cannot offer the leg — such a server would need an entry in `codegen_lang_has_value_probe` saying so, the way the state leg's predicate already carries C-only.

## Not checked / left open — please read before merging

* **The C# half was not compiled.** This machine has no .NET SDK, so `TaCodegenServe.cs` was neither built nor run; I read the generated text and it mirrors the Java arm and the surrounding C# idiom exactly, but the PR gate's C# compile job is the first thing that actually proves it. If it fails, it fails on syntax in five lines per function.
* `scripts/build.py regen-check` itself did not run here (`build.py` refuses to run as root, and this environment has no other account). I ran `cargo run -- generate` and checked `git status` by hand, which is the same property, not the same script.
* `value_legs` and `clone_legs` are accumulated by the driver (`ctx.streamValueLegs`, `ctx.streamCloneLegs`) and **asserted nowhere** — before this change and after it. A per-leg floor of the kind the peek probe carries (`streamPeekRepProbes < streamFunctions * 4`) would be a separate change; I did not make one, so a leg that survives on one site of two still reads as full coverage.
* Not run: `--xlang-hash`, `--fuzz-064`, and the C# and full four-server sweeps. None of them are affected by a change that only adds counters, but I did not run them.
