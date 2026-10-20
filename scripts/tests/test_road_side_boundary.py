"""Controlled boundary-report invariants and failure paths, with no Unity requirement."""
import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_road_side_frames as frames  # noqa: E402
import verify_road_side_boundary as boundary  # noqa: E402
from scripts.tests import test_road_side_frames as fixture  # noqa: E402


class RoadSideBoundaryReferenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture.RoadSideFrameEvidenceTests("test_identical_calibration_keeps_scope_limits")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        markers = [(0, 0, 0), (6, 0, 15), (0, 0, 30)]
        records = []
        for width in (2, 6, 12):
            for resolution, count in ((1, 31), (2, 17), (4, 9)):
                half = (count-1)//2
                parameters = [i/half for i in range(half+1)] + [i/half for i in range(1, half+1)]
                center = []
                segment = 0
                for u in parameters:
                    center.append(dict(zip(("x", "y", "z"), boundary.catmull(markers, segment, u), strict=True)))
                    if u == 1:
                        segment += 1
                author = [dict(zip(("x", "y", "z"), p, strict=True)) for p in markers]
                records.append({"name": f"width{width}-resolution{resolution}", "seedApplied": True, "seed": 12345, "sourceScale": dict(x=2, y=3, z=4),
                                "roadWidth": width, "actualWidth": width, "resolution": resolution,
                                "actualResolution": resolution, "markers": author, "actualMarkers": author,
                                "splineStrengths": [.5]*3, "centerPoints": center,
                                "initialCenterPoints": copy.deepcopy(center), "preBuildCenterPoints": copy.deepcopy(center),
                                "leftPoints": [dict(p, x=p["x"]-width/2) for p in center],
                                "rightPoints": [dict(p, x=p["x"]+width/2) for p in center],
                                "markerFacts": [f"road/tValues/{i}={u}" for i, u in enumerate(parameters)]})
        baseline = next(s for s in records if s["name"] == "width6-resolution2")
        for name in ("repeat-seed12345", "repeat-seed17", "source-z1", "source-z8"):
            record = dict(copy.deepcopy(baseline), name=name)
            if name == "repeat-seed17":
                record["seed"] = 17
            if name == "source-z1":
                record["sourceScale"]["z"] = 1
            if name == "source-z8":
                record["sourceScale"]["z"] = 8
            records.append(record)
        self.report = {"schema": "eve.tests.easyroads-side-boundary", "version": 1, "samples": records,
                       "probeSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectOrientationProbe.cs"),
                       "assemblySha256": frames.digest(self.root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")}
        (self.root / "side-boundary-contracts.log").write_text(
            "SIDE_OBJECT_BOUNDARY_PROBE_COMPLETE samples=13\nApplication will terminate with return code 0\n",
            encoding="utf-8")
        self.write_report()

    def write_report(self):
        (self.root / "side-boundary-contracts.json").write_text(json.dumps(self.report), encoding="utf-8")

    def test_valid_data_does_not_confirm_endpoint_algorithm(self):
        result = boundary.verify(self.root)
        self.assertEqual(result["cases"], 13)
        self.assertFalse(result["nativeParityVerified"])
        self.assertFalse(result["endpointRuleConfirmed"])

    def test_changed_center_during_facility_generation_rejected(self):
        self.report["samples"][0]["initialCenterPoints"][0]["x"] += .01
        self.write_report()
        with self.assertRaises(AssertionError):
            boundary.verify(self.root)

    def test_corrupt_middle_record_rejected_even_with_matching_boundaries(self):
        sample = self.report["samples"][0]
        for key in ("centerPoints", "initialCenterPoints", "preBuildCenterPoints", "leftPoints", "rightPoints"):
            sample[key][5]["x"] += .01
        self.write_report()
        with self.assertRaises(AssertionError):
            boundary.verify(self.root)

    def test_missing_seed_control_rejected(self):
        self.report["samples"][0]["seedApplied"] = False
        self.write_report()
        with self.assertRaises(AssertionError):
            boundary.verify(self.root)
