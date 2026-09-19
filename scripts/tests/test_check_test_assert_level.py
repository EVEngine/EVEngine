"""Unit tests for scripts/check_test_assert_level.py.

The checker's judgement depends on masking comments, string literals and raw strings before
brace matching, and on distinguishing warning-level from failure-level assertions. Both are
easy to break silently, so each rule it relies on is pinned here.
"""

import importlib.util
import sys
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
