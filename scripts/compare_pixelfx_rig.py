#!/usr/bin/env python3
"""Keep skeletal numeric and sparse/spatial pixel differences visible independently."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import utf8_stdio


def compare(reference: dict, actual: dict, tolerance: float = 1e-10) -> dict:
    """Compare complete sparse snapshots; identical totals never establish pixel equivalence."""
    if reference.get("schema") == "eve.pixelfx.track-probe/v1":
        return compare_track(reference, actual, tolerance)
    for value in (reference, actual):
        if value.get("schema") != "eve.pixelfx.rig-probe/v1":
            raise ValueError("Unsupported rig snapshot schema")
        count = len(value["color"])
        if any(len(value[key]) != count for key in ("xs", "ys", "src", "ink")):
            raise ValueError("Rig sparse array lengths differ")
    joint_names = set(reference["joints"])
    if joint_names != set(actual["joints"]):
        raise ValueError("Rig joint sets differ")
    errors = []
    for name in sorted(joint_names):
        a, b = reference["joints"][name], actual["joints"][name]
        if len(a) != 3 or len(b) != 3 or any(not math.isfinite(v) for v in (*a, *b)):
            raise ValueError("Expected finite 3D joints")
        errors.extend(abs(x - y) for x, y in zip(a, b))
    numeric = max(errors, default=0)
    shortfall = abs(reference["shortfall"] - actual["shortfall"])
    depth = abs(reference["z"] - actual["z"])
    sparse = {}
    for key in ("xs", "ys", "color", "src", "ink", "bounds"):
        a, b = reference[key], actual[key]
        sparse[key] = sum(x != y for x, y in zip(a, b)) + abs(len(a) - len(b))

    def cells(value):
        out = {}
        for x, y, color, source, ink in zip(value["xs"], value["ys"], value["color"], value["src"], value["ink"]):
            if (x, y) in out:
                raise ValueError("Duplicate sparse rig cell")
            out[x, y] = (color, source, ink)
        return out

    a, b = cells(reference), cells(actual)
    differences = [{"cell": list(key), "expected": a.get(key), "actual": b.get(key)}
                   for key in sorted(a.keys() | b.keys()) if a.get(key) != b.get(key)]
    parts = []
    if "parts" in reference or "parts" in actual:
        if "parts" not in reference or "parts" not in actual:
            raise ValueError("Part snapshot sets differ")
        if [(p["name"], p["part"]) for p in reference["parts"]] != [(p["name"], p["part"]) for p in actual["parts"]]:
            raise ValueError("Part snapshot order/identities differ")
        for a_part, b_part in zip(reference["parts"], actual["parts"]):
            for part in (a_part, b_part):
                if len(part["xs"]) != len(part["color"]) or len(part["ys"]) != len(part["color"]):
                    raise ValueError("Part sparse array lengths differ")
            arrays = {key: sum(x != y for x, y in zip(a_part[key], b_part[key])) + abs(len(a_part[key]) - len(b_part[key]))
                      for key in ("xs", "ys", "color")}
            error = max(abs(a_part[k] - b_part[k]) for k in ("cx", "cy", "z"))
            parts.append({"name": a_part["name"], "differentEntries": arrays, "maximumNumericError": error,
                          "passed": not any(arrays.values()) and error <= tolerance})
    return {"schema": "eve.pixelfx-rig-comparison/v1", "maximumJointError": numeric,
            "shortfallError": shortfall, "meanDepthError": depth, "numericTolerance": tolerance,
            "differentSparseEntries": sparse, "differentSpatialCells": len(differences),
            "examples": differences[:16], "parts": parts, "exactPixels": not any(sparse.values()) and all(p["passed"] for p in parts),
            "passed": not any(sparse.values()) and numeric <= tolerance and shortfall <= tolerance and depth <= tolerance and all(p["passed"] for p in parts)}


# Independent effective defaults from the pinned rig contract; this is an oracle, not runtime registration.
POSE_DEFAULTS = {"lean": 0, "crouch": 0, "yaw": 0, "fx": -3, "fy": -15, "bx": 5, "by": -15,
                 "ffx": -2, "ffy": -4, "fbx": 3, "fby": -4, "x": 0, "y": 0, "sa": 0, "wl": 1,
                 "wz": 1, "twist": 0, "hy": 0, "hx": 0, "sb": math.pi, "ghost": 0, "face": 1}
POSE_FLAGS = ("two", "armsFront", "toeF", "toeB", "pull", "nocked")
POSE_WEAPONS = ("weapon", "offWeapon")
POSE_OPTIONAL = ("turn", "fz", "bz", "fzz", "bzz")


def compare_track(reference: dict, actual: dict, tolerance: float = 1e-10) -> dict:
    """Compare actual source poseAt output with owning effective poses and public skeletons."""
    if actual.get("schema") != "eve.pixelfx.track-probe/v1" or len(reference["frames"]) != len(actual["frames"]):
        raise ValueError("Track snapshot schema/count mismatch")
    numeric = joint = shortfall = 0.0
    metadata, presence = [], []
    for a, b in zip(reference["frames"], actual["frames"]):
        if a["time"] != b["time"]:
            raise ValueError("Track sample times differ")
        expected, value = a["pose"], b["pose"]
        for name, default in POSE_DEFAULTS.items():
            x, y = expected.get(name, default), value[name]
            if any(not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) for v in (x, y)):
                raise ValueError(f"Expected finite numeric pose field {name}")
            numeric = max(numeric, abs(x - y))
        for name in POSE_OPTIONAL:
            if (name in expected) != (name in value):
                presence.append({"time": a["time"], "field": name})
            elif name in expected:
                numeric = max(numeric, abs(expected[name] - value[name]))
        for name in (*POSE_FLAGS, *POSE_WEAPONS):
            default = False if name in POSE_FLAGS else "none"
            x, y = expected.get(name, default), value[name]
            if type(x) is not type(y) or x != y:
                metadata.append({"time": a["time"], "field": name, "expected": x, "actual": y})
        if set(a["joints"]) != set(b["joints"]):
            raise ValueError("Track joint sets differ")
        for name, position in a["joints"].items():
            if len(position) != 3 or len(b["joints"][name]) != 3:
                raise ValueError("Expected 3D track joints")
            for x, y in zip(position, b["joints"][name]):
                if not math.isfinite(x) or not math.isfinite(y):
                    raise ValueError("Expected finite track joints")
                joint = max(joint, abs(x - y))
        shortfall = max(shortfall, abs(a["shortfall"] - b["shortfall"]))
    return {"schema": "eve.pixelfx-track-comparison/v1", "samples": len(reference["frames"]),
            "maximumPoseError": numeric, "maximumJointError": joint, "maximumShortfallError": shortfall,
            "metadataDifferences": len(metadata), "presenceDifferences": len(presence),
            "examples": (metadata + presence)[:16],
            "passed": max(numeric, joint, shortfall) <= tolerance and not metadata and not presence}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = compare(json.loads(args.reference.read_text(encoding="utf-8-sig")),
                     json.loads(args.native.read_text(encoding="utf-8-sig")))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if "samples" in report:
        print(f"Track: {report['samples']} samples; pose error {report['maximumPoseError']:.3g}; passed={report['passed']}")
    else:
        print(f"Rig: {report['differentSpatialCells']} different cells; joint error {report['maximumJointError']:.3g}; passed={report['passed']}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
