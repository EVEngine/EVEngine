"""Exercise refusal of incomplete/stale reference evidence and malformed fixtures."""

import argparse
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import road_parity  # noqa: E402


class RoadParityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.case = self.root / "fixtures" / "straight"
        self.case.mkdir(parents=True)
        self.path = self.case / "fixture.json"
        self.fixture = {
            "schema": "eve.tests.road-parity", "schemaVersion": 1,
            "scope": "surface-only", "units": "metres", "axis": "Y-up",
            "caseId": "straight", "width": 32, "height": 32,
            "mesh": {"positions": [0, 0, 0, 1, 0, 0, 0, 0, 1],
                     "normals": [0, 1, 0] * 3, "uvs": [0, 0] * 3, "indices": [0, 1, 2]},
        }
        self.save()

    def save(self):
        self.path.write_text(json.dumps(self.fixture), encoding="utf-8")

    def evidence(self, side):
        # Synthetic pixels are only a metric oracle in this isolated unit test.
        for view in ("top", "perspective"):
            image = Image.new("RGB", (32, 32), (32, 32, 32))
            for x in range(8, 24):
                for y in range(8, 24):
                    image.putpixel((x, y), (255, 0, 255))
            image.save(self.case / f"{side}-{view}.png")
        record = {
            "fixtureSha256": road_parity.digest(self.path),
            "images": {view: road_parity.digest(self.case / f"{side}-{view}.png")
                       for view in ("top", "perspective")},
        }
        if side == "ev":
            record.update(road_parity.ev_adapter_hashes())
            for view in ("perspective", "top"):
                game = self.root / "games" / f"straight-{view}"
                game.mkdir(parents=True, exist_ok=True)
                for name in ("main.nut", "config.nut", "mesh_data.nut"):
                    if not (game / name).is_file():
                        (game / name).write_text("synthetic test input", encoding="utf-8")
            record["gameInputSha256"] = road_parity.ev_game_hashes(self.root, "straight")
        road_parity.write_json(self.case / f"{side}-evidence.json", record)

    def test_changed_ev_adapter_rejects_matching_fixture_and_images(self):
        self.evidence("ev")
        self.evidence("unity")
        self.assertEqual(self.compare(), 0)
        evidence = self.case / "ev-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record["renderAdapterSha256"] = "0" * 64
        road_parity.write_json(evidence, record)
        with self.assertRaisesRegex(ValueError, "Stale EVEngine.*adapter"):
            self.compare()

    def test_changed_shared_renderer_dll_rejects_version_two_evidence(self):
        self.evidence("ev")
        self.evidence("unity")
        runtime = self.root / "runtime"
        executable = runtime / "src/engine/eve.exe"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"synthetic executable")
        library = runtime / "EVWorld.dll"
        library.write_bytes(b"synthetic renderer version one")
        evidence = self.case / "ev-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record.update(schemaVersion=2, rendererExecutable=str(executable),
                      rendererRuntimeSha256=road_parity.renderer_runtime_hashes(executable))
        road_parity.write_json(evidence, record)
        self.assertEqual(self.compare(), 0)
        library.write_bytes(b"synthetic renderer version two")
        with self.assertRaisesRegex(ValueError, "Changed EVEngine runtime dependency"):
            self.compare()

    def test_unknown_renderer_evidence_version_is_rejected(self):
        self.evidence("ev")
        self.evidence("unity")
        evidence = self.case / "ev-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record["schemaVersion"] = 3
        road_parity.write_json(evidence, record)
        with self.assertRaisesRegex(ValueError, "Unsupported EVEngine evidence version"):
            self.compare()

    def test_missing_ev_game_input_cannot_skip_provenance_validation(self):
        self.evidence("ev")
        self.evidence("unity")
        self.assertEqual(self.compare(), 0)
        (self.root / "games/straight-top/main.nut").unlink()
        with self.assertRaisesRegex(ValueError, "Missing EVEngine game input"):
            self.compare()

    def test_changed_ev_game_input_rejects_matching_fixture_and_images(self):
        for view in ("perspective", "top"):
            game = self.root / "games" / f"straight-{view}"
            game.mkdir(parents=True)
            for name in ("main.nut", "config.nut", "mesh_data.nut"):
                (game / name).write_text("prepared input", encoding="utf-8")
        self.evidence("ev")
        self.evidence("unity")
        self.assertEqual(self.compare(), 0)
        (self.root / "games/straight-top/mesh_data.nut").write_text("changed geometry", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Stale EVEngine game input"):
            self.compare()

    def compare(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return road_parity.compare(argparse.Namespace(root=self.root, minimum_iou=0.98))

    def test_missing_reference_cannot_pass(self):
        self.evidence("ev")
        self.assertEqual(self.compare(), 2)
        result = json.loads((self.root / "comparison.json").read_text(encoding="utf-8"))
        self.assertEqual(result["cases"][0]["status"], "blocked")
        self.assertEqual(result["cases"][0]["views"], [])

    def test_unity_coordinates_reflect_handedness_without_changing_height(self):
        self.assertEqual(road_parity.unity_vector([3, 4, 5]), {"x": 3, "y": 4, "z": -5})
        self.assertEqual(road_parity.unity_vector([0, 65, 0]), {"x": 0, "y": 65, "z": 0})

    def test_zero_bumpiness_calibration_rejects_a_string_boolean(self):
        for key in ("referenceZeroBumpiness", "referenceCreatedDefaults"):
            self.fixture["roads"] = [{key: "false"}]
            self.save()
            with self.assertRaisesRegex(ValueError, f"{key}.*boolean"):
                road_parity.load_fixture(self.path)
            self.fixture["roads"][0][key] = True
            self.save()
            self.assertIs(road_parity.load_fixture(self.path)["roads"][0][key], True)

    def test_changed_unity_projection_rejects_reference_even_when_core_fixture_is_unchanged(self):
        self.evidence("ev")
        self.evidence("unity")
        projection = self.case / "unity-fixture.json"
        road_parity.write_json(projection, {"coordinates": "reflected-Z"})
        evidence = self.case / "unity-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record["adapterSha256"] = road_parity.digest(road_parity.TOOLS / "UnityCapture.cs")
        record["projectionSha256"] = road_parity.digest(projection)
        road_parity.write_json(evidence, record)
        self.assertEqual(self.compare(), 0)
        road_parity.write_json(projection, {"coordinates": "incorrect-unreflected-Z"})
        with self.assertRaisesRegex(ValueError, "Stale Unity"):
            self.compare()

    def test_changed_fixture_rejects_old_frames(self):
        self.evidence("ev")
        self.fixture["mesh"]["positions"][0] = 0.1
        self.save()
        with self.assertRaisesRegex(ValueError, "Stale"):
            self.compare()

    def test_changed_reference_positions_reject_matching_images(self):
        self.evidence("ev")
        self.evidence("unity")
        positions = self.case / "unity-positions.json"
        road_parity.write_json(positions, {"positions": [0, 1, 2]})
        evidence = self.case / "unity-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record["positionsSha256"] = road_parity.digest(positions)
        road_parity.write_json(evidence, record)
        self.assertEqual(self.compare(), 0)
        road_parity.write_json(positions, {"positions": [9, 1, 2]})
        with self.assertRaisesRegex(ValueError, "reference positions"):
            self.compare()

    def test_changed_runtime_dll_rejects_matching_images(self):
        self.evidence("ev")
        self.evidence("unity")
        runtime = self.root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"
        runtime.parent.mkdir(parents=True)
        runtime.write_bytes(b"isolated unit-test identity oracle")
        evidence = self.case / "unity-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        record["runtimeDllSha256"] = road_parity.digest(runtime)
        road_parity.write_json(evidence, record)
        self.assertEqual(self.compare(), 0)
        runtime.write_bytes(b"changed isolated oracle")
        with self.assertRaisesRegex(ValueError, "reference runtime"):
            self.compare()

    def test_changed_image_rejects_old_evidence(self):
        self.evidence("ev")
        Image.new("RGB", (32, 32), (0, 0, 0)).save(self.case / "ev-top.png")
        with self.assertRaisesRegex(ValueError, "changed"):
            self.compare()

    def test_empty_or_wrong_size_capture_rejected(self):
        path = self.case / "empty.png"
        Image.new("RGB", (32, 32), (0, 0, 0)).save(path)
        with self.assertRaisesRegex(ValueError, "empty"):
            road_parity.surface_mask(path, (32, 32))
        with self.assertRaisesRegex(ValueError, "dimensions"):
            road_parity.surface_mask(path, (64, 64))

    def test_invalid_mesh_and_future_schema_rejected(self):
        self.fixture["mesh"]["indices"][-1] = 3
        self.save()
        with self.assertRaisesRegex(ValueError, "index"):
            road_parity.load_fixture(self.path)
        self.fixture["schemaVersion"] = 2
        self.save()
        with self.assertRaisesRegex(ValueError, "schema"):
            road_parity.load_fixture(self.path)

    def test_metric_identical_and_different_shapes(self):
        self.evidence("ev")
        self.evidence("unity")
        self.assertEqual(self.compare(), 0)
        for view in ("perspective", "top"):
            path = self.case / f"unity-{view}.png"
            with Image.open(path) as image:
                image.putpixel((8, 8), (32, 32, 32))
                for x in range(8, 24):
                    image.putpixel((x, 8), (32, 32, 32))
                image.save(path)
        record_path = self.case / "unity-evidence.json"
        record = json.loads(record_path.read_text(encoding="utf-8"))
        record["images"] = {view: road_parity.digest(self.case / f"unity-{view}.png")
                            for view in ("perspective", "top")}
        road_parity.write_json(record_path, record)
        self.assertEqual(self.compare(), 2)
        result = json.loads((self.root / "comparison.json").read_text(encoding="utf-8"))
        self.assertEqual(result["cases"][0]["status"], "different")


if __name__ == "__main__":
    unittest.main()
