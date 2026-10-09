"""Project audited UE SingleLayerWater controls into EV's native water model."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


SOURCE = (
    "/Game/Medieval_Docks/Water/Materials/WaterSurface/"
    "Water_Material_CustomMesh.Water_Material_CustomMesh"
)


def _number(value: float) -> str:
    encoded = format(float(value), ".9g")
    return encoded if any(c in encoded for c in ".eE") else encoded + ".0"


def build_profile(audit: dict) -> dict:
    material = next((item for item in audit["materials"] if item["asset"] == SOURCE), None)
    if material is None:
        raise ValueError(f"missing audited water material: {SOURCE}")
    if "MSM_SINGLE_LAYER_WATER" not in material.get("shading_model", ""):
        raise ValueError("audited custom-mesh water is not SingleLayerWater")
    scalar = material["scalarParameters"]
    vector = material["vectorParameters"]
    required = (
        "Default Disant Water Speed", "Default Near Water Scale", "WPO Flowmap",
        "Foam Distance", "Foam Texture Blend Width", "Foam Opacity",
        "Water Fresnel  Exponenth", "Refraction", "Caustics Brightness Adjust",
        "Caustics Finite Offset", "Water Roughness", "Water Specular",
    )
    missing = [name for name in required if name not in scalar]
    if missing:
        raise ValueError(f"missing audited water parameters: {', '.join(missing)}")
    # UE scene units are centimetres. EV's native water shader uses metres for
    # spatial controls, while refraction is a normalized screen-space offset.
    return {
        "sourceMaterial": SOURCE,
        "waveSpeed": scalar["Default Disant Water Speed"],
        "waveAmplitude": scalar["WPO Flowmap"] / 100.0,
        "waveScale": scalar["Default Near Water Scale"] / 100.0,
        "foamWidth": scalar["Foam Distance"] / 100.0,
        "foamSoftness": scalar["Foam Texture Blend Width"],
        # UE opacity modulates a multi-texture foam graph; EV strength drives a
        # direct procedural overlay. Preserve the source value separately and
        # use the calibrated cross-model energy adapter for equivalent weight.
        "foamStrength": scalar["Foam Opacity"] * 0.08,
        "fresnelPower": scalar["Water Fresnel  Exponenth"],
        "refractionStrength": min(scalar["Refraction"] * 0.01, 0.05),
        # UE brightness is applied inside a refracted SingleLayerWater graph;
        # EV adds caustics directly to scene color, so its energy is normalized.
        "causticsStrength": scalar["Caustics Brightness Adjust"] * 0.05,
        "causticsScale": scalar["Caustics Finite Offset"],
        "sourceRoughness": scalar["Water Roughness"],
        "sourceSpecular": scalar["Water Specular"],
        "sourceFoamOpacity": scalar["Foam Opacity"],
        "sourceCausticsBrightness": scalar["Caustics Brightness Adjust"],
        "sourceAbsorption": [vector["Absorption"][key] for key in ("r", "g", "b", "a")],
        "sourceScattering": [vector["Scattering"][key] for key in ("r", "g", "b", "a")],
    }


def write_nut(profile: dict, output: Path) -> None:
    lines = [
        "// Generated from UE's effective Water_Material_CustomMesh instance.",
        "// Spatial values are converted cm -> m; unsupported source terms remain audit fields.",
        "persist medievalWaterMaterialProfile = {",
        f'    sourceMaterial = "{profile["sourceMaterial"]}",',
    ]
    for key in (
        "waveSpeed", "waveAmplitude", "waveScale", "foamWidth", "foamSoftness",
        "foamStrength", "fresnelPower", "refractionStrength", "causticsStrength",
        "causticsScale", "sourceRoughness", "sourceSpecular", "sourceFoamOpacity",
        "sourceCausticsBrightness",
    ):
        lines.append(f"    {key} = {_number(profile[key])},")
    for key in ("sourceAbsorption", "sourceScattering"):
        lines.append(f"    {key} = [{', '.join(_number(v) for v in profile[key])}],")
    lines.extend(("}", ""))
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--material-audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profile = build_profile(json.loads(args.material_audit.read_text(encoding="utf-8")))
    write_nut(profile, args.output)
    print(f"wrote UE-derived water profile to {args.output}")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parents[2] / "scripts"))
    from utf8_stdio import enable_utf8_stdio

    enable_utf8_stdio()
    raise SystemExit(main())
