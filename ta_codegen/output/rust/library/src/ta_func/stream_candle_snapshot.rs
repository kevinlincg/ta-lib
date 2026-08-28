//! What a stream handle carries out of `Core` — issue #274.
//!
//! Hand-written, not generated: this file lives in
//! `ta_codegen/generator/templates/rust/stream_candle_snapshot.rs` and is copied
//! verbatim into the crate by `generate` (the Rust backend's `clean_keep` holds
//! it, so `generate` never deletes it). It is declared `#[cfg(test)]` in
//! `mod.rs`, so nothing here ships in a release build. Run it with
//! `cargo test --tests -p ta-lib`.
//!
//! A handle used to embed a whole `Core`. It now embeds `<NAME>_StreamCore`, a
//! per-function snapshot carrying only the candle settings that function's
//! per-bar step actually reads — nothing for 119 of the 176 streamable
//! functions, and at most five settings for the rest.
//!
//! Two failure modes survive that change, and neither is a compile error:
//!
//! 1. the snapshot **drops** a setting the step needs and the step falls back to
//!    a default (it cannot: a missing field fails to compile — but a snapshot
//!    built from `CandleSettings::default_settings()` instead of the caller's
//!    `Core` would);
//! 2. the snapshot **cross-wires** one, copying `body_long` into the slot the
//!    step reads as `shadow_short`.
//!
//! So each row below perturbs ONE setting away from its default and asserts
//! both halves:
//!
//! - **the answer** — the stream's per-bar values equal the batch's over the
//!   same bars on that same perturbed `Core`. The batch reads the live `Core`,
//!   so this fails for a dropped or cross-wired snapshot.
//! - **the control** — those values differ from the same stream opened on a
//!   default `Core`. Without it the first assertion passes vacuously for a
//!   perturbation the data never reaches, which is most of them: the multipliers
//!   here are the ones measured to move this series, not round numbers.
//!
//! Coverage is by SETTING, not by function count: every one of the ten settings
//! any generated step reads appears at least once (`BodyVeryLong` is read by no
//! step, so there is nothing to pin). `CDLADVANCEBLOCK` — the widest snapshot in
//! the corpus at five settings — is then swept one setting at a time, which is
//! what a cross-wire has to survive.
//!
//! The values must come from `update`, never from `Open` or `OpenAndFill`:
//! those transcribe the batch body and run on `Core` itself, so they would read
//! the live settings no matter what the handle carries.

use crate::ta_func::types::{
    CandleSetting, CandleSettingType, CandleSettings, Core, RetCode,
};

/// Bars of history handed to the opener before the stepping starts. Comfortably
/// past every candlestick lookback in the corpus (the widest is 12).
const WARM: usize = 40;
/// Total bars. Long enough that each perturbation below moves at least one bar.
const N: usize = 400;

/// A deterministic OHLC series with real bodies, real shadows and gaps — enough
/// shape for the candlestick predicates to fire on the defaults, which is what
/// makes a perturbation visible.
fn ohlc() -> (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut open = Vec::with_capacity(N);
    let mut high = Vec::with_capacity(N);
    let mut low = Vec::with_capacity(N);
    let mut close = Vec::with_capacity(N);
    for i in 0..N {
        let i = i as f64;
        let mid = 100.0 + 12.0 * (0.07 * i).sin() + 3.0 * (0.31 * i).cos();
        let body = 1.4 * (0.23 * i).sin();
        let o = mid - body / 2.0;
        let c = mid + body / 2.0;
        open.push(o);
        close.push(c);
        high.push(o.max(c) + (0.9 * (0.41 * i).sin()).abs());
        low.push(o.min(c) - (0.9 * (0.53 * i).cos()).abs());
    }
    (open, high, low, close)
}

/// The default for one setting, read back off `CandleSettings` so a perturbation
/// is stated as a multiple of the shipped factor rather than a bare literal.
fn default_of(t: CandleSettingType) -> CandleSetting {
    let d = CandleSettings::default_settings();
    match t {
        CandleSettingType::BodyLong => d.body_long,
        CandleSettingType::BodyVeryLong => d.body_very_long,
        CandleSettingType::BodyShort => d.body_short,
        CandleSettingType::BodyDoji => d.body_doji,
        CandleSettingType::ShadowLong => d.shadow_long,
        CandleSettingType::ShadowVeryLong => d.shadow_very_long,
        CandleSettingType::ShadowShort => d.shadow_short,
        CandleSettingType::ShadowVeryShort => d.shadow_very_short,
        CandleSettingType::Near => d.near,
        CandleSettingType::Far => d.far,
        CandleSettingType::Equal => d.equal,
        _ => panic!("unknown candle setting"),
    }
}

/// A `Core` whose one named setting has its factor scaled, everything else at
/// the shipped default.
fn core_with(t: CandleSettingType, factor_scale: f64) -> Core {
    let d = default_of(t);
    let perturbed = CandleSetting {
        range_type: d.range_type,
        avg_period: d.avg_period,
        factor: d.factor * factor_scale,
    };
    Core::builder()
        .candle_setting(t, perturbed)
        .build()
        .expect("a scaled factor is a legal setting")
}

/// One row: a function, a setting its step reads, and a factor multiple
/// measured to change that function's answers on this series.
struct Row {
    what: &'static str,
    setting: CandleSettingType,
    factor_scale: f64,
}

/// Drive one row.
///
/// `open` and `step` are closures and `batch` fills the whole series through the
/// batch tier on the same `Core`, returning the range it wrote — so one body
/// serves every candlestick signature.
fn snapshot_carries_the_setting<H>(
    row: &Row,
    open: impl Fn(&Core) -> Result<H, RetCode>,
    step: impl Fn(&mut H, usize) -> i32,
    batch: impl Fn(&Core, &mut [i32]) -> Result<(usize, usize), RetCode>,
) {
    let what = row.what;

    let drive = |core: &Core| -> Vec<i32> {
        let mut h = open(core).unwrap_or_else(|e| panic!("{what}: open failed: {e:?}"));
        (WARM..N).map(|t| step(&mut h, t)).collect()
    };

    // The perturbed Core: the stream must answer what the batch answers.
    let core = core_with(row.setting, row.factor_scale);
    let streamed = drive(&core);
    let mut out = vec![0_i32; N];
    let (beg, count) =
        batch(&core, &mut out).unwrap_or_else(|e| panic!("{what}: batch failed: {e:?}"));
    assert!(beg <= WARM, "{what}: lookback {beg} reaches past the warm-up");
    assert_eq!(beg + count, N, "{what}: the batch did not fill to the last bar");
    let expected: Vec<i32> = (WARM..N).map(|t| out[t - beg]).collect();
    assert_eq!(
        streamed, expected,
        "{what}: the handle's answers under a perturbed {:?} do not match the batch's — \
         its snapshot dropped that setting or cross-wired it",
        row.setting
    );

    // The control: a snapshot that never carried the setting would answer the
    // same as one opened on the defaults, and the assertion above would hold
    // vacuously.
    let defaults = drive(&Core::new());
    let moved = streamed.iter().zip(&defaults).filter(|(a, b)| a != b).count();
    assert!(
        moved > 0,
        "{what}: scaling {:?} by {} changed no bar of {} — this row proves nothing and \
         needs a multiplier the series can see",
        row.setting,
        row.factor_scale,
        N - WARM
    );
}

/// The candlestick tier's `update` reads its handle's snapshot; every setting
/// any generated step reads is pinned here at least once.
#[test]
fn a_handle_answers_on_the_candle_settings_it_was_opened_with() {
    let (o, h, l, c) = ohlc();

    macro_rules! case {
        ($openf:ident, $batchf:ident, $setting:expr, $scale:expr) => {{
            let row = Row {
                what: concat!(stringify!($batchf), " / ", stringify!($setting)),
                setting: $setting,
                factor_scale: $scale,
            };
            snapshot_carries_the_setting(
                &row,
                |core: &Core| {
                    core.$openf(&o[..WARM], &h[..WARM], &l[..WARM], &c[..WARM])
                        .map(|(handle, _last)| handle)
                },
                |handle, t| {
                    handle
                        .update(o[t], h[t], l[t], c[t])
                        .expect("a finite bar is always accepted")
                },
                |core: &Core, out: &mut [i32]| {
                    core.$batchf(0, N - 1, &o, &h, &l, &c, out)
                        .map(|r| (r.beg_idx, r.count))
                },
            );
        }};
    }

    // One row per setting kind. The multiplier on each is the one measured to
    // move this series; a different series would need different ones.
    case!(CDLDOJI_Open, CDLDOJI, CandleSettingType::BodyDoji, 20.0);
    case!(CDLSPINNINGTOP_Open, CDLSPINNINGTOP, CandleSettingType::BodyShort, 0.01);
    case!(CDLMATCHINGLOW_Open, CDLMATCHINGLOW, CandleSettingType::Equal, 100.0);
    case!(CDLHIKKAKEMOD_Open, CDLHIKKAKEMOD, CandleSettingType::Near, 4.0);
    case!(CDLLONGLEGGEDDOJI_Open, CDLLONGLEGGEDDOJI, CandleSettingType::ShadowLong, 100.0);
    case!(CDLLONGLINE_Open, CDLLONGLINE, CandleSettingType::ShadowShort, 2.0);
    case!(CDLLONGLINE_Open, CDLLONGLINE, CandleSettingType::BodyLong, 2.0);
    case!(CDLHIGHWAVE_Open, CDLHIGHWAVE, CandleSettingType::ShadowVeryLong, 0.01);
    case!(CDLMARUBOZU_Open, CDLMARUBOZU, CandleSettingType::ShadowVeryShort, 20.0);

    // The widest snapshot in the corpus, one setting at a time: five slots that
    // a cross-wire has to keep straight.
    case!(CDLADVANCEBLOCK_Open, CDLADVANCEBLOCK, CandleSettingType::BodyLong, 2.0);
    case!(CDLADVANCEBLOCK_Open, CDLADVANCEBLOCK, CandleSettingType::Far, 0.01);
    case!(CDLADVANCEBLOCK_Open, CDLADVANCEBLOCK, CandleSettingType::Near, 2.0);
    case!(CDLADVANCEBLOCK_Open, CDLADVANCEBLOCK, CandleSettingType::ShadowLong, 0.01);
    case!(CDLADVANCEBLOCK_Open, CDLADVANCEBLOCK, CandleSettingType::ShadowShort, 2.0);
}

/// The other half of the snapshot's contract: it is taken at `Open`, so two
/// handles opened from two different `Core`s stay on their own settings even
/// while stepping the same bars. A snapshot shared, or re-read from somewhere
/// global, would make these two converge.
#[test]
fn two_handles_keep_their_own_settings_while_stepping_together() {
    let (o, h, l, c) = ohlc();
    let strict = Core::new();
    let loose = core_with(CandleSettingType::BodyDoji, 20.0);

    let (mut a, _) = strict
        .CDLDOJI_Open(&o[..WARM], &h[..WARM], &l[..WARM], &c[..WARM])
        .expect("open on the defaults");
    let (mut b, _) = loose
        .CDLDOJI_Open(&o[..WARM], &h[..WARM], &l[..WARM], &c[..WARM])
        .expect("open on the widened BodyDoji");

    let mut disagreements = 0_usize;
    for t in WARM..N {
        let va = a.update(o[t], h[t], l[t], c[t]).expect("finite bar");
        let vb = b.update(o[t], h[t], l[t], c[t]).expect("finite bar");
        if va != vb {
            disagreements += 1;
        }
    }
    assert!(
        disagreements > 0,
        "two handles opened from different BodyDoji settings answered identically on \
         every one of {} bars — the handles are not carrying their own settings",
        N - WARM
    );
}
