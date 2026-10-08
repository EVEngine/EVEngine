"""Build EV tree-wind profiles from audited Unreal mesh and material data."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _fmt(value: float) -> str:
    encoded = format(float(value), ".9g")
    return encoded if any(marker in encoded for marker in (".", "e", "E")) else encoded + ".0"


def build_profiles(asset_audit: dict, material_audit: dict, manifest: dict) -> list[dict]:
    meshes = {
        entry["package"]: entry
        for entry in asset_audit["assets"]
        if entry.get("class") == "StaticMesh"
    }
    materials = {entry["asset"]: entry for entry in material_audit["materials"]}
    profiles = []
    for artifact in manifest["artifacts"]:
        if artifact.get("assetClass") != "StaticMesh":
            continue
        mesh = meshes.get(artifact["sourceAsset"])
        if not mesh:
            continue
        candidates = []
        for slot in mesh.get("materialSlots", []):
            material = materials.get(slot.get("material"))
            scalar = material.get("scalarParameters", {}) if material else {}
            if "Wind Branch Intensity" in scalar:
                candidates.append(material)
        if not candidates:
            continue
        # A mesh's atlas, bark, and detail instances expose the same master-wind
        # controls. Prefer the leaf atlas (largest leaf intensity) deterministically.
        material = max(
            candidates,
            key=lambda item: (
                item.get("scalarParameters", {}).get("Wind Leave Intensity", 0.0),
                item["asset"],
            ),
        )
        scalar = material["scalarParameters"]
        vector = material.get("vectorParameters", {}).get("Wind Global Direction", {})
        size = mesh["boundsCentimeters"]["size"]
        profiles.append(
            {
                "file": artifact["path"],
                "sourceAsset": artifact["sourceAsset"],
                "sourceMaterial": material["asset"],
                "width": max(size[0], size[1]) / 100.0,
                "height": size[2] / 100.0,
                "directionX": vector.get("r", 0.05),
                "directionZ": vector.get("g", 0.0),
                # EV's PW-compatible shader consumes displacement in metres.
                # These fixed adapters keep UE's relative branch/leaf hierarchy
                # while avoiding centimetre-sized movement in metre-space meshes.
                "mainFlex": scalar["Wind Branch Intensity"] * 0.18,
                "branchFlex": scalar.get("Wind Branch Secondary Intensity", 1.0) * 0.12,
                "leafFlex": scalar.get("Wind Leave Intensity", 0.0) * 0.018,
                "mainFrequency": scalar.get("Wind Branch Speed", 1.0),
                "branchFrequency": scalar.get("Wind Branch Secondary Speed", 1.0),
                "leafFrequency": scalar.get("Wind Leave Speed", 1.0),
                # The UE master exposes this camera range in metres after its
                # world-unit conversion; EV performs the same radial attenuation.
                "maximumDistance": scalar.get("Wind Distance", 100.0),
                "strength": min(max(scalar.get("Wind Local Intensity", 1.0), 0.0), 1.2),
                "bendFactor": max(scalar.get("Wind Foliage Base", 1.0), 0.05),
            }
        )
    return sorted(profiles, key=lambda item: item["file"])


def write_nut(profiles: list[dict], output: Path) -> None:
    lines = [
        "// Generated from ue-asset-audit.json + ue-material-audit.json.",
        "// Conversion policy: UE bounds cm -> EV metres; wind magnitudes use the",
        "// documented metre-space adapters in build_foliage_wind_profiles.py.",
        "persist medievalFoliageWindProfiles = {",
    ]
    for item in profiles:
        fields = ", ".join(
            f"{name} = {_fmt(item[name])}"
            for name in (
                "width", "height", "directionX", "directionZ", "mainFlex",
                "branchFlex", "leafFlex", "mainFrequency", "branchFrequency",
                "leafFrequency", "maximumDistance", "strength", "bendFactor",
            )
        )
        lines.append(f'    ["{item["file"]}"] = {{ {fields} }},')
    lines.extend(("}", ""))
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-audit", type=Path, required=True)
    parser.add_argument("--material-audit", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profiles = build_profiles(
        json.loads(args.asset_audit.read_text(encoding="utf-8")),
        json.loads(args.material_audit.read_text(encoding="utf-8")),
        json.loads(args.manifest.read_text(encoding="utf-8")),
    )
    write_nut(profiles, args.output)
    print(f"wrote {len(profiles)} UE-derived foliage wind profiles to {args.output}")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parents[2] / "scripts"))
    from utf8_stdio import enable_utf8_stdio

    enable_utf8_stdio()
    raise SystemExit(main())
