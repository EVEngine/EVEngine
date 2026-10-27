"""Regression evidence for selective CI scheduling; no native builds required."""
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ci_change_scope as scope
import run_ci_tests as runner


class SelectivePlanTests(unittest.TestCase):
    def test_docs_do_not_schedule_native_or_script_tests(self):
        self.assertEqual(("none", False, []), scope.source_plan(["docs/usr/guide.md"]))
        self.assertFalse(any(scope.classify(["docs/usr/guide.md"]).values()))

    def test_python_only_runs_script_checks(self):
        self.assertEqual(("scripts", True, []), scope.source_plan(["scripts/release.py"]))
        self.assertEqual(("none", ""), scope.classify_tests(["scripts/release.py"]))
        self.assertFalse(any(scope.classify(["scripts/release.py"]).values()))

    def test_example_only_builds_linux_without_unit_tests(self):
        paths = ["examples/basic/main.nut"]
        self.assertEqual(("examples", False, ["basic"]), scope.source_plan(paths))
        self.assertEqual(["linux"], [name for name, enabled in scope.classify(paths).items() if enabled])
        self.assertEqual(("none", ""), scope.classify_tests(paths))

    def test_module_changes_use_existing_impact_index(self):
        self.assertEqual(("affected", ""), scope.classify_tests(["src/modules/scene/Scene.cpp"]))
        self.assertEqual(("full", False, []), scope.source_plan(["src/modules/scene/Scene.cpp"]))

    def test_shared_core_and_build_changes_still_run_full_tests(self):
        for path in ["src/engine/common/Value.h", "cmake/modules.cmake", "CMakeLists.txt"]:
            self.assertEqual(("full", ""), scope.classify_tests([path]))

    def test_release_still_runs_full_checks(self):
        self.assertTrue(all(scope.classify([], force_all=True).values()))
        self.assertEqual(("full", ""), scope.classify_tests([], force_all=True))
        self.assertEqual(("full", True, []), scope.source_plan([], force_all=True))

    def test_dispatch_passes_exact_changed_paths_to_impact_tool(self):
        paths = ["src/modules/scene/Scene.cpp"]
        with patch.dict(os.environ, {"CI_TEST_MODE": "affected", "CI_NATIVE_PATHS": json.dumps(paths)}), \
             patch.object(sys, "argv", ["runner", "--platform", "linux-debug"]), \
             patch.object(runner.subprocess, "run") as execute:
            execute.return_value.returncode = 0
            self.assertEqual(runner.main(), 0)
        command = execute.call_args.args[0]
        self.assertIn("test_impact.py", command[3])
        self.assertEqual(command[-2:], ["--changed", paths[0]])

    def test_dispatch_none_never_invokes_native_tests(self):
        with patch.dict(os.environ, {"CI_TEST_MODE": "none"}), \
             patch.object(sys, "argv", ["runner", "--platform", "linux-debug"]), \
             patch.object(runner.subprocess, "run") as execute:
            self.assertEqual(runner.main(), 0)
        execute.assert_not_called()
