"""Verify measured public-SDK orientation and placement along the generated center polyline."""
import argparse
import json
import math
from pathlib import Path

import compare_road_side_frames as frames
import utf8_stdio


def vector(value):
    return tuple(value[axis] for axis in ("x", "y", "z"))


def verify(root):
    path = root / "side-orientation-contracts.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    log_path = root / "side-orientation-contracts.log"
    log = log_path.read_text(encoding="utf-8")
    assert report["schema"] == "eve.tests.easyroads-side-orientation" and report["version"] == 1
    assert report["probeSha256"] == frames.digest(frames.ADAPTERS / "UnitySideObjectOrientationProbe.cs")
    assert report["assemblySha256"] == frames.digest(root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")
    assert "SIDE_OBJECT_ORIENTATION_PROBE_COMPLETE samples=8" in log
    assert "Application will terminate with return code 0" in log
    assert "SIDE_OBJECT_ORIENTATION_FAILED" not in log and "error CS" not in log
    samples = {sample["name"]: sample for sample in report["samples"]}
    assert len(report["samples"]) == len(samples) == 8
    max_position_error = max_forward_error = max_midpoint_error = 0.
    for shape in ("grade", "curve"):
        for align in range(4):
            sample = samples[f"{shape}-align{align}"]
            assert not sample.get("error") and sample["align"] == align
            assert sample["actualMarkers"] == sample["markers"]
            assert sample["splineStrengths"] == [.5, .5, .5]
            center = list(map(vector, sample["centerPoints"]))
            left, right = sample["leftPoints"], sample["rightPoints"]
            assert len(center) == len(left) == len(right) and len(center) >= 3
            for point, a, b in zip(center, left, right, strict=True):
                midpoint = tuple((a[axis]+b[axis])*.5 for axis in ("x", "y", "z"))
                error = math.dist(point, midpoint)
                max_midpoint_error = max(max_midpoint_error, error)
                assert error < 3e-5
            lengths = [math.dist(a, b) for a, b in zip(center, center[1:])]
            assert all(length > 0 for length in lengths)
            assert abs(sum(lengths)-sample["measuredLength"]) < .001
            intervals = max(1, round(sample["measuredLength"]/5))
            assert len(sample["points"]) == len(sample["rotations"]) == len(sample["forwards"]) == len(sample["ups"]) == intervals+1
            step = sample["measuredLength"]/intervals+.005
            for index, point in enumerate(sample["points"]):
                distance = index*step
                segment = 0
                while segment < len(lengths)-1 and distance > lengths[segment]:
                    distance -= lengths[segment]
                    segment += 1
                a, b = center[segment:segment+2]
                expected = tuple(a[i]+(b[i]-a[i])*distance/lengths[segment]+(.02 if i == 1 else 0) for i in range(3))
                error = math.dist(vector(point), expected)
                max_position_error = max(max_position_error, error)
                assert error < 3e-5, (sample["name"], index, error)
                forward = tuple((b[i]-a[i])/lengths[segment] for i in range(3))
                if shape == "grade" and align < 2:
                    forward = (0, 0, 1)
                error = math.dist(vector(sample["forwards"][index]), forward)
                max_forward_error = max(max_forward_error, error)
                assert error < 1e-4, (sample["name"], index, error)
                up = vector(sample["ups"][index])
                assert abs(math.dist(up, (0, 0, 0))-1) < 1e-4
                assert abs(sum(x*y for x, y in zip(up, vector(sample["forwards"][index]), strict=True))) < 1e-4
                if shape == "curve" or align < 2:
                    assert math.dist(up, (0, 1, 0)) < 1e-4
    return {"schema": "eve.tests.easyroads-side-orientation-evidence", "version": 1, "cases": 8,
            "status": "passed", "nativeParityVerified": False,
            "scope": "grade/planar curve, source cube, align0..3, no terrain provider or bank",
            "maxPositionErrorMeters": max_position_error, "maxForwardVectorError": max_forward_error,
            "maxBoundaryMidpointErrorMeters": max_midpoint_error,
            "reportSha256": frames.digest(path), "logSha256": frames.digest(log_path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=frames.REPO / "build/road-parity")
    args = parser.parse_args()
    result = verify(args.root)
    (args.root / "side-orientation-reference-evidence.json").write_text(
        json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
