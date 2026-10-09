"""Compare exported glTF material semantics with an Unreal Editor audit."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def _glb_document(path: Path) -> dict:
    encoded = path.read_bytes()
    if len(encoded) < 20 or encoded[:4] != b"glTF":
        raise ValueError(f"{path.name}: not a GLB")
    length, kind = struct.unpack_from("<II", encoded, 12)
    if kind != 0x4E4F534A:
        raise ValueError(f"{path.name}: first GLB chunk is not JSON")
    return json.loads(encoded[20 : 20 + length].decode("utf-8").rstrip("\0 \t\r\n"))


def _short_name(object_path: str) -> str:
    return object_path.rsplit("/", 1)[-1].split(".", 1)[0]


def _expected_alpha(blend_mode: str) -> str:
    if "BLEND_MASKED" in blend_mode:
        return "MASK"
    if "BLEND_OPAQUE" in blend_mode:
        return "OPAQUE"
    return "BLEND"


def _portability(material: dict) -> str:
    shading = material.get("shading_model", "")
    if "MSM_DEFAULT_LIT" in shading or "MSM_UNLIT" in shading:
        return "core-gltf"
    if "MSM_SINGLE_LAYER_WATER" in shading:
        return "native-water-required"
    if "MSM_FROM_MATERIAL_EXPRESSION" in shading:
        return "material-graph-runtime-required"
    if "MSM_CLOTH" in shading:
        return "cloth-shading-runtime-required"
    if "MSM_SUBSURFACE" in shading:
        return "subsurface-runtime-required"
    return "specialized-runtime-required"


def build_report(asset_audit: dict, material_audit: dict, manifest: dict, asset_root: Path) -> dict:
    meshes = {
        entry["package"]: entry
        for entry in asset_audit["assets"]
        if entry.get("class") == "StaticMesh"
    }
    materials = {entry["asset"]: entry for entry in material_audit["materials"]}
    rows = []
    failures = []
    for artifact in manifest["artifacts"]:
        if artifact.get("assetClass") != "StaticMesh":
            continue
        source = artifact["sourceAsset"]
        mesh = meshes.get(source)
        if mesh is None:
            failures.append({"sourceAsset": source, "error": "missing UE StaticMesh audit"})
            continue
        assigned = []
        for slot in mesh.get("materialSlots", []):
            material_path = slot.get("material")
            material = materials.get(material_path)
            if material:
                assigned.append((material_path, material, _short_name(material_path)))
        document = _glb_document(asset_root / artifact["path"])
        referenced = {
            primitive["material"]
            for gltf_mesh in document.get("meshes", [])
            for primitive in gltf_mesh.get("primitives", [])
            if "material" in primitive
        }
        for index in sorted(referenced):
            exported = document.get("materials", [])[index]
            exported_name = exported.get("name", "")
            candidates = [item for item in assigned if exported_name == item[2] or exported_name.startswith(item[2] + "_")]
            if not candidates:
                if exported_name == "WorldGridMaterial":
                    ue = {
                        "shading_model": "<MaterialShadingModel.MSM_DEFAULT_LIT: 1>",
                        "blend_mode": "<BlendMode.BLEND_OPAQUE: 0>",
                        "two_sided": False,
                        "opacity_mask_clip_value": 0.33329999446868896,
                    }
                    material_path = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
                    candidates = [(material_path, ue, exported_name)]
                else:
                    failures.append({
                        "sourceAsset": source,
                        "exportedMaterial": exported_name,
                        "error": "no assigned UE material name matches exported material",
                    })
                    continue
            material_path, ue, _ = max(candidates, key=lambda item: len(item[2]))
            actual_alpha = exported.get("alphaMode", "OPAQUE")
            expected_alpha = _expected_alpha(ue.get("blend_mode", ""))
            expected_cutoff = float(ue.get("opacity_mask_clip_value", 0.5))
            actual_cutoff = float(exported.get("alphaCutoff", 0.5))
            alpha_matches = actual_alpha == expected_alpha
            cutoff_matches = actual_alpha != "MASK" or abs(actual_cutoff - expected_cutoff) < 1e-6
            two_sided_matches = bool(exported.get("doubleSided", False)) == bool(ue.get("two_sided", False))
            row = {
                "sourceAsset": source,
                "outputFile": artifact["path"],
                "exportedMaterial": exported_name,
                "ueMaterial": material_path,
                "shadingModel": ue.get("shading_model"),
                "portability": _portability(ue),
                "alphaMode": {"ueExpected": expected_alpha, "gltf": actual_alpha, "matches": alpha_matches},
                "alphaCutoff": {"ue": expected_cutoff, "gltf": actual_cutoff, "matches": cutoff_matches},
                "doubleSided": {
                    "ue": bool(ue.get("two_sided", False)),
                    "gltf": bool(exported.get("doubleSided", False)),
                    "matches": two_sided_matches,
                },
            }
            rows.append(row)
    mismatch_count = sum(
        not row["alphaMode"]["matches"]
        or not row["alphaCutoff"]["matches"]
        or not row["doubleSided"]["matches"]
        for row in rows
    )
    portability = {}
    for row in rows:
        key = row["portability"]
        portability[key] = portability.get(key, 0) + 1
    return {
        "schema": "eve.unreal-gltf-material-parity/1",
        "staticMeshArtifactCount": sum(a.get("assetClass") == "StaticMesh" for a in manifest["artifacts"]),
        "referencedMaterialCount": len(rows),
        "mappingFailureCount": len(failures),
        "semanticMismatchCount": mismatch_count,
        "portabilityCounts": dict(sorted(portability.items())),
        "failures": failures,
        "materials": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-audit", type=Path, required=True)
    parser.add_argument("--material-audit", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = build_report(
        json.loads(args.asset_audit.read_text(encoding="utf-8")),
        json.loads(args.material_audit.read_text(encoding="utf-8")),
        json.loads(args.manifest.read_text(encoding="utf-8")),
        args.manifest.parent,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"materials={report['referencedMaterialCount']} "
        f"mappingFailures={report['mappingFailureCount']} "
        f"semanticMismatches={report['semanticMismatchCount']}"
    )
    if report["mappingFailureCount"] or report["semanticMismatchCount"]:
        raise SystemExit(1)


if __name__ == "__main__":
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts import utf8_stdio

    utf8_stdio.enable_utf8_stdio()
    main()
