"""Fixture tests for the bounded quality-debt metadata gate."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_quality_metadata as quality


FIXTURE = ROOT / "scripts" / "tests" / "fixtures_quality_metadata"


def load_fixture(name: str):
    return json.loads((FIXTURE / name).read_text(encoding="utf-8"))


class QualityMetadataFixtureTest(unittest.TestCase):
    def findings(self):
        return quality.discover_findings(
            FIXTURE, FIXTURE / "scripts" / "check_bindings_gaps.txt"
        )

    def test_fixture_passes_with_exact_baseline(self):
        errors, counts = quality.evaluate(
            load_fixture("metadata.json"),
            load_fixture("baseline.json"),
            self.findings(),
            today=date(2026, 8, 26),
        )
        self.assertEqual(errors, [])
        self.assertEqual(
            counts,
            {
                "fixture-binding": 1,
                "fixture-fallback": 1,
                "fixture-soft-skip": 1,
                "fixture-todo": 1,
            },
        )

    def test_required_metadata_field_is_enforced(self):
        metadata = load_fixture("metadata.json")
        del metadata["entries"][0]["owner"]
        errors = quality.validate_metadata(metadata, today=date(2026, 8, 26))
        self.assertTrue(any("owner" in error for error in errors))

    def test_net_growth_is_rejected(self):
        baseline = load_fixture("baseline.json")
        baseline["counts"]["fixture-todo"] = 0
        errors, _ = quality.evaluate(
            load_fixture("metadata.json"), baseline, self.findings(), today=date(2026, 8, 26)
        )
        self.assertTrue(any("fixture-todo" in error and "net growth" in error for error in errors))

    def test_unmatched_kind_is_rejected(self):
        metadata = load_fixture("metadata.json")
        metadata["entries"] = [
            entry for entry in metadata["entries"] if entry["kind"] != "fallback"
        ]
        errors, _ = quality.evaluate(
            metadata, load_fixture("baseline.json"), self.findings(), today=date(2026, 8, 26)
        )
        self.assertTrue(any("unallowlisted fallback" in error for error in errors))

    def test_expired_metadata_is_rejected(self):
        metadata = copy.deepcopy(load_fixture("metadata.json"))
        metadata["entries"][0]["expiry"] = "2026-08-25"
        errors = quality.validate_metadata(metadata, today=date(2026, 8, 26))
        self.assertTrue(any("expiry has passed" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
