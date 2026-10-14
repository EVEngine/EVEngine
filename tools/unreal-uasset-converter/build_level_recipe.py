#!/usr/bin/env python3
"""Convert an Unreal level-audit report into an EVEngine Squirrel recipe."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any


SCHEMA = "eve.module-recipe/1"


def unreal_quaternion_to_ev_euler_degrees(
    quaternion: list[float],
) -> tuple[float, float, float]:
    """Convert UE X/Y/Z-up rotation to EVEngine yaw(Y), pitch(X), roll(Z)."""
    if len(quaternion) != 4:
        raise ValueError("rotationQuaternion must contain x, y, z, w")
    ux, uy, uz, w = (float(value) for value in quaternion)
    # GLTFExporter maps UE (X,Y,Z) to glTF/EVEngine (X,Z,Y). Because this basis
    # swap is a reflection, a rotation axis transforms as an axial vector:
    # det(B) * B * axis. The scalar part is unchanged.
    x, y, z = -ux, -uz, -uy
    length = math.sqrt(x * x + y * y + z * z + w * w)
    if length <= 1.0e-12:
        raise ValueError("rotationQuaternion must not be zero length")
    x, y, z, w = x / length, y / length, z / length, w / length
    m02 = 2.0 * (x * z + y * w)
    m10 = 2.0 * (x * y + z * w)
    m11 = 1.0 - 2.0 * (x * x + z * z)
    m12 = 2.0 * (y * z - x * w)
    m20 = 2.0 * (x * z - y * w)
    m22 = 1.0 - 2.0 * (x * x + y * y)
    pitch = math.asin(max(-1.0, min(1.0, -m12)))
    if abs(math.cos(pitch)) > 1.0e-7:
        yaw = math.atan2(m02, m22)
        roll = math.atan2(m10, m11)
    else:
        yaw = math.atan2(-m20, 1.0 - 2.0 * (y * y + z * z))
        roll = 0.0
    factor = 180.0 / math.pi
    return yaw * factor, pitch * factor, roll * factor


def output_file_for_asset(asset: str) -> str:
    """Return the converter's collision-safe GLB name for a /Game asset."""
    if not asset.startswith("/Game/"):
        raise ValueError(f"recipe assets must use /Game paths: {asset}")
    return asset.removeprefix("/").replace("/", "__") + ".glb"


def collect_placements(
    report: dict[str, Any], include_prefix: str, origin_cm: tuple[float, float, float],
    *, exclude_fragments: tuple[str, ...] = (), radius_cm: float | None = None,
    min_radius_cm: float | None = None,
    sample_rules: tuple[tuple[str, int], ...] = (),
) -> list[dict[str, Any]]:
    """Collect deterministic mesh-component and instance placements."""
    placements: list[dict[str, Any]] = []
    ox, oy, oz = origin_cm
    for actor in report.get("actors", []):
        for component in actor.get("components", []):
            asset = component.get("mesh", "")
            if not asset.startswith(include_prefix) or any(fragment in asset for fragment in exclude_fragments):
                continue
            # An InstancedStaticMeshComponent's transform is a container-space
            # transform, not an additional rendered instance.  The audit stores
            # resolved world transforms in `instances`; ordinary mesh components
            # have an empty list and use the component transform itself.
            instances = component.get("instances", [])
            transforms = instances if instances else [component.get("transform", {})]
            for transform in transforms:
                location = transform.get("locationCentimeters", [0.0, 0.0, 0.0])
                if radius_cm is not None:
                    dx = float(location[0]) - ox
                    dy = float(location[1]) - oy
                    if dx * dx + dy * dy > radius_cm * radius_cm:
                        continue
                if min_radius_cm is not None:
                    dx = float(location[0]) - ox
                    dy = float(location[1]) - oy
                    if dx * dx + dy * dy < min_radius_cm * min_radius_cm:
                        continue
                rotation = transform.get("rotationDegrees", [0.0, 0.0, 0.0])
                quaternion = transform.get("rotationQuaternion")
                if quaternion is not None:
                    yaw, pitch, roll = unreal_quaternion_to_ev_euler_degrees(quaternion)
                else:
                    # Legacy audit fallback. New authoritative reports always
                    # carry a quaternion so compound rotations are unambiguous.
                    yaw, pitch, roll = (
                        -float(rotation[2]), -float(rotation[0]), -float(rotation[1])
                    )
                scale = transform.get("scale", [1.0, 1.0, 1.0])
                placements.append(
                    {
                        "asset": asset,
                        "file": output_file_for_asset(asset),
                        # Match Unreal GLTFExporter: UE (X,Y,Z) -> EV (X,Z,Y).
                        "x": (float(location[0]) - ox) / 100.0,
                        "y": (float(location[2]) - oz) / 100.0,
                        "z": (float(location[1]) - oy) / 100.0,
                        "yaw": yaw,
                        "pitch": pitch,
                        "roll": roll,
                        # Scale follows the same axis basis as positions.
                        "scale": [float(scale[0]), float(scale[2]), float(scale[1])],
                    }
                )
    for item in placements:
        divisor = next(
            (value for fragment, value in sample_rules if fragment in item["asset"]), 1
        )
        item["sampleDivisor"] = divisor
        fingerprint = json.dumps(item, sort_keys=True, separators=(",", ":"))
        item["sampleBucket"] = int.from_bytes(
            hashlib.sha256(fingerprint.encode("utf-8")).digest()[:8], "big"
        ) % divisor
    placements = [item for item in placements if item.pop("sampleBucket") == 0]
    for item in placements:
        item.pop("sampleDivisor")
    placements.sort(
        key=lambda item: (
            item["asset"], item["y"], item["z"], item["x"], item["yaw"],
            item["pitch"], item["roll"], item["scale"]
        )
    )
    return placements


def render_squirrel(
    source_level: str,
    include_prefix: str,
    origin_cm: tuple[float, float, float],
    placements: list[dict[str, Any]],
    variable: str = "medievalBuilding01aRecipe",
) -> str:
    """Render a self-contained Squirrel data file."""
    lines = [
        "// Generated by tools/unreal-uasset-converter/build_level_recipe.py; do not hand-edit.",
        f'// schema={SCHEMA} sourceLevel={source_level}',
        f'// includePrefix={include_prefix} originCm={origin_cm[0]:.6f},{origin_cm[1]:.6f},{origin_cm[2]:.6f}',
        f"persist {variable} = [",
    ]
    for item in placements:
        sx, sy, sz = item["scale"]
        lines.append(
            '    { file = "%s", x = %.6f, y = %.6f, z = %.6f, yawDegrees = %.6f, '
            "pitchDegrees = %.6f, rollDegrees = %.6f, sx = %.6f, sy = %.6f, sz = %.6f },"
            % (
                item["file"], item["x"], item["y"], item["z"], item["yaw"],
                item["pitch"], item["roll"], sx, sy, sz
            )
        )
    lines.extend(["]", ""])
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--include-prefix",
        default="/Game/Medieval_Docks/Static_Meshes/Building_Kit/",
        help="Only components below this Unreal asset path are emitted.",
    )
    parser.add_argument(
        "--exclude-fragment", action="append", default=[],
        help="Exclude assets containing this text; may be repeated.",
    )
    parser.add_argument("--radius-cm", type=float, help="Optional horizontal radius around the origin.")
    parser.add_argument(
        "--min-radius-cm", type=float,
        help="Optional inner horizontal radius to exclude around the origin.",
    )
    parser.add_argument(
        "--sample-fragment", action="append", default=[], metavar="TEXT=N",
        help="Deterministically retain about one in N placements whose asset contains TEXT.",
    )
    parser.add_argument("--variable", default="medievalBuilding01aRecipe")
    parser.add_argument(
        "--origin-cm", nargs=3, type=float, metavar=("X", "Y", "Z"), required=True
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    if report.get("schema") != "eve.unreal-level-audit/1":
        raise ValueError("input is not an eve.unreal-level-audit/1 report")
    origin = tuple(args.origin_cm)
    if args.radius_cm is not None and args.radius_cm <= 0.0:
        raise ValueError("--radius-cm must be positive")
    if args.min_radius_cm is not None and args.min_radius_cm < 0.0:
        raise ValueError("--min-radius-cm must not be negative")
    if (args.radius_cm is not None and args.min_radius_cm is not None
            and args.min_radius_cm >= args.radius_cm):
        raise ValueError("--min-radius-cm must be smaller than --radius-cm")
    sample_rules: list[tuple[str, int]] = []
    for rule in args.sample_fragment:
        fragment, separator, divisor_text = rule.rpartition("=")
        if not separator or not fragment:
            raise ValueError("--sample-fragment must use TEXT=N")
        divisor = int(divisor_text)
        if divisor <= 0:
            raise ValueError("--sample-fragment divisor must be positive")
        sample_rules.append((fragment, divisor))
    placements = collect_placements(
        report, args.include_prefix, origin,
        exclude_fragments=tuple(args.exclude_fragment), radius_cm=args.radius_cm,
        min_radius_cm=args.min_radius_cm,
        sample_rules=tuple(sample_rules),
    )
    if not placements:
        raise ValueError("the selected prefix produced no recipe placements")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        render_squirrel(report.get("level", ""), args.include_prefix, origin, placements, args.variable),
        encoding="utf-8",
    )
    print(f"wrote {len(placements)} placements to {args.output}")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parents[2] / "scripts"))
    from utf8_stdio import enable_utf8_stdio

    enable_utf8_stdio()
    raise SystemExit(main())
