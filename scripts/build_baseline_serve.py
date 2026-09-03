#!/usr/bin/env python3
"""Build bin/ta_baseline_serve for ta_regtest --fuzz-baseline.

Links the frozen released lib (a worktree pinned at serve_version.RELEASE_TAG)
behind the current JSON-RPC transport, shadow-patched (no committed file
changes) for seed-input generation, hash output, and the baseline stamp the
driver checks. Needs the tag (CI: fetch-depth 0).
See src/tools/ta_regtest/CLAUDE.md.

Which release this builds is serve_version.RELEASE_TAG and nothing here.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serve_version

REF_TAG = serve_version.RELEASE_TAG
SERVE = serve_version.BASELINE_SERVE


def find_repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, check=True, cwd=here,
        )
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Fallback: walk up from this file looking for the repo markers.
        d = os.path.dirname(os.path.abspath(__file__))
        while d != os.path.dirname(d):
            if os.path.isdir(os.path.join(d, "ta_codegen")) and \
               os.path.isfile(os.path.join(d, "CMakeLists.txt")):
                return d
            d = os.path.dirname(d)
        sys.exit("build_baseline_serve: cannot locate repo root")


def die(msg):
    sys.exit("build_baseline_serve: " + msg)


def tag_available(root):
    return subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{REF_TAG}"],
        cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0


def worktree_head(ref_root):
    r = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ref_root,
                       capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def ensure_worktree_and_lib(root):
    """Return the path to the frozen libta-lib.a for REF_TAG, building it once.

    The worktree path carries the tag, so rolling REF_TAG cannot land on a
    checkout of the previous one -- it names a directory that does not exist
    yet. The HEAD check below is for the other way in: a worktree somebody
    moved off the tag by hand, which the path alone cannot see."""
    ref_root = serve_version.baseline_worktree(root)
    ref_build = os.path.join(ref_root, "cmake-build")

    if not tag_available(root):
        die(f"tag '{REF_TAG}' unavailable. Fetch tags (git fetch --tags) or use\n"
            f"  actions/checkout with fetch-depth: 0. The baseline oracle cannot\n"
            f"  be built without the released tag.")

    if not os.path.isdir(ref_root):
        print(f"  Creating {REF_TAG} worktree {ref_root}")
        subprocess.run(["git", "worktree", "add", ref_root, REF_TAG],
                       check=True, cwd=root)

    want = subprocess.run(["git", "rev-parse", f"{REF_TAG}^{{commit}}"], cwd=root,
                          capture_output=True, text=True).stdout.strip()
    have = worktree_head(ref_root)
    if want and have and want != have:
        die(f"worktree {ref_root} is at {have[:12]}, not {REF_TAG} ({want[:12]}).\n"
            f"  Remove it (git worktree remove --force {ref_root}) and re-run.")

    lib_a = serve_version.build_frozen_lib(ref_root, ref_build, SERVE)
    if not os.path.exists(lib_a):
        die(f"frozen {REF_TAG} libta-lib.a was not produced")
    return lib_a


def include_dirs(root, bin_dir):
    c_out = os.path.join(root, "ta_codegen", "output", "c")
    c_tools = os.path.join(c_out, "tools")
    return [
        bin_dir,                                        # patched ta_abstract_serve.c
        os.path.join(root, "src", "tools", "ta_regtest"),  # fuzz_data.h (shared)
        c_tools,
        os.path.join(root, "include"),
        os.path.join(c_out, "ta_common"),
        os.path.join(c_out, "ta_abstract"),
        os.path.join(c_out, "ta_abstract", "frames"),
        os.path.join(root, "ta_codegen", "generator", "templates", "c"),
        os.path.join(root, "src", "ta_common"),
        os.path.join(root, "src", "ta_func"),
        os.path.join(root, "src"),
        os.path.join(root, "src", "ta_abstract"),
        os.path.join(root, "src", "ta_abstract", "frames"),
    ]


INPUT_HOOK = r'''
   /* [fuzz] seed-based input generation (baseline differential harness) */
   if( json_find_int(json, "gen_present") ) {
      int fz_shape = json_find_int(json, "gen_shape");
      int fz_seed  = json_find_int(json, "gen_seed");
      int fz_n     = json_find_int(json, "gen_n");
      fuzz_gen(fz_shape, fz_seed, fz_n,
               g_inBuf0, g_inBuf1, g_inBuf2, g_inBuf3, g_inBuf4, g_inBuf5);
      /* Real inputs read buf0,buf1..; match driver (real0=close, real1=volume),
       * incl. mixed real+price funcs (OBV). */
      { unsigned int fz_i; int fz_k, fz_realIdx = 0;
        for( fz_i = 0; fz_i < fi->nbInput; fz_i++ ) {
           const TA_InputParameterInfo *fz_ii;
           TA_GetInputParameterInfo(handle, fz_i, &fz_ii);
           if( fz_ii->type != TA_Input_Real ) continue;
           double *fz_dst = (fz_realIdx == 0) ? g_inBuf0 : (fz_realIdx == 1) ? g_inBuf1
                          : (fz_realIdx == 2) ? g_inBuf2 : g_inBuf3;
           double *fz_src = (fz_realIdx == 1) ? g_inBuf4 : g_inBuf3;
           if( fz_dst != fz_src )
              for( fz_k = 0; fz_k < fz_n; fz_k++ ) fz_dst[fz_k] = fz_src[fz_k];
           fz_realIdx++;
        }
      }
   }
'''

OUTPUT_HOOK = r'''
   /* [fuzz] hash mode: 64-bit digest of raw output unless full_output. */
   if( json_find_int(json, "gen_present") && !json_find_int(json, "full_output") ) {
      unsigned long long fz_h = fuzz_hash_init();
      if( rc == TA_SUCCESS && outNBElement > 0 ) {
         int fz_rIdx = 0, fz_iIdx = 0; unsigned int fz_o;
         for( fz_o = 0; fz_o < fi->nbOutput && fz_o < 3; fz_o++ ) {
            if( outputIsInteger[fz_o] ) {
               int *fz_b = (fz_iIdx == 0) ? g_outIntBuf0 : g_outIntBuf1;
               fz_h = fuzz_hash_bytes(fz_h, fz_b, (unsigned long)outNBElement * sizeof(int));
               fz_iIdx++;
            } else {
               double *fz_b = (fz_rIdx == 0) ? g_outBuf0 : (fz_rIdx == 1) ? g_outBuf1 : g_outBuf2;
               fz_h = fuzz_hash_bytes(fz_h, fz_b, (unsigned long)outNBElement * sizeof(double));
               fz_rIdx++;
            }
         }
      }
      fz_h = fuzz_hash_fin(fz_h);
      pos = json_appendf(resp, resp_size, pos, ",\"out_hash\":\"%016llx\"", fz_h);
      json_appendf(resp, resp_size, pos, "}");
      return;
   }
'''


def build(root, bin_dir, lib_a):
    c_out = os.path.join(root, "ta_codegen", "output", "c")
    serve_src = os.path.join(c_out, "tools", "ta_codegen_serve.c")
    abstract_src = os.path.join(root, "ta_codegen", "generator", "templates", "c", "ta_abstract_serve.c")

    # 1. Main transport: strip generated .c includes, add ref headers + init.
    #    The full_output debug arrays need no patch of their own any more --
    #    see the hex-bits assertion below.
    with open(serve_src) as f:
        src = f.read()
    # VALUES only. ta_func/ and ta_common/ come from the frozen lib_a, but
    # ta_abstract_all.c / ta_func_api.c are NOT stripped (and ta_abstract_serve.c
    # below comes from the current templates/), so this serve's metadata answers
    # are the generator compared against itself. Never build a metadata gate on
    # it. #116 generalized this script over RELEASE_TAG and that did NOT change:
    # the circularity is a property of the machinery, and it follows every
    # release this is ever pointed at (#161).
    src = re.sub(r'#include "ta_func/[^"]*\.c"\n', '', src)
    src = re.sub(r'#include "ta_common/[^"]*\.c"\n', '', src)
    # Stamp the release into list_functions, so the driver can refuse a
    # baseline its tolerance manifest was not measured against.
    src = serve_version.stamp_baseline_tag(src)
    src = src.replace('#include <stdio.h>',
                      '#include <stdio.h>\n#include "ta_func.h"\n'
                      '#include "ta_memory.h"\n#include "ta_utility.h"\n')
    # Functions added since the baseline are absent from the frozen lib: drop
    # them from list_functions (the subset gate skips them) and stub their
    # symbols so the current transport links against it. See serve_version.
    version_root = serve_version.baseline_worktree(root)
    post_funcs = serve_version.post_version_funcs(root, version_root)
    if post_funcs:
        print(f"  post-{REF_TAG} functions (skipped by the subset gate): {', '.join(post_funcs)}")
        src = serve_version.filter_list_functions(src, post_funcs)
        stubs = serve_version.stub_definitions(
            post_funcs, os.path.join(root, "include", "ta_func.h"))
        src = src.replace('int main(void) {', stubs + 'int main(void) {', 1)
    src = src.replace('int main(void) {',
                      'int main(void) { TA_Initialize(); '
                      'TA_RestoreCandleDefaultSettings(TA_AllCandleSettings);')
    # No output-serializer patch any more: json_write_double_array writes the
    # 16-hex-char IEEE-754 bits of every value (issues #257/#258), so the
    # transport this oracle is frozen FROM is already exact. It used to be
    # %.15g here and was shadow-patched to %a, which is the whole reason a
    # freeze taken from the ordinary server silently cost ~1 ULP. Asserted, not
    # assumed: if the writer ever goes back to decimal text, this dies rather
    # than quietly freezing rounded values.
    if '"%016llx", bits' not in src:
        die("output serializer is not the hex-bits writer — a freeze from it would be lossy")
    with open(os.path.join(bin_dir, "_%s.c" % SERVE), "w") as f:
        f.write(src)

    # 2. Shadow-patch ta_abstract_serve.c into bin/ (searched relative to the
    #    includer first, so it wins over the pristine copy on the -I path).
    with open(abstract_src) as f:
        a = f.read()
    a = '#include "fuzz_data.h"\n' + a
    m = re.search(r'int endIdx\s*=\s*json_find_int\(json, "endIdx"\);\n', a)
    if not m:
        die("endIdx anchor not found for input hook")
    a = a[:m.end()] + INPUT_HOOK + a[m.end():]
    anchor = ('"{\\"retCode\\":%d,\\"outBegIdx\\":%d,\\"outNBElement\\":%d,\\"lookback\\":%d",'
              '\n      (int)rc, outBegIdx, outNBElement, (int)lookback);\n')
    idx = a.find(anchor)
    if idx < 0:
        die("response-header anchor not found for output hook")
    a = a[:idx + len(anchor)] + OUTPUT_HOOK + a[idx + len(anchor):]
    with open(os.path.join(bin_dir, "ta_abstract_serve.c"), "w") as f:
        f.write(a)

    # 3. Compile + link against the frozen lib. FP_CONTRACT_FLAG is
    #    load-bearing here, not hygiene: this TU compiles fuzz_data.h, the
    #    seeded INPUT generator, whose own `#pragma STDC FP_CONTRACT OFF` is
    #    honoured by clang but silently ignored by GCC (issue #150). Without the
    #    flag, an FMA-baseline GCC target generates different inputs on this side
    #    than in ta_regtest, and the gate would read that as a library change.
    cmd = ["cc", "-O3", "-flto", "-DNDEBUG", "-DTA_REF_SERVE", "-Wno-everything",
           serve_version.FP_CONTRACT_FLAG, serve_version.MATH_ERRNO_FLAG]
    cmd += [f"-I{d}" for d in include_dirs(root, bin_dir)]
    cmd += ["-o", os.path.join(bin_dir, SERVE),
            os.path.join(bin_dir, "_%s.c" % SERVE), lib_a, "-lm"]
    rc = subprocess.run(cmd).returncode
    for tmp in ("_%s.c" % SERVE, "ta_abstract_serve.c"):
        p = os.path.join(bin_dir, tmp)
        if os.path.exists(p):
            os.unlink(p)
    return rc


def main():
    root = find_repo_root()
    bin_dir = os.path.join(root, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    print(f"=== Building {SERVE} (frozen {REF_TAG} differential oracle) ===")
    lib_a = ensure_worktree_and_lib(root)
    rc = build(root, bin_dir, lib_a)
    print(f"{SERVE}:", "OK" if rc == 0 else f"FAILED (exit {rc})")
    sys.exit(rc)


if __name__ == "__main__":
    main()
