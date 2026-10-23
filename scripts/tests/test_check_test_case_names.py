"""Unit tests for scripts/check_test_case_names.py.

The name extraction pairs a macro match found in masked text with a *string literal* read
from the original text, and it has to skip a file's own case-macro definition (whose body
sits on a continuation line). Each of those rules is pinned here.
"""

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

SPEC = importlib.util.spec_from_file_location(
    "check_test_case_names", ROOT / "scripts" / "check_test_case_names.py"
)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class CaseNameTests(unittest.TestCase):
    def test_merged_sources_use_their_own_fixture(self):
        source = (
            'UnitSciptTest(First, first_content);\n'
            'TestScript(basic)\n'
            'UnitReloadScriptTest(Second, second_content);\n'
            'TestScript(basic)\n'
        )
        self.assertEqual(self.names(source), ["First.basic", "Second.basic"])

    def names(self, source: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.cpp"
            path.write_text(source, encoding="utf-8")
            return [name for name, _ in checker.declared_names(path)]

    def scan(self, source: str) -> dict[str, list[str]]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "case.cpp").write_text(source, encoding="utf-8")
            return checker.scan(root)

    def test_plain_and_fixture_names_are_extracted(self):
        source = (
            'TEST_CASE("a.plain") { REQUIRE(true); }\n'
            'TEST_CASE_FIXTURE(Fx, "a.fixture") { REQUIRE(true); }\n'
        )
        self.assertEqual(self.names(source), ["a.plain", "a.fixture"])

    def test_duplicate_name_is_reported(self):
        source = 'TEST_CASE("a.same") { REQUIRE(true); }\nTEST_CASE("a.same") { REQUIRE(true); }\n'
        duplicates = self.scan(source)
        self.assertIn("a.same", duplicates)
        self.assertEqual(len(duplicates["a.same"]), 2)

    def test_unique_names_are_not_reported(self):
        source = 'TEST_CASE("a.one") { REQUIRE(true); }\nTEST_CASE("a.two") { REQUIRE(true); }\n'
        self.assertEqual(self.scan(source), {})

    def test_script_test_idiom_resolves_the_fixture_name(self):
        source = (
            '#include "ScriptTest.h"\n'
            "extern const char* content;\n"
            "UnitSciptTest(FilesystemTest, content);\n"
            "#define TestScript(func) \\\n"
            '    TEST_CASE_FIXTURE(FilesystemTest, "FilesystemTest." #func) { REQUIRE(callScript(#func)); }\n'
            "TestScript(basic)\n"
            "TestScript(readDir)\n"
        )
        self.assertEqual(self.names(source), ["FilesystemTest.basic", "FilesystemTest.readDir"])

    def test_macro_definition_body_is_not_a_case(self):
        # The definition lives on a continuation line, so the check has to follow the
        # backslash rather than only looking at the `#define` line.
        source = (
            "UnitSciptTest(MyTest, content);\n"
            "#define TestScript(func) \\\n"
            '    TEST_CASE_FIXTURE(MyTest, "MyTest." #func) { REQUIRE(true); }\n'
            "TestScript(one)\n"
        )
        self.assertEqual(self.names(source), ["MyTest.one"])
        self.assertEqual(self.scan(source), {})

    def test_case_inside_a_comment_or_string_is_ignored(self):
        source = (
            '// TEST_CASE("commented.out") { REQUIRE(true); }\n'
            'const char* text = "TEST_CASE(\\"in.a.string\\")";\n'
            'TEST_CASE("a.real") { REQUIRE(true); }\n'
        )
        self.assertEqual(self.names(source), ["a.real"])

    def test_name_inside_a_raw_string_is_ignored(self):
        source = (
            'const char* script = R"SQ( TEST_CASE("raw.inside") { REQUIRE(true); } )SQ";\n'
            'TEST_CASE("a.outsideRaw") { REQUIRE(true); }\n'
        )
        self.assertEqual(self.names(source), ["a.outsideRaw"])

    def test_escaped_quote_in_a_name_is_read_back(self):
        source = 'TEST_CASE("a.quote\\"inside") { REQUIRE(true); }\n'
        self.assertEqual(self.names(source), ['a.quote"inside'])


if __name__ == "__main__":
    unittest.main()
