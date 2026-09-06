---
title: Java Streaming API
description: "Java streaming API for live feeds: a stream carries indicator state from bar to bar at O(1) per update, bit-identical to the batch calls."
toc: false
---

::: warning Not yet released
The Java API is not yet released. Estimated release: **Q1 2027**.
:::

The **streaming API** is built for live feeds: open a stream once, then feed it one bar at a time. The stream carries its state from bar to bar, so each new bar costs O(1) — and every value is **bit-identical** to what the [batch method](/api/java/) (`core.SMA`, `core.RSI`, …) would return by recomputing over the whole array.

Each streamable function adds two factory methods on `Core` and a handful of methods on its stream (a class nested in `Core`, e.g. `Core.SmaStream` — unrelated to `java.util.stream`):

| Call | When | Does |
|------|------|------|
| `core.<name>Open(history, params)` | once | validate params, consume warm-up history, return a **stream** |
| `stream.update(bar)` | once per **closed** bar | commit one bar and answer the new value |
| `stream.peek(bar)` | any time on the **forming** bar | evaluate a provisional bar **without** committing |

A single-output function answers with a `double` (or an `int` for a candlestick
pattern) return. A multi-output one writes into a sink you pass and own — see
[Multi-input / multi-output](#multi-input-multi-output).

One more call, `openAndFill`, writes array output instead of a single value — see [Array-Fill Open](#array-fill-open) below.

Additional read-only [utility functions](#utility-calls) are available.

There is no `close` — a stream is ordinary heap state, so an unreferenced stream is simply garbage-collected.

## Example (SMA)

```java
import io.github.talib.Core;

Core core = Core.DEFAULT;

// Seed with warm-up history (>= SMA_Lookback(period) + 1 bars).
double[] history = /* ...your closing prices... */;
Core.SmaStream s = core.smaOpen(history, 30); // value() starts at the last history bar

// Each time a bar closes:
double v = s.update(newClose);                  // throws only on a non-finite bar

// Intra-bar, on the not-yet-closed bar (repeat as the price ticks):
double provisional = s.peek(formingClose);      // state left unchanged
```

`open` returns the stream directly; its `value()` starts at the last history bar's value. After a successful `open`, the only thing `update` and `peek` reject is invalid input such as NaN or ±Inf. A rejection changes nothing at all — no state, no value, and no range. To count a rejected bar rather than re-feed it, call `advance()`; `value()` then answers the value(s) at the last bar the stream counted (see [Utility Calls](#utility-calls)).

## Rules

- **Warm-up.** `open` succeeds only if `history.length >= <NAME>_Lookback(params) + 1` — with fewer bars there is no defined value yet. Too little history throws `InsufficientHistoryException` (see [Error model](#error-model)). After `open`, the history can be discarded — the stream keeps everything it needs.
- **Closed vs forming bar.** `update` commits state irreversibly, so use it only for **closed** bars. `peek` returns exactly the value the next `update` would, without committing — call it as often as the forming bar ticks. `value()` re-reads the last committed value without recomputing.
- **Parameters are fixed at `open`.** Changing a parameter means a new stream. [Unstable period](/api/#numerical_stability) and [candle settings](/api/#candle_settings) are read from the owning `Core` at `open`. Since `Core` is immutable they cannot change underneath a live stream — to stream with different settings, build a new `Core` and open from that.
- **Threads.** A stream is single-writer — `update`, `peek`, `value()`, and `clone()` must not race with an `update` on the same stream. With no concurrent `update`, `peek`/`value()`/`clone()` are read-only and safe to call concurrently after safe publication. Distinct streams (including `clone()` results) are fully independent.
- **Not serializable.** To checkpoint, retain the history and re-open — the result is bit-identical by contract.

## Multi-input / multi-output

Inputs and outputs mirror the batch method. A multi-output function writes its
outputs into a `Core.<Name>Out` — a plain mutable object with one public field
per output, in batch output order — that **you** allocate and pass in.
Candlestick patterns return `int`:

```java
// MACD: one input, three outputs
Core.MacdStream m = core.macdOpen(history, 12, 26, 9);
Core.MacdOut out = new Core.MacdOut();   // allocate once, reuse every bar
m.update(newClose, out);
// out.macd, out.macdSignal, out.macdHist

// A candlestick pattern returns int
Core.CdldojiStream c = core.cdldojiOpen(open, high, low, close);
int pattern = c.update(o, h, l, cl);
```

Reusing one sink is the point: `update`, `peek` and `value` overwrite its fields
and allocate nothing, so a hot loop costs zero bytes per bar. The price is that
**its contents are only valid until the next call that writes it**. It is a
buffer, not a reading — a reference kept past that call, or one put in a
collection, sees the value change underneath it. Copy the fields out if the
reading has to outlive the call, or allocate one sink per slot. For the same
reason `<Name>Out` deliberately has no `equals`/`hashCode`: value equality on a
mutable object breaks `HashMap`/`HashSet` the moment a reused sink becomes a key.
Passing `null` is an `IllegalArgumentException`, taken before the bar is
committed.

## Array-Fill Open

`open` and `update` each write a single value. One more call writes a full array instead — the same shape the [batch method](/api/java/) would produce — while still opening the stream:

| Call | When | Does |
|------|------|------|
| `core.<name>OpenAndFill(..)` | once, instead of `open` | like `open`, but also fills the output for **every** history bar |

```java
import io.github.talib.OutRange;

double[] warmup = new double[history.length];

Core.SmaStream s = core.smaOpenAndFill(history, 30, warmup);
OutRange r = s.outRange();                      // the bars it has an output for

// warmup[0 .. r.count() - 1] is the SMA over all of history; then stream on:
double v = s.update(newClose);
```

The optional parameters and output arrays are exactly the [batch method](/api/java/)'s. The range written is reported on the returned stream as `outRange()` rather than through out-parameters — see [Utility Calls](#utility-calls) below. The output arrays must not alias the input or each other.

## Utility Calls

| Call | When | Does |
|------|------|------|
| `stream.value()` / `stream.value(out)` | any time | the value(s) at the last bar the stream counted, without recomputing |
| `stream.clone()` | any time | an independent fork of the stream, at the same bar |
| `stream.outRange()` | any time | the bars the stream has an output for — the batch range over the same bars |
| `stream.advance()` | after a bar you will not feed | counts that bar and nothing else |

```java
Core.SmaStream s = core.smaOpen(history, 30);

double v = s.value();               // the value at the last bar s counted
Core.SmaStream fork = s.clone();    // independent from here on
OutRange r = s.outRange();          // the bars s has an output for
s.advance();                        // a bar you skipped, counted
```

`value()` hands back what `open` or the last `update` already gave you: it
recomputes nothing and takes no bar. A single-output function returns `double`; a
multi-output one takes a `Core.<Name>Out` and writes every output into it at
once. `open` seeds it, an accepted bar replaces it, and a bar you skip with
`advance()` holds it — a held value is that bar's output — while `peek` and a
rejected bar leave it alone. So it always names the bar `outRange()` reports.

`clone()` gives a second, independent stream at the same bar: arrays are copied and
sub-streams cloned recursively, and the fork carries the value and the range
verbatim. The `Core` reference is shared, because a `Core` is immutable for a
stream's lifetime. It overrides `Object.clone()` but does not use the `Cloneable`
protocol — the body is a copy constructor, so it needs no marker interface and
throws no `CloneNotSupportedException`. It is the only way to fork a live stream —
the warm-up history is gone once `open` returns — and it is what makes `value()`
worth having, since a fork has no call that handed you its value.

`outRange()` reports the bars the stream has an output for: `(lookback,
historyLen - lookback)` at `open`, then one more for every bar `update` accepts. A
rejected `update` adds nothing, and neither does `peek`.

`advance()` counts a bar the stream was never fed — one an `update` rejected and
that will not be re-fed, or a session with no print. It moves the range by one and
nothing else: the state is untouched and `value()` keeps answering the previous
output, which is that bar's output. Without it two streams on one feed drift a bar
apart the moment one of them skips, so decide at the rejection: re-feed the bar
with the corrected value, or count it here.

See [Rules](#rules) for when concurrent reads of these are safe.

## Error model

| Call | Behaviour |
|------|-----------|
| `<name>Open` / `<name>OpenAndFill` | Too little history throws `InsufficientHistoryException` (a subclass of `IllegalArgumentException` — catch it to accumulate more bars and retry). Out-of-range parameters throw plain `IllegalArgumentException`. |
| `update` / `peek` | `IllegalArgumentException` on invalid input such as NaN or ±Inf. A rejection changes nothing at all — no state, no value, and no range — so to count a rejected bar rather than re-feed it, call `advance()`. Nothing else throws after a successful `open` (see the note below for the one composed-indicator corner). |
| `value` / `clone` / `outRange` / `advance` | Never throw. |

## Discovering streamable functions

When driving TA-Lib through the [abstraction layer](/api/#abstract), streamable functions carry the `TA_FUNC_FLG_STREAM` flag in their function info.
