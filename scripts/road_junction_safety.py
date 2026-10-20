"""Check horizontal guardrail footprints against actual driving-surface triangles."""

from __future__ import annotations

import json
import math
import re
from pathlib import Path


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def area(points):
    return abs(sum(cross((0, 0), a, b) for a, b in zip(points, points[1:] + points[:1]))) * 0.5


def overlap(first, second):
    """Return positive-area triangle intersection, including edge-only crossings."""
    if area(first) < 1e-10 or area(second) < 1e-10:
        return 0.0
    polygon = list(first)
    sign = 1 if cross(*second) > 0 else -1
    for a, b in zip(second, second[1:] + second[:1]):
        source, polygon = polygon, []
        if not source:
            return 0.0
        previous = source[-1]
        old = sign * cross(a, b, previous)
        for current in source:
            new = sign * cross(a, b, current)
            if (old >= 0) != (new >= 0):
                t = old / (old - new)
                polygon.append(tuple(previous[i] + t * (current[i] - previous[i]) for i in (0, 1)))
            if new >= 0:
                polygon.append(current)
            previous, old = current, new
    return area(polygon) if len(polygon) >= 3 else 0.0


def audit(road, facility):
    def bounds(t):
        return (
            min(p[0] for p in t),
            min(p[1] for p in t),
            max(p[0] for p in t),
            max(p[1] for p in t),
        )

    for t in road + facility:
        if len(t) != 3 or not all(math.isfinite(v) for p in t for v in p):
            raise ValueError("Invalid footprint triangle")
    roads = [(t, bounds(t)) for t in road if area(t) > 1e-10]
    maximum, crossings = 0.0, 0
    for triangle in facility:
        if area(triangle) < 1e-10:
            continue
        x0, z0, x1, z1 = bounds(triangle)
        for surface, (a, b, c, d) in roads:
            if x1 <= a or c <= x0 or z1 <= b or d <= z0:
                continue
            value = overlap(triangle, surface)
            maximum = max(maximum, value)
            crossings += value > 1e-4
    return {
        "maxOverlapSquareMeters": maximum,
        "overlappingPairs": crossings,
        "clear": crossings == 0,
    }


def native_footprint(directory: Path):
    text = (directory / "mesh_data.nut").read_text(encoding="utf-8")
    text = text.removeprefix("road_data <-").strip().removesuffix(";")
    text = re.sub(r"\b(positions|normals|uvs|indices)\s*=", r'"\1":', text)
    data = json.loads(re.sub(r",\s*([}\]])", r"\1", text))
    groups = json.loads((directory / "groups.json").read_text(encoding="utf-8"))["groups"]
    result = {"road": [], "facility": []}
    for group, mesh in zip(groups, data, strict=True):
        if group not in ("asphalt", "curb"):
            continue
        positions, indices = mesh["positions"], mesh["indices"]
        for i in range(0, len(indices), 3):
            # Reflect world Z into the same coordinate system as Unity.
            result["road" if group == "asphalt" else "facility"].append(
                [(positions[v * 3], -positions[v * 3 + 2]) for v in indices[i : i + 3]]
            )
    return result


def validate_case(directory: Path):
    native = native_footprint(directory)
    reference = json.loads((directory / "unity-footprint.json").read_text(encoding="utf-8"))
    if (
        reference.get("schema") != "eve.tests.road-junction-footprint"
        or reference.get("version") != 1
    ):
        raise ValueError("Unsupported SDK footprint")
    unity = {
        k: [[(t[p]["x"], t[p]["y"]) for p in ("a", "b", "c")] for t in reference[k]]
        for k in ("road", "facility")
    }
    if any(not mesh[k] for mesh in (native, unity) for k in ("road", "facility")):
        raise ValueError("Empty road or guardrail footprint")
    return {"EVEngine": audit(**native), "Unity": audit(**unity)}
