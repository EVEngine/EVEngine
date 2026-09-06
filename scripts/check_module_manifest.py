#!/usr/bin/env python3
"""Validate the module manifest as a build/layering contract.

The CMake manifest is intentionally the only module declaration source.  This
check is kept in Python so it can run before a toolchain is installed and so a
bad section marker cannot be hidden by CMake's parser.  A declaration must have
an explicit ``LAYER`` and must sit below a matching ``# L<n>`` section marker.
The check also compares the manifest with ``src/modules`` (including nested
``DIR`` paths such as ``animation/editing``); a new module cannot silently
compile outside the profile resolver.

Usage::

    python3 scripts/check_module_manifest.py
    python3 scripts/check_module_manifest.py --json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "cmake" / "module_manifest.cmake"
MODULES = ROOT / "src" / "modules"

DECLARATION_START = re.compile(r"^\s*eve_declare_module\s*\(")
NAME_RE = re.compile(r"\bNAME\s+([A-Za-z0-9_.-]+)")
DIR_RE = re.compile(r"\bDIR\s+([A-Za-z0-9_./-]+)")
LAYER_RE = re.compile(r"\bLAYER\s+(-?\d+)")
SECTION_RE = re.compile(r"^\s*#\s*((?:L-?\d+\s*(?:/|,)\s*)*L-?\d+)\b")
MANIFEST_INCLUDE_RE = re.compile(
    r"^\s*include\(\$\{CMAKE_CURRENT_LIST_DIR\}/(module_manifest/[^)]+\.cmake)\)\s*$",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Declaration:
    name: str
    dir: str
    layer: int | None
    section_layers: tuple[int, ...]
    line: int
    core: bool
    header_only: bool


SATELLITE_FACETS = (
    "streaming",
    "graphics",
    "physics",
    "procgen",
    "import",
    "replay",
    "scene",
    "thread",
)
SPECIAL_DIRS = {
    "buildingfx": "building/fx",
}
HOST_DIR_ALIASES = {
    "biome": "procgen/biome",
    "domain_gizmo": "editor/gizmo",
    "localization": "i18n",
    "material": "graphics/material",
    "lighting": "graphics/lighting",
    "input": "editor/input",
    "queue": "editor/queue",
    "level": "map/level",
    "sceneloader": "scene/loader",
}


def package_root(stem: str) -> str:
    return HOST_DIR_ALIASES.get(stem, stem)


def default_module_dir(name: str) -> str:
    """Match cmake/modules.cmake DIR inference for nested packages."""

    if name.endswith("_graphics_editing"):
        return f"{package_root(name[: -len('_graphics_editing')])}/graphics_editing"
    if name.endswith("_editing"):
        return f"{package_root(name[: -len('_editing')])}/editing"
    if name.endswith("_editor"):
        return f"{package_root(name[: -len('_editor')])}/editor"
    if name in SPECIAL_DIRS:
        return SPECIAL_DIRS[name]
    for facet in SATELLITE_FACETS:
        suffix = f"_{facet}"
        if name.endswith(suffix):
            return f"{package_root(name[: -len(suffix)])}/{facet}"
    return package_root(name)


def is_authoring_facet(name: str, directory: str | None = None) -> bool:
    """True when a runtime-only profile must drop this module by directory."""

    leaf = Path(directory or default_module_dir(name)).name
    return leaf in {"editing", "editor", "graphics_editing"} or name.endswith("_target")


def _section_layers(line: str) -> tuple[int, ...] | None:
    """Return layer numbers from a heading, or None for an ordinary comment."""

    match = SECTION_RE.match(line)
    if not match:
        return None
    return tuple(int(value) for value in re.findall(r"L(-?\d+)", match.group(1)))


def _balanced_block(lines: list[str], start: int) -> tuple[str, int]:
    """Read one CMake command without interpreting comments or quoted strings."""

    block: list[str] = []
    depth = 0
    quoted = False
    escaped = False
    for index in range(start, len(lines)):
        line = lines[index]
        block.append(line)
        for char in line:
            if escaped:
                escaped = False
                continue
            if char == "\\" and quoted:
                escaped = True
                continue
            if char == '"':
                quoted = not quoted
            elif not quoted:
                if char == "(":
                    depth += 1
                elif char == ")":
                    depth -= 1
        if depth == 0:
            return "".join(block), index
    return "".join(block), len(lines) - 1


def manifest_files(path: Path = MANIFEST) -> list[Path]:
    """Return the entrypoint and its ordered declaration fragments."""

    text = path.read_text(encoding="utf-8")
    return [path, *(path.parent / match for match in MANIFEST_INCLUDE_RE.findall(text))]


def parse_manifest(path: Path = MANIFEST) -> list[Declaration]:
    """Parse declarations and the section marker active at each declaration."""

    declarations: list[Declaration] = []
    for manifest_file in manifest_files(path):
        lines = manifest_file.read_text(encoding="utf-8").splitlines(keepends=True)
        section: tuple[int, ...] = ()
        index = 0
        while index < len(lines):
            marker = _section_layers(lines[index])
            if marker is not None:
                section = marker
            if DECLARATION_START.match(lines[index]):
                block, end = _balanced_block(lines, index)
                name = NAME_RE.search(block)
                layer = LAYER_RE.search(block)
                declared_name = name.group(1) if name else ""
                declared_dir = DIR_RE.search(block)
                declarations.append(
                    Declaration(
                        name=declared_name,
                        dir=(
                            declared_dir.group(1)
                            if declared_dir
                            else default_module_dir(declared_name)
                        ),
                        layer=int(layer.group(1)) if layer else None,
                        section_layers=section,
                        line=index + 1,
                        core=bool(re.search(r"\bCORE\b", block)),
                        header_only=bool(re.search(r"\bHEADER_ONLY\b", block)),
                    )
                )
                index = end + 1
                continue
            index += 1
    return declarations


def source_modules(root: Path = ROOT) -> set[str]:
    """Return top-level directory names under src/modules (containers included)."""

    modules_dir = root / "src" / "modules"
    return {path.name for path in modules_dir.iterdir() if path.is_dir()}


def validate(
    declarations: list[Declaration],
    modules_root: Path = MODULES,
) -> list[str]:
    """Return human-readable errors; an empty list means the contract passes."""

    modules = {path.name for path in modules_root.iterdir() if path.is_dir()}
    errors: list[str] = []
    seen: set[str] = set()
    justified_tops: set[str] = set()
    for declaration in declarations:
        if not declaration.name:
            errors.append(f"line {declaration.line}: declaration has no NAME")
            continue
        if declaration.name in seen:
            errors.append(f"line {declaration.line}: duplicate module '{declaration.name}'")
        seen.add(declaration.name)
        if declaration.layer is None:
            errors.append(f"line {declaration.line}: {declaration.name} has no LAYER")
        elif not declaration.section_layers:
            errors.append(
                f"line {declaration.line}: {declaration.name} is outside an L<n> section"
            )
        elif declaration.layer not in declaration.section_layers:
            expected = "/".join(f"L{value}" for value in declaration.section_layers)
            errors.append(
                f"line {declaration.line}: {declaration.name} declares L{declaration.layer} "
                f"inside {expected} section"
            )
        if not declaration.core:
            justified_tops.add(Path(declaration.dir).parts[0])
            module_path = modules_root / Path(*declaration.dir.split("/"))
            if not module_path.is_dir():
                errors.append(
                    f"line {declaration.line}: {declaration.name} has no "
                    f"src/modules/{declaration.dir}"
                )

    for missing in sorted(modules - justified_tops):
        errors.append(f"src/modules/{missing}: no declaration in cmake/module_manifest.cmake")
    return errors


def report(declarations: list[Declaration], errors: list[str], as_json: bool) -> int:
    if as_json:
        print(
            json.dumps(
                {
                    "declarations": [asdict(item) for item in declarations],
                    "errors": errors,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
    else:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        if not errors:
            print(f"module manifest OK: {len(declarations)} declarations")
    return 1 if errors else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--modules-dir", type=Path, default=MODULES)
    parser.add_argument("--json", action="store_true", help="emit machine-readable output")
    args = parser.parse_args(argv)
    declarations = parse_manifest(args.manifest)
    return report(declarations, validate(declarations, args.modules_dir), args.json)


if __name__ == "__main__":
    sys.exit(main())
