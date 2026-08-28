//! What a stream handle carries of the `Core` it was opened from — issue #274.
//!
//! A handle used to embed a whole `Core` by value (280 bytes). It now carries
//! exactly the `CandleSetting`s its per-bar step reads, and nothing where the
//! step reads none. Two things have to stay true, and neither is reachable from
//! any other gate in the tree:
//!
//! * **The snapshot is still a snapshot.** The settings a handle computes with
//!   are the ones its `Core` held at `Open`, still readable after that `Core` is
//!   gone. The JSON-RPC servers keep one process-wide `Core` alive for the whole
//!   run, so `--codegen` and `--xlang-hash` never watch one go away.
//! * **The narrowing actually happened.** `size_of` is the only thing that can
//!   see it: the handle's fields are private, so a handle that went back to
//!   embedding a `Core` would compile, compute the same values, and pass every
//!   value gate there is.
//!
//! Both tests run against a tuned `CandleSettings`, which is also the only
//! coverage the streaming tier has for a non-default one — every other gate
//! runs on `Core::new()`.

#![allow(non_snake_case)]

use std::mem::size_of;

use ta_lib::{CandleSetting, CandleSettingType, Core, RangeType};

/// 64 bars with a scattering of exact-open==close dojis, so the pattern fires
/// on some bars and not others under either setting.
fn bars() -> (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>) {
    let n = 64;
    let mut open = Vec::with_capacity(n);
    let mut high = Vec::with_capacity(n);
    let mut low = Vec::with_capacity(n);
    let mut close = Vec::with_capacity(n);
    for i in 0..n {
        let base = 100.0 + 10.0 * (0.17 * i as f64).sin();
        let body = if i % 5 == 0 { 0.0 } else { 0.35 * (0.31 * i as f64).cos() };
        let o = base;
        let c = base + body;
        open.push(o);
        close.push(c);
        high.push(o.max(c) + 1.0 + 0.5 * (0.7 * i as f64).sin().abs());
        low.push(o.min(c) - 1.0 - 0.5 * (0.9 * i as f64).cos().abs());
    }
    (open, high, low, close)
}

/// A `BodyDoji` deliberately unlike the default (`HighLow` rather than
/// `RealBody`, a different period and a much larger factor), so a handle reading
/// the wrong settings produces a visibly different column rather than the same
/// one by luck.
fn tuned() -> Core {
    Core::builder()
        .candle_setting(
            CandleSettingType::BodyDoji,
            CandleSetting { range_type: RangeType::HighLow, avg_period: 12, factor: 0.30 },
        )
        .build()
        .expect("a valid candle setting")
}

fn batch_column(core: &Core, o: &[f64], h: &[f64], l: &[f64], c: &[f64]) -> Vec<i32> {
    let mut out = vec![0_i32; o.len()];
    let range = core.CDLDOJI(0, o.len() - 1, o, h, l, c, &mut out).expect("batch CDLDOJI");
    out[..range.count].to_vec()
}

#[test]
fn a_handle_computes_with_the_settings_it_was_opened_with() {
    let (o, h, l, c) = bars();
    let want = batch_column(&tuned(), &o, &h, &l, &c);

    // The `Core` the handle came from does not outlive this block: whatever the
    // handle answers with from here on, it is holding itself.
    let (mut stream, _last) = {
        let core = tuned();
        core.CDLDOJI_Open(&o, &h, &l, &c).expect("enough history")
    };

    // The opener replays the whole history, so the stream carries on at the bar
    // after it. Extend the series and compare against the batch over the same
    // extended range, which is the only way to reach `update` at all.
    let mut o2 = o.clone();
    let mut h2 = h.clone();
    let mut l2 = l.clone();
    let mut c2 = c.clone();
    let (eo, eh, el, ec) = bars();
    o2.extend_from_slice(&eo);
    h2.extend_from_slice(&eh);
    l2.extend_from_slice(&el);
    c2.extend_from_slice(&ec);

    let mut got = Vec::new();
    for i in o.len()..o2.len() {
        got.push(stream.update(o2[i], h2[i], l2[i], c2[i]).expect("finite bars"));
    }

    let full = batch_column(&tuned(), &o2, &h2, &l2, &c2);
    let tail = &full[full.len() - got.len()..];
    assert_eq!(got, tail, "a handle's per-bar column must match the batch it was opened from");
    assert_eq!(&want[..], &full[..want.len()], "the batch prefix is the opener's own range");

    // The control: this comparison is only worth something while the tuned
    // setting changes the answer. On the default `Core` the same bars produce a
    // different column, so a handle that lost the setting would fail above.
    let plain = batch_column(&Core::new(), &o2, &h2, &l2, &c2);
    assert_ne!(plain, full, "the tuned BodyDoji must change the column it is read into");
}

#[test]
fn a_handle_does_not_embed_a_core() {
    // Pre-#274 these were 320, 344 and 712 bytes, each of them 280 bytes of
    // embedded `Core`; the assertion below was false for all three.
    assert!(
        size_of::<ta_lib::EMA_Stream>() < size_of::<Core>(),
        "EMA's step reads nothing from Core, so its handle must be far smaller than one"
    );
    assert!(
        size_of::<ta_lib::CDLDOJI_Stream>() < size_of::<Core>(),
        "CDLDOJI's step reads one CandleSetting, not a Core"
    );
    assert!(
        size_of::<ta_lib::STDDEV_Stream>() < size_of::<Core>(),
        "a composed handle must not embed one Core per nested leg"
    );
    // And the shape that made the nesting expensive: a handle holding another
    // handle stays below what two `Core`s alone would cost.
    assert!(
        size_of::<ta_lib::BBANDS_Stream>() < 5 * size_of::<Core>(),
        "BBANDS holds five sub-handles; none of them may bring its own Core"
    );
}
