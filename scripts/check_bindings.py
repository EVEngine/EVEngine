#!/usr/bin/env python3
"""Check script API coverage in user guides or the generated SDK catalog.

This automates the manual step recorded in docs/usr/REVIEW.md ("各目录 C++ 文件
中的全部 addFunc 均出现在对应章节 API 快查"). It scans each CMake-declared
module DIR for `addFunc("name", ...)` / `addVar("name", ...)` (skipping nested
satellite dirs) and greps the matching docs/usr/modules/*.md chapter for the
name. Nested packages keep their CMake NAME (e.g. animation_editor), so host
chapters are not charged with editor bindings.

Exit code is non-zero when a bound name is missing from its doc chapter
(CI gate). Doc-only names (removed bindings that the doc still lists) are
reported as warnings so the doc can be cleaned up without blocking.

Strict mode rejects every binding missing from both references; there are no
legacy gap exemptions. --modules limits the check to named CMake modules.

The forward direction also accepts the generated SDK catalog: human guides
explain workflows and binding semantics, while complete signatures come from
generate_binding_contracts.py. A catalog entry counts only for its own source
file, so a similarly named API in another module cannot hide a missing binding,
and unresolved catalog declarations fail --strict. Bindings absent from both
references fail strict mode.

Reverse direction, and what it cost to make it readable. "Documented but not
bound" started at 221 names whose actionable yield was one, because it compared
doc mentions against a partial view of the script surface. Four scans had to be
added before the list was worth reading, taking it 221 -> 114 -> 19 -> 12 -> 10:

  * script-side definitions (SCRIPT_DEF_RES): most of the surface is Squirrel --
    bootstrap chunks in C++ raw strings and src/scripts/*.nut;
  * engine-core chunks (ENGINE_CORE): the ECS bootstrap in common/ECS.cpp defines
    eve.ShaderSystem.bindFields and friends, which module chapters document;
  * nested satellite dirs, for the reverse direction only: a host chapter may
    document its satellite (physics.md documents the cloth API);
  * raw native closures (RAW_CLOSURE_RE): voxel pushes its member names with
    sq_pushstring before sq_newclosure.

What counts as a promise was tightened the same way: a member call inside a
Squirrel fence (DOC_MEMBER_CALL_RE), not every inline `` `name()` `` mention.
Chapters use that inline form for prose about C++-only APIs (ui.md's
screenshotSize() is a PcgPhotoModePhotoAuthority method) and for helpers a sample
defines for itself (editor.md's previewRoad()), and both were noise. Measured
composition of the original 221: 21 bound by another module, 111 prose-only, and
one genuine gap (procgen.newShapeGrammar, a documented factory that was never
bound). The scan is deliberately *not* a gate: what remains is a triage list, and
the drift that actually blocks CI is the forward direction above.

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

import check_module_manifest
import generate_binding_contracts
import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent
MODULES = ROOT / "src" / "modules"
DOCS = ROOT / "docs" / "usr" / "modules"
# Scripts the engine ships to every game; their members are user-facing too.
SHIPPED_SCRIPTS = ROOT / "src" / "scripts"
# The engine core injects script chunks that belong to no module (the ECS bootstrap in
# common/ECS.cpp defines eve.ShaderSystem.bindFields/requestUpload/...), and those are
# documented in module chapters, so the surface scan has to include it.
ENGINE_CORE = ROOT / "src" / "engine"

# CMake module NAME -> user-doc chapter (doc filename without .md).
DOC_ALIASES = {
    "card": "cardgame",
    "data": "data",          # class DataModule
    "tensor": "tensor",      # class TF
    "grid": "grid",
}

ADDFUNC_RE = re.compile(r'\badd(?:Func|Var)\(\s*"([^"]+)"')
# Raw-closure bindings push the member name and then install a native closure:
#     sq_pushstring(vm, "streamAround", -1); sq_newclosure(vm, fn, 0);
# (voxel's setVoxelByName/streamAround, Module.cpp's `eve` delegate `_get`). Requiring the
# file to create closures keeps this from collecting unrelated sq_pushstring uses; measured
# against the repository it matches exactly those three names.
RAW_CLOSURE_RE = re.compile(
    r'sq_pushstring\(\s*vm\s*,\s*(?:_SC\()?"([A-Za-z_][A-Za-z0-9_]*)"\)?\s*,\s*-?\d+\s*\)\s*;'
    r"(?:\s*//[^\n]*)?\s*\n\s*sq_newclosure\(",
)

# Script-side members are part of the same script surface, and the engine defines
# much of it with Squirrel syntax rather than `addFunc`: bootstrap chunks live in
# C++ raw strings (kSceneEntityScript, kUIComponentScript, kEcsScript, ...) and more
# ships in src/scripts/*.nut. The *reverse* check has to see those definitions or it
# reports most of the documented surface as drift -- it listed 221 names, dominated
# by `onMount` / `ui()` / `node()` / `pickRay()` and friends, all of which do exist.
#
# Deliberately asymmetric: script-defined names only *suppress* a "documented but not
# bound" warning. They are not required to be documented themselves, because the
# bootstrap chunks are full of internal helpers (`currentHostName`, `note`, ...) and
# charging the doc chapters with those would drown the forward check.
SCRIPT_DEF_RES = (
    re.compile(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\("),
    re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*<-\s*(?:function|class)\b"),
    re.compile(r'\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\]\s*<-'),
    re.compile(r'\baddClass(?:\s*<[^>]*>)?\(\s*"([^"]+)"'),
)

# Where the *reverse* check looks. A promise about the script surface is a member call
# inside a Squirrel code fence. The chapters use the same `` `name()` `` inline syntax
# for prose mentions of C++-only APIs (ui.md's `screenshotSize()` is a
# PcgPhotoModePhotoAuthority method) and for helpers a sample defines for itself
# (editor.md's `previewRoad()` is the tool script's own function), so counting every
# inline mention produced 221 names whose actionable yield was one. Counting member
# calls in runnable samples keeps the real case this check ever found
# (`procgen.newShapeGrammar`) and leaves a list short enough to read.
SQUIRREL_LANGS = frozenset({"", "nut", "squirrel", "sq"})
# Pair every fence in order and keep the script ones. Scanning for ```nut directly is
# wrong: a ```cpp block's *closing* fence looks like an unlabelled opening, so the prose
# after it was captured as script code (that is how editor.md's prose mention of
# `session.constraints()` was reported as a script promise).
FENCE_RE = re.compile(r"^```([^\n]*)\n(.*?)^```", re.DOTALL | re.MULTILINE)
# Commented-out lines are not promises either: dialogue.md's Inspector sample carries a
# disabled `// viewModel.addEditor(...)`, and animation.md records the C++-only
# `spine.collectDrawItems` in a comment next to the script accessors that replace it.
SQUIRREL_COMMENT_RE = re.compile(r"//[^\n]*")
DOC_MEMBER_CALL_RE = re.compile(r"\b([a-z][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\(")
# Squirrel's own methods are not engine API, however they are spelled in a sample.
SQUIRREL_BUILTINS = frozenset(
    {
        "tostring", "tointeger", "tofloat", "tostring", "len", "type", "constructor",
        "bindenv", "getroottable", "setroottable", "rawget", "rawset", "newslot",
        "setdelegate", "getdelegate", "getclass", "typeof", "weakref", "clone",
        "clear", "append", "extend", "push", "pop", "top", "sort", "reverse", "map",
        "apply", "filter", "reduce", "find", "slice", "print", "assert",
    }
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
        if "sq_newclosure" in text:
            names.update(RAW_CLOSURE_RE.findall(text))
    return names


def module_sources(module_dir: Path, skip_heads: set[str]) -> list[Path]:
    """Module .cpp/.h/.hpp, so Squirrel chunks in headers are seen too."""
    files = list(iter_module_cpp(module_dir, skip_heads))
    for suffix in ("*.h", "*.hpp"):
        for header in module_dir.rglob(suffix):
            if "third-party" in header.as_posix():
                continue
            rel = header.relative_to(module_dir)
            if rel.parts and rel.parts[0] in skip_heads:
                continue
            files.append(header)
    return files


def script_defined_names(module_dir: Path, skip_heads: set[str]) -> set[str]:
    """Names the engine defines in Squirrel: module chunks, core chunks, shipped scripts."""
    names: set[str] = set()
    paths = module_sources(module_dir, skip_heads)
    if SHIPPED_SCRIPTS.is_dir():
        paths.extend(sorted(SHIPPED_SCRIPTS.rglob("*.nut")))
    if ENGINE_CORE.is_dir():
        for suffix in ("*.cpp", "*.h", "*.hpp"):
            paths.extend(sorted(ENGINE_CORE.rglob(suffix)))
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in SCRIPT_DEF_RES:
            names.update(pattern.findall(text))
    return names


def doc_for(module: str) -> Path | None:
    doc = DOC_ALIASES.get(module, module)
    p = DOCS / f"{doc}.md"
    return p if p.is_file() else None


def squirrel_fences(text: str) -> str:
    """Only the live Squirrel code of a doc chapter's runnable samples."""
    parts = [
        SQUIRREL_COMMENT_RE.sub("", match.group(2))
        for match in FENCE_RE.finditer(text)
        if match.group(1).strip().lower() in SQUIRREL_LANGS
    ]
    return "\n".join(parts)


def declared_dirs() -> list[Path]:
    """Every declared module directory, core satellites included.

    The forward check skips core declarations (they have no chapter of their own), but
    the reverse check must know that a name exists *somewhere*: `editor.md` shows
    `actionEditor.hasPreviewHost()`, bound in the core-declared animation editing
    satellite, and reporting that as drift would be wrong.
    """
    out: list[Path] = []
    for declaration in check_module_manifest.parse_manifest():
        if not declaration.dir:
            continue
        path = MODULES.joinpath(*declaration.dir.split("/"))
        if path.is_dir():
            out.append(path)
    return out


def doc_only_names() -> list[tuple[str, str]]:
    """(module, name) pairs the docs promise as script API but nothing defines."""
    surface: set[str] = set()
    directories = declared_dirs()
    roots = [directory for directory in directories
             if not any(parent != directory and parent in directory.parents for parent in directories)]
    for directory in roots:
        surface |= bound_names(directory, set())
    paths = {path for directory in roots for path in module_sources(directory, set())}
    if SHIPPED_SCRIPTS.is_dir():
        paths.update(SHIPPED_SCRIPTS.rglob("*.nut"))
    if ENGINE_CORE.is_dir():
        paths.update(path for path in ENGINE_CORE.rglob("*") if path.suffix in {".cpp", ".h", ".hpp"})
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern in SCRIPT_DEF_RES:
            surface.update(pattern.findall(text))

    found: list[tuple[str, str]] = []
    for module, directory, _ in iter_modules():
        doc = doc_for(module)
        if doc is None:
            continue
        text = doc_text(doc)
        promised = set(ADDFUNC_RE.findall(text))
        for receiver, name in DOC_MEMBER_CALL_RE.findall(squirrel_fences(text)):
            if receiver != "eve" and name not in SQUIRREL_BUILTINS:
                promised.add(name)
        for name in sorted(promised - surface):
            found.append((module, name))
    return found


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
    args = parser.parse_args()

    selected = {m.strip() for m in args.modules.split(",") if m.strip()}

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
                if args.strict:
                    failures.append(f"{module} ({doc.name}): NEW gap, add a guide explanation or generated signature: {name}")
                else:
                    warnings.append(f"{module} ({doc.name}): bound but not documented: {name}")
        else:
            if not args.quiet:
                print(f"ok  {module}: {len(names)} bindings covered by guide or generated API")

    # Doc-only names: the doc promises a script member that nothing defines. See
    # doc_only_names() for what counts as a promise and why the rest does not.
    for module, name in doc_only_names():
        warnings.append(f"{module}: documented but not bound: {name}")

    for w in warnings:
        print(f"WARN {w}")
    for f in failures:
        print(f"FAIL {f}")

    print(f"checked {checked} bindings across {len(modules)} module dirs")
    return 1 if failures else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
