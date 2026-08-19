#!/usr/bin/env python3
"""Verify every test/*.cpp on disk is registered in test/CMakeLists.txt.

The unit-test suite is listed explicitly in test/CMakeLists.txt (file(GLOB
all_test_cpp ...) plus conditionals). A source file that lands in test/ but is
missing from that list silently never compiles or runs, which is how
test/hair.cpp and test/runtime.cpp rotted before this check existed.

Usage:
    python3 scripts/check_test_manifest.py   # exit 1 on drift
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "test"
CMAKE_LIST = TEST_DIR / "CMakeLists.txt"

ENTRY_RE = re.compile(r"^\s+([\w.-]+\.cpp)\s*$")
APPEND_RE = re.compile(r"list\(APPEND\s+all_test_cpp\s+([\w./-]+\.cpp)\)")

# Test sources appended conditionally inside test/CMakeLists.txt.
CONDITIONAL = {
    "demo.cpp",  # only when EVENGINE_BUILD_DEMO=ON
}


def listed_sources() -> set[str]:
    text = CMAKE_LIST.read_text(encoding="utf-8")
    listed = set()
    in_glob = False
    for line in text.splitlines():
        if "file(GLOB all_test_cpp" in line:
            in_glob = True
            continue
        if in_glob:
            if line.rstrip().endswith(")"):
                in_glob = False
                continue
            m = ENTRY_RE.match(line)
            if m:
                listed.add(m.group(1))
        m = APPEND_RE.search(line)
        if m:
            listed.add(pathlib.PurePosixPath(m.group(1)).name)
    return listed


def main() -> int:
    if not CMAKE_LIST.exists():
        print(f"error: {CMAKE_LIST} not found", file=sys.stderr)
        return 1

    on_disk = {p.name for p in TEST_DIR.glob("*.cpp")}
    listed = listed_sources()
    missing = sorted((on_disk - listed) - CONDITIONAL)
    stale = sorted(listed - on_disk - CONDITIONAL)

    if not missing and not stale:
        print(f"test manifest OK: {len(listed)} sources listed, {len(on_disk)} on disk")
        return 0

    if missing:
        print("error: test source files missing from test/CMakeLists.txt:", file=sys.stderr)
        for name in missing:
            print(f"  {name}", file=sys.stderr)
    if stale:
        print("error: entries in test/CMakeLists.txt with no source file:", file=sys.stderr)
        for name in stale:
            print(f"  {name}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
