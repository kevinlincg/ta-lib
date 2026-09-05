//! The domain-guard gate: a division by a running sum names that sum.
//!
//! Every case here is driven by MUTATING a shipped body and asserting the gate
//! changes its answer. A gate that only ever runs over a corpus it already
//! passes proves nothing about what it would catch — the two defects that
//! motivated it (#381) both sat in bodies every other gate called green.

mod common;

use ta_codegen_lib::domain_guard;
use ta_codegen_lib::ir::FuncDef;

fn findings(func: &FuncDef) -> Vec<String> {
    match domain_guard::validate_all(std::slice::from_ref(func)) {
        Ok(()) => Vec::new(),
        Err(msgs) => msgs,
    }
}

fn kama_source() -> String {
    let path =
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../ta_codegen/input/kama/kama.c");
    std::fs::read_to_string(path).expect("kama.c is readable")
}

#[test]
fn every_shipped_indicator_gates_its_running_sum_divisions() {
    let names = common::discover_indicators();
    assert!(
        names.len() > 170,
        "only {} indicator(s) discovered — the sweep has nothing to judge",
        names.len()
    );
    let mut reported = Vec::new();
    for name in &names {
        let (func, _) = common::load_indicator(name);
        reported.extend(findings(&func));
    }
    assert!(
        reported.is_empty(),
        "unguarded division(s) by a running sum:\n  {}",
        reported.join("\n  ")
    );
}

/// The control. KAMA's efficiency ratio divided by a sliding sum whose only
/// zero test was a bar counter; on `[0, 1e16, 1e-300, 0, 0, 0, 0, 0]` with
/// period 4 that sum reaches exactly 0.0 three bars before the counter says
/// flat, and `TA_KAMA` answered NaN with `TA_SUCCESS`. Deleting the guard that
/// fixed it must bring the report back.
#[test]
fn deleting_kamas_denominator_test_is_reported() {
    let live = kama_source();
    let mutated = live.replace("sumROC1 <= 0.0 || ", "");
    assert_ne!(mutated, live, "the guard this control deletes has moved");

    let (fixed, _) = common::load_indicator_with_source("kama", &live);
    assert!(
        findings(&fixed).is_empty(),
        "the shipped body must pass: {:?}",
        findings(&fixed)
    );

    let (broken, _) = common::load_indicator_with_source("kama", &mutated);
    let reported = findings(&broken);
    assert!(
        reported.iter().any(|m| m.contains("sumROC1")),
        "deleting the denominator test went unreported: {reported:?}"
    );
}

/// A comparison against the numerator is the shape that shipped. It is a guard,
/// it encloses the division, and it must not count — which is the one thing
/// separating this gate from "is the division inside an `if`".
#[test]
fn a_guard_on_the_numerator_does_not_count_as_one_on_the_divisor() {
    let src = wrap(
        "double sum, roc, out;
         int i;
         sum = 0.0;
         for( i = 0; i < 10; i++ )
            sum += inReal[i];
         roc = inReal[9] - inReal[0];
         if( roc > 0.0 )
            out = roc / sum;
         else
            out = 0.0;
         outReal[0] = out;",
    );
    let (func, _) = common::load_indicator_with_source("sma", &src);
    assert_eq!(findings(&func).len(), 1, "{:?}", findings(&func));

    let guarded = src.replace("if( roc > 0.0 )", "if( sum > 0.0 )");
    let (func, _) = common::load_indicator_with_source("sma", &guarded);
    assert!(findings(&func).is_empty(), "{:?}", findings(&func));
}

/// The sum is usually not the name the division reads: a window sum is copied
/// into a `cur` local first, and the guard sits on the copy. Both hops have to
/// be followed, or the gate silently analyzes nothing in the functions shaped
/// like VORTEX.
#[test]
fn a_copy_of_the_sum_is_still_the_sum() {
    let src = wrap(
        "double sum, cur, out;
         int i;
         sum = 0.0;
         for( i = 0; i < 10; i++ )
            sum += inReal[i];
         cur = sum;
         out = inReal[0] / cur;
         outReal[0] = out;",
    );
    let (func, _) = common::load_indicator_with_source("sma", &src);
    assert_eq!(
        findings(&func).len(),
        1,
        "a division by a copy of the sum is a division by the sum: {:?}",
        findings(&func)
    );

    let guarded = src.replace(
        "cur = sum;",
        "cur = sum;\n         if( cur <= 0.0 ) cur = 1.0;",
    );
    let (func, _) = common::load_indicator_with_source("sma", &guarded);
    assert!(findings(&func).is_empty(), "{:?}", findings(&func));
}

/// A scratch register that is accumulated into on one line and reloaded from an
/// input on the next is not a running sum. Every Hilbert-transform function has
/// one called `tempReal`, and a gate that reported them would be turned off.
#[test]
fn a_reloaded_scratch_register_is_not_a_running_sum() {
    let src = wrap(
        "double acc, out;
         int i;
         acc = 0.0;
         for( i = 0; i < 10; i++ )
            acc += inReal[i];
         acc = inReal[3];
         out = inReal[0] / acc;
         outReal[0] = out;",
    );
    let (func, _) = common::load_indicator_with_source("sma", &src);
    assert!(findings(&func).is_empty(), "{:?}", findings(&func));
}

/// An early-out `if( … ) return;` guards the statements after it exactly as an
/// enclosing `if` guards the ones inside it.
#[test]
fn an_early_return_guards_what_follows_it() {
    let src = wrap(
        "double sum;
         int i;
         sum = 0.0;
         for( i = 0; i < 10; i++ )
            sum += inReal[i];
         if( sum <= 0.0 )
            return TA_SUCCESS;
         outReal[0] = inReal[0] / sum;",
    );
    let (func, _) = common::load_indicator_with_source("sma", &src);
    assert!(findings(&func).is_empty(), "{:?}", findings(&func));

    let without = src.replace("if( sum <= 0.0 )\n            return TA_SUCCESS;", "");
    let (func, _) = common::load_indicator_with_source("sma", &without);
    assert_eq!(findings(&func).len(), 1, "{:?}", findings(&func));
}

/// Borrow SMA's signature so the fixture parses through the production path.
fn wrap(body: &str) -> String {
    format!(
        "int sma_lookback(int optInTimePeriod)
{{
   return optInTimePeriod-1;
}}

TA_RetCode sma(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx,
   int *outNBElement,
   double outReal[])
{{
   {body}
   *outBegIdx = startIdx;
   *outNBElement = 1;
   return TA_SUCCESS;
}}
"
    )
}
