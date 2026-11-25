#!/usr/bin/env python3
"""Compare PixelFX raw color/light planes without reducing spatial errors to image totals."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import utf8_stdio


def compare_bytes(reference: bytes, actual: bytes, width: int) -> dict:
    """Return spatial and channel evidence; lengths must be identical RGBA8 planes."""
    if width <= 0 or len(reference) != len(actual) or len(reference) % 4:
        raise ValueError("Raw RGBA planes require equal byte lengths and a positive width")
    if len(reference) % (width * 4):
        raise ValueError("Plane byte length is not a whole number of rows")
    if reference == actual:
        return {
            "pixels": len(reference) // 4,
            "differentPixels": 0,
            "differentColorPixels": 0,
            "differentAlphaPixels": 0,
            "differentCoveragePixels": 0,
            "maximumChannelError": 0,
            "totalAbsoluteChannelError": 0,
            "differenceBounds": None,
            "examples": [],
            "exact": True,
        }
    different = color = alpha = coverage = maximum = total_error = 0
    examples = []
    bounds = None
    for offset in range(0, len(reference), 4):
        expected = reference[offset:offset + 4]
        observed = actual[offset:offset + 4]
        if expected == observed:
            continue
        different += 1
        color += expected[:3] != observed[:3]
        alpha += expected[3] != observed[3]
        coverage += (expected == bytes(4)) != (observed == bytes(4))
        errors = [abs(a - b) for a, b in zip(expected, observed)]
        maximum = max(maximum, *errors)
        total_error += sum(errors)
        pixel = offset // 4
        x, y = pixel % width, pixel // width
        if bounds is None:
            bounds = [x, y, x, y]
        else:
            bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
        if len(examples) < 16:
            examples.append({"x": x, "y": y, "reference": list(expected), "actual": list(observed)})
    return {
        "pixels": len(reference) // 4,
        "differentPixels": different,
        "differentColorPixels": color,
        "differentAlphaPixels": alpha,
        "differentCoveragePixels": coverage,
        "maximumChannelError": maximum,
        "totalAbsoluteChannelError": total_error,
        "differenceBounds": bounds,
        "examples": examples,
        "exact": different == 0,
    }


def accepts_plane(result: dict, plane: str, color_tolerance: int = 0) -> bool:
    """Keep exact evidence while permitting an explicit RGB-only quantization contract."""
    if plane not in ("color", "light") or color_tolerance not in (0, 1):
        raise ValueError("Expected color/light and a color tolerance of zero or one")
    return result["exact"] or (
        plane == "color" and color_tolerance == 1
        and result["maximumChannelError"] <= 1
        and result["differentAlphaPixels"] == 0
        and result["differentCoveragePixels"] == 0
    )


def compare_actor_visibility(reference: dict, actual: dict | None) -> dict:
    """Compare optional v1 body-policy evidence; unknown metadata keys are ignored."""
    frames = reference["frames"]
    present = ["actorVisible" in frame for frame in frames]
    if not any(present):
        return {"available": False, "exact": True, "differences": []}
    if not all(present) or actual is None:
        raise ValueError("Complete native actor visibility evidence is required")
    if actual.get("schema") != "eve.pixelfx-actor-visibility/v1" or actual.get("id") != reference["id"]:
        raise ValueError("Actor visibility schema/id mismatch")
    rows = actual.get("frames")
    if not isinstance(rows, list) or len(rows) != len(frames):
        raise ValueError("Actor visibility frame count mismatch")
    differences = []
    for expected, observed in zip(frames, rows):
        if not isinstance(observed, dict) or type(expected["actorVisible"]) is not bool or type(observed.get("actorVisible")) is not bool:
            raise ValueError("Actor visibility must be boolean")
        if type(observed.get("time")) not in (int, float) or observed["time"] != expected["time"]:
            raise ValueError("Actor visibility sample time mismatch")
        if expected["actorVisible"] != observed["actorVisible"]:
            differences.append({"time": expected["time"], "reference": expected["actorVisible"], "actual": observed["actorVisible"]})
    return {"available": True, "exact": not differences, "differences": differences}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path, help="Directory containing reference frames.json")
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--color-tolerance", type=int, choices=(0, 1), default=0,
                        help="Optional RGB LSB bound; coverage, alpha and light remain exact")
    args = parser.parse_args()
    manifest = json.loads((args.reference / "frames.json").read_text(encoding="utf-8"))
    results = []
    for frame in manifest["frames"]:
        for plane in ("color", "light"):
            name = frame[plane]
            expected = (args.reference / name).read_bytes()
            actual = (args.native / name).read_bytes()
            result = compare_bytes(expected, actual, manifest["w"])
            results.append({"time": frame["time"], "plane": plane, "file": name, **result,
                            "accepted": accepts_plane(result, plane, args.color_tolerance)})
    visibility_path = args.native / "actor-visibility.json"
    visibility = compare_actor_visibility(manifest, json.loads(visibility_path.read_text(encoding="utf-8")) if visibility_path.exists() else None)
    report = {"schema": "eve.pixelfx-frame-comparison/v1", "id": manifest["id"], "results": results,
              "exact": all(item["exact"] for item in results) and visibility["exact"],
              "actorVisibility": visibility,
              "colorTolerance": args.color_tolerance,
              "accepted": all(item["accepted"] for item in results) and visibility["exact"]}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{manifest['id']}: {sum(item['exact'] for item in results)}/{len(results)} exact planes")
    if visibility["available"]:
        print(f"  actor visibility: {len(visibility['differences'])} differing samples")
    if args.color_tolerance:
        print(f"  accepted {sum(item['accepted'] for item in results)}/{len(results)} planes; "
              f"RGB tolerance={args.color_tolerance}, coverage/alpha/light exact")
    for item in results:
        if not item["exact"]:
            print(f"  t={item['time']:.9f} {item['plane']}: {item['differentPixels']} different pixels; "
                  f"bounds={item['differenceBounds']}, max-channel-error={item['maximumChannelError']}")
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
