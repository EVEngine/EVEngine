#!/usr/bin/env python3
"""Check measured EasyRoads rigid-prefab pose contracts; never claims native parity."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

import utf8_stdio


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def vector(row: str, key: str) -> tuple[float, ...]:
    match = re.search(r"(?:^| )" + re.escape(key) + r"=\(([^)]+)\)", row)
    if match is None:
        raise ValueError(f"Missing {key}: {row}")
    result = tuple(float(value) for value in match[1].split(","))
    if len(result) != 3 or not all(math.isfinite(value) for value in result):
        raise ValueError(f"Invalid {key}: {row}")
    return result


def expect(actual: tuple[float, ...], expected: tuple[float, ...]) -> None:
    if len(actual) != len(expected) or any(abs(a - b) > 2e-5 for a, b in zip(actual, expected)):
        raise ValueError(f"Pose differs: {actual} != {expected}")


def verify(root: Path) -> dict:
    reports = {}
    evidence = {}
    for name in ("identity", "translated", "rotated", "scaled", "child"):
        path = root / f"side-pose-{name}.json"
        log = path.with_suffix(".log")
        data = json.loads(path.read_text(encoding="utf-8"))
        text = log.read_text(encoding="utf-8")
        if (data.get("schema") != "eve.tests.easyroads-side-object-contracts"
                or data.get("schemaVersion") != 1 or data.get("sourcePose") != name
                or "SIDE_OBJECT_CONTRACT_PROBE_COMPLETE" not in text
                or "Application will terminate with return code 0" not in text):
            raise ValueError(f"Unconfirmed probe execution: {name}")
        rows = data["generationObservations"]
        if any(row.startswith("FAILED") for row in rows):
            raise ValueError(f"Probe failed: {name}")
        for required in ("activeState=True", "markerState0=True", "markerState1=True",
                         "markerState2=True", "generatedComponent=contract-cube id=710032"):
            if required not in rows:
                raise ValueError(f"Missing {required}: {name}")
        renderers = [row for row in rows if row.startswith("renderer=")]
        if len(renderers) != (14 if name == "child" else 7):
            raise ValueError(f"Unexpected generated renderer count: {name}")
        reports[name] = renderers
        evidence[name] = {"reportSha256": digest(path), "logSha256": digest(log)}
    for name in ("translated", "rotated"):
        if reports[name] != reports["identity"]:
            raise ValueError(f"Source root {name} changed generated renderers")
    for name, rows in reports.items():
        roots = [row for row in rows if row.startswith("renderer=contract-source ")]
        if len(roots) != 7:
            raise ValueError(f"Unexpected root renderer count: {name}")
        for index, row in enumerate(roots):
            expect(vector(row, "position"), (0, .02, 5.005 * index))
            expect(vector(row, "rotation"), (0, 0, 0))
            scale = (2, 3, 4) if name in ("scaled", "child") else (1, 1, 1)
            expect(vector(row, "scale"), scale)
            expect(vector(row, "boundsSize"), scale)
    children = [row for row in reports["child"] if row.startswith("renderer=contract-child ")]
    if len(children) != 7:
        raise ValueError("Unexpected child renderer count")
    for index, row in enumerate(children):
        expect(vector(row, "localPosition"), (1, .5, 2))
        expect(vector(row, "position"), (2, 1.52, 8 + 5.005 * index))
        expect(vector(row, "rotation"), (0, 35, 0))
        expect(vector(row, "boundsSize"), (2.253093, 2.25, 5.242913))
    return {"schema": "eve.tests.easyroads-rigid-prefab-pose-evidence", "schemaVersion": 1,
            "scope": "reference-only; straight road, Mesh type 0, default alignment, no bumpiness",
            "nativeParityVerified": False, "cases": evidence,
            "observed": {"rootTranslation": "overridden", "rootRotation": "overridden",
                         "rootScale": "preserved", "childLocalPose": "preserved",
                         "requestedSpacing": 5, "observedSpacing": 5.005,
                         "lastInstanceZ": 30.03, "roadSurfaceOffsetY": .02}}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/road-parity"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    evidence = verify(args.root)
    if args.output:
        args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print("PASS: five reference pose cases; native parity remains unverified")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
