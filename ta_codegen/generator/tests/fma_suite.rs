//! FMA fusion-site consistency guard.
//!
//! Every backend fuses `a*b + c` at the sites the shared `fma::fuse_operands`
//! detector selects (see `backends/fma.rs`, PR #96). Cross-language bit-parity
//! therefore depends on the three independent translations (C, Rust, Java)
//! identifying the *same* sites. The Rust backend threads its own render context
//! into the detector; C and Java rebuild the equivalent name-sets from the same
//! IR body via `build_fma_var_sets`, but — because they emit typed declarations
//! and don't otherwise track variable types — they seed the range/count index
//! vars uniformly with the full set, instead of the per-variant split the Rust
//! context uses (the guarded body seeds only startIdx/endIdx where it delegates;
//! the `_private` body seeds all four).
//!
//! This test proves that seed difference is immaterial: for every function body,
//! `fuse_operands` makes the identical decision at every `a*b+c` site whether the
//! context was built with the 2-seed or the 4-seed index-param set. So C/Java
//! (uniform seeding) fuse exactly the sites Rust does. It also confirms every
//! function in `fma::FUSING_INVENTORY` actually fuses.

use std::cell::Cell;
use std::path::Path;

use ta_codegen_lib::backends::fma;
use ta_codegen_lib::ir::{self, Expr, Statement};
use ta_codegen_lib::parser;
use ta_codegen_lib::streaming;

/// Load a function fully wired the way production does (body, private_body,
/// has_explicit_private, lookback).
fn load_func_full(name: &str) -> ir::FuncDef {
    let base = Path::new(env!("CARGO_MANIFEST_DIR"));
    let yaml_path = base.join(format!("../../ta_codegen/input/{name}/{name}.yaml"));
    let c_path = base.join(format!("../../ta_codegen/input/{name}/{name}.c"));
    let mut func_def = parser::yaml::parse_yaml(&yaml_path);
    let parsed = parser::c_source::parse_c_source(&c_path);
    parser::c_source::wire_parsed_source(&mut func_def, &parsed);
    func_def
}

/// Discover every indicator directory under `ta_codegen/input/` (a subdir that
/// contains `<name>/<name>.c` and `<name>/<name>.yaml`; skips `helpers/`, etc.).
fn all_function_names() -> Vec<String> {
    let base = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../ta_codegen/input");
    let mut names = Vec::new();
    for entry in std::fs::read_dir(&base).expect("input dir") {
        let entry = entry.expect("dir entry");
        if !entry.file_type().map(|t| t.is_dir()).unwrap_or(false) {
            continue;
        }
        let name = entry.file_name().to_string_lossy().to_string();
        let dir = entry.path();
        if dir.join(format!("{name}.c")).is_file() && dir.join(format!("{name}.yaml")).is_file() {
            names.push(name);
        }
    }
    names.sort();
    names
}

/// Walk every `a*b+c` site in `body` and, under two FMA contexts, tally how many
/// fuse under each and how many disagree. Returns `(fused_a, fused_b, disagreements)`.
fn compare_fusion(body: &[Statement], a: &fma::FmaCtx, b: &fma::FmaCtx) -> (usize, usize, usize) {
    let fused_a = Cell::new(0usize);
    let fused_b = Cell::new(0usize);
    let disagree = Cell::new(0usize);
    for stmt in body {
        streaming::walk_stmt_exprs(stmt, &mut |top| {
            streaming::walk_expr(top, &mut |sub| {
                if let Expr::BinOp(l, op, r) = sub {
                    let fa = fma::fuse_operands(l, op, r, a).is_some();
                    let fb = fma::fuse_operands(l, op, r, b).is_some();
                    if fa {
                        fused_a.set(fused_a.get() + 1);
                    }
                    if fb {
                        fused_b.set(fused_b.get() + 1);
                    }
                    if fa != fb {
                        disagree.set(disagree.get() + 1);
                    }
                }
            });
        });
    }
    (fused_a.get(), fused_b.get(), disagree.get())
}

/// Total fused sites in a body under one context (guarded seeds).
fn fused_count(body: &[Statement], ctx: &fma::FmaCtx) -> usize {
    let n = Cell::new(0usize);
    for stmt in body {
        streaming::walk_stmt_exprs(stmt, &mut |top| {
            streaming::walk_expr(top, &mut |sub| {
                if let Expr::BinOp(l, op, r) = sub {
                    if fma::fuse_operands(l, op, r, ctx).is_some() {
                        n.set(n.get() + 1);
                    }
                }
            });
        });
    }
    n.get()
}

/// The 2-seed and 4-seed index-param sets must yield identical fusion decisions
/// at every site of every function body — the property that lets C/Java seed
/// uniformly yet fuse exactly the sites Rust does per variant.
#[test]
fn fma_fusion_is_seed_invariant_across_all_functions() {
    let mut checked = 0usize;
    for name in all_function_names() {
        let f = load_func_full(&name);
        for body in [&f.body, &f.private_body] {
            let g = fma::build_fma_var_sets(body, &f.outputs, &fma::GUARDED_INDEX_SEEDS);
            let u = fma::build_fma_var_sets(body, &f.outputs, &fma::INDEX_PARAM_SEEDS);
            let (fa, fb, disagree) = compare_fusion(body, &g.view(), &u.view());
            assert_eq!(
                disagree, 0,
                "{name}: {disagree} site(s) fuse differently under the 2-seed vs 4-seed \
                 index-param sets (2-seed fused {fa}, 4-seed fused {fb}) — C/Java would \
                 desync from Rust"
            );
        }
        checked += 1;
    }
    assert!(checked >= 150, "expected the whole corpus, only checked {checked}");
}

/// The known fusion-candidate functions must actually fuse at least one site
/// (guards against the detector silently going dark for all of them).
#[test]
fn fma_fusion_fires_for_known_candidates() {
    for name in fma::FUSING_INVENTORY {
        let f = load_func_full(name);
        let u = fma::build_fma_var_sets(&f.private_body, &f.outputs, &fma::INDEX_PARAM_SEEDS);
        let n = fused_count(&f.private_body, &u.view());
        assert!(n > 0, "{name}: expected >=1 fused a*b+c site, found 0");
    }
}

/// Which of a rendered C section's definitions carry `TA_FMA_MULTIVERSION`, by
/// the name in the signature the attribute sits above.
fn multiversioned(section: &str) -> Vec<String> {
    let mut out = Vec::new();
    let lines: Vec<&str> = section.lines().collect();
    for (i, l) in lines.iter().enumerate() {
        if l.trim_end() != "TA_FMA_MULTIVERSION" {
            continue;
        }
        let sig = lines.get(i + 1).copied().unwrap_or("");
        let open = match sig.find('(') {
            Some(o) => o,
            None => continue,
        };
        let name: String = sig[..open]
            .chars()
            .rev()
            .take_while(|c| c.is_alphanumeric() || *c == '_')
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect();
        out.push(name);
    }
    out.sort();
    out
}

/// The streaming tiers of a fusing function carry the FMA multiversion
/// attribute, and the `static` step they inline does not.
///
/// Both halves are load-bearing and they pull in opposite directions.
/// A tier that fuses only through the static step contains no `fma(` of its
/// own, so the batch tier's text predicate cannot see it — that is the bug this
/// pins. And `target_clones` makes a function an ifunc, which gcc cannot inline:
/// marking the static instead would stop it folding into the per-bar entry
/// points, leaving the very site this is meant to fix on a `call fma@PLT`.
/// A non-fusing function must stay unmarked, which is what keeps the predicate
/// from degenerating into "mark everything".
#[test]
fn fma_multiversion_marks_the_stream_tiers_that_fuse_and_not_their_static_step() {
    let input = Path::new(env!("CARGO_MANIFEST_DIR")).join("../input");
    let enums = parser::enums::load_enums(&input.join("enums.yaml"));
    let registry = ta_codegen_lib::registry::Registry::from_dir(&input);
    let helpers = ta_codegen_lib::helper_registry::HelperRegistry::from_dir(&input);

    // EMA fuses (`FUSING_INVENTORY`) and streams as a plain Loop plan, so its
    // step is small enough that gcc folds it into all three per-bar entries.
    let ema = load_func_full("ema");
    let section = ta_codegen_lib::backends::c_stream::generate(&ema, &enums, &registry, &helpers);
    let marked = multiversioned(&section);
    for tier in [
        "TA_EMA_Update",
        "TA_EMA_UpdateAndFill",
        "TA_EMA_Peek",
        "TA_EMA_OpenInternal",
        "TA_EMA_OpenAndFillInternal",
    ] {
        assert!(
            marked.iter().any(|m| m == tier),
            "{tier} fuses through the step it inlines but carries no \
             TA_FMA_MULTIVERSION — it will emit a bare `call fma@PLT` per bar. \
             Marked: {marked:?}"
        );
    }
    for stat in ["TA_EMA_StepImpl", "TA_EMA_OpenImpl"] {
        assert!(
            !marked.iter().any(|m| m == stat),
            "{stat} is static and must NOT be multiversioned: target_clones makes \
             it an ifunc, which gcc will not inline into the per-bar entries"
        );
    }

    // SMA streams the same way and fuses nowhere, so nothing in its section may
    // be marked.
    let sma = load_func_full("sma");
    let sma_section =
        ta_codegen_lib::backends::c_stream::generate(&sma, &enums, &registry, &helpers);
    assert!(
        multiversioned(&sma_section).is_empty(),
        "SMA fuses no site; nothing in its streaming section may be \
         multiversioned, got {:?}",
        multiversioned(&sma_section)
    );
}
