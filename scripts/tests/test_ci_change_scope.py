"""Tests for conservative CI change-scope classification."""

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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

    def test_mixed_deleted_and_modified_test_sources_run_full_suite(self):
        mode, label = ci_change_scope.classify_tests(
            ["test/not_present.cpp", "test/math.cpp"]
        )
        self.assertEqual("full", mode)
        self.assertEqual("", label)

    def test_fixture_only_source_uses_fast_lane_label(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "test" / "fixture_only.cpp"
            source.parent.mkdir()
            source.write_text(
                'TEST_CASE_FIXTURE(Fixture, "fixture.case") {}\n',
                encoding="utf-8",
            )
            with mock.patch.object(ci_change_scope, "ROOT", root):
                mode, label = ci_change_scope.classify_tests(
                    ["test/fixture_only.cpp"]
                )
        self.assertEqual("selected", mode)
        self.assertEqual(r"^source:(fixture_only[.]cpp)$", label)

    def test_direct_diff_reports_changes_when_new_head_is_ancestor(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo = Path(temp_dir)
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            subprocess.run(
                ["git", "config", "user.email", "ci@example.invalid"],
                cwd=repo,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.name", "CI Test"], cwd=repo, check=True
            )
            tracked = repo / "platform" / "android" / "setting.txt"
            tracked.parent.mkdir(parents=True)
            tracked.write_text("before\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=repo, check=True)
            subprocess.run(["git", "commit", "-qm", "before"], cwd=repo, check=True)
            older = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=repo,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            ).stdout.strip()
            tracked.write_text("after\n", encoding="utf-8")
            subprocess.run(["git", "commit", "-qam", "after"], cwd=repo, check=True)
            newer = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=repo,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            ).stdout.strip()

            merge_base_paths = ci_change_scope.changed_paths(newer, older, cwd=repo)
            direct_paths = ci_change_scope.changed_paths(
                newer, older, diff_mode="direct", cwd=repo
            )

        self.assertEqual([], merge_base_paths)
        self.assertEqual(["platform/android/setting.txt"], direct_paths)

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
