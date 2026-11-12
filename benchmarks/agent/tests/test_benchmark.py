"""Harness correctness tests; no paid model calls, engine, or network required."""

from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "adapters"))
from common import fingerprint, read_json, run_process, usage_metrics, write_json  # noqa: E402
from grade import Client, RpcFailure, contract_grade, engine_grade, runtime_checks  # noqa: E402
from run import changed_files, compare, load_suite, prepare, summarize  # noqa: E402
from cli import codex_usage  # noqa: E402


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="eve-eval-test-")
        self.root = Path(self.temporary.name)
        self.suite = load_suite(HERE / "suite.json")

    def tearDown(self):
        self.temporary.cleanup()

    def harness(self, adapter, output="run", extra=()):
        return subprocess.run(
            [
                sys.executable,
                str(HERE / "run.py"),
                "run",
                "--adapter",
                str(adapter),
                "--output",
                str(self.root / output),
                "--task",
                "contract-observation",
                "--seeds",
                "17",
                "--repeats",
                "1",
                *extra,
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=20,
        )

    def config(self, code):
        path = self.root / "adapter.json"
        write_json(
            path,
            {
                "schema": "eve.agent-benchmark-adapter/v1",
                "id": "test",
                "command": [sys.executable, "-c", code, "{request}", "{result}"],
            },
        )
        return path

    def test_each_mutation_is_real_and_reproducible(self):
        for i, task in enumerate(self.suite["tasks"]):
            a = prepare(self.suite, task, self.root / f"a{i}")
            b = prepare(self.suite, task, self.root / f"b{i}")
            self.assertEqual(a, b)
            self.assertIn(
                task["mutation"]["new"],
                (self.root / f"a{i}" / task["mutation"]["file"]).read_text(encoding="utf-8"),
            )

    def test_noop_fails_reference_passes(self):
        for adapter, status in (("noop", "fail"), ("reference", "pass")):
            result = self.harness(
                HERE / "adapters" / f"{adapter}.json", adapter, ("--calibration",)
            )
            self.assertEqual(result.returncode, int(status != "pass"), result.stderr)
            report = read_json(self.root / adapter / "report.json")
            self.assertEqual(report["trials"][0]["status"], status)
            self.assertTrue(report["complete"])

    def test_control_cannot_pose_as_model(self):
        result = self.harness(HERE / "adapters/reference.json")
        self.assertEqual(result.returncode, 2)
        self.assertIn("--calibration", result.stderr)

    def test_claimed_success_does_not_pass(self):
        adapter = self.config(
            "import json,sys;json.dump({'schema':'eve.agent-benchmark-adapter-result/v1','success':True},open(sys.argv[2],'w'))"
        )
        result = self.harness(adapter)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(read_json(self.root / "run/report.json")["trials"][0]["status"], "fail")

    def test_out_of_scope_edit_fails(self):
        adapter = self.config(
            "from pathlib import Path;Path('config.nut').write_text('config <- {};',encoding='utf-8')"
        )
        self.harness(adapter)
        row = read_json(self.root / "run/report.json")["trials"][0]
        self.assertEqual(row["status"], "scope_violation")

    def test_added_file_fails(self):
        adapter = self.config(
            "from pathlib import Path;Path('fake-proof.json').write_text('{}',encoding='utf-8')"
        )
        self.harness(adapter)
        self.assertEqual(
            read_json(self.root / "run/report.json")["trials"][0]["status"], "scope_violation"
        )

    def test_bad_usage_is_adapter_error(self):
        adapter = self.config(
            "import json,sys;json.dump({'schema':'eve.agent-benchmark-adapter-result/v1','usage':{'costUsd':-1}},open(sys.argv[2],'w'))"
        )
        self.harness(adapter)
        self.assertEqual(
            read_json(self.root / "run/report.json")["trials"][0]["status"], "adapter_error"
        )

    def test_missing_engine_is_blocked(self):
        result = self.harness(
            HERE / "adapters/noop.json", extra=("--calibration", "--task", "combat-damage")
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        rows = read_json(self.root / "run/report.json")["trials"]
        self.assertEqual(
            next(r for r in rows if r["taskId"] == "combat-damage")["status"], "blocked"
        )

    def test_existing_output_not_overwritten(self):
        output = self.root / "run"
        output.mkdir()
        (output / "keep").write_text("keep", encoding="utf-8")
        result = self.harness(HERE / "adapters/noop.json", extra=("--calibration",))
        self.assertEqual(result.returncode, 2)
        self.assertEqual((output / "keep").read_text(encoding="utf-8"), "keep")

    def test_symlinks_rejected(self):
        if os.name != "posix":
            self.skipTest("POSIX symlink test")
        (self.root / "escape").symlink_to(HERE / "suite.json")
        with self.assertRaisesRegex(ValueError, "symlink"):
            fingerprint(self.root)

    def test_timeout_kills_child(self):
        result = run_process(
            [sys.executable, "-c", "import time;time.sleep(10)"], self.root, 0.05, self.root / "log"
        )
        self.assertTrue(result["timedOut"])
        self.assertIsNone(result["exitCode"])
        self.assertLess(result["wallSeconds"], 3)

    @unittest.skipUnless(os.name == "posix", "process groups require POSIX")
    def test_normal_exit_also_kills_descendants(self):
        script = "import subprocess,sys; p=subprocess.Popen([sys.executable,'-c',\"import time;from pathlib import Path;time.sleep(.4);Path('leaked').write_text('bad')\"]);"
        run_process([sys.executable, "-c", script], self.root, 3, self.root / "log")
        time.sleep(0.5)
        self.assertFalse((self.root / "leaked").exists())

    def test_missing_telemetry_not_zero(self):
        self.assertIsNone(
            usage_metrics({"schema": "eve.agent-benchmark-adapter-result/v1"})["costUsd"]
        )

    def test_nonfinite_and_boolean_usage_rejected(self):
        for value in (True, -1, float("nan"), float("inf"), "12"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                usage_metrics(
                    {"schema": "eve.agent-benchmark-adapter-result/v1", "usage": {"costUsd": value}}
                )

    def test_rpc_and_business_errors_not_success(self):
        for value in ({"isError": True}, {"content": [{"type": "text", "text": '{"ok":false}'}]}):
            client = Client.__new__(Client)
            client.rpc = Mock(return_value=value)
            with self.assertRaises(RpcFailure):
                client.tool("test", {})

    def test_grader_rejects_unrelated_contract_changes(self):
        prepare(self.suite, self.suite["tasks"][0], self.root / "project")
        original = read_json(HERE.parents[1] / "examples/ai-game/game.agent.json")
        candidate = dict(original, entry="other.nut")
        write_json(self.root / "project/game.agent.json", candidate)
        self.assertFalse(contract_grade(self.root / "project", original)[0]["passed"])

    def test_deleted_file_is_a_change(self):
        self.assertEqual(changed_files({"a": "hash"}, {}), ["a"])

    def test_summary_includes_failures_in_cost_and_time(self):
        rows = [
            {
                "taskId": "a",
                "status": status,
                "agent": {"wallSeconds": seconds},
                "usage": {"costUsd": cost},
            }
            for status, seconds, cost in (("pass", 2, 0.1), ("timeout", 10, 0.2))
        ]
        summary = summarize(rows)
        self.assertEqual(summary["macroSuccessRate"], 0.5)
        self.assertEqual(summary["agentSecondsPerSuccess"], 12)
        self.assertAlmostEqual(summary["usage"]["costUsd"]["total"], 0.3)
        self.assertIsNone(summary["usage"]["inputTokens"]["total"])
        self.assertTrue(summary["rankable"])

    def test_codex_usage_sums_turns(self):
        events = [
            {"type": "turn.completed", "usage": {"input_tokens": 12, "output_tokens": 3}},
            {"type": "turn.completed", "usage": {"input_tokens": 9, "output_tokens": 2}},
            {"type": "item.completed", "item": {"type": "command_execution"}},
        ]
        self.assertEqual(codex_usage(events)["inputTokens"], 21)
        self.assertEqual(codex_usage(events)["toolCalls"], 1)
        self.assertIsNone(codex_usage([])["inputTokens"])

    def test_provider_denial_is_blocked_not_model_failure(self):
        adapter = self.config(
            "import json,sys;json.dump({'schema':'eve.agent-benchmark-adapter-result/v1','failureKind':'provider_policy_denied'},open(sys.argv[2],'w'))"
        )
        self.harness(adapter)
        report = read_json(self.root / "run/report.json")
        self.assertEqual(report["trials"][0]["status"], "blocked")
        self.assertFalse(report["summary"]["rankable"])

    @unittest.skipUnless(os.name == "posix", "shell fixture requires POSIX")
    def test_engine_candidate_load_failure_is_failed_task(self):
        project = self.root / "project"
        prepare(self.suite, self.suite["tasks"][1], project)
        engine = self.root / "broken-engine"
        engine.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        engine.chmod(0o700)
        checks = engine_grade(project, "repair", 17, engine, self.root)
        self.assertEqual(checks[0]["id"], "engine-loads-candidate")
        self.assertFalse(checks[0]["passed"])

    def test_runtime_oracle_accepts_trace_and_rejects_healing(self):
        start = {
            "tick": 0,
            "time": 0.0,
            "hits": 0,
            "seed": 17,
            "player.hp": 100.0,
            "player.maxHp": 100.0,
            "player.attack": 14.0,
            "enemy.hp": 80.0,
            "enemy.maxHp": 80.0,
            "enemy.attack": 9.0,
        }
        for broken in (False, True):
            trace = [
                dict(
                    start,
                    tick=i * 8,
                    time=i * 0.8,
                    hits=i,
                    **{"enemy.hp": max(0.0, 80.0 + (14.0 if broken else -14.0) * i)},
                )
                for i in range(1, 7)
            ]
            if broken:
                trace.append(dict(trace[-1], time=6.4, tick=64))
            client = Mock()
            client.play.return_value = {"json": "checkpoint"}
            client.observe.side_effect = [start, *trace, trace[-1], trace[-1], start]
            checks = runtime_checks(client, "repair", 17)
            self.assertEqual(all(c["passed"] for c in checks), not broken)
            if broken:
                self.assertFalse(next(c for c in checks if c["id"] == "damage-per-hit")["passed"])

    def test_pinned_input_drift_rejected(self):
        self.suite["inputTextSha256"]["examples/ai-game/main.nut"] = "0" * 64
        path = self.root / "drift.json"
        write_json(path, self.suite)
        with self.assertRaisesRegex(ValueError, "pinned input drift"):
            load_suite(path)

    def test_invalid_candidate_json_is_failure(self):
        project = self.root / "project"
        project.mkdir()
        (project / "game.agent.json").write_text("not json", encoding="utf-8")
        self.assertFalse(contract_grade(project, {})[0]["passed"])

    def test_paired_comparison_requires_working_controls(self):
        for adapter in ("noop", "reference"):
            self.harness(HERE / "adapters" / f"{adapter}.json", adapter, ("--calibration",))
        baseline = read_json(self.root / "noop/report.json")
        candidate = read_json(self.root / "reference/report.json")
        for report in (baseline, candidate):
            report["experiment"]["calibration"] = False
            report["experiment"]["adapter"] = {"id": "test", "model": "test-v1", "tools": "test"}
        a, b = self.root / "a.json", self.root / "b.json"
        write_json(a, baseline)
        write_json(b, candidate)
        args = argparse.Namespace(
            baseline=a,
            candidate=b,
            output=self.root / "comparison.json",
            positive_control=self.root / "reference/report.json",
            negative_control=self.root / "noop/report.json",
        )
        with redirect_stdout(io.StringIO()):
            self.assertEqual(compare(args), 0)
        self.assertEqual(read_json(args.output)["successRateDelta"], 1)
        self.assertEqual(read_json(args.output)["candidateWins"], 1)
        args.negative_control = args.positive_control
        with self.assertRaisesRegex(ValueError, "control provenance"):
            compare(args)

    def test_compare_rejects_different_inputs_and_calibration(self):
        base = {
            "schema": "eve.agent-benchmark-report/v1",
            "complete": True,
            "summary": {"rankable": True},
            "experiment": {"identity": "a", "calibration": False},
        }
        a, b = self.root / "a.json", self.root / "b.json"
        write_json(a, base)
        altered = dict(base, experiment={"identity": "b", "calibration": False})
        write_json(b, altered)
        args = argparse.Namespace(baseline=a, candidate=b, output=self.root / "comparison.json")
        with self.assertRaisesRegex(ValueError, "identity"):
            compare(args)
        altered["experiment"]["calibration"] = True
        write_json(b, altered)
        with self.assertRaisesRegex(ValueError, "calibration"):
            compare(args)


if __name__ == "__main__":
    unittest.main()
