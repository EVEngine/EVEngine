"""Failure propagation and concurrency contract for the source gate runner."""
import contextlib
import io
import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_source_quality as runner


class RunnerTests(unittest.TestCase):
    def test_failure_does_not_skip_other_checks(self):
        commands = [("failure", [sys.executable, "-c", "raise SystemExit(3)"]),
                    ("success", [sys.executable, "-c", "print('completed sibling')"])]
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(runner.run_commands(commands, 2), 1)
        self.assertIn("completed sibling", output.getvalue())
        self.assertIn("exit 3", output.getvalue())

    def test_all_success(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.run_commands([("ok", [sys.executable, "-c", "pass"])], 1), 0)

    def test_script_mode_has_no_native_source_gates(self):
        with patch.dict(os.environ, {"SOURCE_QUALITY_MODE": "scripts"}), \
             patch.object(sys, "argv", ["runner"]), \
             patch.object(runner, "run_commands", return_value=0) as execute:
            self.assertEqual(runner.main(), 0)
        names = [name for name, _ in execute.call_args.args[0]]
        self.assertIn("check/scripts", names)
        self.assertIn("test_source_quality_runner", names)
        self.assertNotIn("check/bindings", names)

    def test_commands_come_from_make_inventory(self):
        with patch.dict(os.environ, {"SOURCE_QUALITY_MODE": "full", "SOURCE_QUALITY_SCRIPT_TESTS": "true"}), \
             patch.object(sys, "argv", ["runner", "--base", "fixture-base"]), \
             patch.object(runner.subprocess, "check_output", return_value="check/custom\n"), \
             patch.object(runner, "run_commands", return_value=0) as execute:
            self.assertEqual(runner.main(), 0)
        commands, jobs = execute.call_args.args
        self.assertEqual(commands[0][0], "check/custom")
        self.assertEqual(commands[0][1][-2:], ["check/custom", "CI_BASE=fixture-base"])
        self.assertTrue(any(name == "test_source_quality_runner" for name, _ in commands))
        self.assertGreater(jobs, 0)

    def test_inherited_lookup_reuses_masked_class_bodies(self):
        import generate_binding_contracts as bindings
        sources = {Path("fixture.h"): "struct Base { void inherited(int count); }; struct Child {};"}
        index = bindings.SignatureIndex(sources)
        with patch.object(bindings, "mask_comments", side_effect=AssertionError("repeated scan")):
            signature = index.member("Child", "inherited")
            self.assertIsNotNone(signature)
            self.assertEqual(signature[1][0].name, "count")
            self.assertIsNone(index.member("Child", "absent"))
