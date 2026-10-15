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

    def test_undeclared_module_and_flat_tests_are_errors(self):
        data, inventory, modules = self.fixture()
        data["sources"]["unit/rules/rules.cpp"]["module"] = "unknown"
        self.assertIn("undeclared owner", coverage.audit(data, inventory, modules)[1][0])


if __name__ == "__main__":
    unittest.main()
