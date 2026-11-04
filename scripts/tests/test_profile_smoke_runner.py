"""Profile admission must execute every required smoke, including playback."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import profile_matrix  # noqa: E402


class ProfileSmokeRunnerTests(unittest.TestCase):
    def make_binary(self, directory, name):
        path = directory / name
        path.write_text("fixture", encoding="utf-8")
        path.chmod(0o755)
        return path

    def test_missing_playback_or_capability_is_not_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "physics-bake-only"
            directory.mkdir()
            self.make_binary(directory, "eve_capability_present_check")
            absent = self.make_binary(directory, "eve_capability_absent_check")
            args = argparse.Namespace(build_root=temporary)
            with mock.patch.object(profile_matrix, "run_command") as run:
                with self.assertRaisesRegex(RuntimeError, "eve_bake_playback_check"):
                    profile_matrix.run_smoke(args, "physics-bake-only")
                run.assert_not_called()
                self.make_binary(directory, "eve_bake_playback_check")
                absent.unlink()
                with self.assertRaisesRegex(RuntimeError, "eve_capability_absent_check"):
                    profile_matrix.run_smoke(args, "physics-bake-only")
                run.assert_not_called()

    def test_script_profile_requires_and_runs_vm_probe(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = "physics-bake-script-only"
            directory = Path(temporary) / profile
            directory.mkdir()
            names = ["eve_capability_present_check", "eve_capability_absent_check", "eve_bake_playback_check"]
            for name in names:
                self.make_binary(directory, name)
            args = argparse.Namespace(build_root=temporary)
            with mock.patch.object(profile_matrix, "run_command") as run:
                with self.assertRaisesRegex(RuntimeError, "eve_bake_script_check"):
                    profile_matrix.run_smoke(args, profile)
                run.assert_not_called()
                self.make_binary(directory, "eve_bake_script_check")
                with mock.patch.object(os, "access", return_value=True):
                    profile_matrix.run_smoke(args, profile)
                self.assertEqual([Path(call.args[0][0]).name for call in run.call_args_list],
                                 names + ["eve_bake_script_check"])

    def test_render_profile_runs_process_isolated_ctest(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = "physics-bake-render"
            directory = Path(temporary) / profile
            directory.mkdir()
            for name in ("eve_capability_present_check", "eve_capability_absent_check"):
                self.make_binary(directory, name)
            args = argparse.Namespace(build_root=temporary)
            with mock.patch.object(profile_matrix, "run_command") as run:
                with mock.patch.object(os, "access", return_value=True):
                    profile_matrix.run_smoke(args, profile)
                self.assertEqual(run.call_args_list[-1].args[0],
                                 ["ctest", "--test-dir", str(directory), "--no-tests=error",
                                  "--output-on-failure", "-R", "^bake.render."])

    def test_exe_playback_failure_propagates(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "physics-bake-only"
            directory.mkdir()
            for name in ("eve_capability_present_check", "eve_capability_absent_check", "eve_bake_playback_check"):
                self.make_binary(directory, name + ".exe")
            args = argparse.Namespace(build_root=temporary)
            seen = []

            def execute(command, cwd):
                seen.append(Path(command[0]).name)
                if seen[-1] == "eve_bake_playback_check.exe":
                    raise subprocess.CalledProcessError(1, command)

            with mock.patch.object(profile_matrix, "run_command", side_effect=execute):
                with mock.patch.object(os, "access", return_value=True):
                    with self.assertRaises(subprocess.CalledProcessError):
                        profile_matrix.run_smoke(args, "physics-bake-only")
            self.assertEqual(seen, ["eve_capability_present_check.exe", "eve_capability_absent_check.exe",
                                    "eve_bake_playback_check.exe"])
