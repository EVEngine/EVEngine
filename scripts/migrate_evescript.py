#!/usr/bin/env python3
"""Migrate repository game scripts to EveScript v1 declarations.

The migration is deliberately syntax-aware enough to preserve nested table,
array, call, string and comment contents.  It only rewrites root-scope forms
whose EveScript lowering is exactly equivalent:

    if (!("score" in getroottable())) score <- 0;
    score <- persist("score", function() { return 0; });

Both become:

    persist score = 0

Run without --write to audit; --check fails when migratable legacy forms remain.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


EXCLUDED_DIRS = {".git", "build", "third-party"}


@dataclass(frozen=True)
class Edit:
    start: int
    end: int
    replacement: str
    kind: str
    name: str


def _masked(source: str) -> str:
    """Return source with comments/strings blanked while preserving newlines."""
    out = list(source)
    quote: str | None = None
    line_comment = False
    block_comment = False
    i = 0
    while i < len(out):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
            else:
                out[i] = " "
        elif block_comment:
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 1
                block_comment = False
            elif ch != "\n":
                out[i] = " "
        elif quote is not None:
            if ch == "\\":
                out[i] = " "
                if i + 1 < len(out):
                    if source[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 1
            elif ch == quote:
                out[i] = " "
                quote = None
            elif ch != "\n":
                out[i] = " "
        elif ch in {'"', "'"}:
            out[i] = " "
            quote = ch
        elif ch == "/" and nxt == "/":
            out[i] = out[i + 1] = " "
            i += 1
            line_comment = True
        elif ch == "/" and nxt == "*":
            out[i] = out[i + 1] = " "
            i += 1
            block_comment = True
        i += 1
    return "".join(out)


def _brace_depth(masked: str) -> list[int]:
    depth = 0
    result = [0] * (len(masked) + 1)
    for i, ch in enumerate(masked):
        result[i] = depth
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth = max(0, depth - 1)
    result[len(masked)] = depth
    return result


def _statement_end(masked: str, start: int) -> int | None:
    paren = bracket = brace = 0
    for i in range(start, len(masked)):
        ch = masked[i]
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren -= 1
        elif ch == "[":
            bracket += 1
        elif ch == "]":
            bracket -= 1
        elif ch == "{":
            brace += 1
        elif ch == "}":
            brace -= 1
        elif ch == ";" and paren == bracket == brace == 0:
            return i
    return None


_GUARD = re.compile(
    r"(?m)^(?P<indent>[ \t]*)if\s*\(\s*!\s*\(\s*"
    r"(?P<quote>[\"'])(?P<slot>[A-Za-z_]\w*)(?P=quote)\s+in\s+getroottable\s*\(\s*\)\s*"
    r"\)\s*\)\s*(?P<brace>\{\s*)?(?P=slot)\s*<-\s*"
)

_LEGACY_PERSIST = re.compile(
    r"(?m)^(?P<indent>[ \t]*)(?P<slot>[A-Za-z_]\w*)\s*<-\s*persist\s*\(\s*"
    r"(?P<quote>[\"'])(?P=slot)(?P=quote)\s*,\s*function\s*\(\s*\)\s*\{\s*return\s+"
)


def _guard_edits(source: str, masked: str, depths: list[int]) -> list[Edit]:
    edits: list[Edit] = []
    for match in _GUARD.finditer(source):
        if depths[match.start()] != 0:
            continue
        expr_end = _statement_end(masked, match.end())
        if expr_end is None:
            continue
        end = expr_end + 1
        if match.group("brace") is not None:
            closing = re.match(r"[ \t]*\}[ \t]*(?:\r?\n)?", source[end:])
            if closing is None:
                continue
            end += closing.end()
        expression = source[match.end():expr_end].strip()
        replacement = f"{match.group('indent')}persist {match.group('slot')} = {expression}"
        if source[end - 1:end] == "\n":
            replacement += "\n"
        edits.append(Edit(match.start(), end, replacement, "guard", match.group("slot")))
    return edits


def _legacy_persist_edits(source: str, masked: str, depths: list[int]) -> list[Edit]:
    edits: list[Edit] = []
    for match in _LEGACY_PERSIST.finditer(source):
        if depths[match.start()] != 0:
            continue
        expr_end = _statement_end(masked, match.end())
        if expr_end is None:
            continue
        tail = re.match(r";\s*\}\s*\)\s*;?", source[expr_end:])
        if tail is None:
            continue
        end = expr_end + tail.end()
        expression = source[match.end():expr_end].strip()
        replacement = f"{match.group('indent')}persist {match.group('slot')} = {expression}"
        edits.append(Edit(match.start(), end, replacement, "persist-call", match.group("slot")))
    return edits


def migrate_source(source: str) -> tuple[str, list[Edit]]:
    masked = _masked(source)
    depths = _brace_depth(masked)
    edits = _guard_edits(source, masked, depths) + _legacy_persist_edits(source, masked, depths)
    edits.sort(key=lambda edit: edit.start)
    for previous, current in zip(edits, edits[1:]):
        if previous.end > current.start:
            raise ValueError(f"overlapping migration edits for {previous.name} and {current.name}")
    migrated = source
    for edit in reversed(edits):
        migrated = migrated[:edit.start] + edit.replacement + migrated[edit.end:]
    return migrated, edits


def nut_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*.nut")
        if not any(part in EXCLUDED_DIRS for part in path.relative_to(root).parts)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--write", action="store_true", help="rewrite migratable files in place")
    parser.add_argument("--check", action="store_true", help="fail if migratable legacy syntax remains")
    args = parser.parse_args()

    root = args.root.resolve()
    files = nut_files(root)
    changed: list[tuple[Path, list[Edit]]] = []
    for path in files:
        source = path.read_text(encoding="utf-8-sig")
        migrated, edits = migrate_source(source)
        if not edits:
            continue
        changed.append((path, edits))
        if args.write:
            path.write_text(migrated, encoding="utf-8", newline="")

    action = "migrated" if args.write else "migratable"
    edit_count = sum(len(edits) for _, edits in changed)
    print(f"EveScript scan: {len(files)} .nut files, {edit_count} {action} declarations in {len(changed)} files")
    for path, edits in changed:
        names = ", ".join(edit.name for edit in edits)
        print(f"  {path.relative_to(root).as_posix()}: {names}")
    if args.check and changed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
