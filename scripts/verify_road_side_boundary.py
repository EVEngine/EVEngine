"""Check controlled public-SDK width/resolution/seed/source boundary experiments."""
import argparse
import hashlib
import json
import math
from pathlib import Path

import compare_road_side_frames as frames
import utf8_stdio
from verify_road_side_orientation import vector


def catmull(markers, segment, u):
    p0 = markers[max(0, segment-1)]
    p1, p2 = markers[segment:segment+2]
    p3 = markers[min(len(markers)-1, segment+2)]
    return tuple(.5*(2*p1[i]+(-p0[i]+p2[i])*u+(2*p0[i]-5*p1[i]+4*p2[i]-p3[i])*u*u
                       +(-p0[i]+3*p1[i]-3*p2[i]+p3[i])*u*u*u) for i in range(3))


def verify(root, prefix="side-boundary-contracts"):
    path = root / f"{prefix}.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    log_path = root / f"{prefix}.log"
    log = log_path.read_text(encoding="utf-8")
    assert report["schema"] == "eve.tests.easyroads-side-boundary" and report["version"] == 1
    assert report["probeSha256"] == frames.digest(frames.ADAPTERS / "UnitySideObjectOrientationProbe.cs")
    assert report["assemblySha256"] == frames.digest(root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")
    assert "SIDE_OBJECT_BOUNDARY_PROBE_COMPLETE samples=13" in log
    assert "Application will terminate with return code 0" in log
    assert "SIDE_OBJECT_BOUNDARY_FAILED" not in log and "error CS" not in log
    samples = {s["name"]: s for s in report["samples"]}
    assert len(samples) == len(report["samples"]) == 13
    max_interior_error = max_midpoint_error = max_width_error = max_width_center_error = 0.
    endpoint_offsets = []
    outer_record_deviations = []
    for sample in samples.values():
        assert not sample.get("error") and sample["seedApplied"]
        assert sample["seed"] == (17 if sample["name"] == "repeat-seed17" else 12345)
        z_scale = 1 if sample["name"] == "source-z1" else 8 if sample["name"] == "source-z8" else 4
        assert vector(sample["sourceScale"]) == (2, 3, z_scale)
        assert sample["actualWidth"] == sample["roadWidth"] and sample["actualResolution"] == sample["resolution"]
        assert sample["actualMarkers"] == sample["markers"] and sample["splineStrengths"] == [.5]*3
        center = sample["centerPoints"]
        assert center == sample["initialCenterPoints"] == sample["preBuildCenterPoints"]
        left, right = sample["leftPoints"], sample["rightPoints"]
        assert len(center) == len(left) == len(right) and len(center) >= 3
        parameters = [float(fact.split("=", 1)[1]) for fact in sample["markerFacts"] if fact.startswith("road/tValues/")]
        assert len(parameters) == len(center) and parameters[0] == 0 and parameters[-1] == 1
        markers = list(map(vector, sample["markers"]))
        segment = 0
        for i, (point, a, b, u) in enumerate(zip(center, left, right, parameters, strict=True)):
            assert 0 <= u <= 1 and segment <= 1
            midpoint = tuple((a[axis]+b[axis])*.5 for axis in ("x", "y", "z"))
            max_midpoint_error = max(max_midpoint_error, math.dist(vector(point), midpoint))
            max_width_error = max(max_width_error, abs(math.dist(vector(a), vector(b))-sample["roadWidth"]))
            error = math.dist(vector(point), catmull(markers, segment, u))
            # This is a measured middle-record invariant, not a proposed endpoint algorithm.
            if 2 <= i < len(center)-2:
                max_interior_error = max(max_interior_error, error)
                assert error < 3e-5, (sample["name"], i, error)
            elif sample["roadWidth"] == 6 and sample["name"].startswith("width6-"):
                outer_record_deviations.append({"case": sample["name"], "index": i, "parameter": u,
                                                "analyticDeviationMeters": error})
            if u == 1:
                segment += 1
        assert segment == 2
    for resolution, count in ((1, 31), (2, 17), (4, 9)):
        baseline = samples[f"width6-resolution{resolution}"]
        assert len(baseline["centerPoints"]) == count
        for width in (2, 12):
            candidate = samples[f"width{width}-resolution{resolution}"]
            assert len(candidate["centerPoints"]) == count
            error = max(math.dist(vector(a), vector(b)) for a, b in zip(baseline["centerPoints"], candidate["centerPoints"], strict=True))
            max_width_center_error = max(max_width_center_error, error)
            assert error < 3e-5
        center = baseline["centerPoints"]
        endpoint_offsets.append({"resolution": resolution,
                                 "start": vector(center[0]), "end": vector(center[-1]),
                                 "startOffsetMeters": math.dist(vector(center[0]), (0, 0, 0)),
                                 "endOffsetMeters": math.dist(vector(center[-1]), (0, 0, 30))})
    baseline = samples["width6-resolution2"]
    for name in ("repeat-seed12345", "repeat-seed17", "source-z1", "source-z8"):
        assert samples[name]["centerPoints"] == baseline["centerPoints"], name
    assert max_midpoint_error < 3e-5 and max_width_error < 3e-5
    geometry = [{key: samples[name][key] for key in ("name", "markers", "actualMarkers", "leftPoints", "rightPoints", "centerPoints")}
                for name in sorted(samples)]
    fingerprint = hashlib.sha256(json.dumps(geometry, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    return {"schema": "eve.tests.easyroads-side-boundary-evidence", "version": 1, "status": "passed", "cases": 13,
            "nativeParityVerified": False, "endpointRuleConfirmed": False, "geometrySha256": fingerprint,
            "scope": "three-marker planar curve, widths2/6/12 resolutions1/2/4, no bump, controlled seeds/source-Z scales",
            "maxMiddleCatmullErrorMeters": max_interior_error, "excludedOuterRecordsPerEnd": 2,
            "outerRecordDeviations": outer_record_deviations, "maxMidpointErrorMeters": max_midpoint_error,
            "maxWidthErrorMeters": max_width_error, "maxWidthCenterErrorMeters": max_width_center_error,
            "endpointOffsets": endpoint_offsets, "reportSha256": frames.digest(path), "logSha256": frames.digest(log_path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=frames.REPO / "build/road-parity")
    parser.add_argument("--repeat-prefix")
    args = parser.parse_args()
    result = verify(args.root)
    if args.repeat_prefix:
        repeat = verify(args.root, args.repeat_prefix)
        if repeat["geometrySha256"] != result["geometrySha256"]:
            raise ValueError("Independent reference run changed geometry")
        result["independentRepeatVerified"] = True
        result["repeatReportSha256"] = repeat["reportSha256"]
        result["repeatLogSha256"] = repeat["logSha256"]
    (args.root / "side-boundary-reference-evidence.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
