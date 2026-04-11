#!/usr/bin/env python3
"""
generate_reloc_stubs.py — Emit extern declarations and zero-valued definitions
for every `ll*` linker symbol referenced by the decomp sources, so that the
port build does not need the decomp's proprietary reloc_data.h/.c pair.

Generates:
    include/reloc_data.h            — `#define <name> ((intptr_t)0)` per symbol

On the original N64 build these are linker symbols pointing at ROM offsets
inside the compressed relocData blob; the port loads file contents through
libultraship's RelocFile resource factory (keyed by file_id), so the scalar
values never matter at runtime. Emitting them as preprocessor constants lets
the decomp use them as compile-time initializers for file-scope arrays, which
an `extern intptr_t` cannot satisfy on C11.

Usage: run from the repo root, `python3 tools/generate_reloc_stubs.py`.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"
HEADER_OUT = ROOT / "include" / "reloc_data.h"

SYMBOL_RE = re.compile(r"\bll[A-Z_][A-Za-z0-9_]*\b")


def collect_symbols() -> list[str]:
    symbols: set[str] = set()
    for path in SRC_DIR.rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        for match in SYMBOL_RE.findall(text):
            symbols.add(match)
    return sorted(symbols)


def write_header(symbols: list[str]) -> None:
    lines = [
        "#ifndef _RELOC_DATA_H_",
        "#define _RELOC_DATA_H_",
        "",
        "/*",
        " * reloc_data.h — AUTO-GENERATED. Run tools/generate_reloc_stubs.py to",
        " * regenerate after adding new decomp sources that reference `ll*`",
        " * linker symbols.",
        " *",
        " * In the original decomp these symbols are produced by a reloc-extractor",
        " * tool that emits a matching C/H pair describing every entry inside the",
        " * compressed relocData blob. Under PORT mode we serve file contents",
        " * through libultraship's RelocFile resource factory (keyed by file_id),",
        " * so the scalar token values never actually matter at runtime — stubbing",
        " * them to zero is enough to satisfy the compiler.",
        " *",
        " * Each symbol is emitted as a `#define` (rather than `extern intptr_t`)",
        " * so it works as a compile-time integer constant inside file-scope",
        " * struct initializers, which C11 does not allow for external variables.",
        " */",
        "",
        "#include <stdint.h>",
        "",
        f"/* {len(symbols)} ll* linker symbols referenced by src/. */",
        "",
    ]
    for sym in symbols:
        lines.append(f"#define {sym} ((intptr_t)0)")
    lines += ["", "#endif /* _RELOC_DATA_H_ */", ""]
    HEADER_OUT.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    symbols = collect_symbols()
    print(f"Found {len(symbols)} ll* symbols in src/")
    write_header(symbols)
    print(f"Wrote {HEADER_OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
