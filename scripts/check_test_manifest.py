#!/usr/bin/env python3
"""Verify the test suite retains automatic source discovery.

Top-level test/*.cpp files are discovered by CMake with CONFIGURE_DEPENDS, like
engine/module sources. This check prevents a regression to a central append-only
list or a plain configure-time glob that misses files added after configuration.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "test"
CMAKE_LIST = TEST_DIR / "CMakeLists.txt"

DISCOVERY_RE = re.compile(
    r'file\(GLOB\s+all_test_cpp\s+CONFIGURE_DEPENDS\s+'
    r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\*\.cpp"\s*\)',
    re.MULTILINE,
)
DEMO_EXCLUSION_RE = re.compile(
    r'list\(REMOVE_ITEM\s+all_test_cpp\s+'
    r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/demo\.cpp"\s*\)'
)
DEMO_APPEND_RE = re.compile(r"list\(APPEND\s+all_test_cpp\s+demo\.cpp\s*\)")


def discovery_contract_errors(text: str) -> list[str]:
    """Return errors when CMake no longer auto-discovers all test sources."""
    errors: list[str] = []
    if not DISCOVERY_RE.search(text):
        errors.append("all_test_cpp must glob test/*.cpp with CONFIGURE_DEPENDS")
    if not DEMO_EXCLUSION_RE.search(text):
        errors.append("demo.cpp must be removed from the unconditional source set")
    if not DEMO_APPEND_RE.search(text):
        errors.append("demo.cpp must remain conditionally appended")
    return errors


def main() -> int:
    if not CMAKE_LIST.exists():
        print(f"error: {CMAKE_LIST} not found", file=sys.stderr)
        return 1

    errors = discovery_contract_errors(CMAKE_LIST.read_text(encoding="utf-8"))
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    count = sum(1 for _ in TEST_DIR.glob("*.cpp"))
    print(f"test auto-discovery OK: CONFIGURE_DEPENDS covers {count} sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
