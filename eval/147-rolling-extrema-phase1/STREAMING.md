# Streaming path and per-bar latency, per candidate

The maintainer asked specifically: *"These functions also have a streaming
(`TA_S_*`) path that must stay bit-exact with the batch path. An algorithm with a
periodic O(period) rebuild is fine amortized but changes per-bar latency — say
what your approach does here."*

Two separate things have to be answered, and they came out differently than
expected.

## 1. Bit-exactness is not the binding constraint. Expressibility is.

`ta_codegen` does not have a hand-written stream per function; it **synthesizes**
`TA_S_*` by transcribing the batch body, rewriting every `in[X]` into
`ring[X % cap]`. `classify_locals` (streaming.rs:4216-4222) accepts as carried
stream state only scalars and **literal-fixed-size** local arrays. A `malloc`'d,
period-sized deque or Van Herk scratch is a pointer, so it is
`StreamError::NonScalarState`, and because all eight in-scope functions are
`stream`-flagged, `generate` exits 1 (`main.rs:551`). This is a *build* failure,
not a numeric one — for value-only functions a deque and the current automaton
produce identical bits.

So: **O(1)-amortized sliding extrema needs O(period) state; the stream
synthesizer forbids runtime-sized O(period) state.** Those two are incompatible
as the generator stands.

## 2. The existing escape hatch decides the latency answer

`analyze_fastpath_skip` (streaming.rs:469-489) recognises

    <prologue> if( <param> <= <literal> ) { fast } else { general } <epilogue>

as a *bit-identical batch-only split*. Only the `else` arm is streamed — "for
EVERY param" — and `stream_verify` enforces batch/stream bit-exactness across the
threshold. MIDPRICE already ships this at `<= 20`.

Consequence for the latency question: if a new algorithm is landed behind that
predicate, **the streaming path is untouched and its per-bar latency is exactly
what it is today.** No new spikes, no rebuild, no new state. The batch gets the
new algorithm; `TA_S_*` keeps the cached-extremum automaton it already has.

That is the honest answer for C1–C4 and C7: *they do not change streaming
latency at all, because they are not in the stream.* Whether that factoring is
acceptable is the maintainer's call, and the alternative (teach the generator a
stream tier that carries period-sized scratch) is real work whose cost should be
weighed against the measured win.

## 3. If a candidate WERE put in the stream

For completeness, the per-bar profile each candidate would have:

| candidate | amortized / bar | worst case on a single bar | periodic O(period) rebuild? |
|---|---|---|---|
| C0 baseline | O(period) on trend and flat input | O(period), on **every** bar | no |
| C1 / C7 deque | O(1) | O(period) — one bar can pop the whole deque | no |
| C2 Van Herk block-batched | O(1) | **not a per-bar algorithm**: it emits `period` outputs per block iteration | inherent |
| C3 Van Herk per-sample | O(1) | O(period), **1 bar in `period`** (suffix materialisation) | yes |
| C4 two-stack | O(1) | O(period), **1 bar in `period`** (drain) | yes |
| C5 / C6 rescan fix | improved (rescans fire far less often on tie-heavy input) | O(period), on every bar — **unchanged** | no |

Two things worth stating plainly:

* **C3 and C4 would strictly improve worst-case per-bar latency, not worsen it.**
  Today's worst case is a full O(period) rescan on *every* bar for flat or
  trending input; theirs is O(period) on one bar in `period`. The "periodic
  rebuild changes per-bar latency" concern is real in general but points the
  favourable way here.
* **C2 is inherently batch-only.** It cannot be a streaming algorithm: it
  produces its outputs in bursts of `period` and needs the whole older block
  complete before it emits any of them. So for C2 the fast-path-skip factoring is
  not a workaround, it is the correct structure — the batch and the stream really
  are different algorithms, and they are bit-identical because both are exact
  selections over the same window.
* **C6 needs none of this.** It is scalar-only, so the generator transcribes it
  into the stream exactly as it does today, and batch/stream stay bit-exact by
  construction. It is the only candidate with no structural question attached.

## 4. In-place aliasing (#130) shaped the implementations

Input and output may be the same buffer, and `outReal[outIdx]` can land exactly
on `inReal[trailingIdx]`. Every candidate therefore keeps **copies of values** in
its scratch and never re-reads `inReal[i]` for an `i` the loop has already
written. (An index-only deque that dereferences `inReal[dq[head]]` happens to be
safe too — the invariant is that no dereferenced index is ever `< trailingIdx` —
but relying on that is fragile, so the candidates here do not.) The Van Herk
forms read the older block *before* the first write into it, and only ever read
ahead of the write pointer afterwards. This is checked, not argued: the harness
runs every candidate with `outReal == inReal` and compares bitwise.
