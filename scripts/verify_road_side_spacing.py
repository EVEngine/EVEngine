#!/usr/bin/env python3
"""Verify measured default rigid-side-object spacing samples; native producer remains unverified."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import utf8_stdio
from verify_road_side_pose import expect, vector

REPO = Path(__file__).resolve().parent.parent


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(root: Path) -> dict:
    cases = (("fixed-30", 30, 5, False), ("fixed-31", 31, 5, False), ("average-31", 31, 5, True),
             ("average-13", 13, 5, True), ("average-12", 12, 5, True),
             ("average-tie", 12.5, 5, True), ("average-odd-tie", 17.5, 5, True), ("average-short", 2, 5, True),
             ("average-distance-7", 31, 7, True), ("average-distance-2", 13, 2, True))
    rows = []
    for name, length, distance, average in cases:
        path = root / f"side-spacing-{name}.json"
        log_path = path.with_suffix(".log")
        data = json.loads(path.read_text(encoding="utf-8"))
        log = log_path.read_text(encoding="utf-8")
        if (data.get("schema") != "eve.tests.easyroads-side-object-contracts"
                or data.get("schemaVersion") != 1 or data.get("sourcePose") != "combined"
                or data.get("requestedSpacing") != distance or data.get("requestedLength") != length
                or data.get("averageDistance") is not average
                or "SIDE_OBJECT_CONTRACT_PROBE_COMPLETE" not in log
                or "Application will terminate with return code 0" not in log):
            raise ValueError(f"Unsupported or incomplete probe execution: {name}")
        if data.get("probeSha256") != digest(REPO / "scripts/road_parity/UnitySideObjectContractProbe.cs"):
            raise ValueError(f"Stale spacing adapter: {name}")
        if data.get("referenceAssemblySha256") != digest(root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"):
            raise ValueError(f"Stale reference assembly: {name}")
        expect((data["measuredRoadLength"], data["measuredRoadDistance"]), (length, length))
        expect(tuple(data["centerPoints"][-1][key] for key in ("x", "y", "z")), (0, 0, length))
        observations = data["generationObservations"]
        if any(row.startswith("FAILED") for row in observations):
            raise ValueError(f"Probe failed: {name}")
        instances = [vector(row, "position") for row in observations if row.startswith("renderer=contract-source ")]
        intervals = max(1, round(length / distance)) if average else 0
        spacing = length / intervals + .005 if average else distance
        count = intervals + 1 if average else math.ceil(length / distance)
        if len(instances) != count:
            raise ValueError(f"Unexpected instance count: {name}")
        for index, point in enumerate(instances):
            expect(point, (0, .02, index * spacing))
        rows.append({"case": name, "requestedLength": length, "requestedSpacing": distance, "averageDistance": average,
                     "instances": len(instances), "observedStep": spacing, "lastPoint": instances[-1],
                     "reportSha256": digest(path), "logSha256": digest(log_path)})
    return {"schema": "eve.tests.easyroads-side-spacing-evidence", "schemaVersion": 1,
            "scope": "ten straight centerline fixtures, Mesh type 0, distances 2/5/7, no bumpiness",
            "nativeProducerVerified": False,
            "observedSampleRule": "fixed: i*distance < length from zero; average: n=max(1,round-to-even(length/distance)), step=length/n+.005",
            "unverified": ["other requested distances", "curves", "side offsets", "connections", "start/end offsets"],
            "cases": rows}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "build/road-parity")
    args = parser.parse_args()
    result = verify(args.root)
    (args.root / "side-spacing-evidence.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("PASS: ten measured reference spacing cases; native producer remains unverified")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
