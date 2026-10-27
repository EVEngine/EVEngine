"""Coverage reconciliation must fail closed without losing historical evidence."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audit_integration_tests as audit


class IntegrationAuditTests(unittest.TestCase):
    def baseline(self, retirement=""):
        result = audit.Baseline()
        result["integration/task/original.cpp"] = {"task.real": (7, "module-composition", retirement)}
        return result

    def test_unexpected_missing_identity_still_fails(self):
        text, missing = audit.render(self.baseline(), {}, {})
        self.assertEqual(missing, 1)
        self.assertIn("UNRESOLVED", text)
        with patch.object(sys, "argv", ["audit", "--output", "audit.md"]), patch.object(
            Path, "read_text", return_value=text
        ), patch.object(Path, "write_text"), patch.object(Path, "mkdir"), patch.object(
            audit, "current_locations", return_value=({}, {})
        ):
            self.assertEqual(audit.main(), 1)

    def test_only_explicit_retirement_survives_exact_roundtrip(self):
        reason = "Only sizeof(header type)>0; real importer retains the runtime contract."
        baseline = self.baseline(reason)
        baseline["integration/task/original.cpp"]["task.other"] = (9, "module-composition", "")
        text, missing = audit.render(baseline, {}, {})
        self.assertEqual(missing, 1)
        self.assertIn(" — RETIRED: " + reason, text)
        with patch.object(Path, "read_text", return_value=text):
            restored = audit.read_baseline(Path("audit.md"))
        self.assertEqual(restored, baseline)
        again, missing = audit.render(restored, {}, {})
        self.assertEqual(missing, 1)
        self.assertEqual(again, text)

    def test_commented_or_string_subcase_cannot_preserve_coverage(self):
        body = '''// SUB_CASE("task.real") {}
        /* SUB_CASE("task.real") {} */
        const char* text = "SUB_CASE(\\"task.real\\")";
        SUB_CASE("task.valid") {};
        '''
        self.assertEqual(audit.subcase_names(body), ["task.valid"])
        inventory = {"integration/current.cpp": {"cases": {"task.parent": {"line": 1, "body": body}}}}
        with patch.object(audit.test_levels, "source_inventory", return_value=inventory):
            direct, subcases = audit.current_locations()
        _, missing = audit.render(self.baseline(), direct, subcases)
        self.assertEqual(missing, 1)

    def test_generated_appendix_regenerates_without_git(self):
        original = '''#define TestScript(func) \\
        TEST_CASE_FIXTURE(Example, "Example." #func) { REQUIRE(run(#func)); }
TestScript(basic)
// TestScript(disabled)
/* TestScript(alsoDisabled) */
TestScript(other)
'''
        generated = audit.generated_calls(original)
        self.assertEqual(set(generated), {"Example.basic", "Example.other"})
        baseline = self.baseline()
        baseline.generated["integration/generated.cpp"] = generated
        locations = {name: [("integration/current.cpp", name, 5)] for name in ("task.real", *generated)}
        text, missing = audit.render(baseline, locations, {})
        self.assertEqual(missing, 0)
        self.assertIn("1 literal registered test identities", text)
        self.assertIn("Additional generated identities: 2", text)
        with patch.object(Path, "read_text", return_value=text):
            restored = audit.read_baseline(Path("audit.md"))
        self.assertEqual(restored.generated, baseline.generated)
        self.assertEqual(audit.render(restored, locations, {}), (text, 0))


if __name__ == "__main__":
    unittest.main()
