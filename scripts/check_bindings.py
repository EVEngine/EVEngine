#!/usr/bin/env python3
"""Binding self-check: every script-facing `addFunc` must appear in the user docs.

This automates the manual step recorded in docs/usr/REVIEW.md ("各目录 C++ 文件
中的全部 addFunc 均出现在对应章节 API 快查"). It scans the module sources for
`addFunc("name", ...)` / `addVar("name", ...)` and greps the matching
docs/usr/modules/*.md chapter for the name.

Exit code is non-zero when a bound name is missing from its doc chapter
(CI gate). Doc-only names (removed bindings that the doc still lists) are
reported as warnings so the doc can be cleaned up without blocking.

Existing chapters document a subset of their bindings (a large pre-existing
debt), so by default missing names are warnings and the exit code stays 0.
Pass --strict to turn missing names into failures; --modules limits the check
to the given module dirs (used by CI to gate newly-authored chapters).

Usage:
    python3 scripts/check_bindings.py                 # report all gaps (exit 0)
    python3 scripts/check_bindings.py --strict        # fail on any gap
    python3 scripts/check_bindings.py --strict --modules avatar,database
    python3 scripts/check_bindings.py --quiet         # only print problems
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULES = ROOT / "src" / "modules"
DOCS = ROOT / "docs" / "usr" / "modules"

# Module dir -> user-doc chapter (doc filename without .md).
DOC_ALIASES = {
    "card": "cardgame",
    "data": "data",          # class DataModule
    "tensor": "tensor",      # class TF
    "grid": "grid",
}

ADDFUNC_RE = re.compile(r'\badd(?:Func|Var)\(\s*"([^"]+)"')


def iter_modules() -> list[tuple[str, Path]]:
    out = []
    for d in sorted(MODULES.iterdir()):
        if not d.is_dir():
            continue
        cpp = [p for p in d.rglob("*.cpp") if "third-party" not in str(p)]
        if cpp:
            out.append((d.name, d))
    return out


def bound_names(module_dir: Path) -> set[str]:
    names: set[str] = set()
    for cpp in module_dir.rglob("*.cpp"):
        text = cpp.read_text(encoding="utf-8", errors="replace")
        names.update(ADDFUNC_RE.findall(text))
    return names


def doc_for(module: str) -> Path | None:
    doc = DOC_ALIASES.get(module, module)
    p = DOCS / f"{doc}.md"
    return p if p.is_file() else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only print failures")
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero when a bound name is missing from its doc")
    parser.add_argument("--modules", default="",
                        help="comma-separated module dirs to check (default: all)")
    args = parser.parse_args()

    selected = {m.strip() for m in args.modules.split(",") if m.strip()}

    failures: list[str] = []
    warnings: list[str] = []
    checked = 0

    for module, d in iter_modules():
        if selected and module not in selected:
            continue
        names = bound_names(d)
        if not names:
            continue
        doc = doc_for(module)
        if doc is None:
            warnings.append(f"{module}: no user doc chapter ({DOCS / module}.md)")
            continue
        text = doc.read_text(encoding="utf-8", errors="replace")
        missing = sorted(n for n in names if n not in text)
        checked += len(names)
        if missing:
            msg = f"{module} ({doc.name}): bound but not documented: {', '.join(missing)}"
            (failures if args.strict else warnings).append(msg)
        else:
            if not args.quiet:
                print(f"ok  {module}: {len(names)} bindings documented")

    # Doc-only names: bound surface shrank but the doc still lists old methods.
    for module, d in iter_modules():
        doc = doc_for(module)
        if doc is None:
            continue
        names = bound_names(d)
        text = doc.read_text(encoding="utf-8", errors="replace")
        doc_names = set(ADDFUNC_RE.findall(text)) | {
            m for m in re.findall(r"`([a-zA-Z][a-zA-Z0-9_]*)\(\)", text)
        }
        for name in sorted(doc_names - names):
            warnings.append(f"{module} ({doc.name}): documented but not bound: {name}")

    for w in warnings:
        print(f"WARN {w}")
    for f in failures:
        print(f"FAIL {f}")

    print(f"checked {checked} bindings across {len(iter_modules())} module dirs")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
