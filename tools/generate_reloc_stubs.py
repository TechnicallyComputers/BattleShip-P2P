#!/usr/bin/env python3
"""
generate_reloc_stubs.py — Emit a macro-only reloc_data.h header for the
SSB64 PC port.

The decomp references ~3900 `ll*` linker symbols across src/. On N64 the
MIPS linker turns each one into an absolute address constant via
`symbols/reloc_data_symbols.us.txt` (vendored here as
tools/reloc_data_symbols.us.txt). On PC we cannot use a MIPS linker, but
the decomp uses many of these symbols as file-scope struct-literal
initialisers — which C11 only accepts if they are **constant
expressions**, not `extern` variables. So we emit a `#define`-only header
with the real numeric values from the symbols table.

Generates:
    include/reloc_data.h  — `#define <name> <value>` per `ll*` symbol

Value sourcing:
    1. tools/reloc_data_symbols.us.txt — real file_id / offset values
       carried over from ssb-decomp-re. Primary source.
    2. Any symbol referenced by src/ that is not listed in the table
       falls back to `((intptr_t)0)`. That keeps the build green when a
       future decomp change adds a new `ll*` symbol; the symbol just
       loads "file 0" at runtime until somebody extends the table.

Usage: run from the repo root.
    python3 tools/generate_reloc_stubs.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"
HEADER_OUT = ROOT / "include" / "reloc_data.h"
SYMBOLS_TXT = ROOT / "tools" / "reloc_data_symbols.us.txt"

SYMBOL_RE = re.compile(r"\bll[A-Z_][A-Za-z0-9_]*\b")
ASSIGN_RE = re.compile(r"^\s*(ll[A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+?)\s*;\s*$")


def collect_symbols() -> list[str]:
    """Scan src/ for every `ll*` identifier referenced in the decomp."""
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


def parse_symbol_values() -> dict[str, str]:
    """Parse tools/reloc_data_symbols.us.txt into {name: value_literal}.

    Blank lines and `#`-comment lines are ignored. Values are kept as
    their original textual form (e.g. `0x73c0`, `2132`) so the generated
    header preserves the same base as upstream — it reads better when
    you grep for a specific file ID.
    """
    if not SYMBOLS_TXT.is_file():
        return {}
    values: dict[str, str] = {}
    for lineno, raw in enumerate(SYMBOLS_TXT.read_text(encoding="utf-8").splitlines(), 1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        m = ASSIGN_RE.match(raw)
        if not m:
            print(
                f"warning: {SYMBOLS_TXT.name}:{lineno}: unrecognised line: {raw!r}",
                file=sys.stderr,
            )
            continue
        name, literal = m.group(1), m.group(2)
        values[name] = literal
    return values


def write_header(symbols: list[str], values: dict[str, str]) -> None:
    resolved = sum(1 for s in symbols if s in values)
    stubbed = len(symbols) - resolved

    lines = [
        "#ifndef _RELOC_DATA_H_",
        "#define _RELOC_DATA_H_",
        "",
        "/*",
        " * reloc_data.h — AUTO-GENERATED. Run tools/generate_reloc_stubs.py to",
        " * regenerate after adding new decomp sources that reference `ll*`",
        " * linker symbols, or after updating tools/reloc_data_symbols.us.txt.",
        " *",
        " * Real values come from tools/reloc_data_symbols.us.txt (vendored from",
        " * ssb-decomp-re); symbols referenced by src/ but missing from the table",
        " * fall back to ((intptr_t)0) so the build stays green. Symbols that fall",
        " * back will hit \"file 0\" at runtime — track them down by grep'ing the",
        " * file for `STUBBED`.",
        " *",
        " * Each symbol is emitted as a `#define` (rather than `extern intptr_t`)",
        " * so it works as a compile-time integer constant inside file-scope",
        " * struct initialisers, which C11 does not allow for external variables.",
        " */",
        "",
        "#include <stdint.h>",
        "",
        f"/* {len(symbols)} ll* symbols referenced by src/: "
        f"{resolved} resolved, {stubbed} stubbed. */",
        "",
    ]

    # Use `((intptr_t)<literal>)` for every entry so downstream tools
    # (tools/generate_yamls.py) see a consistent regex-friendly format.
    for sym in symbols:
        if sym in values:
            lines.append(f"#define {sym} ((intptr_t){values[sym]})")
        else:
            lines.append(f"#define {sym} ((intptr_t)0) /* STUBBED */")

    lines += ["", "#endif /* _RELOC_DATA_H_ */", ""]
    HEADER_OUT.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    symbols = collect_symbols()
    values = parse_symbol_values()

    resolved = sum(1 for s in symbols if s in values)
    stubbed = len(symbols) - resolved

    print(f"Found {len(symbols)} ll* symbols in src/")
    print(f"Loaded {len(values)} values from {SYMBOLS_TXT.relative_to(ROOT)}")
    print(f"  resolved: {resolved}")
    print(f"  stubbed:  {stubbed}")

    write_header(symbols, values)
    print(f"Wrote {HEADER_OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
