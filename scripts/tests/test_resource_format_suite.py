import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("suite", Path(__file__).parents[1] / "resource_format_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)


class ResourceSuiteTests(unittest.TestCase):
    def invoke(self, extra):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            with patch.object(sys, "argv", ["suite", "--family", "font", "--report", str(report)] + extra):
                code = suite.main()
            return code, json.loads(report.read_text())

    def test_inventory_is_not_execution(self):
        code, report = self.invoke([])
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "not-run")

    def test_missing_cases_fail_before_execution(self):
        with patch.object(suite.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, '{"tests": []}', '')) as run:
            code, report = self.invoke(["--build-dir", "/unused"])
        self.assertEqual(code, 1)
        self.assertTrue(report["missing_registrations"])
        self.assertEqual(run.call_count, 1)

    def test_failed_runtime_is_not_green(self):
        names = suite.inventory()["font"]["tests"]
        listing = json.dumps({"tests": [{"name": n} for n in names]})
        with patch.object(suite.subprocess, "run", side_effect=[subprocess.CompletedProcess([], 0, listing, ''), subprocess.CompletedProcess([], 8, 'failure', '')]):
            code, report = self.invoke(["--build-dir", "/unused"])
        self.assertEqual(code, 8)
        self.assertEqual(report["status"], "failed")

    def test_disabled_test_is_not_success(self):
        listing = {"tests": [{"name": "example", "properties": [{"name": "DISABLED", "value": True}]}]}
        with patch.object(suite.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, json.dumps(listing), '')) as run:
            code, report = suite.run_cases(Path("/unused"), ["example"])
        self.assertEqual(code, 1)
        self.assertEqual(report["disabled_tests"], ["example"])
        self.assertEqual(run.call_count, 1)

    def real_ctest(self, script, properties=""):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "case.py").write_text(script)
            (root / "CTestTestfile.cmake").write_text(
                f'add_test(example "{Path(sys.executable).as_posix()}" "{(root / "case.py").as_posix()}")\n' + properties)
            return suite.run_cases(root, ["example"])

    def test_real_ctest_success_has_per_case_evidence(self):
        code, report = self.real_ctest("print('assertions passed')")
        self.assertEqual(code, 0)
        self.assertEqual(report["results"][0]["status"], "passed")

    def test_real_ctest_zero_exit_with_assertion_log_is_failure(self):
        code, report = self.real_ctest("print('ERROR Assertion Failed: pixels differ')")
        self.assertNotEqual(code, 0)
        self.assertEqual(report["results"][0]["status"], "failed")

    def test_real_ctest_skip_is_not_pass(self):
        code, report = self.real_ctest("raise SystemExit(77)",
                                       "set_tests_properties(example PROPERTIES SKIP_RETURN_CODE 77)\n")
        self.assertNotEqual(code, 0)
        self.assertEqual(report["results"][0]["status"], "skipped")

    def test_missing_junit_cannot_pass(self):
        listing = json.dumps({"tests": [{"name": "example"}]})
        with patch.object(suite.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, listing, ''),
                subprocess.CompletedProcess([], 0, 'success without evidence', '')]):
            code, report = suite.run_cases(Path("/unused"), ["example"])
        self.assertEqual(code, 1)
        self.assertEqual(report["results"][0]["status"], "missing-result")

    def test_fixture_tampering_is_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixtures = root / "test/fixtures/resource_formats"
            fixtures.mkdir(parents=True)
            (fixtures / "manifest.json").write_text(json.dumps({
                "schema": "evengine.resource-format-fixtures", "version": 1,
                "sha256": {"pattern.png": "0" * 64}}))
            (fixtures / "pattern.png").write_bytes(b"modified bytes")
            self.assertEqual(suite.fixture_errors(root), ["pattern.png"])

    def test_extensions_include_uncovered_routing(self):
        self.assertIn(".dae", suite.inventory()["model"]["advertised_resource_extensions"])


if __name__ == "__main__":
    unittest.main()
