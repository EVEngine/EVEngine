import sys
import unittest
from pathlib import Path

# check_test_manifest imports its sibling utf8_stdio by flat name, so scripts/ has
# to be importable. The CI discovery command (python -m unittest discover -s
# scripts/tests) only guarantees that when another test module happens to insert
# scripts/ into sys.path first, which makes this file fail when run alone.
SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from check_test_manifest import (  # noqa: E402
    DOMAIN_TABLE,
    TEST_DIR,
    classify,
    discovery_contract_errors,
    parse_domain_table,
    partition,
    table_contract_errors,
)


VALID_CMAKE = """
file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_src.txt" "")
list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")
if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()
"""

VALID_MAKEFILE = """
	for cpp in test/*.cpp; do
	  if [ "$$cpp" -nt build/win32-debug/test/test_src.txt ]; then stale=1; fi
	done
"""

VALID_TABLE = """
set(EVE_TEST_DOMAINS
    core
    graphics
    ui
    scripts
    CACHE INTERNAL "domains")

set(EVE_TEST_MODULE_DOMAIN
    "math;core"
    "graphics;graphics"
    "ui;ui"
    CACHE INTERNAL "roots")

set(EVE_TEST_PACKAGE_OVERRIDE
    "map/level;core"
    CACHE INTERNAL "overrides")

set(EVE_TEST_PREFIX_DOMAIN
    "=runtime;scripts"
    "runtime_handle;core"
    "editor_;core"
    CACHE INTERNAL "prefixes")
"""


class CheckTestManifestTest(unittest.TestCase):
    def test_accepts_src_list_discovery(self):
        self.assertEqual(discovery_contract_errors(VALID_CMAKE, VALID_MAKEFILE), [])

    def test_rejects_configure_depends_glob(self):
        invalid = VALID_CMAKE.replace(
            'file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")',
            'file(GLOB all_test_cpp CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")',
        )
        errors = discovery_contract_errors(invalid, VALID_MAKEFILE)
        self.assertTrue(any("CONFIGURE_DEPENDS" in error for error in errors))

    def test_rejects_glob_without_stale_makefile_check(self):
        errors = discovery_contract_errors(VALID_CMAKE, "")
        self.assertTrue(any("test_src.txt" in error for error in errors))

    def test_requires_demo_to_remain_conditional(self):
        invalid = VALID_CMAKE.replace(
            'list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")',
            "",
        )
        errors = discovery_contract_errors(invalid, VALID_MAKEFILE)
        self.assertTrue(any("removed" in error for error in errors))

    def test_parses_domain_table(self):
        tables = parse_domain_table(VALID_TABLE)
        self.assertEqual(
            tables["domains"], ["core", "graphics", "ui", "scripts"]
        )
        self.assertEqual(tables["module_domain"]["graphics"], "graphics")
        self.assertEqual(tables["package_override"]["map/level"], "core")
        self.assertIn(("=runtime", "scripts"), tables["prefix_rules"])

    def test_include_decides_domain_not_file_name(self):
        tables = parse_domain_table(VALID_TABLE)
        # A file named pcg_photo_mode_panels.cpp is an EVUI test: its include wins
        # over the misleading basename.
        domain, reason, error = classify(
            "pcg_photo_mode_panels",
            '#include "ui/PcgPhotoModePanels.h"\n',
            tables,
        )
        self.assertIsNone(error)
        self.assertEqual(domain, "ui")
        self.assertEqual(reason, "include:ui")

    def test_package_override_wins_over_root(self):
        tables = parse_domain_table(VALID_TABLE)
        domain, _reason, error = classify(
            "editor_level", '#include "map/level/LevelDocument.h"\n', tables
        )
        self.assertIsNone(error)
        self.assertEqual(domain, "core")

    def test_exact_rule_is_not_shadowed_by_longer_name(self):
        tables = parse_domain_table(VALID_TABLE)
        runtime, _r, error = classify("runtime", "#include <string>\n", tables)
        self.assertIsNone(error)
        self.assertEqual(runtime, "scripts")
        handle, _r2, error2 = classify("runtime_handle", "#include <string>\n", tables)
        self.assertIsNone(error2)
        self.assertEqual(handle, "core")

    def test_shared_runner_is_not_a_domain_member(self):
        tables = parse_domain_table(VALID_TABLE)
        domain, reason, error = classify("main", "#include <string>\n", tables)
        self.assertIsNone(domain)
        self.assertIsNone(error)
        self.assertEqual(reason, "shared runner")

    def test_unclassified_source_is_an_error(self):
        tables = parse_domain_table(VALID_TABLE)
        domain, _reason, error = classify("mystery", "#include <string>\n", tables)
        self.assertIsNone(domain)
        self.assertIsNotNone(error)

    def test_rejects_prefix_shadowing_across_domains(self):
        invalid = VALID_TABLE.replace('"=runtime;scripts"', '"runtime;scripts"')
        errors = table_contract_errors(parse_domain_table(invalid))
        self.assertTrue(any("prefix of" in error for error in errors))

    def test_rejects_undeclared_domain(self):
        invalid = VALID_TABLE.replace('"math;core"', '"math;nosuchdomain"')
        errors = table_contract_errors(parse_domain_table(invalid))
        self.assertTrue(any("undeclared domain" in error for error in errors))

    def test_real_table_classifies_every_test_source(self):
        tables = parse_domain_table(DOMAIN_TABLE.read_text(encoding="utf-8"))
        self.assertEqual(table_contract_errors(tables), [])
        buckets, errors = partition(tables, TEST_DIR)
        self.assertEqual(errors, [])
        # Every top-level test/*.cpp is either a domain member or the shared runner.
        covered = sum(len(members) for members in buckets.values()) + 1
        self.assertEqual(covered, len(list(TEST_DIR.glob("*.cpp"))))


if __name__ == "__main__":
    unittest.main()
