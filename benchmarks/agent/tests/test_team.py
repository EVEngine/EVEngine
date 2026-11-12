"""Exercise real concurrent adapter processes and Git merges without paid models."""

from __future__ import annotations

import argparse
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from common import read_json, write_json  # noqa: E402
from team import compare  # noqa: E402
from team_project import (  # noqa: E402
    create_project,
    initial_files,
    integration_checks,
    reference_edit,
    reference_integrate,
    reference_plan,
    requirement_checks,
    spec_for,
    validate_plan,
)


class TeamProjectTests(unittest.TestCase):
    def test_reference_plans_have_expected_parallelism(self):
        for scenario, width, depth in (
            ("independent-levels", 3, 1),
            ("shared-catalog", 3, 1),
            ("dependency-chain", 1, 4),
        ):
            spec = spec_for(scenario, 17)
            result = validate_plan(reference_plan(spec), spec)
            self.assertTrue(result["valid"], result)
            self.assertEqual(result["maxRunnableWidth"], width)
            self.assertEqual(result["criticalPathTaskCount"], depth)
        self.assertEqual(
            len(
                validate_plan(
                    reference_plan(spec_for("shared-catalog", 17)), spec_for("shared-catalog", 17)
                )["writeOverlapPairs"]
            ),
            3,
        )

    def test_missing_ownership_duplicate_and_cycles_rejected(self):
        spec = spec_for("dependency-chain", 17)
        for mutation in ("missing", "duplicate", "cycle", "missing-edge", "escape"):
            plan = reference_plan(spec)
            if mutation == "missing":
                plan["tasks"].pop()
            elif mutation == "duplicate":
                plan["tasks"][1]["requirements"].append("level-village")
            elif mutation == "cycle":
                plan["tasks"][0]["dependsOn"] = ["mine"]
            elif mutation == "missing-edge":
                plan["tasks"][1]["dependsOn"] = []
            else:
                plan["tasks"][0]["allowedFiles"].append("../baseline/catalog.json")
            with self.subTest(mutation=mutation):
                self.assertFalse(validate_plan(plan, spec)["valid"])

    def test_transitive_handoff_allowed(self):
        spec = spec_for("dependency-chain", 17)
        plan = reference_plan(spec)
        plan["tasks"][-1]["dependsOn"] = ["mine"]
        self.assertTrue(validate_plan(plan, spec)["valid"])

    def test_clean_json_can_fail_semantic_integration(self):
        spec = spec_for("dependency-chain", 17)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            create_project(root, spec)
            reference_edit(root, spec, ["level-village", "level-forest", "level-mine"])
            self.assertTrue(requirement_checks(root, spec, ["world-links"])[0]["passed"])
            checks = integration_checks(root, spec)
            self.assertFalse(next(c for c in checks if c["id"] == "connected-campaign")["passed"])
            reference_integrate(root, spec)
            self.assertTrue(all(c["passed"] for c in integration_checks(root, spec)))

    def test_overwrite_shared_file_cannot_hide_lost_requirement(self):
        spec = spec_for("shared-catalog", 17)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            create_project(root, spec)
            reference_edit(root, spec, [r["id"] for r in spec["requirements"]])
            self.assertTrue(all(c["passed"] for c in integration_checks(root, spec)))
            catalog = read_json(root / "catalog.json")
            del catalog["assets"]["forest.landmark"]
            write_json(root / "catalog.json", catalog)
            self.assertFalse(all(c["passed"] for c in integration_checks(root, spec)))

    def test_fixtures_are_seeded_and_do_not_share_mutable_objects(self):
        a, b = (
            initial_files(spec_for("dependency-chain", 17)),
            initial_files(spec_for("dependency-chain", 42)),
        )
        self.assertNotEqual(a["levels/village.json"]["budget"], b["levels/village.json"]["budget"])
        a["catalog.json"]["assets"].clear()
        self.assertTrue(b["catalog.json"]["assets"])


class TeamWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="eve-team-tests-")
        cls.root = Path(cls.temporary.name)
        cls.reports = {}
        for mode in (
            "reference",
            "noop",
            "no-merge",
            "shared-workspace",
            "bad-plan",
            "leak",
            "drop-change",
        ):
            command = [
                sys.executable,
                str(HERE / "team.py"),
                "run",
                "--adapter",
                str(HERE / f"adapters/team-{mode}.json"),
                "--output",
                str(cls.root / mode),
                "--seeds",
                "17",
                "--repeats",
                "1",
                "--calibration",
            ]
            if mode not in ("reference", "noop", "no-merge"):
                command.extend(["--scenario", "shared-catalog"])
            process = subprocess.run(
                command, capture_output=True, text=True, encoding="utf-8", timeout=40
            )
            if process.returncode not in (0, 1):
                raise AssertionError(process.stdout + process.stderr)
            cls.reports[mode] = read_json(cls.root / mode / "report.json")
        command = [
            sys.executable,
            str(HERE / "team.py"),
            "run",
            "--adapter",
            str(HERE / "adapters/team-reference.json"),
            "--output",
            str(cls.root / "serial"),
            "--scenario",
            "independent-levels",
            "--seeds",
            "17",
            "--repeats",
            "1",
            "--calibration",
            "--schedule",
            "serial",
        ]
        result = subprocess.run(
            command, capture_output=True, text=True, encoding="utf-8", timeout=30
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.reports["serial"] = read_json(cls.root / "serial/report.json")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def case(self, mode, scenario):
        return next(r for r in self.reports[mode]["trials"] if r["scenario"] == scenario)

    def test_all_positive_and_negative_controls(self):
        self.assertEqual(self.reports["reference"]["summary"]["passed"], 3)
        self.assertEqual(self.reports["noop"]["summary"]["passed"], 0)
        self.assertFalse(self.reports["reference"]["summary"]["rankable"])

    def test_real_parallel_processes_and_serial_control(self):
        parallel = self.case("reference", "independent-levels")
        serial = self.case("serial", "independent-levels")
        self.assertGreaterEqual(parallel["isolation"]["peakConcurrentAdapters"], 2)
        self.assertEqual(serial["isolation"]["peakConcurrentAdapters"], 1)
        self.assertTrue(parallel["workspaceProvisioning"]["passed"])
        self.assertEqual(parallel["workerRequirementRetention"], 1)

    def test_actual_git_conflicts_and_resolutions(self):
        row = self.case("reference", "shared-catalog")
        self.assertGreater(row["conflictCount"], 0)
        conflicts = [e for e in row["integration"]["events"] if e["conflictFiles"]]
        self.assertTrue(all(e["resolutionAccepted"] for e in conflicts))
        self.assertEqual(row["status"], "pass")
        self.assertTrue(
            any((self.root / "reference/shared-catalog-s17-r0/workers").rglob("candidate.patch"))
        )

    def test_clean_merge_is_not_semantic_success(self):
        row = self.case("no-merge", "dependency-chain")
        self.assertEqual(row["conflictCount"], 0)
        self.assertTrue(all(w["status"] == "completed" for w in row["workers"].values()))
        self.assertTrue(row["integration"]["semanticRepairRequired"])
        self.assertEqual(row["status"], "fail")

    def test_unresolved_conflict_fails(self):
        row = self.case("no-merge", "shared-catalog")
        self.assertGreater(row["conflictCount"], 0)
        self.assertEqual(row["integration"]["status"], "merge_failed")

    def test_shared_working_files_rejected_before_workers(self):
        row = self.case("shared-workspace", "shared-catalog")
        self.assertEqual(row["status"], "workspace_failed")
        self.assertTrue(any("hardlinked" in e for e in row["workspaceProvisioning"]["errors"]))
        self.assertFalse(row["workers"])

    def test_cross_workspace_contamination_rejected(self):
        row = self.case("leak", "shared-catalog")
        self.assertEqual(row["status"], "scope_violation")
        self.assertTrue(row["isolation"]["violations"])

    def test_invalid_plan_does_not_launch_workers(self):
        row = self.case("bad-plan", "shared-catalog")
        self.assertEqual(row["status"], "invalid_plan")
        self.assertFalse(row["workers"])

    def test_integrator_cannot_silently_drop_worker_change(self):
        row = self.case("drop-change", "shared-catalog")
        self.assertEqual(row["status"], "fail")
        self.assertLess(row["workerRequirementRetention"], 1)

    def test_comparison_pairs_complete_real_trials_and_requires_controls(self):
        a = copy.deepcopy(self.reports["reference"])
        b = copy.deepcopy(a)
        for report in (a, b):
            report["summary"]["rankable"] = True
            report["experiment"]["calibration"] = False
            for adapter in report["experiment"]["adapters"].values():
                adapter.update(id="test-tool", model="test-model-v1", tools="test-tools")
        left, right = self.root / "a.json", self.root / "b.json"
        write_json(left, a)
        write_json(right, b)
        args = argparse.Namespace(
            baseline=left,
            candidate=right,
            output=self.root / "compare.json",
            axis="tools",
            positive_control=self.root / "reference/report.json",
            negative_control=self.root / "noop/report.json",
        )
        self.assertEqual(compare(args), 0)
        self.assertEqual(len(read_json(args.output)["pairs"]), 3)
        b["experiment"]["identity"]["workers"] = 1
        write_json(right, b)
        with self.assertRaisesRegex(ValueError, "identity"):
            compare(args)


if __name__ == "__main__":
    unittest.main()
