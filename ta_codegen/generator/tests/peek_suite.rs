//! `Peek` commits nothing — swept over the REAL ta_codegen/input corpus.
//!
//! `Peek` runs the transition on a stack copy of the handle, inline — the frame
//! has one caller and always will, since a cross-indicator call enters the
//! callee's PUBLIC `Peek`. The copy carries the scalars and the in-struct
//! arrays; it CANNOT carry the buffers, which are heap pointers it shares with
//! the live handle. What keeps those read-only is the frame itself: each store
//! lives in two locals and each load that could reach the stored slot selects
//! them back.
//!
//! Two properties are load-bearing and neither is visible at runtime:
//!
//! - **No store reaches a buffer.** `stream_verify` peeks bar t and then
//!   updates bar t with the same arguments, so a peek that wrote what the
//!   update was about to write leaves a bit-identical handle and identical
//!   values. That whole class is invisible there and has to be caught here.
//! - **Peek fuses where update fuses.** `expr_is_float_typed` classifies a
//!   `Ternary` by its THEN arm and reads `Var` and `ArrayAccess` through
//!   disjoint name lists, so a select that put the shadow local in the THEN arm
//!   would silently unfuse a multiply-add that update still fuses — a ~1 ULP
//!   divergence in the one comparison ta_regtest declares must be bitwise.

use std::collections::{BTreeSet, HashMap};
use std::path::Path;

use ta_codegen_lib::backends::c_stream;
use ta_codegen_lib::helper_registry::HelperRegistry;
use ta_codegen_lib::ir;
use ta_codegen_lib::parser;
use ta_codegen_lib::registry::Registry;
use ta_codegen_lib::streaming::NameMap;

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
                    break;
                }
            }
            _ => {}
        }
        k += 1;
    }
    Some(src[j..=k].to_string())
}

/// Every buffer the handle owns, read off the SHIPPED struct rather than from a
/// list of name prefixes: any pointer-to-scalar field is a buffer. Deriving it
/// from the struct is the point — a prefix list restates what the transform
/// keys on, so one omission would blind the transform and this gate together.
fn handle_buffers(src: &str, upper: &str) -> BTreeSet<String> {
    let Some(body) = body_of(src, &format!("struct TA_{upper}_Stream {{")) else {
        return BTreeSet::new();
    };
    let mut out = BTreeSet::new();
    for line in body.lines() {
        let l = line.trim().trim_end_matches(';');
        let Some((ty, name)) = l.rsplit_once('*') else { continue };
        let name = name.trim();
        if name.is_empty() || !name.chars().all(|c| c.is_alphanumeric() || c == '_') {
            continue;
        }
        // A sub-stream handle is a pointer too, and peek routes into it by
        // calling its own Peek — it is not a buffer this frame indexes.
        if ty.contains("_Stream") {
            continue;
        }
        out.insert(name.to_string());
    }
    out
}

/// Every `sp-><buffer>[...]` store in `body`, as `(buffer, whole line)`.
fn buffer_stores(body: &str, buffers: &BTreeSet<String>) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for line in body.lines() {
        // `sp->` in a frame, `stream->` in the two tiers that hand-roll Peek —
        // keying on one of them made the sweep of those two vacuous.
        let Some((pos, skip)) = line
            .find("sp->")
            .map(|i| (i, 4))
            .or_else(|| line.find("stream->").map(|i| (i, 8)))
        else {
            continue;
        };
        let rest = &line[pos + skip..];
        let Some(br) = rest.find('[') else { continue };
        let name = &rest[..br];
        if !buffers.contains(name) {
            continue;
        }
        // Balance the index so `] =` is found on the real closing bracket.
        let mut depth = 0usize;
        let mut end = None;
        for (k, c) in rest[br..].char_indices() {
            match c {
                '[' => depth += 1,
                ']' => {
                    depth -= 1;
                    if depth == 0 {
                        end = Some(br + k);
                        break;
                    }
                }
                _ => {}
            }
        }
        let Some(end) = end else { continue };
        let after = rest[end + 1..].trim_start();
        let is_store = after.starts_with('=') && !after.starts_with("==")
            || after.starts_with("+=")
            || after.starts_with("-=");
        if is_store {
            out.push((name.to_string(), line.trim().to_string()));
        }
    }
    out
}

/// The whole point: a peek frame stores into no handle buffer, in any tier.
///
/// Both directions are asserted — the update frames must between them carry the
/// stores this looks for, or an emitter that dropped every buffer would pass.
#[test]
fn a_peek_frame_stores_into_no_handle_buffer() {
    let mut peek_frames = 0usize;
    let mut update_stores = 0usize;
    let mut buffers_seen = 0usize;
    let mut offenders: Vec<String> = Vec::new();

    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        // The public entry too, so the two tiers that hand-roll it and emit no
        // frame of their own — Dispatch (MA) and PeriodBank (MAVP), both pure
        // delegation — are swept rather than silently skipped.
        let buffers = handle_buffers(&src, &upper);
        buffers_seen += buffers.len();
        let mut bodies: Vec<String> = Vec::new();
        if let Some(f) = body_of(&src, &format!("TA_{upper}_PeekImpl(")) {
            bodies.push(f);
        }
        bodies.push(
            body_of(&src, &format!("TA_RetCode TA_{upper}_Peek("))
                .unwrap_or_else(|| panic!("{upper} streams but emits no Peek")),
        );
        peek_frames += 1;
        for body in &bodies {
            for (buf, line) in buffer_stores(body, &buffers) {
                offenders.push(format!("{upper}: {buf} <- {line}"));
            }
        }
        if let Some(step) = body_of(&src, &format!("TA_{upper}_StepImpl(")) {
            update_stores += buffer_stores(&step, &buffers).len();
        }
    }

    assert!(peek_frames > 170, "only {peek_frames} peek entry points swept");
    assert!(
        buffers_seen > 150,
        "only {buffers_seen} handle buffer(s) found across the corpus, so the store scan \
         is looking for something that is not there"
    );
    assert!(
        update_stores > 200,
        "the update frames carry only {update_stores} buffer stores, so the peek sweep \
         proves nothing"
    );
    assert!(
        offenders.is_empty(),
        "a peek frame stores into a buffer the handle shares with the live stream \
         ({} site(s)):\n{}",
        offenders.len(),
        offenders.join("\n")
    );
}

/// A peek frame never drives a sub-stream's `Update`. That call is the composed
/// and dispatch tiers' only way to write the live handle — the sub-handles are
/// heap pointers the struct copy shares — and it carries no subscript, so the
/// store sweep above cannot see it.
#[test]
fn no_peek_entry_point_commits_a_sub_stream() {
    let mut with_subs = 0usize;
    let mut offenders: Vec<String> = Vec::new();
    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        let Some(peek) = body_of(&src, &format!("TA_RetCode TA_{upper}_Peek(")) else { continue };
        if peek.contains("_Peek(") {
            with_subs += 1;
        }
        for line in peek.lines() {
            if line.contains("_Update(") {
                offenders.push(format!("{upper}: {}", line.trim()));
            }
        }
    }
    assert!(
        with_subs >= 12,
        "only {with_subs} peek entry point(s) drive a sub-stream, so this gate is not \
         watching the tiers it exists for"
    );
    assert!(
        offenders.is_empty(),
        "a peek entry point commits a sub-stream:\n{}",
        offenders.join("\n")
    );
}

/// Every exit from a `Peek` answers a code. The transition's own early exit —
/// the param-degenerate identity short-circuit — is valueless, because the
/// transition is `void`; inlined into `Peek` it would be a `return;` in a
/// function returning `TA_RetCode`, which C accepts with a warning and answers
/// with whatever was in the return register. Measured before the fix:
/// `EMA(1) stream Peek: retCode 2`.
#[test]
fn every_return_in_a_peek_answers_a_code() {
    let mut early_exits = 0usize;
    let mut with_short_circuit = 0usize;
    let mut offenders: Vec<String> = Vec::new();
    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        let Some(peek) = body_of(&src, &format!("TA_RetCode TA_{upper}_Peek(")) else { continue };
        // More than the tail return means the body carries an early exit — the
        // identity short-circuit, which is exactly where a valueless one lives.
        if peek.matches("return TA_SUCCESS;").count() > 1 {
            with_short_circuit += 1;
        }
        for line in peek.lines() {
            let l = line.trim();
            if l == "return;" {
                offenders.push(format!("{upper}: {l}"));
            } else if l.starts_with("return ") {
                early_exits += 1;
            }
        }
    }
    assert!(
        early_exits > 150 && with_short_circuit >= 5,
        "{early_exits} answered exit(s) over {with_short_circuit} function(s) with an \
         early exit — too few for this to be checking the shape the defect lived in"
    );
    assert!(
        offenders.is_empty(),
        "a Peek exits without answering a code:\n{}",
        offenders.join("\n")
    );
}


/// Replace every `sp-><buffer>[...]` with one token, so two bodies that differ
/// only in which slot they name compare equal.
fn mask_buffer_reads(body: &str, buffers: &BTreeSet<String>) -> String {
    let b: Vec<char> = body.chars().collect();
    let mut out = String::with_capacity(body.len());
    let mut i = 0usize;
    while i < b.len() {
        if b[i..].starts_with(&['s', 'p', '-', '>']) {
            let rest: String = b[i + 4..].iter().collect();
            if let Some(br) = rest.find('[') {
                let name = &rest[..br];
                if buffers.contains(name) {
                    // Skip to the bracket that closes this subscript.
                    let mut depth = 0usize;
                    let mut k = i + 4 + br;
                    while k < b.len() {
                        match b[k] {
                            '[' => depth += 1,
                            ']' => {
                                depth -= 1;
                                if depth == 0 {
                                    break;
                                }
                            }
                            _ => {}
                        }
                        k += 1;
                    }
                    out.push_str("<BUF>");
                    i = k + 1;
                    continue;
                }
            }
        }
        out.push(b[i]);
        i += 1;
    }
    out
}

/// Collapse `(<idx> != pkSlotN) ? <arm> : pkValN` down to `<arm>` — the inverse
/// of the peek rewrite, so what is left is the expression update renders.
fn undo_selects(body: &str) -> String {
    let mut s = body.to_string();
    while let Some(hit) = s.find("pkSlot") {
        // Back to the `(` that opens the comparison.
        let b: Vec<char> = s.chars().collect();
        let mut depth = 0i32;
        let mut open = None;
        for k in (0..hit).rev() {
            match b[k] {
                ')' => depth += 1,
                '(' => {
                    if depth == 0 {
                        open = Some(k);
                        break;
                    }
                    depth -= 1;
                }
                _ => {}
            }
        }
        let Some(open) = open else { break };
        // Past `) ? ` to the arm, then over the arm to ` : pkValN`.
        let Some(q) = s[hit..].find("? ").map(|k| hit + k + 2) else { break };
        let mut depth = 0i32;
        let mut end = None;
        for (k, c) in s[q..].char_indices() {
            match c {
                '(' | '[' => depth += 1,
                ')' | ']' => {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                }
                ':' if depth == 0 => {
                    end = Some(q + k);
                    break;
                }
                _ => {}
            }
        }
        let Some(end) = end else { break };
        let arm = s[q..end].trim().to_string();
        let Some(tail) = s[end..].find("pkVal").map(|k| end + k) else { break };
        let after = s[tail..]
            .find(|c: char| !c.is_ascii_digit() && c != 'p' && c != 'k' && c != 'V' && c != 'a' && c != 'l')
            .map_or(s.len(), |k| tail + k);
        // The select sits inside the parens the comparison opened.
        let close = if after < s.len() && s.as_bytes()[after] == b')' { after + 1 } else { after };
        s = format!("{}{}{}", &s[..open], arm, &s[close..]);
    }
    s
}

/// Every balanced `fma(...)` call in `body`, in order.
fn fma_calls(body: &str) -> Vec<String> {
    let mut out = Vec::new();
    let b: Vec<char> = body.chars().collect();
    let mut i = 0usize;
    while let Some(rel) = body[i..].find("fma(") {
        let start = i + rel;
        let mut depth = 0usize;
        let mut k = start + 3;
        while k < b.len() {
            match b[k] {
                '(' => depth += 1,
                ')' => {
                    depth -= 1;
                    if depth == 0 {
                        break;
                    }
                }
                _ => {}
            }
            k += 1;
        }
        out.push(body[start..=k.min(b.len() - 1)].split_whitespace().collect::<Vec<_>>().join(" "));
        i = k + 1;
        if i >= body.len() {
            break;
        }
    }
    out
}

/// Peek fuses where update does, in the same order and with the same
/// ARGUMENTS, not merely the same number of them. A count is blind to a product
/// moving across the fusion boundary, which `canonicalize_accumulator_add` can
/// do on its own: it matches the accumulator by raw string equality, and the
/// peek rewrite is what first changes a store's target (`buf[k]` -> `pkValN`).
/// The peek body is compared after undoing the rewrite — buffer subscripts
/// masked to one token, selects collapsed to the arm update would render.
///
/// **A SUBSEQUENCE, not equality.** Two peek-only passes drop whole statements:
/// `peek_tail_trimmed` cuts everything after the last output-sink store, and
/// `drop_dead_temp_stores` then removes the stores nothing left reads. Neither
/// cut is at the end of the fusion list — MAMA's dead `Q2`/`I2` sit between two
/// live ones (10 sites / 3), HT_PHASOR's are its last (8 / 3) — so peek's list
/// is a subsequence of update's, and was a prefix only while the trim was the
/// one pass. Do not restore equality by re-implementing either cut here: a gate
/// duplicating generator logic is worse than this.
///
/// The embedding must be UNIQUE, which is what keeps the weakening from
/// admitting a site matched against the wrong twin: MAMA and HT_PHASOR each fuse
/// the same text twice (their even and odd arms), so a peek that kept one of a
/// pair and dropped the other would embed two ways and fail here rather than
/// read green. What the gate no longer catches on its own is a site vanishing
/// from peek's middle — that is a VALUE divergence, and peek is compared against
/// the committed update bit-for-bit by `ta_regtest`'s peek non-commit leg.
///
/// Measured today: 29 peek bodies fuse, 107 carry a select, 16 carry both, and
/// ZERO fuse over a select operand. So the argument comparison is live against
/// a reordering, and the select-collapse half is armed for the first function
/// that puts a buffer read inside a multiply-add — the case where the THEN-arm
/// classification rule would otherwise bite silently.
#[test]
fn a_peek_frame_fuses_the_same_multiply_adds_as_its_update_frame() {
    let mut total_sites = 0usize;
    let mut fusing_functions = 0usize;
    let mut mismatches: Vec<String> = Vec::new();

    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        let (Some(step), Some(peek)) = (
            body_of(&src, &format!("TA_{upper}_StepImpl(")),
            body_of(&src, &format!("TA_RetCode TA_{upper}_Peek(")),
        ) else {
            continue;
        };
        let buffers = handle_buffers(&src, &upper);
        let a = fma_calls(&mask_buffer_reads(&step, &buffers));
        let b = fma_calls(&undo_selects(&mask_buffer_reads(&peek, &buffers)));
        total_sites += a.len();
        if !a.is_empty() {
            fusing_functions += 1;
        }
        if embeddings(&b, &a) != 1 {
            mismatches.push(format!(
                "{upper}: update fuses {} site(s), peek {} ({} embedding(s))\n    update: {:?}\n    peek:   {:?}",
                a.len(),
                b.len(),
                embeddings(&b, &a),
                a,
                b
            ));
        }
    }

    assert!(
        fusing_functions >= 10 && total_sites >= 20,
        "only {fusing_functions} function(s) / {total_sites} site(s) fuse in the update \
         frame, so this gate is not measuring anything"
    );
    assert!(
        mismatches.is_empty(),
        "a peek frame's multiply-adds are not a uniquely embedded subsequence of its update \
         frame's, which is a silent ~1 ULP divergence in a comparison that must be \
         bitwise:\n{}",
        mismatches.join("\n")
    );
}

/// How many ways `needle` embeds into `haystack` as a subsequence, saturating at
/// 2 — the gate only distinguishes none, one, and ambiguous.
///
/// Counting rather than greedily matching is the whole point: a greedy walk
/// accepts a peek site paired with the wrong one of two identical update sites
/// and reports nothing.
fn embeddings(needle: &[String], haystack: &[String]) -> usize {
    // row[j] = ways to embed needle[..i] into haystack[..j].
    let mut row: Vec<usize> = vec![1; haystack.len() + 1];
    for n in needle {
        let mut next = vec![0usize; haystack.len() + 1];
        for j in 0..haystack.len() {
            next[j + 1] = next[j] + if haystack[j] == *n { row[j] } else { 0 };
            next[j + 1] = next[j + 1].min(2);
        }
        row = next;
    }
    row[haystack.len()]
}

/// The mirror buffers and the composed tier's `peekMode` are gone, and nothing
/// re-grows them: both existed only to let ONE step body serve both frames.
#[test]
fn no_tier_carries_a_peek_mirror_or_a_routing_flag() {
    // A pure-absence sweep reads the same whether it examined 176 functions or
    // none, which is why every test here counts what it looked at.
    let mut swept = 0usize;
    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        swept += 1;
        let src = stream_c(&func, &enums);
        assert!(
            !src.contains("Mirror"),
            "{}: a peek scratch mirror is a per-handle copy of every buffer, which is \
             what the peek frame exists to retire",
            func.name
        );
        assert!(
            !src.contains("peekMode"),
            "{}: routing one step body between frames costs `update` a branch per \
             sub-call — the peek frame is its own body now",
            func.name
        );
    }
    assert!(swept > 170, "only {swept} function(s) examined");
}

/// SMA is the whole mechanism in one function: the degenerate `cap == 0` store
/// is loaded back by the trailing read (`ringPos` is 0 exactly then), so it
/// becomes a shadow and that read becomes a select; the tail push is loaded by
/// nothing, so it is deleted rather than shadowed.
#[test]
fn sma_shadows_the_store_its_own_bar_reads_and_deletes_the_one_it_does_not() {
    let (func, enums) = load("sma").expect("SMA streams");
    let src = stream_c(&func, &enums);
    let peek = body_of(&src, "TA_RetCode TA_SMA_Peek(").expect("SMA has a Peek");
    let step = body_of(&src, "TA_SMA_StepImpl(").expect("SMA has an update frame");

    assert_eq!(
        step.matches("sp->ring_trailingIdx_inReal[").count(),
        3,
        "update reads the trailing slot and stores twice (the cap-0 guard and the \
         tail push): {step}"
    );
    assert!(
        peek.contains("pkSlot0 = 0;") && peek.contains("pkVal0 = inReal;"),
        "the cap-0 guard becomes a shadow: {peek}"
    );
    assert!(
        peek.contains(
            "(sp->ringPos_trailingIdx != pkSlot0) ? sp->ring_trailingIdx_inReal[sp->ringPos_trailingIdx] : pkVal0"
        ),
        "the trailing read selects the shadow, with the array in the THEN arm so the \
         operand classifies as it does in update: {peek}"
    );
    assert!(
        !peek.contains("pkSlot1"),
        "the tail push is loaded by nothing this bar, so it is deleted, not shadowed: {peek}"
    );
    assert_eq!(
        peek.matches("sp->ring_trailingIdx_inReal[").count(),
        1,
        "exactly one load survives, and it is a load: {peek}"
    );
}

/// The handle's fixed-size REAL accumulator fields — the set
/// `transition_buffers_with_state_arrays` offers. `handle_buffers` above cannot
/// see them: it keys on `*`, and these are in the struct.
fn handle_accumulators(src: &str, upper: &str) -> BTreeSet<String> {
    let Some(body) = body_of(src, &format!("struct TA_{upper}_Stream {{")) else {
        return BTreeSet::new();
    };
    let mut out = BTreeSet::new();
    for line in body.lines() {
        let l = line.trim().trim_end_matches(';');
        if l.contains('*') || !l.ends_with(']') || !l.starts_with("double ") {
            continue;
        }
        let Some((decl, size)) = l.rsplit_once('[') else { continue };
        if !size.trim_end_matches(']').chars().all(|c| c.is_ascii_digit()) {
            continue;
        }
        let Some((_, name)) = decl.rsplit_once(' ') else { continue };
        if !name.is_empty() && name.chars().all(|c| c.is_alphanumeric() || c == '_') {
            out.insert(name.to_string());
        }
    }
    out
}

/// A peek frame deletes EVERY accumulator store, so Java and C# have nothing
/// to copy the field for. A function that lands one the frame must keep fails
/// here rather than quietly reintroducing a copy per peek.
#[test]
fn a_peek_frame_deletes_every_accumulator_store() {
    let mut with_accumulators = 0usize;
    let mut step_stores = 0usize;
    let mut unhandled: Vec<String> = Vec::new();
    let mut kept: Vec<String> = Vec::new();

    for name in indicators() {
        let Some((func, enums)) = load(&name) else { continue };
        let src = stream_c(&func, &enums);
        let upper = func.name.to_uppercase();
        let accs = handle_accumulators(&src, &upper);
        if accs.is_empty() {
            continue;
        }
        let Some(peek) = body_of(&src, &format!("TA_RetCode TA_{upper}_Peek(")) else {
            unhandled.push(format!("{upper}: holds an accumulator but emits no Peek"));
            continue;
        };
        // Reported, not skipped: no such handle exists today.
        let Some(step) = body_of(&src, &format!("TA_{upper}_StepImpl(")) else {
            unhandled.push(format!("{upper}: holds an accumulator but emits no StepImpl"));
            continue;
        };
        let stored = buffer_stores(&step, &accs).len();
        if stored == 0 {
            continue; // nothing for the frame to delete
        }
        with_accumulators += 1;
        step_stores += stored;
        let left = buffer_stores(&peek, &accs);
        if !left.is_empty() {
            kept.push(format!("{upper}: {} accumulator store(s)", left.len()));
        }
    }

    assert!(unhandled.is_empty(), "unswept handle(s):\n{}", unhandled.join("\n"));
    assert!(
        with_accumulators >= 21,
        "only {with_accumulators} function(s) store into a fixed-size accumulator, so this \
         sweep is looking for something that is not there"
    );
    assert!(
        step_stores >= 60,
        "the update frames carry only {step_stores} accumulator store(s), so the peek \
         sweep proves nothing"
    );
    assert!(
        kept.is_empty(),
        "a peek frame keeps an accumulator store, so Java and C# copy the field \
         again ({} function(s)):\n{}",
        kept.len(),
        kept.join("\n")
    );
}

/// A `NameMap` for the sweep below. Only the spellings have to be
/// self-consistent — the same map builds the transition and locates its sinks —
/// so this deliberately does not mirror any backend's.
struct SweepNames;

impl ta_codegen_lib::streaming::NameMap for SweepNames {
    fn state(&self, name: &str) -> String {
        format!("sp->{name}")
    }
    fn bar(&self, array: &str) -> String {
        array.to_string()
    }
    fn output(&self, name: &str) -> ir::Expr {
        ir::Expr::PointerDeref(format!("out_{name}"))
    }
    fn ring_buf(&self, var: &str, array: &str) -> String {
        format!("sp->ring_{var}_{array}")
    }
    fn ring_pos(&self, var: &str) -> String {
        format!("sp->ringPos_{var}")
    }
    fn ring_lag(&self, var: &str) -> String {
        format!("sp->ringLag_{var}")
    }
    fn ring_cap(&self, var: &str) -> String {
        format!("sp->ringCap_{var}")
    }
    fn win_buf(&self, var: &str, array: &str) -> String {
        format!("sp->win_{var}_{array}")
    }
    fn win_pos(&self, var: &str) -> String {
        format!("sp->winPos_{var}")
    }
    fn win_cap(&self, var: &str) -> String {
        format!("sp->winCap_{var}")
    }
    fn circ_buf(&self, storage: &str) -> String {
        format!("sp->circ_{storage}")
    }
    fn extrema_buf(&self, array: &str) -> String {
        format!("sp->ext_{array}")
    }
    fn extrema_mask(&self) -> String {
        "sp->extMask".to_string()
    }
}

/// Whether `s`, or anything nested in it, assigns one of `sinks`.
///
/// Re-derived here rather than reached for in the generator: a gate calling the
/// pass's own predicate would agree with it about a sink it fails to recognise.
fn assigns_a_sink(s: &ir::Statement, sinks: &[ir::Expr]) -> bool {
    let nested: Vec<&[ir::Statement]> = match s {
        ir::Statement::While { body, .. }
        | ir::Statement::DoWhile { body, .. }
        | ir::Statement::For { body, .. }
        | ir::Statement::Block { body } => vec![body.as_slice()],
        ir::Statement::ForC { init, update, body, .. } => vec![
            std::slice::from_ref(init.as_ref()),
            std::slice::from_ref(update.as_ref()),
            body.as_slice(),
        ],
        ir::Statement::If { then_body, else_body, .. } => {
            vec![then_body.as_slice(), else_body.as_slice()]
        }
        ir::Statement::Switch { cases, default, .. } => {
            let mut v: Vec<&[ir::Statement]> = cases.iter().map(|(_, b)| b.as_slice()).collect();
            v.push(default.as_slice());
            v
        }
        _ => Vec::new(),
    };
    if let ir::Statement::Assign { target, .. } = s {
        if sinks.contains(target) {
            return true;
        }
    }
    nested.iter().any(|b| b.iter().any(|x| assigns_a_sink(x, sinks)))
}

/// No peek frame computes anything below its last output-sink store (#321).
///
/// `check_no_output_read_back` makes a sink write-only in a transition, so the
/// suffix cannot change what the bar answers and `peek` keeps nothing else. The
/// FLOOR is the load-bearing half: a pass that silently stopped trimming would
/// satisfy the property on nothing and read green without it.
///
/// Swept over the neutral analysis, not over emitted text: the four emitters
/// render this one trimmed transition, and a per-backend epilogue below it (a
/// state write-back into the peek frame's own scratch copy) is not the tail this
/// is about.
#[test]
fn no_peek_frame_computes_below_its_last_output_sink_store() {
    let registry = Registry::from_dir(&input_dir());
    let (mut swept, mut trimmed_frames) = (0usize, 0usize);
    let mut offenders: Vec<String> = Vec::new();

    for name in indicators() {
        let Some((func, _)) = load(&name) else { continue };
        let resolved = func.resolved_for(ir::Lang::C);
        let Ok(plan) = ta_codegen_lib::streaming::validate_streamable(&resolved, &registry) else {
            continue;
        };
        let models = match &plan {
            ta_codegen_lib::streaming::StreamPlan::Loop(m) => vec![m],
            ta_codegen_lib::streaming::StreamPlan::DualMode(dm) => vec![&dm.mode_a, &dm.mode_b],
            ta_codegen_lib::streaming::StreamPlan::Composed(cp) => {
                cp.producer.as_ref().map_or_else(Vec::new, |m| vec![m])
            }
            _ => Vec::new(),
        };
        for model in models {
            let Ok(transition) = ta_codegen_lib::streaming::build_transition(model, &SweepNames)
            else {
                continue;
            };
            let sinks: Vec<ir::Expr> =
                model.outputs.iter().map(|o| SweepNames.output(o)).collect();
            if !transition.iter().any(|s| assigns_a_sink(s, &sinks)) {
                continue;
            }
            swept += 1;
            let trimmed = ta_codegen_lib::streaming::peek_tail_trimmed(
                model,
                &SweepNames,
                &transition,
            );
            if trimmed.len() < transition.len() {
                trimmed_frames += 1;
            }
            let dead = trimmed.len()
                - 1
                - trimmed.iter().rposition(|s| assigns_a_sink(s, &sinks)).unwrap_or(0);
            if dead > 0 {
                offenders.push(format!("{}: {dead} statement(s) below the last sink store", func.name));
            }
        }
    }

    assert!(
        swept >= 150 && trimmed_frames >= 150,
        "swept {swept} frame(s) and trimmed {trimmed_frames}, so this gate is not measuring \
         the pass it exists for"
    );
    assert!(
        offenders.is_empty(),
        "a peek frame still computes a tail no peek can observe:\n{}",
        offenders.join("\n")
    );
}

/// Every name `stmts` READS, by this gate's own reckoning: a plain `x = <expr>`
/// writes `x` and every other mention is a read, a compound assignment's own
/// target included.
///
/// The per-statement enumeration is written out here rather than reached for in
/// the generator, and that is the whole point of the gate: `walk_stmt_exprs`
/// hands back a nested `Assign`'s TARGET as an expression, so a scan built on it
/// counts every store as a read of what it writes and this sweep would agree
/// with any pass at all.
fn names_read(stmts: &[ir::Statement], out: &mut std::collections::BTreeSet<String>) {
    let read = |e: &ir::Expr, out: &mut std::collections::BTreeSet<String>| {
        ta_codegen_lib::streaming::expr_var_names(e, out);
    };
    for s in stmts {
        match s {
            // The one shape whose target is a write and not a read.
            ir::Statement::Assign { target: ir::Expr::Var(_), value, compound: false } => {
                read(value, out);
            }
            ir::Statement::Assign { target, value, .. } => {
                read(target, out);
                read(value, out);
            }
            ir::Statement::VarDecl { init: Some(e), .. }
            | ir::Statement::Return { value: Some(e) }
            | ir::Statement::Expr(e) => read(e, out),
            ir::Statement::While { condition, .. }
            | ir::Statement::DoWhile { condition, .. }
            | ir::Statement::If { condition, .. }
            | ir::Statement::ForC { condition, .. } => read(condition, out),
            ir::Statement::For { var, count, .. } => {
                out.insert(var.clone());
                read(count, out);
            }
            ir::Statement::Switch { expr, .. } => read(expr, out),
            ir::Statement::CircBuf(ir::CircBuf::Init { size, .. }) => read(size, out),
            _ => {}
        }
        let nested: Vec<&[ir::Statement]> = match s {
            ir::Statement::While { body, .. }
            | ir::Statement::DoWhile { body, .. }
            | ir::Statement::For { body, .. }
            | ir::Statement::Block { body } => vec![body.as_slice()],
            ir::Statement::ForC { init, update, body, .. } => vec![
                std::slice::from_ref(init.as_ref()),
                std::slice::from_ref(update.as_ref()),
                body.as_slice(),
            ],
            ir::Statement::If { then_body, else_body, .. } => {
                vec![then_body.as_slice(), else_body.as_slice()]
            }
            ir::Statement::Switch { cases, default, .. } => {
                let mut v: Vec<&[ir::Statement]> = cases.iter().map(|(_, b)| b.as_slice()).collect();
                v.push(default.as_slice());
                v
            }
            _ => Vec::new(),
        };
        for b in nested {
            names_read(b, out);
        }
    }
}

/// No peek frame declares a local it only ever writes (#321 follow-up).
///
/// `peek_tail_trimmed` cuts the frame's tail and `drop_dead_temp_stores` then
/// removes the stores nothing left reads; the two together are what makes this
/// hold. Left standing, such a local is `-Wunused-but-set-variable` in the
/// consumer's C build (`-Wall -Wextra`), CS0219 under the C# package's
/// `TreatWarningsAsErrors`, and wasted arithmetic in all four backends.
///
/// Asked of the DECLARED set — what `temps_used` reports over the frame the four
/// emitters render — because that is the set each backend turns into
/// declarations. It also pins the pass's precondition, that no temp is also a
/// state scalar: each backend appends a state write-back as TEXT below the body,
/// so a name that were both would be read by an epilogue the analysis cannot
/// see, and a peek discards the scratch copy it wrote — no value gate in the
/// tree would report it. The FLOOR on `pruned` is the load-bearing half: a pass that
/// stopped firing would satisfy the property on nothing and read green. Measured
/// today: 174 frames, 349 declared locals, and the store pass fires on 3
/// (CORREL, HT_PHASOR, MAMA).
#[test]
fn no_peek_frame_declares_a_local_it_only_ever_writes() {
    let registry = Registry::from_dir(&input_dir());
    let (mut swept, mut pruned, mut declared_total) = (0usize, 0usize, 0usize);
    let mut offenders: Vec<String> = Vec::new();

    for name in indicators() {
        let Some((func, _)) = load(&name) else { continue };
        let resolved = func.resolved_for(ir::Lang::C);
        let Ok(plan) = ta_codegen_lib::streaming::validate_streamable(&resolved, &registry) else {
            continue;
        };
        let models = match &plan {
            ta_codegen_lib::streaming::StreamPlan::Loop(m) => vec![m],
            ta_codegen_lib::streaming::StreamPlan::DualMode(dm) => vec![&dm.mode_a, &dm.mode_b],
            ta_codegen_lib::streaming::StreamPlan::Composed(cp) => {
                cp.producer.as_ref().map_or_else(Vec::new, |m| vec![m])
            }
            _ => Vec::new(),
        };
        for model in models {
            let Ok(transition) = ta_codegen_lib::streaming::build_transition(model, &SweepNames)
            else {
                continue;
            };
            let trimmed =
                ta_codegen_lib::streaming::peek_tail_trimmed(model, &SweepNames, &transition);
            // Compared as text, not by length: the corpus's dead stores sit
            // INSIDE the even/odd predicate in HT_PHASOR and MAMA, so the
            // top-level count does not move and only CORREL's would be seen.
            let kept = ta_codegen_lib::streaming::drop_dead_temp_stores(model, trimmed.clone());
            if format!("{kept:?}") != format!("{trimmed:?}") {
                pruned += 1;
            }
            let Ok(pt) =
                ta_codegen_lib::streaming::peek_transition_widest(model, &SweepNames, &transition, None)
            else {
                continue;
            };
            swept += 1;
            // The pass's whole soundness argument: a temp is a frame local and
            // nothing outside the frame can read it. Each backend appends a
            // state write-back as TEXT below the body, so a name that were both
            // a temp and a state scalar would be read by an epilogue this
            // analysis cannot see — and a peek discards the scratch copy it
            // wrote, so no value gate anywhere would notice.
            let state: std::collections::BTreeSet<&str> =
                model.state.iter().map(|(n, _)| n.as_str()).collect();
            for (n, _) in &model.temps {
                if state.contains(n.as_str()) {
                    offenders.push(format!("{}: `{n}` is both a temp and a state scalar", func.name));
                }
            }
            let declared = ta_codegen_lib::streaming::temps_used(&model.temps, &pt.body);
            declared_total += declared.len();
            let mut read = std::collections::BTreeSet::new();
            names_read(&pt.body, &mut read);
            for (n, _) in declared {
                if !read.contains(&n) {
                    offenders.push(format!("{}: declares `{n}` and never reads it", func.name));
                }
            }
        }
    }

    assert!(
        swept >= 150 && declared_total >= 300 && pruned >= 3,
        "swept {swept} frame(s) declaring {declared_total} local(s), with the store pass firing \
         on {pruned} — so this gate is not measuring what it exists for"
    );
    assert!(
        offenders.is_empty(),
        "a peek frame declares a local nothing reads, or names one thing twice:\n{}",
        offenders.join("\n")
    );
}
