"""Unit tests for scripts/check_test_assert_level.py.

The checker's judgement depends on masking comments, string literals and raw strings before
brace matching, and on distinguishing warning-level from failure-level assertions. Both are
easy to break silently, so each rule it relies on is pinned here.
"""

import importlib.util
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

SPEC = importlib.util.spec_from_file_location(
    "check_test_assert_level", ROOT / "scripts" / "check_test_assert_level.py"
)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class AssertLevelTests(unittest.TestCase):
    def scan(self, source: str) -> list[tuple[int, str]]:
        """Run the file scan over one source string."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.cpp"
            path.write_text(source, encoding="utf-8")
            masked = checker.mask_non_code(source)
            return checker.scan_file(path, checker.failing_helpers(masked))

    def test_check_only_case_is_flagged(self):
        source = 'TEST_CASE("a.onlyCheck") { CHECK(1 + 1 == 2); CHECK_EQ(2, 2); }\n'
        self.assertEqual([name for _, name in self.scan(source)], ["a.onlyCheck"])

    def test_require_case_is_not_flagged(self):
        source = 'TEST_CASE("a.requires") { REQUIRE(1 + 1 == 2); CHECK_EQ(2, 2); }\n'
        self.assertEqual(self.scan(source), [])

    def test_engine_assert_counts_as_failure(self):
        for macro in ("EV_ASSERT(true)", 'EV_PARAM_CHECK(true, "x")'):
            with self.subTest(macro=macro):
                source = f'TEST_CASE("a.engine") {{ {macro}; CHECK(true); }}\n'
                self.assertEqual(self.scan(source), [])

    def test_same_file_require_helper_exempts_the_caller(self):
        source = (
            "void requireReady() { REQUIRE(true); }\n"
            'TEST_CASE("a.delegates") { requireReady(); CHECK(true); }\n'
        )
        self.assertEqual(self.scan(source), [])

    def test_unused_attribute_does_not_hide_a_require_helper(self):
        source = (
            "[[maybe_unused]] bool requireReady() { REQUIRE(true); return true; }\n"
            'TEST_CASE("a.delegates") { CHECK(requireReady()); }\n'
        )
        self.assertEqual(self.scan(source), [])

    def test_helper_in_another_file_does_not_exempt(self):
        # No definition in this file, so the checker cannot know; it reports and the author
        # either states a REQUIRE or baselines the case (documented limitation).
        source = 'TEST_CASE("a.otherFileHelper") { helperElsewhere(); CHECK(true); }\n'
        self.assertEqual([name for _, name in self.scan(source)], ["a.otherFileHelper"])

    def test_raw_string_with_braces_does_not_end_the_body_early(self):
        source = (
            'TEST_CASE("a.rawBraces") {\n'
            '    const char* s = R"SQ( function f() { return 1 } )SQ";\n'
            "    REQUIRE(s != nullptr);\n"
            "    CHECK(true);\n"
            "}\n"
            'TEST_CASE("a.nextCaseOnlyCheck") { CHECK(true); }\n'
        )
        names = [name for _, name in self.scan(source)]
        self.assertEqual(names, ["a.nextCaseOnlyCheck"])

    def test_macro_inside_comment_or_string_is_ignored(self):
        source = (
            'TEST_CASE("a.masked") {\n'
            "    // CHECK(false) in a comment is not an assertion\n"
            '    const char* text = "CHECK(false)";\n'
            "    REQUIRE(text != nullptr);\n"
            "}\n"
        )
        self.assertEqual(self.scan(source), [])

    def test_fixture_and_method_macros_are_scanned(self):
        source = (
            'TEST_CASE_FIXTURE(Fx, "a.fixture") { CHECK(true); }\n'
            'TEST_CASE_METHOD(Fx, "a.method") { CHECK(true); }\n'
        )
        self.assertEqual([name for _, name in self.scan(source)], ["a.fixture", "a.method"])

    def test_case_without_assertions_is_not_reported(self):
        # Not this checker's subject: a case with no assertions at all is covered by review,
        # not by the warning-level rule.
        source = 'TEST_CASE("a.silent") { doSomething(); }\n'
        self.assertEqual(self.scan(source), [])

    def test_masking_keeps_offsets_stable(self):
        source = 'const char* s = "a{b}c";\nTEST_CASE("a.after") { CHECK(true); }\n'
        masked = checker.mask_non_code(source)
        self.assertEqual(len(masked), len(source))
        self.assertEqual(masked.count("\n"), source.count("\n"))

    def test_digit_separator_is_not_a_character_literal(self):
        # A C++14 digit separator used to be read as a character literal, which blanked every
        # character up to the next apostrophe -- hiding the REQUIRE below and turning a real
        # case into a false offender (or hiding an offender entirely).
        source = (
            'TEST_CASE("a.separator") {\n'
            "    const auto n = 1'000'000;\n"
            "    REQUIRE(n == 1'000'000);\n"
            "    CHECK(true);\n"
            "}\n"
        )
        self.assertEqual(self.scan(source), [])

    def test_stray_apostrophe_does_not_swallow_the_file(self):
        source = (
            'TEST_CASE("a.stray") {\n'
            "    // don't let an apostrophe in prose break the scanner\n"
            '    const char* s = "x";\n'
            "    REQUIRE(s != nullptr);\n"
            "}\n"
            'TEST_CASE("a.afterStray") { CHECK(true); }\n'
        )
        self.assertEqual([name for _, name in self.scan(source)], ["a.afterStray"])


class StrictFileTests(unittest.TestCase):
    """`STRICT_FILES` evidence must be able to fail even when a case also has a REQUIRE.

    The baseline only freezes whole cases whose assertions are all warning-level, so one
    incidental `REQUIRE(x != nullptr)` hides a case whose decisive assertions are `CHECK`.
    A file listed in `STRICT_FILES` closes that gap; these tests pin the rule and its report.
    """

    def strict(self, source: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.cpp"
            path.write_text(source, encoding="utf-8")
            return checker.strict_violations([str(path)])

    def test_check_is_a_violation_even_beside_a_require(self):
        source = (
            'TEST_CASE("a.mixed") {\n'
            "    const char* s = \"x\";\n"
            "    REQUIRE(s != nullptr);\n"
            "    CHECK_EQ(s[0], 'x');\n"
            "}\n"
        )
        violations = self.strict(source)
        self.assertEqual(len(violations), 1)
        path, case, line = violations[0].split("|")
        self.assertTrue(path.endswith("evidence.cpp"))
        self.assertEqual(case, "a.mixed")
        self.assertEqual(line, "4")

    def test_require_only_file_is_clean(self):
        self.assertEqual(self.strict('TEST_CASE("a.ok") { REQUIRE(true); }\n'), [])

    def test_warning_level_assertions_outside_any_case_are_reported(self):
        self.assertEqual(len(self.strict("CHECK(true);\n")), 1)

    def test_missing_file_is_a_violation(self):
        # A renamed/deleted evidence file must not silently drop the requirement.
        violations = checker.strict_violations(["does/not/exist.cpp"])
        self.assertEqual(violations, ["does/not/exist.cpp|<missing>|0"])

    def test_the_repository_list_is_actually_clean(self):
        # The rule is only useful while the checked-in list passes; a failure here means the
        # gate is red on the repository, not that this test is wrong.
        self.assertEqual(checker.strict_violations(), [])

    def test_relocated_evidence_checks_only_its_origin_and_preserves_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "merged.cpp"
            source.write_text(
                '#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_other)\n'
                'TEST_CASE("other") { CHECK(false); }\n#endif\n'
                '#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_strict)\n'
                'void helper() { CHECK(false); }\n'
                'TEST_CASE("evidence") { REQUIRE(true); CHECK(false); }\n#endif\n',
                encoding="utf-8",
            )
            catalogue = root / "test_levels.json"
            catalogue.write_text(json.dumps({"sources": {
                "merged.cpp": {"origins": ["strict.cpp", "other.cpp"]}
            }}), encoding="utf-8")
            with patch.object(checker, "TEST_CATALOGUE", catalogue):
                self.assertEqual(checker.strict_violations(["test/strict.cpp"]), [
                    f"{source.as_posix()}|<outside any case>|5",
                    f"{source.as_posix()}|evidence|6",
                ])
                self.assertEqual(checker.relocated_baseline([
                    "test/strict.cpp|evidence|1", "test/strict.cpp|new.case|1"
                ]), {f"{source.as_posix()}|evidence|1", "test/strict.cpp|new.case|1"})


if __name__ == "__main__":
    unittest.main()
