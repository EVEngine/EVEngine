#!/usr/bin/env python3
"""Capture/check calibrated rigid-prefab frames; supports a scoped straight-road producer check."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from PIL import Image

import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
ADAPTERS = REPO / "scripts/road_parity"
NATIVE_SOURCES = (REPO / "test/asset_import/RoadSideGraphicsProbe.cpp",
                  REPO / "test/asset_import/RoadSideParityProbe.inc")
VIEWS = ("perspective", "top")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runtime_hashes(executable: Path) -> dict[str, str]:
    # This probe lives under <profile>/test. DLL search precedence is exe-adjacent,
    # then the explicitly prefixed profile directory. This is not an OS module trace.
    files = {executable.name.casefold(): executable}
    for directory in (executable.parent, executable.parent.parent):
        for path in sorted(directory.glob("*.dll")):
            files.setdefault(path.name.casefold(), path)
    return {str(path): digest(path) for path in files.values()}


def capture_native(root: Path, executable: Path, *, prefix: str = "side-render-native-calibrated",
                   mode: str = "--road-side-parity", renderers: int = 7) -> None:
    executable = executable.resolve()
    before = runtime_hashes(executable)
    sources = {str(path): digest(path) for path in NATIVE_SOURCES}
    env = dict(os.environ)
    env["PATH"] = str(executable.parent.parent) + os.pathsep + env.get("PATH", "")
    command = [str(executable), mode, str(root / prefix)]
    result = subprocess.run(command, cwd=REPO, env=env, capture_output=True, text=True, encoding="utf-8", check=False)
    log = result.stdout + result.stderr
    (root / f"{prefix}.log").write_text(log, encoding="utf-8")
    if result.returncode or f"ROAD_SIDE_PARITY_COMPLETE renderers={renderers}" not in log:
        raise ValueError(f"Native capture failed ({result.returncode}): {log[-2000:]}")
    if before != runtime_hashes(executable) or sources != {str(path): digest(path) for path in NATIVE_SOURCES}:
        raise ValueError("Native sources or executable/DLL search roots changed during capture")
    evidence = {"schema": "eve.tests.road-side-native-frame-evidence", "schemaVersion": 1,
                "rendererExecutable": str(executable), "probeMode": mode,
                "sources": sources, "runtimeSearchDirectorySha256": before,
                "frames": {view: digest(root / f"{prefix}-{view}.png") for view in VIEWS},
                "logSha256": digest(root / f"{prefix}.log")}
    placement_log = root / f"{prefix}-placements.csv"
    if placement_log.exists():
        evidence["placementCsvSha256"] = digest(placement_log)
    (root / f"{prefix}-evidence.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")


def compare_pixels(ref_path: Path, ev_path: Path, view: str) -> dict:
    with Image.open(ref_path) as ref, Image.open(ev_path) as ev:
        if ref.size != (640, 480) or ev.size != ref.size:
            raise ValueError(f"Unexpected capture dimensions: {view}")
        a, b = ref.convert("RGB").tobytes(), ev.convert("RGB").tobytes()
    intersection = union = reference_pixels = candidate_pixels = 0
    for i in range(0, len(a), 3):
        am = a[i] > 160 and a[i + 1] < 96 and a[i + 2] > 160
        bm = b[i] > 160 and b[i + 1] < 96 and b[i + 2] > 160
        reference_pixels += am
        candidate_pixels += bm
        intersection += am and bm
        union += am or bm
    if reference_pixels < 100 or candidate_pixels < 100 or union >= 640 * 480 * .9:
        raise ValueError(f"Empty or implausible facility mask: {view}")
    errors = [abs(x - y) for x, y in zip(a, b, strict=True)]
    return {"view": view, "silhouetteIoU": intersection / union,
                 "referencePixels": reference_pixels, "candidatePixels": candidate_pixels,
                 "maxRgbError": max(errors), "meanRgbError": sum(errors) / len(errors),
                 "referenceSha256": digest(ref_path), "candidateSha256": digest(ev_path)}


def compare(root: Path) -> dict:
    reference = json.loads((root / "side-render-combined.json").read_text(encoding="utf-8"))
    log = (root / "side-render-combined.log").read_text(encoding="utf-8")
    if (reference.get("schema") != "eve.tests.easyroads-side-object-contracts"
            or reference.get("schemaVersion") != 1 or reference.get("sourcePose") != "combined"
            or any(row.startswith("FAILED") for row in reference["generationObservations"])):
        raise ValueError("Unsupported/failed reference report")
    if sum(row.startswith("renderer=contract-source ") for row in reference["generationObservations"]) != 7:
        raise ValueError("Reference must contain seven generated source renderers")
    if (reference.get("requestedSpacing"), reference.get("requestedLength"), reference.get("averageDistance")) != (5, 30, True):
        raise ValueError("Reference placement parameters differ from the calibrated fixture")
    for sentinel in ("SIDE_OBJECT_COLOR_SPACE=Gamma", "SIDE_OBJECT_FRAMES_COMPLETE count=7",
                     "SIDE_OBJECT_CONTRACT_PROBE_COMPLETE", "Application will terminate with return code 0"):
        if sentinel not in log:
            raise ValueError(f"Reference execution not confirmed: {sentinel}")
    inputs = {"probeSha256": ADAPTERS / "UnitySideObjectContractProbe.cs",
              "frameAdapterSha256": ADAPTERS / "UnitySideObjectFrameCapture.cs",
              "referenceAssemblySha256": root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"}
    for key, path in inputs.items():
        if reference.get(key) != digest(path):
            raise ValueError(f"Stale reference input: {key}")
    native = json.loads((root / "side-render-native-calibrated-evidence.json").read_text(encoding="utf-8"))
    if native.get("schema") != "eve.tests.road-side-native-frame-evidence" or native.get("schemaVersion") != 1:
        raise ValueError("Unsupported native evidence")
    if native.get("sources") != {str(path): digest(path) for path in NATIVE_SOURCES}:
        raise ValueError("Stale native fixture source")
    if runtime_hashes(Path(native["rendererExecutable"])) != native["runtimeSearchDirectorySha256"]:
        raise ValueError("Stale native runtime search inventory")
    if native["logSha256"] != digest(root / "side-render-native-calibrated.log"):
        raise ValueError("Native capture log changed")
    producer_verified = "ROAD_SIDE_PRODUCER_COMPLETE requestedSpacing=5 length=30 mode=Average reference=Center count=7" in (
        root / "side-render-native-calibrated.log").read_text(encoding="utf-8")
    rows = []
    for view in VIEWS:
        ref_path = root / f"side-render-combined-{view}.png"
        ev_path = root / f"side-render-native-calibrated-{view}.png"
        if reference.get(view + "FrameSha256") != digest(ref_path) or native["frames"][view] != digest(ev_path):
            raise ValueError(f"Capture pixels changed: {view}")
        rows.append(compare_pixels(ref_path, ev_path, view))
    frames_passed = all(row["silhouetteIoU"] >= .98 and row["maxRgbError"] == 0 for row in rows)
    return {"schema": "eve.tests.road-side-frame-comparison", "schemaVersion": 1,
            "scope": ("straight 30m road recipe, requested 5m Average center placement, rigid cube, calibrated unlit output"
                      if producer_verified else "rigid unit-cube prefab, calibrated unlit output and measured placement points only"),
            "nativeRoadSpacingVerified": producer_verified and frames_passed, "defaultDisplayPipelineParityVerified": False,
            "calibration": "Unity Gamma Unlit/Color; native linear background decoded from sRGB, tone mapping None",
            "minimumIoU": .98, "maximumRgbError": 0, "views": rows,
            "status": "passed" if frames_passed else "different",
            "referenceReportSha256": digest(root / "side-render-combined.json"),
            "nativeEvidenceSha256": digest(root / "side-render-native-calibrated-evidence.json")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "build/road-parity")
    parser.add_argument("--run-native", type=Path, metavar="PROBE_EXE")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.run_native:
        capture_native(root, args.run_native)
    result = compare(root)
    (root / "side-render-comparison.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "views": result["views"]}, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
