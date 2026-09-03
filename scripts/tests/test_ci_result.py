import json
import tempfile
import unittest
from pathlib import Path

from scripts.ci_result import CIResult, FailureKind


class CIResultTest(unittest.TestCase):
    def test_failure_has_stable_schema_and_kind(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "nested" / "result.json"
            CIResult.failure(
                FailureKind.PARITY,
                "RENDER_MEAN_RGB_EXCEEDED",
                "scene",
                "mean RGB exceeded contract",
                {"expected": 0.1, "actual": 0.2},
            ).write(path)
            payload = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema"], "evengine.ci-result")
            self.assertEqual(payload["version"], 1)
            self.assertEqual(payload["kind"], "parity")
            self.assertEqual(payload["code"], "RENDER_MEAN_RGB_EXCEEDED")

    def test_success_has_no_failure_classification(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "result.json"
            CIResult.success("render-parity").write(path)
            payload = json.loads(path.read_text(encoding="utf-8"))
            self.assertTrue(payload["ok"])
            self.assertIsNone(payload["kind"])
            self.assertIsNone(payload["code"])


if __name__ == "__main__":
    unittest.main()
