#!/usr/bin/env python3
"""Verify the test suite retains automatic source discovery.

Top-level test/*.cpp files are globbed at configure time and recorded in
test_src.txt. Make reconfigures when an individual test/*.cpp is newer than
that list. This check prevents a regression to a central append-only list,
Ninja CONFIGURE_DEPENDS glob restacking, or a glob that never re-runs after
a new file is added.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "test"
CMAKE_LIST = TEST_DIR / "CMakeLists.txt"
MAKEFILE = REPO / "Makefile"

CONFIGURE_DEPENDS_GLOB_RE = re.compile(
    r"file\(GLOB\s+all_test_cpp\s+CONFIGURE_DEPENDS\b",
    re.MULTILINE,
)
GLOB_RE = re.compile(
    r'file\(GLOB\s+all_test_cpp\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\*\.cpp"\s*\)',
    re.MULTILINE,
)
TEST_SRC_TXT_RE = re.compile(r"test_src\.txt")
MAKE_TEST_CPP_RE = re.compile(r"test/\*\.cpp")
DEMO_EXCLUSION_RE = re.compile(
    r'list\(REMOVE_ITEM\s+all_test_cpp\s+'
    r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/demo\.cpp"\s*\)'
)
DEMO_APPEND_RE = re.compile(r"list\(APPEND\s+all_test_cpp\s+demo\.cpp\s*\)")


def discovery_contract_errors(cmake_text: str, makefile_text: str = "") -> list[str]:
    """Return errors when CMake/Make no longer auto-discover all test sources."""
    errors: list[str] = []
    if CONFIGURE_DEPENDS_GLOB_RE.search(cmake_text):
        errors.append(
            "all_test_cpp must not use CONFIGURE_DEPENDS; write test_src.txt and "
            "reconfigure from make when test/*.cpp changes"
        )
    if not GLOB_RE.search(cmake_text):
        errors.append("all_test_cpp must glob test/*.cpp at configure time")
    if not TEST_SRC_TXT_RE.search(cmake_text):
        errors.append("configure must write test_src.txt for added/removed test sources")
    if not MAKE_TEST_CPP_RE.search(makefile_text) or not TEST_SRC_TXT_RE.search(makefile_text):
        errors.append(
            "Makefile ensure-built must reconfigure when test/*.cpp is newer than test_src.txt"
        )
    if not DEMO_EXCLUSION_RE.search(cmake_text):
        errors.append("demo.cpp must be removed from the unconditional source set")
    if not DEMO_APPEND_RE.search(cmake_text):
        errors.append("demo.cpp must remain conditionally appended")
    return errors


def main() -> int:
    if not CMAKE_LIST.exists():
        print(f"error: {CMAKE_LIST} not found", file=sys.stderr)
        return 1
    if not MAKEFILE.exists():
        print(f"error: {MAKEFILE} not found", file=sys.stderr)
        return 1

    errors = discovery_contract_errors(
        CMAKE_LIST.read_text(encoding="utf-8"),
        MAKEFILE.read_text(encoding="utf-8"),
    )
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    count = sum(1 for _ in TEST_DIR.glob("*.cpp"))
    print(f"test auto-discovery OK: test_src.txt covers {count} sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
