#!/usr/bin/env python3
"""Check script API coverage in user guides or the generated SDK catalog.

Human guides explain workflows and binding semantics; complete signatures are
extracted from source by generate_binding_contracts.py. A catalog entry counts
only for its own source file, so similarly named APIs cannot hide another
module's missing bindings. Unresolved catalog declarations fail --strict.
Bindings absent from both references retain the reviewed gap inventory; new
gaps fail --strict. --write-gaps rewrites that inventory, --modules selects
CMake module names, and --quiet suppresses successful module summaries.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import check_module_manifest
import generate_binding_contracts
import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent
MODULES = ROOT / "src" / "modules"
DOCS = ROOT / "docs" / "usr" / "modules"
GAPS_FILE = Path(__file__).resolve().parent / "check_bindings_gaps.txt"

# CMake module NAME -> user-doc chapter (doc filename without .md).
DOC_ALIASES = {
    "card": "cardgame",
    "data": "data",          # class DataModule
    "tensor": "tensor",      # class TF
    "grid": "grid",
}

ADDFUNC_RE = re.compile(r'\badd(?:Func|Var)\(\s*"([^"]+)"')


def load_gaps() -> set[str]:
    if not GAPS_FILE.is_file():
        return set()
    gaps: set[str] = set()
    for line in GAPS_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            gaps.add(line)
    return gaps


def write_gaps(entries: list[str]) -> None:
    GAPS_FILE.write_text(
        "# Known 'bound but not documented' gaps (module:binding), one per line.\n"
        "# Regenerate with: python3 scripts/check_bindings.py --strict --write-gaps\n"
        "# Remove a line when the doc chapter catches up; new gaps not listed here\n"
        "# fail the CI binding check.\n" + "\n".join(sorted(set(entries))) + "\n",
        encoding="utf-8",
    )


def module_roots() -> list[tuple[str, Path]]:
    """Return (CMake NAME, DIR) for non-core packages, including nested facets."""

    out: list[tuple[str, Path]] = []
    for declaration in check_module_manifest.parse_manifest():
        if declaration.core or not declaration.name:
            continue
        path = MODULES.joinpath(*declaration.dir.split("/"))
        if path.is_dir():
            out.append((declaration.name, path))
    out.sort(key=lambda item: (item[1].as_posix(), item[0]))
    return out


def nested_child_heads(module_dir: Path, roots: list[Path]) -> set[str]:
    """Immediate child names that belong to a nested declared module."""

    heads: set[str] = set()
    root = module_dir.resolve()
    for other in roots:
        if other == module_dir or not other.is_dir():
            continue
        try:
            rel = other.resolve().relative_to(root)
        except ValueError:
            continue
        if rel.parts:
            heads.add(rel.parts[0])
    return heads


def iter_module_cpp(module_dir: Path, skip_heads: set[str]) -> list[Path]:
    files: list[Path] = []
    for cpp in module_dir.rglob("*.cpp"):
        if "third-party" in cpp.as_posix():
            continue
        rel = cpp.relative_to(module_dir)
        if rel.parts and rel.parts[0] in skip_heads:
            continue
        files.append(cpp)
    return files


def iter_modules() -> list[tuple[str, Path, set[str]]]:
    roots = module_roots()
    dirs = [path for _, path in roots]
    out: list[tuple[str, Path, set[str]]] = []
    for name, path in roots:
        skip = nested_child_heads(path, dirs)
        if iter_module_cpp(path, skip):
            out.append((name, path, skip))
    return out


def bound_names(module_dir: Path, skip_heads: set[str]) -> set[str]:
    names: set[str] = set()
    for cpp in iter_module_cpp(module_dir, skip_heads):
        text = cpp.read_text(encoding="utf-8", errors="replace")
        names.update(ADDFUNC_RE.findall(text))
    return names


def doc_for(module: str) -> Path | None:
    doc = DOC_ALIASES.get(module, module)
    p = DOCS / f"{doc}.md"
    return p if p.is_file() else None


def doc_text(doc: Path) -> str:
    """Read a module overview and all topic files in its sibling directory."""

    paths = [doc]
    topic_dir = doc.with_suffix("")
    if topic_dir.is_dir():
        paths.extend(sorted(topic_dir.rglob("*.md")))
    return "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in paths)


def generated_names(files: list[Path], contracts: dict[str, set[str]]) -> set[str]:
    """Only accept catalog entries from this module's own binding sources."""
    return set().union(*(contracts.get(path.relative_to(ROOT).as_posix(), set())
                         for path in files))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only print failures")
    parser.add_argument("--strict", action="store_true",
                        help="fail on unresolved signatures or bindings missing from both references")
    parser.add_argument("--modules", default="",
                        help="comma-separated CMake module names to check (default: all)")
    parser.add_argument("--write-gaps", action="store_true",
                        help="rewrite check_bindings_gaps.txt from the current gaps")
    args = parser.parse_args()

    selected = {m.strip() for m in args.modules.split(",") if m.strip()}
    gaps = load_gaps()

    # Signatures belong in the SDK catalog, not duplicated method-name lists.
    # Reuse its real extractor: unresolved declarations still fail strict CI.
    extracted, unresolved = generate_binding_contracts.extract_contracts(
        generate_binding_contracts.source_files(), None, "")
    contracts: dict[str, set[str]] = {}
    for contract in extracted:
        source = contract.source.rsplit(":", 1)[0]
        contracts.setdefault(source, set()).add(contract.method)

    failures: list[str] = []
    if args.strict:
        failures.extend(f"unresolved generated API: {item}" for item in unresolved)
    warnings: list[str] = []
    gap_entries: list[str] = []
    checked = 0

    modules = iter_modules()
    for module, d, skip in modules:
        if selected and module not in selected:
            continue
        names = bound_names(d, skip)
        if not names:
            continue
        doc = doc_for(module)
        if doc is None:
            warnings.append(f"{module}: no user doc chapter ({DOCS / module}.md)")
            continue
        text = doc_text(doc)
        catalog_names = generated_names(iter_module_cpp(d, skip), contracts)
        missing = sorted(n for n in names if n not in text and n not in catalog_names)
        checked += len(names)
        if missing:
            for name in missing:
                gap_entries.append(f"{module}:{name}")
                if f"{module}:{name}" in gaps:
                    warnings.append(f"{module} ({doc.name}): known gap (document it): {name}")
                elif args.strict:
                    failures.append(f"{module} ({doc.name}): NEW gap, add a guide explanation or generated signature: {name}")
                else:
                    warnings.append(f"{module} ({doc.name}): bound but not documented: {name}")
        else:
            if not args.quiet:
                print(f"ok  {module}: {len(names)} bindings covered by guide or generated API")

    # Doc-only names: bound surface shrank but the doc still lists old methods.
    for module, d, skip in modules:
        doc = doc_for(module)
        if doc is None:
            continue
        names = bound_names(d, skip)
        text = doc_text(doc)
        doc_names = set(ADDFUNC_RE.findall(text)) | {
            m for m in re.findall(r"`([a-zA-Z][a-zA-Z0-9_]*)\(\)", text)
        }
        for name in sorted(doc_names - names):
            warnings.append(f"{module} ({doc.name}): documented but not bound: {name}")

    for w in warnings:
        print(f"WARN {w}")
    for f in failures:
        print(f"FAIL {f}")

    print(f"checked {checked} bindings across {len(modules)} module dirs")
    if args.write_gaps:
        write_gaps(gap_entries)
        print(f"wrote {len(set(gap_entries))} known gaps to {GAPS_FILE}")
    return 1 if failures else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
