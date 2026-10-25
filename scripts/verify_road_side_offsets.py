"""Verify independent public-SDK side-object offset observations, not native parity."""
import argparse
import hashlib
import json
from pathlib import Path

import utf8_stdio


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(root):
    report_path = root / "side-offset-contracts.json"
    log_path = root / "side-offset-contracts.log"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    log = log_path.read_text(encoding="utf-8")
    assert report["schema"] == "eve.tests.easyroads-side-offsets" and report["version"] == 1
    assert report["probeSha256"] == digest(Path(__file__).parent / "road_parity/UnitySideObjectOffsetProbe.cs")
    assert report["assemblySha256"] == digest(root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")
    assert "SIDE_OBJECT_OFFSET_PROBE_COMPLETE samples=8" in log
    assert "Application will terminate with return code 0" in log
    assert "error CS" not in log
    expected = {"baseline": (0, .02, 0), "x2": (2, .02, 0), "y1": (0, 1.02, 0),
                "position1": (-3, .02, 0), "position2": (3, .02, 0),
                "center-x2": (2, .02, 0), "yaw45": (0, .02, 45), "align1": (0, .02, 0)}
    samples = {sample["name"]: sample for sample in report["samples"]}
    assert len(report["samples"]) == len(samples) == len(expected) and samples.keys() == expected.keys()
    for name, (x, y, yaw) in expected.items():
        sample = samples[name]
        assert len(sample["points"]) == len(sample["rotations"]) == 7, name
        for i, (point, rotation) in enumerate(zip(sample["points"], sample["rotations"])):
            for axis, value in (("x", x), ("y", y), ("z", i * 5.005)):
                assert abs(point[axis] - value) < 1e-4, (name, i, axis, point)
            assert abs(rotation["x"]) < 1e-4 and abs(rotation["z"]) < 1e-4, name
            assert abs(rotation["y"] - yaw) < 1e-4, name
    return {"schema": "eve.tests.easyroads-side-offset-evidence", "version": 1,
            "cases": 8, "status": "passed", "nativeParityVerified": False,
            "scope": "straight width-6 unit cube; align1 is indistinguishable on this flat road",
            "reportSha256": digest(report_path), "logSha256": digest(log_path),
            "probeSha256": report["probeSha256"], "assemblySha256": report["assemblySha256"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/road-parity"))
    args = parser.parse_args()
    result = verify(args.root)
    (args.root / "side-offset-evidence.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("PASS: eight measured offset cases; native parity remains unverified")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
