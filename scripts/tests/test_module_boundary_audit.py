"""Evidence grading for the report-only module boundary audit."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import module_boundary_audit as audit  # noqa: E402


class BoundaryAuditTests(unittest.TestCase):
    def test_audit_scans_the_same_composed_manifest_as_the_build_gate(self):
        expected = {entry.name for entry in audit.check_module_manifest.parse_manifest()}
        actual = {module.name for module in audit.parse_manifests()}
        self.assertEqual(expected, actual)
        self.assertIn("graphics_raytracing", actual)

    def test_source_contracts_do_not_require_retired_catalogue(self):
        module = audit.Module("probe", root=audit.REPO / "src")
        module.links = ["probe.cpp:1 registerLinkKind"]
        module.ecs = ["probe.cpp:2 ProbeSystemContract"]
        module.optional_deps = ["audio"]
        findings = dict(audit.findings(module, {}))
        self.assertEqual({"E3-LINK", "E3-ECS", "E2-OPT"}, set(findings))
        self.assertNotIn("catalogue", " ".join(findings.values()))
        self.assertEqual("UNVERIFIED", audit.finding_status("E2-OPT"))
        self.assertEqual("CANDIDATE", audit.finding_status("E3-LINK"))
        self.assertEqual("CANDIDATE", audit.finding_status("E3-ECS"))

    def test_missing_scan_edge_is_not_proof_of_dead_or_absent_capability(self):
        module = audit.Module("probe", root=audit.REPO / "src")
        module.declares["IPlugin"] = ["probe.h:1"]
        module.provides["IPlugin"] = ["probe.cpp:2"]
        matrix = {"IPlugin": {"provided_by": [], "consumed_by": [], "declared_by": ["probe"]}}
        findings = dict(audit.findings(module, matrix))
        for check_id in ("E2-ORPHAN", "E1-DEAD"):
            self.assertIn(check_id, findings)
            self.assertEqual("CANDIDATE", audit.finding_status(check_id))
        self.assertNotIn("恒缺席", findings["E2-ORPHAN"])

    def test_unresolved_source_root_is_a_mechanically_confirmed_failure(self):
        self.assertEqual([("B0", "manifest 声明了模块，但找不到对应源码目录")],
                         audit.findings(audit.Module("missing"), {}))
        self.assertEqual("FAIL", audit.finding_status("B0"))


if __name__ == "__main__":
    unittest.main()
