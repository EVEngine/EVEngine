"""Failure-path coverage for the explicit yaw-coordinate frame comparison."""
import copy
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_road_side_frames as frames  # noqa: E402
import compare_road_side_yaw as yaw  # noqa: E402
from scripts.tests import test_road_side_frames as fixture  # noqa: E402


class RoadSideYawEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture.RoadSideFrameEvidenceTests("test_identical_calibration_keeps_scope_limits")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        expected = {"baseline": (0, .02, 0), "x2": (2, .02, 0), "y1": (0, 1.02, 0),
                    "position1": (-3, .02, 0), "position2": (3, .02, 0),
                    "center-x2": (2, .02, 0), "yaw45": (0, .02, 45), "align1": (0, .02, 0)}
        self.reference = {"schema": "eve.tests.easyroads-side-offsets", "version": 1,
                          "probeSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectOffsetProbe.cs"),
                          "assemblySha256": frames.digest(self.root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"),
                          "frameAdapterSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectFrameCapture.cs"),
                          "samples": [{"name": name,
                                       "points": [{"x": x, "y": y, "z": i*5.005} for i in range(7)],
                                       "rotations": [{"x": 0, "y": angle, "z": 0} for _ in range(7)]}
                                      for name, (x, y, angle) in expected.items()]}
        (self.root / "side-offset-contracts.log").write_text(
            "SIDE_OBJECT_OFFSET_PROBE_COMPLETE samples=8\nApplication will terminate with return code 0\n"
            "SIDE_OBJECT_COLOR_SPACE=Gamma\nSIDE_OBJECT_FRAMES_COMPLETE count=7\n", encoding="utf-8")
        self.native = copy.deepcopy(self.fixture.native)
        self.native["probeMode"] = "--road-side-parity-yaw45"
        native_log = self.root / "side-yaw-native.log"
        native_log.write_text(
            "ROAD_SIDE_YAW_COMPLETE unity=45 native=-45\nROAD_SIDE_PARITY_COMPLETE renderers=7\n"
            "ROAD_SIDE_PRODUCER_COMPLETE requestedSpacing=5 length=30 mode=Average reference=Center count=7\n",
            encoding="utf-8")
        self.native["logSha256"] = frames.digest(native_log)
        for view in frames.VIEWS:
            ref = self.root / f"side-yaw-reference-{view}.png"
            ev = self.root / f"side-yaw-native-{view}.png"
            ref.write_bytes((self.root / f"side-render-combined-{view}.png").read_bytes())
            ev.write_bytes(ref.read_bytes())
            self.reference[view+"FrameSha256"] = frames.digest(ref)
            self.native["frames"][view] = frames.digest(ev)
        self.write_evidence()

    def write_evidence(self):
        (self.root / "side-offset-contracts.json").write_text(json.dumps(self.reference), encoding="utf-8")
        (self.root / "side-yaw-native-evidence.json").write_text(json.dumps(self.native), encoding="utf-8")

    def test_identical_frames_have_explicit_scope(self):
        result = yaw.compare(self.root)
        self.assertEqual(result["status"], "passed")
        self.assertIn("Unity yaw45 to native yaw-45", result["scope"])
        self.assertFalse(result["defaultDisplayPipelineParityVerified"])

    def test_one_channel_error_fails_even_with_valid_frame_hash(self):
        path = self.root / "side-yaw-native-top.png"
        with Image.open(path) as source:
            changed = source.copy()
        changed.putpixel((0, 0), (33, 32, 32))
        changed.save(path)
        self.native["frames"]["top"] = frames.digest(path)
        self.write_evidence()
        result = yaw.compare(self.root)
        self.assertEqual(result["status"], "different")
        self.assertEqual(result["views"][1]["silhouetteIoU"], 1)
        self.assertEqual(result["views"][1]["maxRgbError"], 1)

    def test_wrong_native_mode_rejected(self):
        self.native["probeMode"] = "--road-side-parity"
        self.write_evidence()
        with self.assertRaisesRegex(ValueError, "Unsupported native yaw"):
            yaw.compare(self.root)

    def test_reference_rotation_corruption_rejected(self):
        self.reference["samples"][6]["rotations"][0]["y"] = 0
        self.write_evidence()
        with self.assertRaises(AssertionError):
            yaw.compare(self.root)
