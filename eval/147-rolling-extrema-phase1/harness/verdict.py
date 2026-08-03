#!/usr/bin/env python3
"""Turn harness/gates/*.log into one verdict line per candidate.

Judged as a DELTA against the C0 control, because the pristine tree already fails
two of these gates on this machine for reasons unrelated to MIN/MAX:
  * `--xlang-hash`: 14 C-vs-Java mismatches on TA_HT_TRENDMODE (the documented
    narrow Java-transcendental tolerance area), 0 on Rust.
  * `--fuzz-064`: `ta_064_serve` (the frozen pre-cutover oracle) is not built here.
So the pass criterion is: generate succeeds, every language server BUILDS, and the
candidate introduces **no mismatch naming a function in the rolling-extrema
family** and no new failure the C0 control did not already have.
"""
import glob
import os
import re
import sys

FAMILY = re.compile(r"\b(MAX|MIN|MINMAX|MIDPOINT|MIDPRICE|WILLR|STOCH|STOCHF)\b")


def read(p):
    try:
        with open(p, errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


CTRL_BROKEN = set()


def verdict(d, cand):
    log = read(f"{d}/{cand}.log")
    rt = read(f"{d}/{cand}.regtest.log")
    xl = read(f"{d}/{cand}.--xlang-hash.log")
    cg = read(f"{d}/{cand}.--codegen.log")
    df = read(f"{d}/{cand}.default.log")
    out = {"cand": cand}

    if "GENERATE FAILED" in log:
        m = re.search(r"^error: (.*)$", log, re.M)
        out["fatal"] = "generate: " + (m.group(1) if m else "?")
        return out

    builds = {}
    for lang in ("C", "Rust", "Java", ".NET"):
        ok = f"Building {lang} server... OK" in rt
        bad = f"Building {lang} server... FAILED" in rt
        builds[lang] = "ok" if ok else ("FAILED" if bad else "-")
    out["builds"] = builds
    newly_broken = {k for k, v in builds.items() if v == "FAILED"} - CTRL_BROKEN
    if newly_broken:
        errs = re.findall(r"^error\[(E\d+)\]: (.*)$", rt, re.M)
        loc = re.findall(r"^\s*--> (\S+:\d+:\d+)$", rt, re.M)
        out["fatal"] = ("server build: "
                        + ", ".join(sorted(newly_broken))
                        + (f" | {errs[0][0]} {errs[0][1]} @ {loc[0]}" if errs and loc else ""))
        return out

    # cross-language bitwise
    fam_x = sorted({FAMILY.search(l).group(1)
                    for l in xl.splitlines() if "MISMATCH" in l and FAMILY.search(l)})
    other_x = sorted({w for l in xl.splitlines() if "MISMATCH" in l
                      for w in re.findall(r"TA_(\w+)", l)} - set(fam_x))
    out["xlang_family"] = fam_x
    out["xlang_other"] = other_x
    m = re.search(r"Rust: \d+ cases, (\d+) mismatch", xl)
    out["xlang_rust"] = m.group(1) if m else "?"
    m = re.search(r"Java: \d+ cases, (\d+) mismatch", xl)
    out["xlang_java"] = m.group(1) if m else "?"

    # stream_verify lives inside --codegen; look for family-named failures
    sv = [l for l in cg.splitlines()
          if re.search(r"\bSV (FAIL|MISMATCH)", l) or ("FAIL" in l and "stream" in l.lower())]
    out["sv_family"] = sorted({FAMILY.search(l).group(1) for l in sv if FAMILY.search(l)})
    out["codegen_fail_family"] = sorted({FAMILY.search(l).group(1) for l in cg.splitlines()
                                        if "FAIL" in l and FAMILY.search(l)})
    out["default_ok"] = "All tests succeeded" in df
    out["variant_parity"] = "bit-identical" in df
    return out


def main(d="gates"):
    cands = sorted({os.path.basename(p).split(".")[0] for p in glob.glob(f"{d}/C*.log")},
                   key=lambda c: int(c[1:]))
    global CTRL_BROKEN
    ctrl = verdict(d, "C0") if os.path.exists(f"{d}/C0.log") else None
    if ctrl and "builds" in ctrl:
        CTRL_BROKEN = {k for k, v in ctrl["builds"].items() if v == "FAILED"}
        ctrl = verdict(d, "C0")   # re-evaluate now that the env gaps are known
    ctrl_other = ctrl.get("xlang_other", []) if ctrl else []
    if ctrl:
        if "fatal" in ctrl:
            print("control C0: NOT USABLE —", ctrl["fatal"])
        print("control C0: builds =", ctrl.get("builds"))
        print("control C0: xlang non-family mismatches =", ctrl_other or "none",
              "| family =", ctrl.get("xlang_family") or "none",
              "| default all-succeeded =", ctrl.get("default_ok"))
        print()
    for c in cands:
        v = verdict(d, c)
        if "fatal" in v:
            print(f"{c:4} ELIMINATED  {v['fatal']}")
            continue
        b = v["builds"]
        fam_ok = (not v["xlang_family"] and not v["sv_family"]
                  and not v["codegen_fail_family"])
        new_other = sorted(set(v["xlang_other"]) - set(ctrl_other))
        ok = fam_ok and not new_other and v["default_ok"] and v["variant_parity"]
        print(f"{c:4} {'PASS      ' if ok else 'FAIL      '} "
              f"builds C/Rust/Java/.NET={b['C']}/{b['Rust']}/{b['Java']}/{b['.NET']} "
              f"xlang(rust={v['xlang_rust']},java={v['xlang_java']}) "
              f"family_mismatch={v['xlang_family'] or 'none'} "
              f"sv_family={v['sv_family'] or 'none'} "
              f"new_other={new_other or 'none'} "
              f"default={'ok' if v['default_ok'] else 'FAIL'} "
              f"variant_parity={'ok' if v['variant_parity'] else 'FAIL'}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "gates")
