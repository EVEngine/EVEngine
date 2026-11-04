"""Module ownership and behavioral coverage must not be conflated."""

from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import test_module_coverage as coverage  # noqa: E402


class ModuleCoverageTests(unittest.TestCase):
    def test_policy_comments_do_not_change_code_size_budget(self):
        code = "// ordinary comment\nint value;\n" * 2500
        policy = '// clang-format off\n/* eve-test-policy:\n{"schema":"evengine.test-policy","version":1}\n*/\n// clang-format on\n'
        self.assertEqual(coverage.source_line_count(policy + code), 5000)
        self.assertEqual(coverage.source_line_count(policy + code + "int extra;\n"), 5001)
        self.assertEqual(coverage.source_line_count('const char* s = "eve-test-policy:";\n'), 1)

    def test_v3_automatic_source_discovery_reaches_module_evidence(self):
        text = 'TEST_CASE("rules.added") { CHECK(true); }'
        data = {"schema": "evengine.test-levels", "version": 3, "sources": {}}
        inventory = {"unit/rules/new_rule.cpp": {"text": text,
                     "cases": coverage.test_levels.cases_in(text), "generated": set()}}
        modules = [SimpleNamespace(name="rules", dir="rules", layer=5)]
        report, errors = coverage.audit(data, inventory, modules)
        self.assertEqual(errors, [])
        self.assertEqual(report["modules"]["rules"]["unit_cases"], 1)
        self.assertEqual(report["modules"]["rules"]["unit_sources"], ["unit/rules/new_rule.cpp"])

    def fixture(self):
        modules = [SimpleNamespace(name="rules", dir="rules", layer=5),
                   SimpleNamespace(name="rules_editor", dir="rules/editor", layer=7),
                   SimpleNamespace(name="missing", dir="missing", layer=5)]
        data = {"sources": {
            "unit/rules/rules.cpp": {"role": "tests", "module": "rules", "cases": {"local-logic": ["rules.local"]}},
            "integration/save-restore/journey.cpp": {"role": "tests", "module": "rules", "task": "save-restore",
                "cases": {"module-composition": ["rules.restore"]}},
        }}
        inventory = {"unit/rules/rules.cpp": {"text": '#include "rules/Rules.h"\n'},
                     "integration/save-restore/journey.cpp": {"text": '#include "rules/editor/Session.h"\n'}}
        return data, inventory, modules

    def test_all_modules_and_missing_evidence_are_reported(self):
        report, errors = coverage.audit(*self.fixture())
        self.assertEqual(errors, [])
        self.assertEqual(report["module_count"], 3)
        self.assertEqual(report["modules"]["missing"]["status"], "no-direct-test-evidence")

    def test_path_ownership_matches_explicit_fields_and_keeps_integration_owner(self):
        data, inventory, modules = self.fixture()
        expected, errors = coverage.audit(data, inventory, modules)
        self.assertEqual(errors, [])
        del data["sources"]["unit/rules/rules.cpp"]["module"]
        del data["sources"]["integration/save-restore/journey.cpp"]["task"]
        actual, errors = coverage.audit(data, inventory, modules)
        self.assertEqual(errors, [])
        self.assertEqual(actual, expected)
        del data["sources"]["integration/save-restore/journey.cpp"]["module"]
        self.assertTrue(any("undeclared owner" in error for error in coverage.audit(data, inventory, modules)[1]))

    def test_nested_header_reference_is_not_owned_unit_coverage(self):
        report, _ = coverage.audit(*self.fixture())
        editor = report["modules"]["rules_editor"]
        self.assertEqual(editor["unit_cases"], 0)
        self.assertEqual(editor["status"], "referenced-by-other-tests")
        self.assertEqual(report["tasks"]["save-restore"]["modules"], ["rules", "rules_editor"])

    def test_mixed_or_wrongly_owned_unit_source_is_rejected(self):
        data, inventory, modules = self.fixture()
        policy = data["sources"]["unit/rules/rules.cpp"]
        policy["cases"]["script-binding"] = ["rules.script"]
        self.assertTrue(coverage.audit(data, inventory, modules)[1])
        policy["cases"].pop("script-binding")
        policy["module"] = "rules_editor"
        self.assertTrue(coverage.audit(data, inventory, modules)[1])

    def test_integration_task_is_independent_of_module_directory(self):
        report, errors = coverage.audit(*self.fixture())
        self.assertEqual(errors, [])
        self.assertEqual(report["tasks"]["save-restore"]["cases"], 1)
        data, inventory, modules = self.fixture()
        data["sources"]["integration/save-restore/journey.cpp"]["task"] = "different"
        self.assertTrue(coverage.audit(data, inventory, modules)[1])

    def test_small_unit_module_cannot_regress_to_fragments(self):
        data, inventory, modules = self.fixture()
        source = "unit/rules/fragment.cpp"
        data["sources"][source] = {"role": "tests", "module": "rules", "cases": {"local-logic": ["rules.other"]}}
        inventory[source] = {"text": "TEST_CASE(\"rules.other\") {}\n"}
        self.assertTrue(any("consolidate below 5000" in error for error in coverage.audit(data, inventory, modules)[1]))

    def test_merged_task_preserves_each_composition_owner(self):
        data, inventory, modules = self.fixture()
        data["sources"]["integration/save-restore/journey.cpp"]["modules"] = ["rules", "rules_editor"]
        report, errors = coverage.audit(data, inventory, modules)
        self.assertEqual(errors, [])
        self.assertEqual(report["modules"]["rules_editor"]["status"], "task-tests-recorded")
        self.assertEqual(report["modules"]["rules_editor"]["unit_cases"], 0)

    def test_undeclared_module_and_flat_tests_are_errors(self):
        data, inventory, modules = self.fixture()
        data["sources"]["unit/rules/rules.cpp"]["module"] = "unknown"
        self.assertIn("undeclared owner", coverage.audit(data, inventory, modules)[1][0])


if __name__ == "__main__":
    unittest.main()
