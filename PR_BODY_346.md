feat: VHF -- Vertical Horizontal Filter, and its window pair is not co-terminal (#346)

Implements #346. Adam White's trend-versus-range filter: a window's net
directional travel over the distance the price actually walked.

`ta_codegen/input/vhf/{vhf.yaml,vhf.c,vhf.md}` plus the generated C / Rust /
Java / C# output, a new `src/tools/ta_regtest/ta_test_func/test_vhf.c`, and its
registration in the five places a new test needs (prototype, `DO_TEST` tag,
both source lists, CHANGELOG). **No generator change** -- the body shape below
analyzes clean as it stands.

```
num[t] = max(P[t-n+1..t]) - min(P[t-n+1..t])     /* n closes                */
den[t] = sum |P[j] - P[j-1]|, j in [t-n+1..t]    /* n changes, reads P[t-n]  */
VHF[t] = num[t] / den[t]                            lookback = n
```

## The one decision the whole function turns on

The two windows are **not** co-terminal -- the extrema span `n` closes, the
differences span `n` changes and so reach one bar further back -- which is what
makes the lookback `n` and not `n-1`. Tulip, pandas-ta-classic and the Achelis
book vector agree on it; the prose sources are silent on it, which is where an
implementation written from prose goes wrong.

Two visible consequences, both pinned by tests rather than asserted in a
comment:

- A linear ramp reads exactly **(n-1)/n**, not 1.0. Reading 1.0 on a ramp is
  the signature of a co-terminal pair. Exactly 1.0 is reachable, but only when
  the window's oldest change is zero -- which is why the Achelis p.354 vector
  ends on 1.
- `outBegIdx` is `max(startIdx, n)`.

## Streaming, and the shape it forced

`stream-census` gives `T3 state=0 lags=0 outs=1`; `flags: [stream]` from day
one.

Getting there took the body's shape, not new analyzer support. The natural
(Tulip) transcription is a rolling sum next to an absolute-index extrema
automaton, which `streaming.rs` rejects as mutually exclusive buffer forms, so
the body **rescans each bar**, reading only `inReal[today-i]` -- the AVGDEV
precedent. Two further things are load-bearing:

- **One counter, not two.** The extrema window and the path window have
  different lengths, and one counter carrying two different bounds is refused
  outright (`window counter 'i' has inconsistent loop bounds`). Splitting them
  into `i` and `j` analyzes clean but emits **two** input rings, of `n` and
  `n+1` doubles, and reads every bar twice. Folding both into one pass over the
  longer window, with an inner `if( i < optInTimePeriod )` for the extrema,
  keeps the state at `n+1` doubles and reads each bar once. Both forms were
  generated and diffed; this is the second.
- The `i == 0` term of the path is `|P[t] - P[t]|`, an exact `0.0` added to an
  exact `0.0`, which is what lets the loop keep the uniform ascending `0..n`
  shape `collect_window_bounds` accepts.

## Costs, stated rather than special-cased

- **O(n) per bar, batch and stream**, against Tulip's O(1) rolling sum: 29
  reads per bar at the default n=28. The alternative is the rejected
  automaton-plus-sum shape, i.e. batch-only or new analyzer support. AVGDEV
  already ships on these terms. **I did not benchmark it** -- no performance
  claim is made here in either direction.
- The exact `path > 0.0` guard is a branch in the loop tail. It is what makes
  the 0/0 answer exact.

## The 0/0 case

A flat window makes `den` exactly zero -- and only a flat window does, since a
rescan sums non-negative magnitudes fresh each bar and carries no residue. That
also forces `num` to zero, so the degenerate case is exactly `0/0` and nothing
else. VHF answers exactly `0.0`.

**Not** `TA_IS_ZERO(den)`, which #346 proposed: that is a fixed 1e-14 absolute
band met by a quantity carrying the quote unit -- the #253 / #107 / #244
defect. I tried it, and the repo's own QUOTE-UNIT/SCALE gate reddens:

```
Fail: VHF out0[0] on the 'history' series: at 2^-120 it is 0, the natural
      quote unit gives 0.28895638235045984
```

Tulip returns NaN here (no guard) and pandas-ta returns `+inf`; `vhf.md` says
so, since a user comparing libraries on a flat series will meet it.

## Verification

Four legs in `test_vhf.c`, plus what the corpus-wide gates give for free.

1. **Tulip's published `vhf 5` vector** over its 15-close series: shape
   `(5, 10)` and the ten printed values. Fixes formula, window pair and first
   valid index at once, against an implementation nobody here wrote. The same
   call also runs in-place (`outReal == inReal`, bit-for-bit) and through
   `server_verify`.
2. **Differential against the shipped primitives** on the 252-bar close series
   at periods {2, 5, 14, 28, 100}: `TA_MAX`/`TA_MIN` for the numerator, an
   explicit `|change|` array through `TA_SUM` for the denominator -- the leg
   that reaches the two window extents independently of VHF's own loop.
   Measured max relative difference **0.0**: every value bit-identical, even
   though VHF walks the path newest-first and `TA_SUM` oldest-first. The
   assertion is 1e-13 rather than `memcmp`, because that agreement is measured
   on this corpus, not proved. Non-vacuity floor on the comparison count.
   *Not the composite category*: TA-Lib ships no vector ABS, so the
   denominator needs one hand-written line.
3. **Exact-arithmetic values**: ramp up and ramp down both exactly `(n-1)/n`;
   the zero-oldest-change ramp exactly `1.0`; all-flat exactly `0.0` (NaN
   without the guard, so it cannot pass vacuously); a flat *tail* after a
   moving head, so the guard is shown to fire per window and not per series;
   and a five-bar hand-computed window whose value is wrong under a co-terminal
   pair.
4. `doRangeTestEx` at `TA_STABLE_EPSILON`, no unstable id.

**Sabotage controls -- each applied, generated, built and watched fail:**

| control | what went red |
|---|---|
| extrema window made co-terminal with the path | leg 1 (`out[1]` 0.768 vs 0.232); and, with leg 1 removed so leg 2 speaks first, leg 2 (period 2, bar 2, rel 6.5e+00) |
| `0/0` guard removed | the generic abstract zero-input sweep, error 616 |
| lookback `n-1` | the generic in-place alias sweep, error 621, 225/225 values wrong -- i.e. the aliasing argument in `vhf.c` is exactly `startIdx >= n` |
| exact guard replaced by `TA_IS_ZERO` | QUOTE-UNIT/SCALE, quoted above |
| generated Rust `vhf.rs` perturbed (`path` seeded 1e-3), rust server rebuilt | abstract server parity, `C=0.964285714285714 server=0.964251276740116` on `inputNegData` |

Note on controls 2, 3 and 5: the generic gates speak before the VHF group is
reached, so those runs did **not** separately show my own flat-window, shape
and `server_verify` legs going red. Control 1 is the one where I watched my own
legs fail.

**Gates green on this branch:** full unfiltered `ta_regtest`; `regen-check`
(the second `generate` is a fixed point); `check-source-lists`; generator
`cargo test`; `clippy -D warnings` over both crates; generated crate
`cargo test --lib` (73) and `--doc` (550, VHF's own doctest among them);
`cargo doc --no-deps` warning-free; `--xlang-hash --language=c,rust,java
--function=VHF` **PASS, bit-identical at zero tolerance** (Rust 1948 cases /
Java 1948 cases, 0 mismatches; input-port and array-transport self-checks OK).

`--codegen --language=c,rust,java --function=VHF` runs the structural legs on
all three servers and the fuzz-port self-check (9/9 shapes bit-identical), and
then says what it should say for a post-cutover function:

```
NO VALUE COMPARISON: ... every match was skipped ... This is NOT a pass.
```

so the cross-language value gate for VHF is `--xlang-hash` above, not this.

## Unresolved -- one failure I could not reproduce

On the **first** `--codegen --language=c,rust,java --function=VHF` run, the
abstract server-parity sweep reported:

```
ABSTRACT ERROR [VHF]: real output[0][0] C=0 server=0.0714285714285714
Failed for [VHF][inputRandDblEpsilon]
```

`0.0714... = 1/14` is the ordinary answer for alternating ±DBL_EPSILON data at
period 28; an exact `0` is what a flat window gives. So the two sides appear to
have computed on different data rather than differing numerically.

It has **not** recurred in eight subsequent runs -- per-language (c / rust /
java separately), all three together, six different `--seed=` values including
a replay of the failing run's own seed `1788522031`, and the later run with the
reference oracle present. The failing run started while `build.py servers` was
still writing the server binaries and relinking `bin/ta_regtest`, which is my
best guess and is not a diagnosis. **I did not root-cause it, and I am not
claiming it was a flake.** If it reappears in CI, the discontinuity worth
looking at first is this function's exact `path > 0.0` guard on epsilon-scale
input.

## What else I did not check

- **C# was not compiled or run.** No .NET SDK on this box: `Core_VHF.cs` and
  the C# server are generated output only, and no C# arm of any gate ran --
  including the C# arm of `--xlang-hash`.
- **Tulip was not re-run here.** The golden vector is quoted from #346, which
  transcribed it from Tulip's shipped `tests/untest.txt`; I reproduced the ten
  values from the formula independently before freezing them, at the file's
  printed three decimals, which is also the tolerance. The **Achelis p.354
  vector is deliberately omitted**: its nine input closes live in Tulip's
  `atoz.txt`, which is not vendored here, and a golden without its input is not
  a test.
- **No new oracle arm.** #346 asks for `ta_pandas_serve` and
  `ta_trading_signals` arms; those servers are not in this repository and
  pandas-ta is not installed on this box. So VHF has no live third-party arm:
  the independent evidence is the frozen Tulip vector at three decimals plus
  the in-tree differential.
- `--fuzz-064` was not run (VHF post-dates 0.6.4, so it is auto-skipped there).

## One thing for you to decide

`ta_error_number.h` gets `TA_VHF_DIFFERENTIAL_VACUOUS = **1656**`, skipping
1655 because the unmerged `issue-349-vortex` branch claims 1655. If #349 lands
differently, renumber this one -- the enum is append-only and I did not want
two branches claiming one slot.
