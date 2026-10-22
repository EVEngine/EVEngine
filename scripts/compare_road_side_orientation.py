"""Compare actual recipe slope/curve facilities; preserve genuine differences at fixed thresholds."""
import argparse
import csv
import json
import math
from pathlib import Path

import compare_road_side_frames as frames
import utf8_stdio
import verify_road_side_orientation as reference_check


def compare(root):
    reference_check.verify(root)
    reference = json.loads((root / "side-orientation-contracts.json").read_text(encoding="utf-8"))
    if reference.get("frameAdapterSha256") != frames.digest(frames.ADAPTERS / "UnitySideObjectFrameCapture.cs"):
        raise ValueError("Stale reference frame adapter")
    log = (root / "side-orientation-contracts.log").read_text(encoding="utf-8")
    for sentinel in ("SIDE_OBJECT_COLOR_SPACE=Gamma", "SIDE_OBJECT_FRAMES_COMPLETE count=7",
                     "SIDE_OBJECT_FRAMES_COMPLETE count=8"):
        if sentinel not in log:
            raise ValueError("Missing reference frame completion: " + sentinel)
    cases = []
    for shape, count in (("grade", 7), ("curve", 8)):
        sample = next(s for s in reference["samples"] if s["name"] == f"{shape}-align2")
        prefix = f"side-orientation-native-{shape}"
        evidence_path = root / f"{prefix}-evidence.json"
        native = json.loads(evidence_path.read_text(encoding="utf-8"))
        if (native.get("schema") != "eve.tests.road-side-native-frame-evidence" or native.get("schemaVersion") != 1
                or native.get("probeMode") != f"--road-side-parity-{shape}"):
            raise ValueError("Unsupported native orientation evidence")
        if native.get("sources") != {str(p): frames.digest(p) for p in frames.NATIVE_SOURCES}:
            raise ValueError("Stale native source")
        if frames.runtime_hashes(Path(native["rendererExecutable"])) != native["runtimeSearchDirectorySha256"]:
            raise ValueError("Stale native runtime inventory")
        log_path = root / f"{prefix}.log"
        if native["logSha256"] != frames.digest(log_path):
            raise ValueError("Changed native log")
        native_log = log_path.read_text(encoding="utf-8")
        for sentinel in (f"ROAD_SIDE_ORIENTATION_COMPLETE shape={shape} alignment=PathForward pathSampling=SurfaceSegments",
                         f"ROAD_SIDE_PARITY_COMPLETE renderers={count}"):
            if sentinel not in native_log:
                raise ValueError("Missing native orientation completion: " + sentinel)
        placements = root / f"{prefix}-placements.csv"
        if native.get("placementCsvSha256") != frames.digest(placements):
            raise ValueError("Changed native placement diagnostics")
        with placements.open(encoding="utf-8", newline="") as stream:
            points = list(csv.DictReader(stream))
        if len(points) != count:
            raise ValueError("Native placement count differs")
        point_error = max(math.dist((float(p["x"]), float(p["y"]), -float(p["z"])),
                                   reference_check.vector(q)) for p, q in zip(points, sample["points"], strict=True))
        rows = []
        for view in frames.VIEWS:
            ref = root / f"side-orientation-reference-{shape}-align2-{view}.png"
            ev = root / f"{prefix}-{view}.png"
            if sample.get(view + "FrameSha256") != frames.digest(ref) or native["frames"][view] != frames.digest(ev):
                raise ValueError("Changed orientation frame: " + shape + "/" + view)
            rows.append(frames.compare_pixels(ref, ev, view))
        passed = all(row["silhouetteIoU"] >= .98 and row["maxRgbError"] == 0 for row in rows)
        cases.append({"shape": shape, "status": "passed" if passed else "different", "views": rows,
                      "maxPlacementPositionErrorMeters": point_error,
                      "nativeEvidenceSha256": frames.digest(evidence_path)})
    return {"schema": "eve.tests.road-side-orientation-comparison", "version": 1,
            "scope": "grade/planar curve, actual 5m Average Center .02m recipe, reference align2/native PathForward SurfaceSegments",
            "minimumIoU": .98, "maximumRgbError": 0, "defaultDisplayPipelineParityVerified": False,
            "status": "passed" if all(case["status"] == "passed" for case in cases) else "different", "cases": cases,
            "referenceReportSha256": frames.digest(root / "side-orientation-contracts.json"),
            "referenceLogSha256": frames.digest(root / "side-orientation-contracts.log")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=frames.REPO / "build/road-parity")
    parser.add_argument("--run-native", type=Path)
    args = parser.parse_args()
    if args.run_native:
        for shape, count in (("grade", 7), ("curve", 8)):
            frames.capture_native(args.root, args.run_native, prefix=f"side-orientation-native-{shape}",
                                  mode=f"--road-side-parity-{shape}", renderers=count)
    result = compare(args.root)
    (args.root / "side-orientation-comparison.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
