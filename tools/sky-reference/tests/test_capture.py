import copy
import json
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))
from capture_spec import validate, camera_basis
from capture import validate_result
import numpy as np
from PIL import Image


class CaptureSpecTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads((Path(__file__).parents[1] / "scenarios.json").read_text(encoding="utf-8"))

    def test_repository_scenarios_are_valid(self):
        self.assertEqual(validate(self.spec), self.spec)

    def test_unknown_version_and_fields_are_rejected(self):
        for field, value in (("schema", "eve.sky-reference-capture/2"), ("future", True)):
            spec = {**self.spec, field: value}
            with self.assertRaises(ValueError):
                validate(spec)

    def test_nonfinite_values_and_boolean_dimensions_are_rejected(self):
        for value in (float("nan"), float("inf"), True):
            spec = copy.deepcopy(self.spec)
            spec["scenarios"][0]["timeOfDay"] = value
            with self.assertRaises(ValueError):
                validate(spec)
        self.spec["size"][0] = True
        with self.assertRaises(ValueError):
            validate(self.spec)

    def test_duplicate_and_path_traversal_scenarios_are_rejected(self):
        self.spec["scenarios"][1]["name"] = self.spec["scenarios"][0]["name"]
        with self.assertRaises(ValueError):
            validate(self.spec)
        self.spec["scenarios"][1]["name"] = "../escape"
        with self.assertRaises(ValueError):
            validate(self.spec)


class CapturePublicationTests(unittest.TestCase):
    def stage(self, root, repeat_delta=0):
        spec = json.loads((Path(__file__).parents[1] / "scenarios.json").read_text(encoding="utf-8"))
        spec["scenarios"] = spec["scenarios"][:1]
        spec["size"] = [64, 64]
        payload = root / "payload"
        payload.mkdir()
        Image.fromarray(np.full((64, 64, 3), 90, dtype=np.uint8)).save(payload / "clear-noon.png")
        Image.fromarray(np.full((64, 64, 3), 90 + repeat_delta, dtype=np.uint8)).save(payload / "clear-noon-repeat.png")
        (payload / "clear-noon.actor.t3d").write_text("Begin Object\nEnd Object\n", encoding="utf-8")
        artifacts = []
        for suffix, kind in ((".png", "reference"), ("-repeat.png", "repeat"), (".actor.t3d", "actor")):
            name = "clear-noon" + suffix
            data = (payload / name).read_bytes()
            artifacts.append({"file": name, "kind": kind, "byteLength": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        forward, up = camera_basis(spec["camera"]["rotationDegrees"])
        resolved = {**spec["camera"], "forward": forward, "up": up}
        result = {"schema": "eve.sky-reference-result/2", "status": "captured", "request": spec,
                  "resolvedCamera": resolved, "artifacts": artifacts}
        (root / "result.json").write_text(json.dumps(result), encoding="utf-8")
        return spec

    def test_valid_capture_produces_a_validated_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            validate_result(root, self.stage(root))
            manifest = json.loads((root / "payload/manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "validated")

    def test_nonrepeatable_capture_does_not_publish_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "not pixel-repeatable"):
                validate_result(root, self.stage(root, repeat_delta=1))
            self.assertFalse((root / "payload/manifest.json").exists())

    def test_rolled_camera_cannot_masquerade_as_requested_pitch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spec = self.stage(root)
            result = json.loads((root / "result.json").read_text(encoding="utf-8"))
            result["resolvedCamera"]["forward"], result["resolvedCamera"]["up"] = camera_basis([0, 0, 15])
            (root / "result.json").write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "resolved camera mismatch"):
                validate_result(root, spec)
            self.assertFalse((root / "payload/manifest.json").exists())

    def test_tampered_capture_is_rejected_before_comparison(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spec = self.stage(root)
            (root / "payload/clear-noon.actor.t3d").write_text("changed", encoding="utf-8")
            with self.assertRaises(ValueError):
                validate_result(root, spec)
            self.assertFalse((root / "payload/manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
