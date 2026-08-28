//! What a stream handle keeps of the `Core` it was opened on (issue #274).
//!
//! Hand-written, not generated: this file lives in
//! `ta_codegen/generator/templates/rust/stream_handle_core.rs` and is copied
//! verbatim into the crate by `generate` (the Rust backend's `clean_keep` holds
//! it, so `generate` never deletes it). It is declared `#[cfg(test)]` in
//! `mod.rs`, so nothing here ships in a release build. Run it with
//! `cargo test --tests -p ta-lib`.
//!
//! A handle used to embed the whole `Core` by value — 280 bytes, of which the
//! step reads at most five 16-byte `CandleSetting`s and, for 119 of the 176
//! functions, nothing at all. It now stores exactly the settings its own step
//! reads. Two things have to hold for that to be a pure win, and neither is
//! visible to the value gates that compare outputs:
//!
//! 1. **The footprint is actually gone.** `size_of` is the only observer of a
//!    field nobody reads, so a regression that puts the `Core` back — or widens
//!    the narrowed set to the whole `CandleSettings` block — is silent
//!    everywhere else in the tree. The bounds below sit far under the old
//!    sizes and just as far over the new ones, so they fail on the regression
//!    without failing on an unrelated state-field change.
//!
//! 2. **The settings are still a SNAPSHOT taken at Open.** The whole-`Core`
//!    clone made that automatic. Reading them off a narrowed field keeps it
//!    only while the field is genuinely written at Open and read at every step,
//!    so `tuned_setting_*` below opens on a tuned `Core`, drops that `Core`,
//!    and requires the stream to keep answering as the tuned batch does.
//!
//! Both legs carry their own control. The size bounds are paired with the
//! observation that a handle carrying settings is *larger* than one carrying
//! none, so "narrowed everything away, including what the step reads" cannot
//! pass them — and it could not compile in the first place. The snapshot legs
//! asserts the tuned and default results DIFFER before requiring the stream to
//! match the tuned one: without that, a stream that silently fell back to
//! default settings would satisfy the comparison on any input where the two
//! agree, which is most of them.

use crate::ta_func::types::{CandleSetting, CandleSettingType, Core, RangeType};

const N: usize = 120;

/// Deterministic OHLC with a wide spread of body/shadow ratios, so a change in
/// what counts as a "doji" body actually moves the pattern count.
fn ohlc(n: usize) -> (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut open = Vec::with_capacity(n);
    let mut high = Vec::with_capacity(n);
    let mut low = Vec::with_capacity(n);
    let mut close = Vec::with_capacity(n);
    for i in 0..n {
        let base = 100.0 + 10.0 * (0.1 * i as f64).sin();
        let body = 0.9 * (0.37 * i as f64).sin();
        let o = base;
        let c = base + body;
        open.push(o);
        close.push(c);
        high.push(o.max(c) + 0.6 * (0.23 * i as f64).cos().abs());
        low.push(o.min(c) - 0.6 * (0.31 * i as f64).sin().abs());
    }
    (open, high, low, close)
}

// ---------------------------------------------------------------------------
// 1. Footprint
// ---------------------------------------------------------------------------

/// The handles named here spanned 320–2168 bytes while each embedded a `Core`,
/// which is 280 bytes on its own and multiplies through nesting (`BBANDS`
/// held five). The bounds are the measured new sizes rounded up to the next
/// 8-byte step, so a `Core` coming back anywhere in the tree — directly or
/// through a nested sub-handle — overshoots by more than the slack.
#[test]
fn a_handle_does_not_embed_a_core() {
    use std::mem::size_of;
    use crate::ta_func::types::Core as _CoreTy;

    // A `Core` is the thing that must not be in any of them.
    let core_bytes = size_of::<_CoreTy>();
    assert!(
        core_bytes >= 256,
        "control: a Core is expected to be the large value this test exists to keep out of handles, \
         measured {core_bytes} bytes"
    );

    let cases: [(&str, usize, usize); 5] = [
        // (handle, measured size, ceiling)
        ("EMA_Stream", size_of::<crate::ta_func::ema::EMA_Stream>(), 64),
        ("SMA_Stream", size_of::<crate::ta_func::sma::SMA_Stream>(), 96),
        ("CDLDOJI_Stream", size_of::<crate::ta_func::cdldoji::CDLDOJI_Stream>(), 112),
        ("STDDEV_Stream", size_of::<crate::ta_func::stddev::STDDEV_Stream>(), 192),
        ("BBANDS_Stream", size_of::<crate::ta_func::bbands::BBANDS_Stream>(), 768),
    ];
    for (name, got, ceiling) in cases {
        assert!(
            got <= ceiling,
            "{name} is {got} bytes, over the {ceiling}-byte ceiling — a Core ({core_bytes} B) is \
             back in the handle, or the narrowed settings widened to the whole block"
        );
    }
}

/// The control for the bounds above: narrowing is per function, not a blanket
/// deletion. `CDLDOJI`'s step reads `BodyDoji`, so its handle must carry it —
/// and therefore be strictly larger than the same-shaped handle of a function
/// whose step reads nothing.
///
/// Without this, "emit no settings at all" passes every ceiling above. It could
/// not compile — the step body names the setting — but a ceiling that only ever
/// gets smaller is not a gate, it is a direction.
#[test]
fn a_candlestick_handle_is_larger_for_carrying_its_setting() {
    use std::mem::size_of;
    let doji = size_of::<crate::ta_func::cdldoji::CDLDOJI_Stream>();
    let ema = size_of::<crate::ta_func::ema::EMA_Stream>();
    assert!(
        doji > ema,
        "CDLDOJI ({doji} B) carries a CandleSetting EMA ({ema} B) does not, so it must be larger"
    );
    assert!(
        doji - ema >= size_of::<CandleSetting>(),
        "the difference must account for at least the one CandleSetting CDLDOJI's step reads"
    );
}

// ---------------------------------------------------------------------------
// 2. Snapshot semantics
// ---------------------------------------------------------------------------

/// A `BodyDoji` far off the default (`HighLow`, 10, 0.1), so the pattern count
/// genuinely moves.
///
/// `avg_period` is left at 10 on purpose: it feeds the lookback, and a tuned
/// core with a different one would shift the batch range as well as the values,
/// which turns the comparisons below into an off-by-`n` argument instead of a
/// value one. `range_type` and `factor` change what counts as a doji without
/// moving a single index.
fn tuned_core() -> Core {
    Core::builder()
        .candle_setting(
            CandleSettingType::BodyDoji,
            CandleSetting { range_type: RangeType::RealBody, avg_period: 10, factor: 0.9 },
        )
        .build()
        .expect("a valid candle setting")
}

/// Batch `CDLDOJI` over the whole series under `core`.
fn batch(core: &Core, o: &[f64], h: &[f64], l: &[f64], c: &[f64]) -> Vec<i32> {
    let mut out = vec![0i32; N];
    let r = core
        .CDLDOJI(0, N - 1, o, h, l, c, &mut out)
        .expect("batch CDLDOJI");
    out[..r.count].to_vec()
}

/// The stream opened on `core`, then advanced bar by bar to the end.
///
/// `core` is taken by value and dropped before the updates run: whatever the
/// handle needs from it, it must already hold.
fn streamed(core: Core, o: &[f64], h: &[f64], l: &[f64], c: &[f64], warm: usize) -> Vec<i32> {
    let mut out = vec![0i32; N];
    let (mut s, range) = core
        .CDLDOJI_OpenAndFill(&o[..warm], &h[..warm], &l[..warm], &c[..warm], &mut out)
        .expect("open CDLDOJI");
    drop(core);
    let mut vals: Vec<i32> = out[..range.count].to_vec();
    for i in warm..N {
        vals.push(s.update(o[i], h[i], l[i], c[i]).expect("update"));
    }
    vals
}

/// The control leg: the tuned settings and the defaults must disagree on this
/// series, or the comparison below proves nothing.
#[test]
fn tuned_setting_changes_the_batch_result() {
    let (o, h, l, c) = ohlc(N);
    let default = batch(&Core::new(), &o, &h, &l, &c);
    let tuned = batch(&tuned_core(), &o, &h, &l, &c);
    assert_eq!(default.len(), tuned.len(), "same range, whatever the setting");
    assert_ne!(
        default, tuned,
        "control: the tuned BodyDoji must change CDLDOJI's output on this series, or the \
         snapshot test below cannot fail"
    );
}

/// The property: a handle opened on a tuned `Core` answers as that `Core`'s
/// batch does, for every bar, after the `Core` itself is gone.
#[test]
fn a_stream_keeps_the_settings_it_was_opened_under() {
    let (o, h, l, c) = ohlc(N);
    let expected = batch(&tuned_core(), &o, &h, &l, &c);
    let got = streamed(tuned_core(), &o, &h, &l, &c, 40);
    assert_eq!(
        got.len(),
        expected.len(),
        "the stream must cover exactly the batch's range"
    );
    assert_eq!(
        got, expected,
        "a stream opened on the tuned Core must stay on it — falling back to the defaults is \
         exactly what dropping the stored settings would look like"
    );
}

/// The same property through `peek` and through a clone: both run the step on a
/// copy of the handle, so both go through whatever `restore_from` and `Clone`
/// carry of the settings.
#[test]
fn peek_and_clone_carry_the_opened_settings_too() {
    let (o, h, l, c) = ohlc(N);
    let expected = batch(&tuned_core(), &o, &h, &l, &c);
    let warm = 40;
    let mut out = vec![0i32; N];
    let core = tuned_core();
    let (mut s, range) = core
        .CDLDOJI_OpenAndFill(&o[..warm], &h[..warm], &l[..warm], &c[..warm], &mut out)
        .expect("open CDLDOJI");
    drop(core);

    let mut idx = range.count;
    for i in warm..N {
        // peek first: it must equal what the commit is about to return.
        let peeked = s.peek(o[i], h[i], l[i], c[i]).expect("peek");
        // and a fork must answer identically to its parent.
        let mut forked = s.clone();
        let forked_val = forked.update(o[i], h[i], l[i], c[i]).expect("clone update");
        let committed = s.update(o[i], h[i], l[i], c[i]).expect("update");
        assert_eq!(peeked, committed, "peek and update disagree at bar {i}");
        assert_eq!(forked_val, committed, "a clone diverged at bar {i}");
        assert_eq!(committed, expected[idx], "bar {i} left the tuned setting");
        idx += 1;
    }
    assert_eq!(idx, expected.len(), "every batch bar was covered");
}
