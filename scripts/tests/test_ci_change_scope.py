"""Tests for conservative CI change-scope classification."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import ci_change_scope  # noqa: E402


class CiChangeScopeTests(unittest.TestCase):
    def test_docs_and_python_scripts_skip_native_builds_and_tool_tests(self):
        scopes = ci_change_scope.classify(["docs/usr/guide.md", "scripts/release.py"])
        self.assertFalse(any(scopes.values()))

    def test_standalone_tool_change_selects_only_tool_tests(self):
        scopes = ci_change_scope.classify(["tools/creative-brain/brain.py"])
        self.assertTrue(scopes["tools"])
        self.assertEqual(1, sum(scopes.values()))

    def test_workflow_change_runs_builds_and_tool_tests(self):
        scopes = ci_change_scope.classify([".github/workflows/ci.yml"])
        self.assertTrue(all(scopes.values()))

    def test_android_only_change_selects_android(self):
        scopes = ci_change_scope.classify(["platform/android/apk/build.gradle.kts"])
        self.assertTrue(scopes["android"])
        self.assertEqual(1, sum(scopes.values()))

    def test_webgpu_change_selects_webgpu(self):
        scopes = ci_change_scope.classify(["src/modules/graphics/WebGPUDevice.cpp"])
        self.assertTrue(scopes["webgpu"])

    def test_native_source_change_is_conservatively_full(self):
        scopes = ci_change_scope.classify(["src/modules/scene/Scene.cpp"])
        self.assertTrue(all(scopes[scope] for scope in ci_change_scope.BUILD_SCOPES))
        self.assertFalse(scopes["tools"])

    def test_cmake_lists_is_never_treated_as_plain_text(self):
        scopes = ci_change_scope.classify(["CMakeLists.txt"])
        self.assertTrue(all(scopes[scope] for scope in ci_change_scope.BUILD_SCOPES))
        self.assertFalse(scopes["tools"])

    def test_unknown_path_is_conservatively_full(self):
        scopes = ci_change_scope.classify(["new-build-area/config.toml"])
        self.assertTrue(all(scopes.values()))

    def test_non_pr_run_forces_full_matrix(self):
        scopes = ci_change_scope.classify([], force_all=True)
        self.assertTrue(all(scopes.values()))

    def test_changed_zeroerr_sources_select_their_source_labels(self):
        mode, label = ci_change_scope.classify_tests(
            ["test/math.cpp", "test/procgen.cpp"]
        )
        self.assertEqual("selected", mode)
        self.assertEqual(r"^source:(math[.]cpp|procgen[.]cpp)$", label)

    def test_production_change_runs_full_test_suite(self):
        mode, label = ci_change_scope.classify_tests(
            ["src/modules/scene/Scene.cpp"]
        )
        self.assertEqual("full", mode)
        self.assertEqual("", label)

    def test_test_infrastructure_change_runs_full_test_suite(self):
        mode, label = ci_change_scope.classify_tests(["test/CMakeLists.txt"])
        self.assertEqual("full", mode)
        self.assertEqual("", label)

    def test_deleted_test_source_runs_full_remaining_suite(self):
        mode, label = ci_change_scope.classify_tests(["test/not_present.cpp"])
        self.assertEqual("full", mode)
        self.assertEqual("", label)

    def test_docs_only_change_has_no_native_tests(self):
        mode, label = ci_change_scope.classify_tests(["docs/usr/guide.md"])
        self.assertEqual("none", mode)
        self.assertEqual("", label)

    def test_forced_run_uses_full_test_suite(self):
        mode, label = ci_change_scope.classify_tests([], force_all=True)
        self.assertEqual("full", mode)
        self.assertEqual("", label)


if __name__ == "__main__":
    unittest.main()
