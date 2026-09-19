#!/usr/bin/env python3
"""Verify the test suite keeps automatic source discovery and a complete domain
partition.

Top-level test/*.cpp files are globbed at configure time and recorded in
test_src.txt. Make reconfigures when an individual test/*.cpp is newer than
that list. This check prevents a regression to a central append-only list,
Ninja CONFIGURE_DEPENDS glob restacking, or a glob that never re-runs after
a new file is added.

The suite links as one `unit_test_<domain>` executable per domain so an agent
can link only the domain it is testing (see test/test_domains.cmake). The
partition rule lives in scripts/test_domains.py, which the configure step also
consumes; this check runs it *without* configuring so a test that matches no
rule -- or two rules -- fails in CI as well.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
import utf8_stdio

# Re-exported so this module stays the single entry point for the test manifest
# contract. The implementation is shared with the configure step.
from test_domains import (  # noqa: F401
    DOMAIN_TABLE,
    TEST_DIR,
    classify,
    parse_domain_table,
    partition,
    table_contract_errors,
)

REPO = Path(__file__).resolve().parent.parent
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

# The configure step must obtain the partition from scripts/test_domains.py
# rather than re-implementing the rule; two implementations would drift.
DOMAIN_EMIT_RE = re.compile(r"test_domains\.py")
DOMAIN_INCLUDE_RE = re.compile(r"test_domain_sources\.cmake")
DOMAIN_INTERFACE_RE = re.compile(r"EVE_TEST_DOMAIN_\$\{")
DISCOVER_PER_TARGET_RE = re.compile(r"zeroerr_discover_tests\(\s*\$\{")


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


def domain_wiring_errors(cmake_text: str) -> list[str]:
    """Return errors when CMake stops consuming the shared partition rule."""
    errors: list[str] = []
    if not DOMAIN_EMIT_RE.search(cmake_text):
        errors.append(
            "test/CMakeLists.txt must obtain the domain partition from "
            "scripts/test_domains.py instead of re-implementing the rule"
        )
    if not DOMAIN_INCLUDE_RE.search(cmake_text):
        errors.append("test/CMakeLists.txt must include the generated domain partition")
    if not DOMAIN_INTERFACE_RE.search(cmake_text):
        errors.append(
            "test/CMakeLists.txt must create one target per generated "
            "EVE_TEST_DOMAIN_<domain>_SOURCES list"
        )
    if not DISCOVER_PER_TARGET_RE.search(cmake_text):
        errors.append(
            "every domain target must register its own zeroerr_discover_tests()"
        )
    return errors


def main() -> int:
    if not CMAKE_LIST.exists():
        print(f"error: {CMAKE_LIST} not found", file=sys.stderr)
        return 1
    if not MAKEFILE.exists():
        print(f"error: {MAKEFILE} not found", file=sys.stderr)
        return 1
    if not DOMAIN_TABLE.exists():
        print(f"error: {DOMAIN_TABLE} not found", file=sys.stderr)
        return 1

    cmake_text = CMAKE_LIST.read_text(encoding="utf-8")
    makefile_text = MAKEFILE.read_text(encoding="utf-8")

    errors = discovery_contract_errors(cmake_text, makefile_text)
    errors.extend(domain_wiring_errors(cmake_text))

    tables = parse_domain_table(DOMAIN_TABLE.read_text(encoding="utf-8"))
    errors.extend(table_contract_errors(tables))

    buckets, partition_errors = partition(tables)
    errors.extend(partition_errors)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    total = sum(len(members) for members in buckets.values())
    print(f"test auto-discovery OK: test_src.txt covers {total + 1} sources")
    print(f"test domains OK: {len(buckets)} domains, {total} tests + 1 shared runner")
    for name in tables["domains"]:  # type: ignore[union-attr]
        print(f"  {name:14s} {len(buckets[name]):4d}")
    empty = [name for name, members in buckets.items() if not members]
    if empty:
        print("note: domain(s) with no test in this profile: " + ", ".join(sorted(empty)))
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
