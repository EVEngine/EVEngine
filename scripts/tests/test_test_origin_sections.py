"""Legacy focused source selectors must not swallow unconditional migrated cases."""

from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import test_levels as levels  # noqa: E402


TEXT = '''TEST_CASE("always.before") {}
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_first)
TEST_CASE("first.case") {}
#ifdef BACKEND
TEST_CASE("first.backend") {}
#if INNER
TEST_CASE("first.nested") {}
#endif
#endif
#endif
TEST_CASE("always.between") {}
#if !defined(EVE_TEST_FILTER_ORIGINS) || \\
    defined(EVE_TEST_ORIGIN_last)
TEST_CASE("last.case") {}
#endif
TEST_CASE("always.after") {}
'''


class OriginSourceSectionTests(unittest.TestCase):
    def test_nested_backend_directives_close_the_correct_guard(self):
        sections = levels.origin_source_sections(TEXT, {"origins": ["first.cpp", "last.cpp"]})
        self.assertEqual(set(levels.cases_in(sections["first.cpp"])),
                         {"first.case", "first.backend", "first.nested"})
        self.assertEqual(set(levels.cases_in(sections["last.cpp"])), {"last.case"})
        self.assertEqual(sections["first.cpp"].count("\n"), TEXT.count("\n"))
        self.assertEqual(levels.cases_in(sections["last.cpp"])["last.case"]["line"],
                         levels.cases_in(TEXT)["last.case"]["line"])

    def test_unconditional_alias_does_not_select_guarded_siblings(self):
        sections = levels.origin_source_sections(TEXT, {"origins": ["newly_moved.cpp"]})
        self.assertEqual(set(levels.cases_in(sections["newly_moved.cpp"])),
                         {"always.before", "always.between", "always.after"})
        self.assertEqual(set(levels.cases_in(levels.always_source_text(TEXT))),
                         {"always.before", "always.between", "always.after"})

    def test_raw_strings_and_comments_cannot_create_or_close_guards(self):
        text = '''const char* fake = R"cpp(
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fake)
TEST_CASE("fake.case") {}
#endif
)cpp";
/* #if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_comment)
*/
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_real)
const char* close = R"x(
#endif
)x";
TEST_CASE("real.case") {}
#endif
TEST_CASE("always.case") {}
'''
        sections = levels.origin_source_sections(text, {"origins": ["moved.cpp"]})
        self.assertEqual(set(sections), {"real.cpp", "moved.cpp"})
        self.assertEqual(set(levels.cases_in(sections["real.cpp"])), {"real.case"})
        self.assertEqual(set(levels.cases_in(sections["moved.cpp"])), {"always.case"})

    def test_repeated_origin_sections_are_combined(self):
        text = TEXT + '''#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_first)
TEST_CASE("first.extra") {}
#endif'''
        sections = levels.origin_source_sections(text, {})
        self.assertEqual(set(levels.cases_in(sections["first.cpp"])),
                         {"first.case", "first.backend", "first.nested", "first.extra"})

    def test_unguarded_source_alias_retains_all_cases(self):
        text = 'TEST_CASE("one") {}\nTEST_CASE("two") {}'
        self.assertEqual(levels.origin_source_sections(text, {"origins": ["old.cpp"]}), {"old.cpp": text})

    def test_emit_cmake_exports_always_names_and_precise_aliases(self):
        policy = {"role": "tests", "default": "local-logic",
                  "origins": ["first.cpp", "last.cpp", "moved.cpp"],
                  "cases": {"local-logic": list(levels.cases_in(TEXT))}}
        data = {"schema": "evengine.test-levels", "version": 1, "sources": {"combined.cpp": policy}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "combined.cpp").write_text(TEXT, encoding="utf-8")
            with patch.object(levels, "CATALOGUE", root / "test_levels.json"):
                levels.emit_cmake(data, root / "metadata.cmake")
            rendered = (root / "metadata.cmake").read_text(encoding="utf-8")
        self.assertIn('set("EVE_TEST_ALWAYS_CASE_NAMES_combined.cpp" "always.before;always.between;always.after")',
                      rendered)
        self.assertIn('set("EVE_TEST_CASE_NAMES_last.cpp" "last.case")', rendered)
        self.assertIn('set("EVE_TEST_CASE_NAMES_moved.cpp" "always.before;always.between;always.after")', rendered)

    def test_deleted_alias_cannot_override_existing_physical_source_metadata(self):
        data = {"schema": "evengine.test-levels", "version": 1, "sources": {
            "integration/workflow/same.cpp": {"role": "tests", "default": "script-binding",
                "generated": ["physical.generated."], "cases": {"script-binding": ["physical.case"]}},
            "unit/legacy/merged.cpp": {"role": "tests", "default": "local-logic", "origins": ["same.cpp"],
                "generated": ["legacy.generated."], "cases": {"local-logic": ["legacy.case"]}},
        }}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            physical, merged = root / "integration/workflow/same.cpp", root / "unit/legacy/merged.cpp"
            physical.parent.mkdir(parents=True)
            merged.parent.mkdir(parents=True)
            physical.write_text('TEST_CASE("physical.case") {}', encoding="utf-8")
            merged.write_text('TEST_CASE("legacy.case") {}', encoding="utf-8")
            metadata = root / "metadata.cmake"
            with patch.object(levels, "CATALOGUE", root / "test_levels.json"):
                levels.emit_cmake(data, metadata)
            rendered = metadata.read_text(encoding="utf-8")
            self.assertEqual(rendered.count('set("EVE_TEST_CASE_NAMES_same.cpp"'), 1)
            self.assertIn('set("EVE_TEST_CASE_NAMES_same.cpp" "physical.case")', rendered)
            self.assertIn('set("EVE_TEST_ORIGIN_CASE_NAMES_same.cpp" "legacy.case")', rendered)
            self.assertIn('set("EVE_TEST_GENERATED_same.cpp" "physical.generated.")', rendered)
            self.assertIn('set("EVE_TEST_LEVEL_same.cpp|" "integration;reason:script-binding;no-render")', rendered)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(collision NONE)\n'
                f'include("{(levels.ROOT / "cmake/TestLevels.cmake").as_posix()}")\n'
                f'include("{metadata.as_posix()}")\n'
                'add_custom_target(consumer)\n'
                'set_property(TARGET consumer PROPERTY EVE_TEST_ORIGIN_DEFINITIONS '
                'EVE_TEST_FILTER_ORIGINS EVE_TEST_ORIGIN_same)\n'
                f'eve_test_source_case_names("{physical.as_posix()}" physical_names consumer)\n'
                f'eve_test_source_case_names("{merged.as_posix()}" legacy_names consumer)\n'
                'if(NOT physical_names STREQUAL "physical.case" OR NOT legacy_names STREQUAL "legacy.case")\n'
                'message(FATAL_ERROR "physical metadata or legacy selection lost ownership")\nendif()\n',
                encoding="utf-8")
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, encoding="utf-8", errors="replace", check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
