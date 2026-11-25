"""Coverage acceptance must reject omissions, aliases, duplicates and drift."""
import copy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_pixelfx_coverage as coverage  # noqa: E402


def reference():
    return {
        "revision": coverage.PINNED_REVISION,
        "effects": [
            {"id": f"family/effect-{i}", "size": [16, 16], "origin": [8, 8], "duration": 1, "loop": True}
            for i in range(244)
        ],
    }


class PixelFxCoverageTests(unittest.TestCase):
    def test_accepts_exact_catalogue(self):
        source = reference()
        report = coverage.assess(source, copy.deepcopy(source))
        self.assertTrue(report["complete"])
        self.assertEqual(report["implemented"], 244)

    def test_partial_catalogue_stays_incomplete(self):
        source = reference()
        native = {"effects": copy.deepcopy(source["effects"][:4])}
        report = coverage.assess(source, native)
        self.assertFalse(report["complete"])
        self.assertEqual(len(report["missing"]), 240)

    def test_duplicate_cannot_replace_missing_effect(self):
        source = reference()
        native = copy.deepcopy(source)
        native["effects"][-1] = copy.deepcopy(native["effects"][0])
        report = coverage.assess(source, native)
        self.assertFalse(report["complete"])
        self.assertEqual(report["duplicateIds"], ["family/effect-0"])

    def test_unknown_id_cannot_replace_missing_effect(self):
        source = reference()
        native = copy.deepcopy(source)
        native["effects"][-1]["id"] = "family/alias"
        report = coverage.assess(source, native)
        self.assertFalse(report["complete"])
        self.assertEqual(report["extra"], ["family/alias"])

    def test_metadata_drift_is_reported(self):
        source = reference()
        native = copy.deepcopy(source)
        native["effects"][0]["origin"] = [0, 0]
        report = coverage.assess(source, native)
        self.assertFalse(report["complete"])
        self.assertEqual(report["metadataMismatches"][0]["field"], "origin")

    def test_wrong_upstream_revision_is_rejected(self):
        source = reference()
        source["revision"] = "other"
        report = coverage.assess(source, copy.deepcopy(source))
        self.assertFalse(report["complete"])
        self.assertFalse(report["referenceValid"])


if __name__ == "__main__":
    unittest.main()
