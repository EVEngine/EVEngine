"""Detect test cases that can never run because a sibling shares their source line.

zeroerr registers a case with `getTestSet(type).insert(tc)` into a `std::set`, and
the ordering key of `TestCase` is only (file, line):

    bool TestCase::operator<(const TestCase& rhs) const {
        return (file < rhs.file) || (file == rhs.file && line < rhs.line);
    }

Two case-registering macros on one source line therefore compare equivalent and the
second registration is dropped **silently**: no compile error, no warning, and the
case simply does not exist. `--list-test-cases` is the only way to notice.

This checker is source-only and reports offending file:line pairs.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import utf8_stdio

# Macros that register a case, plus the per-file wrapper macro used by script tests.
CASE_MACRO = re.compile(r"\b(TEST_CASE|TEST_CASE_FIXTURE|TEST_CASE_METHOD|TestScript)\s*\(")


def find_offenders(root: Path) -> list[tuple[Path, int, int, str]]:
    """Return (path, line, case_count, text) for every line declaring 2+ cases."""
    offenders: list[tuple[Path, int, int, str]] = []
    for path in sorted(root.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            count = len(CASE_MACRO.findall(line))
            if count > 1:
                offenders.append((path, number, count, line.strip()))
    return offenders


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("test"), help="directory to scan")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    offenders = find_offenders(args.root)
    dropped = sum(count - 1 for _, _, count, _ in offenders)
    for path, number, count, text in offenders:
        print(f"{path}:{number}: {count} cases on one line -> {count - 1} will never run")
        if not args.quiet:
            print(f"    {text}")
    print(f"test case line check: {len(offenders)} offending lines, {dropped} cases dropped")
    return 1 if offenders else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
