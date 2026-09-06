#!/usr/bin/env python3
"""Every public declaration is reachable from the Windows DLL.

On Windows a symbol leaves the DLL only if its declaration carries
`__declspec(dllexport)` -- which `include/ta_defs.h` spells `TA_LIB_API` -- and
only if something actually defines it. Miss either half and the header still
compiles, the ELF build still links (nothing sets `-fvisibility=hidden`, so
every non-static symbol is exported there), the whole test suite still passes,
and the Windows DLL ships without the function. That is issue #57 exactly:
`TA_GetVersionString` was declared, built, tested and released, and Windows
callers could not link it.

The streaming tier multiplied the exposure -- it is 1,400+ new exported symbols
emitted by four generator sites -- and nothing looked at any of them, which is
why this is a scan and not a list. Both halves are checked:

  DECLARED  every function declaration in a public header carries TA_LIB_API.
  DEFINED   every TA_LIB_API name has a definition under src/.

Why text and not `dumpbin`. The real proof is the export table of a DLL built by
MSVC, and that is a Windows job on the nightly at best. This runs on the PR gate
on any machine with Python, and it fails on the same defect one commit earlier.
It does NOT prove the linker kept a symbol -- an exported name dropped by
`/OPT:REF`, or an entry point compiled out by an `#if`, is invisible here.

Two things keep the scan honest about its own coverage. Per header, the number
of declarations parsed must equal the number of TA_LIB_API tokens present, so a
declaration shape the parser walks past cannot pass as "nothing to check"; and
the count is floored at a LITERAL minimum, so a header that stops matching
altogether fails rather than reporting a clean zero.
"""

import os
import re
import sys

# Every installed header, and the minimum number of exported declarations each
# must still yield. ta_func.h is generated (one entry per function per tier);
# the rest are hand-written. The two that declare no functions are listed at a
# floor of 0 rather than left out: the floor says nothing there, but the
# DECLARED rule still covers a declaration that lands in one of them.
HEADERS = [
    (os.path.join("include", "ta_func.h"), 1900),
    (os.path.join("include", "ta_abstract.h"), 20),
    (os.path.join("include", "ta_common.h"), 8),
    (os.path.join("include", "ta_defs.h"), 0),
    (os.path.join("include", "ta_libc.h"), 0),
]

EXPORT_MACRO = "TA_LIB_API"

# A definition, as it appears at the top of a line in src/: a return type, the
# name, the parameter list, then the brace. Anchored at column 0 because every
# definition in the tree is, and an indented match would be a nested construct.
DEFINITION_RE = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b(TA_[A-Za-z0-9_]+)\s*\([^;{]*?\)\s*\{",
    re.M | re.S,
)


def _strip_noise(src: str) -> str:
    """Comments and preprocessor lines out, line structure preserved.

    Continued macro lines go too: a multi-line `#define` body would otherwise
    read as a statement, and the ones in ta_defs.h contain parentheses.
    """
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    # The C++ linkage block wraps whole headers. Its brace is not scope, and
    # leaving it in would put every declaration at depth 1. The matching close
    # is a bare `}`; depth clamps at 0, so dropping only the open is enough.
    src = re.sub(r'extern\s+"C"\s*\{', " ", src)
    out, in_macro = [], False
    for line in src.split("\n"):
        stripped = line.strip()
        if in_macro or stripped.startswith("#"):
            in_macro = stripped.endswith("\\")
            out.append("")
            continue
        out.append(line)
    return "\n".join(out)


def _top_level_statements(src: str):
    """Yield each `;`-terminated statement written at brace depth 0.

    Depth matters: a `TA_RetCode (*fn)(...)` member inside a struct is not a
    declaration of anything, and neither is a parameter that happens to be a
    function pointer.
    """
    depth, buf = 0, []
    for ch in src:
        # Braces are never part of the statement text. A depth-0 `}` -- the close
        # of a struct body, or of the C++ linkage block whose open was stripped
        # -- would otherwise lead the NEXT statement and stop it parsing.
        if ch == "{":
            depth += 1
            continue
        if ch == "}":
            depth = max(0, depth - 1)
            continue
        if depth == 0:
            if ch == ";":
                yield "".join(buf)
                buf = []
                continue
            buf.append(ch)
    if "".join(buf).strip():
        yield "".join(buf)


def _declared_name(statement: str):
    """The function this statement declares, or None.

    A declaration is `<type> TA_Name ( ... )` with the name at paren depth 0.
    Excluded: typedefs, and function-POINTER declarations (`(*name)(...)`),
    which export nothing on their own.
    """
    text = " ".join(statement.split())
    if not text or text.startswith("typedef"):
        return None
    if re.search(r"\(\s*\*", text):
        return None
    m = re.match(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b(TA_[A-Za-z0-9_]+)\s*\(", text)
    return m.group(1) if m else None


def scan_headers(root: str):
    """(ok, {name: header}) -- every declaration, and whether all were exported."""
    ok, exported = True, {}
    for rel, floor in HEADERS:
        path = os.path.join(root, rel)
        with open(path, encoding="utf-8") as f:
            src = _strip_noise(f.read())
        found, missing = 0, []
        for statement in _top_level_statements(src):
            name = _declared_name(statement)
            if name is None:
                continue
            if EXPORT_MACRO in statement:
                found += 1
                exported[name] = rel
            else:
                missing.append(name)
        if missing:
            print(f"Error: {rel}: {len(missing)} declaration(s) without "
                  f"{EXPORT_MACRO} -- absent from the Windows DLL (#57):")
            for name in missing[:12]:
                print(f"         {name}")
            if len(missing) > 12:
                print(f"         ... and {len(missing) - 12} more")
            ok = False
            continue
        # Every TA_LIB_API token left after the preprocessor lines are stripped
        # belongs to exactly one declaration, so the two counts have to agree.
        # Without this, a declaration shape the parser walks past is simply not
        # checked -- and the DECLARED rule only ever speaks about what it found.
        tokens = len(re.findall(r"\b" + EXPORT_MACRO + r"\b", src))
        if tokens != found:
            print(f"Error: {rel}: {tokens} {EXPORT_MACRO} token(s) but {found} "
                  f"declaration(s) parsed. The parser is walking past a declaration "
                  f"shape, and what it walks past it does not check.")
            ok = False
            continue
        if found < floor:
            print(f"Error: {rel}: {found} exported declaration(s), floor is {floor}. "
                  f"The scan is matching less than it was written to match.")
            ok = False
            continue
        print(f"  {rel}: {found} exported declaration(s). OK.")
    return ok, exported


def scan_definitions(root: str, exported: dict) -> bool:
    """Every exported name is defined somewhere under src/."""
    defined = set()
    src_dir = os.path.join(root, "src")
    for dirpath, _dirnames, filenames in os.walk(src_dir):
        for filename in filenames:
            if not filename.endswith(".c"):
                continue
            with open(os.path.join(dirpath, filename), encoding="utf-8") as f:
                body = _strip_noise(f.read())
            defined.update(m.group(1) for m in DEFINITION_RE.finditer(body))
    undefined = sorted(name for name in exported if name not in defined)
    if undefined:
        print(f"Error: {len(undefined)} exported declaration(s) have no definition "
              f"under src/ -- the header promises a symbol the DLL does not hold:")
        for name in undefined[:12]:
            print(f"         {name}  ({exported[name]})")
        if len(undefined) > 12:
            print(f"         ... and {len(undefined) - 12} more")
        return False
    print(f"  {len(exported)} exported name(s), all defined under src/. OK.")
    return True


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    print("TA_LIB_API on every installed declaration:")
    ok, exported = scan_headers(root)
    ok = scan_definitions(root, exported) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
