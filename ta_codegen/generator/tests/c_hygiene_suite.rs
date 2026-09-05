//! The committed C is a fixed point of the hygiene pass.
//!
//! Read off DISK rather than re-rendered, so it fails for a C producer that
//! never routed its text through the phase — which is the whole maintenance
//! contract: a new emitter inherits the cleanup, and one that bypasses it says
//! so here instead of shipping a cast that claims a name is unused when the
//! block below reads it.

use std::path::{Path, PathBuf};

use ta_codegen_lib::backends::c_hygiene::scrub_void_casts;

fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// Every `.c` under a directory the generator owns.
fn generated_c() -> Vec<PathBuf> {
    let root = repo_root();
    let mut files = Vec::new();
    for dir in ["src/ta_func", "src/ta_abstract", "ta_codegen/output/c/tools"] {
        let mut stack = vec![root.join(dir)];
        while let Some(d) = stack.pop() {
            let Ok(rd) = std::fs::read_dir(&d) else { continue };
            for e in rd.flatten() {
                let p = e.path();
                if p.is_dir() {
                    stack.push(p);
                } else if p.extension().is_some_and(|x| x == "c") {
                    files.push(p);
                }
            }
        }
    }
    files.sort();
    files
}

/// The standalone `free_batch_storages` line that closes a capture block —
/// one or more guarded frees, nothing else on the line.
fn is_batch_release_line(line: &str) -> bool {
    let t = line.trim();
    if t.is_empty() || !t.starts_with("if( ") {
        return false;
    }
    let mut rest = t;
    let mut n = 0;
    while let Some(i) = rest.find("[0] ) { TA_Free( ") {
        let Some(j) = rest[i..].find("); }") else { return false };
        rest = rest[i + j + 4..].trim_start();
        n += 1;
        if rest.is_empty() {
            return n > 0;
        }
        if !rest.starts_with("if( ") {
            return false;
        }
    }
    false
}

/// A capture block runs with the half-built handle AND the batch's own
/// circular buffers both live: the top-level destroy was withheld so the
/// capture can read them. Releasing only the handle strands the ring, so the
/// `if( !sp )` guard frees both — and so must every failure return after it,
/// down to the block's own release. The `if( !sp )` line is the ground truth
/// for liveness, which is why the sweep keys off it rather than a name list:
/// the paths BEFORE the batch prolog must NOT free (the pointer is still
/// indeterminate there), so an unconditional rule would be wrong.
#[test]
fn a_capture_block_frees_the_live_batch_ring_on_every_failure_return() {
    let mut blocks = 0usize;
    let mut sites = 0usize;
    let mut offenders: Vec<String> = Vec::new();
    for p in &generated_c() {
        let src = std::fs::read_to_string(p).expect("readable");
        let lines: Vec<&str> = src.lines().collect();
        for (i, l) in lines.iter().enumerate() {
            if !(l.contains("if( !sp ) {") && l.contains("!= &local_")) {
                continue;
            }
            blocks += 1;
            let end = lines[i + 1..]
                .iter()
                .position(|x| is_batch_release_line(x))
                .map(|k| i + 1 + k);
            let Some(end) = end else {
                offenders.push(format!(
                    "{}:{}: capture block never releases the batch ring",
                    p.display(),
                    i + 1
                ));
                continue;
            };
            for (k, x) in lines[i + 1..end].iter().enumerate() {
                if !x.contains("_ReleaseImpl( sp ); return") {
                    continue;
                }
                sites += 1;
                if !x.contains("!= &local_") {
                    offenders.push(format!(
                        "{}:{}: {}",
                        p.display(),
                        i + 2 + k,
                        x.trim()
                    ));
                }
            }
        }
    }
    // A corpus with no such block, or a block with no failure return between
    // the handle allocation and the release, would pass saying nothing.
    assert!(
        blocks > 0 && sites > 0,
        "{blocks} capture block(s), {sites} failure return(s) — the sweep has nothing to judge"
    );
    assert!(
        offenders.is_empty(),
        "{} failure return(s) release the handle but strand the still-live batch ring \
         (prepend `free_batch_storages` — `fail_pre` in `c_stream::alloc_and_capture`):\n{}",
        offenders.len(),
        offenders.join("\n")
    );
}

#[test]
fn no_committed_c_file_casts_away_a_name_its_block_reads() {
    let files = generated_c();
    assert!(files.len() >= 200, "only {} generated .c file(s) found", files.len());
    let (mut casts, mut with_casts) = (0usize, 0usize);
    let mut offenders: Vec<String> = Vec::new();
    for p in &files {
        let src = std::fs::read_to_string(p).expect("readable");
        let n = src.matches("(void)").count();
        casts += n;
        with_casts += usize::from(n > 0);
        if scrub_void_casts(&src) != src {
            offenders.push(p.display().to_string());
        }
    }
    // A corpus with no casts left would satisfy the sweep saying nothing.
    assert!(
        casts > 0 && with_casts > 0,
        "{casts} cast(s) across {with_casts} file(s) — the sweep has nothing to judge"
    );
    assert!(
        offenders.is_empty(),
        "{} file(s) still cast away a name their own block reads — route the producer's \
         text through `c_hygiene::scrub_void_casts`:\n{}",
        offenders.len(),
        offenders.join("\n")
    );
}
