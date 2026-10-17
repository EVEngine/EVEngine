"""Check actual straight-road producer frames with explicit Unity-to-native yaw conversion."""
import argparse
import json
from pathlib import Path

import compare_road_side_frames as frames
import utf8_stdio
import verify_road_side_offsets as offsets


def compare(root):
    offsets.verify(root)
    reference = json.loads((root / "side-offset-contracts.json").read_text(encoding="utf-8"))
    log = (root / "side-offset-contracts.log").read_text(encoding="utf-8")
    for sentinel in ("SIDE_OBJECT_COLOR_SPACE=Gamma", "SIDE_OBJECT_FRAMES_COMPLETE count=7"):
        if sentinel not in log:
            raise ValueError("Missing reference frame execution: " + sentinel)
    if reference.get("frameAdapterSha256") != frames.digest(frames.ADAPTERS / "UnitySideObjectFrameCapture.cs"):
        raise ValueError("Stale reference frame adapter")
    evidence_path = root / "side-yaw-native-evidence.json"
    native = json.loads(evidence_path.read_text(encoding="utf-8"))
    if (native.get("schema") != "eve.tests.road-side-native-frame-evidence" or native.get("schemaVersion") != 1
            or native.get("probeMode") != "--road-side-parity-yaw45"):
        raise ValueError("Unsupported native yaw evidence")
    if native.get("sources") != {str(p): frames.digest(p) for p in frames.NATIVE_SOURCES}:
        raise ValueError("Stale native sources")
    if frames.runtime_hashes(Path(native["rendererExecutable"])) != native["runtimeSearchDirectorySha256"]:
        raise ValueError("Stale native runtime inventory")
    native_log = root / "side-yaw-native.log"
    if native["logSha256"] != frames.digest(native_log):
        raise ValueError("Native log changed")
    for sentinel in ("ROAD_SIDE_YAW_COMPLETE unity=45 native=-45", "ROAD_SIDE_PARITY_COMPLETE renderers=7",
                     "ROAD_SIDE_PRODUCER_COMPLETE requestedSpacing=5 length=30 mode=Average reference=Center count=7"):
        if sentinel not in native_log.read_text(encoding="utf-8"):
            raise ValueError("Missing native producer marker: " + sentinel)
    rows = []
    for view in frames.VIEWS:
        ref = root / f"side-yaw-reference-{view}.png"
        ev = root / f"side-yaw-native-{view}.png"
        if reference.get(view + "FrameSha256") != frames.digest(ref) or native["frames"][view] != frames.digest(ev):
            raise ValueError("Changed yaw frame: " + view)
        rows.append(frames.compare_pixels(ref, ev, view))
    passed = all(row["silhouetteIoU"] >= .98 and row["maxRgbError"] == 0 for row in rows)
    return {"schema": "eve.tests.road-side-yaw-comparison", "version": 1,
            "scope": "straight 30m, 5m Average Center .02m, cube scale(2,3,4), Unity yaw45 to native yaw-45",
            "status": "passed" if passed else "different", "views": rows,
            "minimumIoU": .98, "maximumRgbError": 0, "defaultDisplayPipelineParityVerified": False,
            "referenceReportSha256": frames.digest(root / "side-offset-contracts.json"),
            "referenceLogSha256": frames.digest(root / "side-offset-contracts.log"),
            "nativeEvidenceSha256": frames.digest(evidence_path)}


def negative_control(root, executable):
    frames.capture_native(root, executable, prefix="side-yaw-wrong-sign", mode="--road-side-parity-yaw45-wrong-sign")
    rows = [frames.compare_pixels(root / f"side-yaw-reference-{view}.png",
                                  root / f"side-yaw-wrong-sign-{view}.png", view) for view in frames.VIEWS]
    if not all(row["silhouetteIoU"] < .98 and row["maxRgbError"] > 0 for row in rows):
        raise ValueError("Wrong-sign yaw must fail the unchanged image thresholds")
    result = {"schema": "eve.tests.road-side-yaw-negative-control", "version": 1,
              "status": "different-as-required", "views": rows,
              "scope": "Unity+45 vs deliberately wrong native+45; otherwise same recipe and display",
              "nativeEvidenceSha256": frames.digest(root / "side-yaw-wrong-sign-evidence.json"),
              "referenceReportSha256": frames.digest(root / "side-offset-contracts.json")}
    path = root / "side-yaw-wrong-sign-comparison.json"
    path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return frames.digest(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=frames.REPO / "build/road-parity")
    parser.add_argument("--run-native", type=Path)
    parser.add_argument("--run-negative-control", action="store_true")
    args = parser.parse_args()
    if args.run_negative_control and not args.run_native:
        parser.error("--run-negative-control requires --run-native")
    if args.run_native:
        frames.capture_native(args.root, args.run_native, prefix="side-yaw-native", mode="--road-side-parity-yaw45")
    result = compare(args.root)
    if args.run_negative_control:
        result["negativeControlSha256"] = negative_control(args.root, args.run_native)
    (args.root / "side-yaw-comparison.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
