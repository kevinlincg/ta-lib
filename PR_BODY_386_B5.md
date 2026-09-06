test(gate): every installed declaration is exported and defined (#386 B5)

Closes the **B5** entry of #386 — *"Nothing verifies the 1,407 new streaming symbols are exported from the Windows DLL — the exact bug class of #57."*

## The gap

On Windows a symbol leaves the DLL only if two things hold:

1. its declaration carries `__declspec(dllexport)`, which `include/ta_defs.h` spells `TA_LIB_API`, and
2. something actually defines it.

Miss either half and everything else still looks fine: the header compiles, the ELF build links (nothing sets `-fvisibility=hidden`, so every non-static symbol is exported on Linux regardless of the macro), the whole suite passes, and the DLL ships without the function. That is #57 exactly — `TA_GetVersionString` was declared, built, tested and released, and Windows callers could not link it.

The streaming tier added 1,400+ exported symbols emitted from four generator sites, and nothing looked at any of them. That is why this is a scan rather than a list.

## What it does

`scripts/check_exports.py`, wired into `regen-check` and available as `scripts/build.py check-exports`. Pure text — Python only, no build, no Windows.

- **DECLARED** — every function declaration in an installed header carries `TA_LIB_API`.
- **DEFINED** — every `TA_LIB_API` name has a definition under `src/`.

Today's tree: 2,018 declarations in the generated `ta_func.h`, 22 in `ta_abstract.h`, 11 in `ta_common.h`; 2,051 names, all defined. It is green on `dev` as of `7625e259` — a regression guard, not a bug report.

## Keeping the scan honest about its own coverage

A scan like this fails quietly when it stops matching, so two rules sit on top of the two above:

- per header, the number of declarations parsed must equal the number of `TA_LIB_API` tokens present, so a declaration shape the parser walks past cannot pass as "nothing to check";
- the count is floored at a literal minimum per header, so a header that stops matching altogether fails rather than reporting a clean zero.

## Controls

Each was introduced and watched to go red:

| control | result |
|---|---|
| `TA_LIB_API` dropped from the C stream emitter's `Close` declaration, tree regenerated | 201 names reported — #57's class end to end through the generator |
| `TA_LIB_API` dropped from `TA_Initialize` in the hand-written `ta_common.h` | reported |
| a declaration added for a function nothing defines | DEFINED leg red |
| the name pattern narrowed so the parser walks past almost everything | 2,018 tokens vs 9 parsed, red |
| `ta_func.h` floor raised above the real count | red |

## What this does not cover

- **It is not `dumpbin`.** The real proof is the export table of a DLL built by MSVC, which is a Windows nightly job at best. This fails on the same defect one commit earlier, on any machine, but it does **not** prove the linker kept a symbol: an exported name dropped by `/OPT:REF`, or an entry point compiled out behind an `#if`, is invisible to it. If you want the export-table check too, that is a separate, Windows-only nightly step and this does not replace it.
- **I did not build a Windows DLL.** The DEFINED leg was cross-checked once by hand against a `libta-lib.a` built here: all 2,051 header-exported names are present as text or ifunc symbols (the 120 ifuncs are the `target_clones` multiversioning), so the text scan agrees with the linker on today's tree. That cross-check is not part of the gate — it needs a C build, and `regen-check` is cargo + Python only.
- **Autotools does not define `TA_LIB_SHARED`** (only CMake does, `CMakeLists.txt:498`), so `TA_LIB_API` expands to nothing in an autotools build and a MinGW/autotools DLL would export nothing at all. That is out of scope here — the released Windows DLL is the CMake one — but it is a second way the export surface can be empty, and this gate would not notice.

## Cost

One more Python scan on the PR gate; it reads five headers and walks `src/**.c` once, well under a second. The floors are literal, so adding indicators only raises the real count — but a header that legitimately loses declarations needs its floor lowered by hand, deliberately.
