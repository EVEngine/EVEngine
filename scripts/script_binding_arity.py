#!/usr/bin/env python3
"""Report script bindings whose C++ declaration has a default argument.

A script binding cannot honour a C++ default argument. `addFunc("addNode", &Scene::addNode)`
binds the *full* parameter list, and Squirrel checks a native closure's arity exactly, so
a script that writes `scene.addNode("id")` gets

    wrong number of parameters

with no hint that the C++ declaration made the second argument optional. The same trap
covers `Vehicle::newVehicle(defId, x, y, heading = 0, faction = "")` and any other method
whose trailing parameters have defaults.

The engine's convention is therefore:

  * a script binding publishes the **full** arity, and script callers pass every argument;
  * a shorter spelling is provided by a script-side wrapper (the wrapper layer already
    exists for the scene script surface), not by the binding.

This scanner keeps that convention falsifiable: it lists the bindings that depend on it,
and `--write-baseline` freezes the set so a new one shows up as a reviewable diff rather
than as a user-visible arity error. Growth is expected to happen deliberately, with the
short form either provided or the default removed from the C++ declaration.

Usage::

    python3 scripts/script_binding_arity.py                 # compare against the baseline
    python3 scripts/script_binding_arity.py --list          # every offender, no comparison
    python3 scripts/script_binding_arity.py --json          # machine-readable
    python3 scripts/script_binding_arity.py --write-baseline
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "scripts" / "script_binding_arity_baseline.json"

# `addFunc("name", &Class::method)` and `addFunc("name", &Class::method, priority)`.
BINDING = re.compile(r'addFunc\(\s*"([^"]+)"\s*,\s*&?([A-Za-z_]\w*(?:::\w+)*)::([A-Za-z_]\w*)\s*[,)]')
# `class Foo {`, and the export-macro spellings the link-group scheme introduced:
# `class EVENGINE_API_WORLD Graphics {`. Leading attribute/macro tokens are skipped; an
# all-caps token cannot swallow a CamelCase class name, because `[_A-Z][_A-Z0-9]*` matches
# only the first letter of `Graphics` and the skipped-token alternative requires whitespace
# after it.
CLASS_OPEN = re.compile(
    r"\b(?:class|struct)\s+(?:(?:\[\[[^\]]*\]\]|[_A-Z][_A-Z0-9]*)\s+)*([A-Za-z_]\w*)\b[^;{]*\{"
)
# A method declaration or definition, capturing the parameter list.
METHOD = re.compile(
    r"\b([A-Za-z_]\w*)\s*\(([^;{)]*)\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?[;{]"
)


@dataclass(frozen=True, order=True)
class Offender:
    """One script binding whose C++ declaration has a default argument."""

    path: str
    script_name: str
    class_name: str
    method: str

    @property
    def key(self) -> str:
        return f"{self.path}|{self.script_name}|{self.class_name}::{self.method}"


def _defaulted_methods(headers: Iterable[Path]) -> set[tuple[str, str]]:
    """{(class, method)} whose declaration has a default argument."""
    found: set[tuple[str, str]] = set()
    for path in headers:
        text = path.read_text(encoding="utf-8", errors="replace")
        # Walk class/brace structure once, recording the innermost class at each offset
        # so a same-named method on another class cannot produce a false positive.
        spans: list[tuple[int, str | None]] = []
        stack: list[str | None] = []
        for match in re.finditer(r"[{}]|" + CLASS_OPEN.pattern, text):
            token = match.group(0)
            if token == "{":
                stack.append(None)
            elif token == "}":
                if stack:
                    stack.pop()
            else:
                if stack:
                    stack[-1] = match.group(1)
                else:
                    stack.append(match.group(1))
            spans.append((match.end(), stack[-1] if stack else None))

        def enclosing(offset: int) -> str | None:
            name: str | None = None
            for end, cls in spans:
                if end > offset:
                    break
                name = cls
            return name

        for match in METHOD.finditer(text):
            if "=" not in match.group(2):
                continue
            cls = enclosing(match.start())
            if cls:
                found.add((cls, match.group(1)))
    return found


def scan(root: Path = REPO) -> list[Offender]:
    """Return every script binding whose C++ declaration has a default argument."""
    defaulted = _defaulted_methods(sorted((root / "src").rglob("*.h")))
    offenders: list[Offender] = []
    seen: set[str] = set()
    for path in sorted((root / "src").rglob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(root).as_posix()
        for match in BINDING.finditer(text):
            script_name, class_name, method = match.group(1), match.group(2), match.group(3)
            if (class_name, method) not in defaulted:
                continue
            offender = Offender(relative, script_name, class_name, method)
            if offender.key in seen:
                continue
            seen.add(offender.key)
            offenders.append(offender)
    return sorted(offenders)


def load_baseline(path: Path = BASELINE) -> list[str]:
    if not path.is_file():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return list(data.get("entries", []))


def write_baseline(entries: list[str], path: Path = BASELINE) -> None:
    # Sorted by key, not by scan order: the file is a review artifact, so its diffs have to
    # be lexicographic and stable.
    path.write_text(
        json.dumps(
            {"schema_version": 1, "entries": sorted(set(entries))}, indent=2, ensure_ascii=False
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )


def compare(entries: list[str], baseline: list[str]) -> tuple[list[str], list[str]]:
    """Return (new, resolved) keys relative to the baseline."""
    known = set(baseline)
    current = set(entries)
    return sorted(current - known), sorted(known - current)


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO, help=argparse.SUPPRESS)
    parser.add_argument("--list", action="store_true", help="print every offender")
    parser.add_argument("--json", action="store_true", help="emit the offenders as JSON")
    parser.add_argument("--write-baseline", action="store_true", help="freeze the current set")
    args = parser.parse_args(list(argv) if argv is not None else None)

    offenders = scan(args.root)
    entries = [offender.key for offender in offenders]

    if args.write_baseline:
        write_baseline(entries, args.root / "scripts" / "script_binding_arity_baseline.json")
        print(f"wrote {len(entries)} entries")
        return 0

    if args.json:
        print(json.dumps({"count": len(entries), "entries": entries}, ensure_ascii=False))
        return 0

    if args.list:
        for offender in offenders:
            print(f"{offender.path}: addFunc(\"{offender.script_name}\", &{offender.class_name}::{offender.method})")
        print(f"total: {len(entries)}")
        return 0

    new, resolved = compare(entries, load_baseline(args.root / "scripts" / "script_binding_arity_baseline.json"))
    if new:
        print("new script bindings on a C++ declaration with a default argument:", file=sys.stderr)
        for key in new:
            print(f"  {key}", file=sys.stderr)
        print(
            "  A script binding cannot honour a C++ default argument: script callers must pass\n"
            "  every argument, or a script-side wrapper must provide the short spelling. Either\n"
            "  drop the default from the C++ declaration, add the wrapper, or record the binding\n"
            "  with `--write-baseline` and say why in the module's `binds` declaration.",
            file=sys.stderr,
        )
    if resolved:
        print(f"{len(resolved)} baseline entries no longer apply; rerun --write-baseline to drop them")
    if not new and not resolved:
        print(f"script binding arity OK: {len(entries)} bindings depend on the pass-every-argument convention")
    return 1 if new else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
