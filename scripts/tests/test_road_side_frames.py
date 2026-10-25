"""Failure-path contracts for the rigid-prefab paired-frame evidence checker."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_road_side_frames as frames  # noqa: E402


class RoadSideFrameEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        assembly = self.root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"
        assembly.parent.mkdir(parents=True)
        assembly.write_bytes(b"test reference assembly identity")
        executable = self.root / "host-profile/test/probe.exe"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"test executable identity")
        self.native = {
            "schema": "eve.tests.road-side-native-frame-evidence", "schemaVersion": 1,
            "rendererExecutable": str(executable),
            "sources": {str(p): frames.digest(p) for p in frames.NATIVE_SOURCES},
            "runtimeSearchDirectorySha256": frames.runtime_hashes(executable), "frames": {},
        }
        self.reference = {
            "schema": "eve.tests.easyroads-side-object-contracts", "schemaVersion": 1,
            "sourcePose": "combined", "generationObservations": ["renderer=contract-source fixture"] * 7,
            "requestedSpacing": 5, "requestedLength": 30, "averageDistance": True,
            "probeSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectContractProbe.cs"),
            "frameAdapterSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectFrameCapture.cs"),
            "referenceAssemblySha256": frames.digest(assembly),
        }
        (self.root / "side-render-combined.log").write_text(
            "SIDE_OBJECT_COLOR_SPACE=Gamma\nSIDE_OBJECT_FRAMES_COMPLETE count=7\n"
            "SIDE_OBJECT_CONTRACT_PROBE_COMPLETE\nApplication will terminate with return code 0\n", encoding="utf-8")
        native_log = self.root / "side-render-native-calibrated.log"
        native_log.write_text("ROAD_SIDE_PARITY_COMPLETE renderers=7\n", encoding="utf-8")
        self.native["logSha256"] = frames.digest(native_log)
        for view in frames.VIEWS:
            image = Image.new("RGB", (640, 480), (32, 32, 32))
            draw = ImageDraw.Draw(image)
            for index in range(7):
                draw.rectangle((30 + index * 70, 100, 55 + index * 70, 140), fill=(255, 0, 255))
            ref = self.root / f"side-render-combined-{view}.png"
            ev = self.root / f"side-render-native-calibrated-{view}.png"
            image.save(ref)
            image.save(ev)
            self.reference[view + "FrameSha256"] = frames.digest(ref)
            self.native["frames"][view] = frames.digest(ev)
        self.write_evidence()

    def write_evidence(self):
        (self.root / "side-render-combined.json").write_text(json.dumps(self.reference), encoding="utf-8")
        (self.root / "side-render-native-calibrated-evidence.json").write_text(json.dumps(self.native), encoding="utf-8")

    def test_identical_calibration_keeps_scope_limits(self):
        result = frames.compare(self.root)
        self.assertEqual(result["status"], "passed")
        self.assertFalse(result["nativeRoadSpacingVerified"])
        self.assertFalse(result["defaultDisplayPipelineParityVerified"])

    def test_changed_pixels_rejected_even_when_geometry_is_nonempty(self):
        path = self.root / "side-render-native-calibrated-perspective.png"
        with Image.open(path) as source:
            image = source.copy()
        image.putpixel((0, 0), (33, 32, 32))
        image.save(path)
        with self.assertRaisesRegex(ValueError, "Capture pixels changed"):
            frames.compare(self.root)
        self.native["frames"]["perspective"] = frames.digest(path)
        self.write_evidence()
        result = frames.compare(self.root)
        self.assertEqual(result["status"], "different")
        self.assertEqual(result["views"][0]["silhouetteIoU"], 1)
        self.assertEqual(result["views"][0]["maxRgbError"], 1)

    def test_added_runtime_dll_invalidates_evidence(self):
        executable = Path(self.native["rendererExecutable"])
        (executable.parent / "new-renderer.dll").write_bytes(b"new runtime dependency")
        with self.assertRaisesRegex(ValueError, "runtime search inventory"):
            frames.compare(self.root)

    def test_stale_reference_adapter_rejected(self):
        self.reference["frameAdapterSha256"] = "0" * 64
        self.write_evidence()
        with self.assertRaisesRegex(ValueError, "Stale reference input"):
            frames.compare(self.root)

    def test_producer_evidence_requires_matching_pixels(self):
        log = self.root / "side-render-native-calibrated.log"
        log.write_text(log.read_text(encoding="utf-8") +
                       "ROAD_SIDE_PRODUCER_COMPLETE requestedSpacing=5 length=30 mode=Average reference=Center count=7\n",
                       encoding="utf-8")
        self.native["logSha256"] = frames.digest(log)
        self.write_evidence()
        result = frames.compare(self.root)
        self.assertTrue(result["nativeRoadSpacingVerified"])
        self.assertIn("straight 30m", result["scope"])
        path = self.root / "side-render-native-calibrated-perspective.png"
        with Image.open(path) as image:
            modified = image.copy()
        modified.putpixel((0, 0), (33, 32, 32))
        modified.save(path)
        self.native["frames"]["perspective"] = frames.digest(path)
        self.write_evidence()
        result = frames.compare(self.root)
        self.assertFalse(result["nativeRoadSpacingVerified"])
        self.assertEqual(result["status"], "different")
