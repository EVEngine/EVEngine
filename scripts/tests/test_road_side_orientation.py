"""Reference consumer contracts and corruption checks independent of Unity availability."""
import json
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_road_side_frames as frames  # noqa: E402
import verify_road_side_orientation as orientation  # noqa: E402
from scripts.tests import test_road_side_frames as fixture  # noqa: E402


class RoadSideOrientationReferenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture.RoadSideFrameEvidenceTests("test_identical_calibration_keeps_scope_limits")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        samples = []
        for shape in ("grade", "curve"):
            coordinates = [(0, 0, 0), (0, 3, 15), (0, 6, 30)] if shape == "grade" else [(0, 0, 0), (8, 0, 15), (0, 0, 30)]
            center = [dict(zip(("x", "y", "z"), p, strict=True)) for p in coordinates]
            lengths = [math.dist(a, b) for a, b in zip(coordinates, coordinates[1:])]
            total = sum(lengths)
            count = round(total/5)
            for align in range(4):
                points, forwards, ups = [], [], []
                for i in range(count+1):
                    distance = i*(total/count+.005)
                    segment = min(int(distance/lengths[0]), 1)
                    distance -= segment*lengths[0]
                    a, b = coordinates[segment:segment+2]
                    point = [a[k]+(b[k]-a[k])*distance/lengths[segment] for k in range(3)]
                    point[1] += .02
                    forward = [(b[k]-a[k])/lengths[segment] for k in range(3)]
                    if shape == "grade" and align < 2:
                        forward = [0, 0, 1]
                    up = [0, forward[2], -forward[1]] if shape == "grade" else [0, 1, 0]
                    points.append(dict(zip(("x", "y", "z"), point, strict=True)))
                    forwards.append(dict(zip(("x", "y", "z"), forward, strict=True)))
                    ups.append(dict(zip(("x", "y", "z"), up, strict=True)))
                samples.append({"name": f"{shape}-align{align}", "align": align, "markers": center,
                                "actualMarkers": center, "splineStrengths": [.5]*3, "centerPoints": center,
                                "leftPoints": [dict(p, x=p["x"]-3) for p in center],
                                "rightPoints": [dict(p, x=p["x"]+3) for p in center],
                                "measuredLength": total, "points": points, "forwards": forwards, "ups": ups,
                                "rotations": [dict(x=0, y=0, z=0) for _ in points]})
        self.report = {"schema": "eve.tests.easyroads-side-orientation", "version": 1, "samples": samples,
                       "probeSha256": frames.digest(frames.ADAPTERS / "UnitySideObjectOrientationProbe.cs"),
                       "assemblySha256": frames.digest(self.root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")}
        (self.root / "side-orientation-contracts.log").write_text(
            "SIDE_OBJECT_ORIENTATION_PROBE_COMPLETE samples=8\nApplication will terminate with return code 0\n",
            encoding="utf-8")
        self.write_report()

    def write_report(self):
        (self.root / "side-orientation-contracts.json").write_text(json.dumps(self.report), encoding="utf-8")

    def test_complete_contract_stays_reference_only(self):
        result = orientation.verify(self.root)
        self.assertEqual(result["cases"], 8)
        self.assertFalse(result["nativeParityVerified"])

    def test_clamped_last_point_rejected(self):
        sample = self.report["samples"][0]
        sample["points"][-1] = dict(sample["centerPoints"][-1], y=6.02)
        self.write_report()
        with self.assertRaises(AssertionError):
            orientation.verify(self.root)

    def test_wrong_curve_direction_rejected(self):
        self.report["samples"][4]["forwards"][0] = dict(x=0, y=0, z=1)
        self.write_report()
        with self.assertRaises(AssertionError):
            orientation.verify(self.root)

    def test_center_not_boundary_midpoint_rejected(self):
        self.report["samples"][4]["leftPoints"][0]["x"] += .1
        self.write_report()
        with self.assertRaises(AssertionError):
            orientation.verify(self.root)
