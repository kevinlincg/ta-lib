//! `Clone` duplicates exactly what `Close` disposes — swept over the REAL
//! `ta_codegen/input` corpus.
//!
//! A fork owns its own copy of everything the original owns, so the inventory
//! `Close` walks and the inventory `Clone` duplicates are the same set. Both
//! are built by hand, in two functions that do not consult each other:
//! `release_free_lines` / the per-plan arms of `emit_close` on one side,
//! `clone_buffer_lines` / `clone_owned_lines` on the other. `clone_buffer_lines`
//! states the correspondence as a doc comment and nothing held it.
//!
//! Either direction of a mismatch is a defect, and neither is visible at
//! runtime on the corpus we ship:
//!
//! - **Disposed, not duplicated.** The fork's field still points at the
//!   original's buffer. The two handles then share state — an `Update` on one
//!   moves the other — and the second `Close` frees a pointer the first already
//!   freed. `stream_verify`'s fork leg compares values at the fork bar and both
//!   handles agree there, so it sees nothing; a double free needs an allocator
//!   that notices.
//! - **Duplicated, not disposed.** The copy is a leak on every fork, and the
//!   original leaks the same buffer at `Close`.
//!
//! The sweep reads the emitted C rather than the plan because the plan is what
//! both sides already agree about: they disagree in the transcription, which is
//! text.

use std::collections::{BTreeSet, HashMap};
use std::path::Path;

use ta_codegen_lib::backends::c_stream;
use ta_codegen_lib::helper_registry::HelperRegistry;
use ta_codegen_lib::ir;
use ta_codegen_lib::parser;
use ta_codegen_lib::registry::Registry;

fn input_dir() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../input")
}

/// Every indicator directory in the input tree.
fn indicators() -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(input_dir())
        .expect("input dir")
        .filter_map(Result::ok)
        .filter(|e| e.path().is_dir())
        .filter_map(|e| {
            let name = e.file_name().to_string_lossy().to_string();
            e.path().join(format!("{name}.yaml")).exists().then_some(name)
        })
        .collect();
    v.sort();
    v
}

fn load(name: &str) -> Option<(ir::FuncDef, HashMap<String, ir::EnumDef>)> {
    let base = input_dir();
    let enums = parser::enums::load_enums(&base.join("enums.yaml"));
    let mut func = parser::yaml::parse_yaml(&base.join(format!("{name}/{name}.yaml")));
    if !func.streaming {
        return None;
    }
    let parsed = parser::c_source::parse_c_source(&base.join(format!("{name}/{name}.c")));
    parser::c_source::wire_parsed_source(&mut func, &parsed);
    Some((func, enums))
}

fn stream_c(func: &ir::FuncDef, enums: &HashMap<String, ir::EnumDef>) -> String {
    let registry = Registry::from_dir(&input_dir());
    let helpers = HelperRegistry::from_dir(&input_dir());
    c_stream::generate(func, enums, &registry, &helpers)
}

/// The brace-balanced body of the definition whose signature line contains
/// `needle`, or `None` when the tier emits no such function.
fn body_of(src: &str, needle: &str) -> Option<String> {
    let i = src.find(needle)?;
    let j = src[i..].find('{')? + i;
    let bytes = src.as_bytes();
    let (mut depth, mut k) = (0usize, j);
    loop {
        match bytes[k] {
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(src[j..=k].to_string());
                }
            }
            _ => {}
        }
        k += 1;
        if k >= bytes.len() {
            return None;
        }
    }
}

/// The argument text of every call in `body` whose callee name starts with
/// `TA_` and ends with `suffix`.
fn call_args(body: &str, suffix: &str) -> Vec<String> {
    let b: Vec<char> = body.chars().collect();
    let mut out = Vec::new();
    for i in 0..b.len() {
        if b[i] != '(' {
            continue;
        }
        let mut j = i;
        while j > 0 && (b[j - 1].is_alphanumeric() || b[j - 1] == '_') {
            j -= 1;
        }
        let name: String = b[j..i].iter().collect();
        if !(name.starts_with("TA_") && name.ends_with(suffix)) {
            continue;
        }
        let (mut depth, mut k) = (0i32, i);
        while k < b.len() {
            match b[k] {
                '(' => depth += 1,
                ')' => {
                    depth -= 1;
                    if depth == 0 {
                        out.push(b[i + 1..k].iter().collect::<String>());
                        break;
                    }
                }
                _ => {}
            }
            k += 1;
        }
    }
    out
}

/// The handle field an owning call's argument names, or `None` when the
/// argument is the handle itself.
///
/// A cast, an index and which receiver name the tier happens to use are all
/// noise: every form the emitter writes — `sp->x`, `stream->bank[k]`,
/// `(TA_SMA_Stream *)stream->sub` — names one slot, and the slot is what the
/// two inventories have to agree on.
fn owned_field(arg: &str) -> Option<String> {
    let mut s = arg.trim();
    if s.starts_with('(') {
        s = s[s.find(')')? + 1..].trim();
    }
    let field = s.rsplit("->").next()?;
    if field == s {
        return None; // the handle itself, not one of its fields
    }
    Some(field.split('[').next()?.trim().to_string())
}

/// Every handle field `Close` (with the `ReleaseImpl` it may delegate to)
/// disposes of.
fn disposed(src: &str, upper: &str) -> BTreeSet<String> {
    let mut body = body_of(src, &format!("TA_{upper}_Close( TA_{upper}_Stream *stream )"))
        .unwrap_or_else(|| panic!("{upper} streams but emits no Close"));
    if let Some(rel) = body_of(src, &format!("TA_{upper}_ReleaseImpl( struct TA_{upper}_Stream *sp )")) {
        body.push_str(&rel);
    }
    call_args(&body, "_Free")
        .into_iter()
        .chain(call_args(&body, "_Close"))
        .filter_map(|a| owned_field(&a))
        .collect()
}

/// Every handle field `Clone` disowns, which is every field it duplicates:
/// the disown is emitted once per duplication and only for a duplication.
fn duplicated(src: &str, upper: &str) -> BTreeSet<String> {
    let body = body_of(
        src,
        &format!("TA_{upper}_Clone( const TA_{upper}_Stream *stream, TA_{upper}_Stream **clone )"),
    )
    .unwrap_or_else(|| panic!("{upper} streams but emits no Clone"));
    body.lines()
        .filter_map(|l| {
            let l = l.trim();
            let target = l.strip_suffix("= NULL;")?.trim();
            // `sp->bank[k] = NULL;` zeroes a slot the loop is about to fill;
            // the array itself is the owned field and is disowned on its own
            // line.
            (!target.contains('[')).then(|| owned_field(target))?
        })
        .collect()
}

/// The whole point: the two inventories are one inventory.
#[test]
fn clone_duplicates_every_field_close_disposes() {
    let (mut offenders, mut checked, mut fields) = (Vec::new(), 0usize, 0usize);
    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();

        let closed = disposed(&src, &upper);
        let cloned = duplicated(&src, &upper);
        checked += 1;
        fields += closed.len();

        for f in closed.difference(&cloned) {
            offenders.push(format!("{upper}: Close disposes {f}, Clone shares it with the original"));
        }
        for f in cloned.difference(&closed) {
            offenders.push(format!("{upper}: Clone duplicates {f}, Close never frees it"));
        }
    }
    assert!(offenders.is_empty(), "{} mismatch(es):\n{}", offenders.len(), offenders.join("\n"));
    // Two ratchets, because the equality above is vacuous on empty sets: a
    // derivation that stopped seeing functions, or stopped seeing their fields,
    // agrees with itself perfectly. Corpus today is 201 and 256.
    assert!(checked >= 200, "the sweep saw only {checked} streaming functions");
    assert!(fields >= 250, "the sweep compared only {fields} owned fields over {checked} functions");
}

/// The disown half runs before the first allocation, so a duplication that
/// fails mid-way hands `Close` a copy whose remaining fields are NULL rather
/// than a copy still pointing into the source.
#[test]
fn every_field_is_disowned_before_the_first_duplication() {
    let mut offenders = Vec::new();
    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        let body = body_of(
            &src,
            &format!("TA_{upper}_Clone( const TA_{upper}_Stream *stream, TA_{upper}_Stream **clone )"),
        )
        .unwrap_or_else(|| panic!("{upper} streams but emits no Clone"));

        // The handle's own allocation is the one that precedes the disowning.
        let after = body.find("*sp = *stream;").unwrap_or_else(|| panic!("{upper}: no struct copy"));
        let mut duplicated_yet = false;
        for line in body[after..].lines() {
            let l = line.trim();
            // A disown is a bare field store; `sp->bank[k] = NULL;` zeroes a
            // slot inside a duplication, and `TA_X_Stream *subClone = NULL;`
            // is a local the arm is about to fill.
            let disown = l.starts_with("sp->") && l.ends_with("= NULL;") && !l.contains('[');
            if disown && duplicated_yet {
                offenders.push(format!("{upper}: `{l}` is disowned after a duplication has run"));
            }
            if l.contains("TA_Malloc") || l.contains("_Clone(") {
                duplicated_yet = true;
            }
        }
    }
    assert!(offenders.is_empty(), "{} late disown(s):\n{}", offenders.len(), offenders.join("\n"));
}
