"""Fixture tests for the executable top-level architecture gates."""

from __future__ import annotations

import copy
import sys
import unittest
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_architecture_contracts as contracts  # noqa: E402


class ArchitectureContractTests(unittest.TestCase):
    def test_repository_catalogue_covers_all_ten_rules(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        self.assertEqual([], contracts.validate_catalogue(metadata, today=date(2026, 8, 26)))

    def test_missing_required_contract_field_is_rejected(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        reduced = copy.deepcopy(metadata)
        entry = next(item for item in reduced["entries"] if item["rule"] == "link")
        del entry["stale"]
        errors = contracts.validate_catalogue(reduced, today=date(2026, 8, 26))
        self.assertTrue(any("stale" in error for error in errors))

    def test_valid_api_fixture_has_no_shape_findings(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/valid_api.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        self.assertEqual([], contracts.lint_api_shapes(lines))

    def test_invalid_api_fixture_reports_all_high_signal_shapes(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/invalid_api.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        findings = contracts.lint_api_shapes(lines)
        codes = {finding.code for finding in findings}
        self.assertIn("ambiguous-operation-bool", codes)
        self.assertIn("last-error-channel", codes)
        self.assertIn("undocumented-raw-pointer-api", codes)

    def test_new_link_and_system_need_catalogue_coverage(self):
        metadata = {
            "entries": [
                {
                    "rule": "link",
                    "scope": "src/modules/test/Link.h",
                }
            ]
        }
        lines = [
            contracts.SourceLine("src/modules/test/OtherLink.h", 3, "struct OtherLink {};"),
            contracts.SourceLine("src/modules/test/NewSystem.h", 4, "class NewSystem {};"),
        ]
        findings = contracts.lint_contract_coverage(lines, metadata)
        self.assertEqual(2, len(findings))
        self.assertTrue(all(finding.code == "missing-contract-entry" for finding in findings))

    def test_catalogue_entry_covers_a_known_link(self):
        metadata = {
            "entries": [
                {
                    "rule": "link",
                    "scope": "src/modules/test/Link.h",
                }
            ]
        }
        lines = [contracts.SourceLine("src/modules/test/Link.h", 3, "struct TestLink {};" )]
        self.assertEqual([], contracts.lint_contract_coverage(lines, metadata))


if __name__ == "__main__":
    unittest.main()
