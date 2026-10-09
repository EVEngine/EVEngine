"""Cross-check every audited UE StaticMesh against its converted GLB artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _glb_material_names(path: Path) -> list[str]:
    with path.open("rb") as stream:
        header = stream.read(20)
        if len(header) != 20 or header[:4] != b"glTF":
            raise ValueError("not a GLB file")
        json_length, json_type = struct.unpack_from("<I4s", header, 12)
        if json_type != b"JSON":
            raise ValueError("GLB first chunk is not JSON")
        document = json.loads(stream.read(json_length).decode("utf-8"))
    referenced = {
        primitive["material"]
        for mesh in document.get("meshes", [])
        for primitive in mesh.get("primitives", [])
        if "material" in primitive
    }
    materials = document.get("materials", [])
    return [str(materials[index].get("name", "")) for index in sorted(referenced)]


def build_report(audit: dict[str, Any], manifest: dict[str, Any], asset_root: Path) -> dict[str, Any]:
    static_meshes = {
        entry["package"]: entry
        for entry in audit.get("assets", [])
        if entry.get("class") == "StaticMesh"
    }
    artifacts = {
        entry["sourceAsset"]: entry
        for entry in manifest.get("artifacts", [])
        if entry.get("assetClass") == "StaticMesh"
    }
    rows: list[dict[str, Any]] = []
    counts = {
        "audited": len(static_meshes),
        "converted": 0,
        "missing": 0,
        "invalid": 0,
        "materialSlotMismatch": 0,
        "hashMismatch": 0,
        "withoutBaseColorTexture": 0,
        "specialMaterialModel": 0,
    }
    for source, mesh in sorted(static_meshes.items()):
        slots = mesh.get("materialSlots", [])
        assigned_materials = [slot.get("material", "") for slot in slots if slot.get("material")]
        expected_materials = len(assigned_materials)
        artifact = artifacts.get(source)
        row: dict[str, Any] = {
            "sourceAsset": source,
            "ue": {
                "lodCount": mesh.get("lodCount"),
                "materialSlotCount": len(slots),
                "assignedMaterialCount": expected_materials,
                "materials": [slot.get("material", "") for slot in slots],
                "boundsCentimeters": mesh.get("boundsCentimeters"),
            },
        }
        issues: list[str] = []
        if artifact is None:
            issues.append("missing-converted-artifact")
            counts["missing"] += 1
        else:
            counts["converted"] += 1
            path = asset_root / artifact["outputFile"]
            content = artifact.get("content", {})
            actual_materials = int(content.get("referencedMaterialCount", 0))
            if not path.is_file():
                issues.append("missing-output-file")
            else:
                if path.stat().st_size != artifact.get("bytes"):
                    issues.append("byte-size-mismatch")
                if _sha256(path) != artifact.get("sha256"):
                    issues.append("sha256-mismatch")
                    counts["hashMismatch"] += 1
                exported_material_names = _glb_material_names(path)
                ue_material_names = [value.rsplit("/", 1)[-1].split(".", 1)[0] for value in assigned_materials]
                unmatched_material_names = [
                    name for name in exported_material_names
                    if not any(name == expected or name.startswith(expected + "_") for expected in ue_material_names)
                ]
                if unmatched_material_names and assigned_materials:
                    issues.append("unmatched-exported-material")
            if int(content.get("meshCount", 0)) < 1 or int(content.get("primitiveCount", 0)) < 1:
                issues.append("empty-mesh")
            if int(content.get("primitivesWithoutMaterial", 0)) != 0:
                issues.append("primitive-without-material")
            generated_default_material = expected_materials == 0 and actual_materials == 1
            unused_assigned_material_count = max(expected_materials - actual_materials, 0)
            if actual_materials > expected_materials and not generated_default_material:
                issues.append("material-slot-count-mismatch")
                counts["materialSlotMismatch"] += 1
            usage = content.get("materialTextureUsage", {})
            special_material_model = expected_materials > 0 and int(usage.get("baseColor", 0)) == 0
            if special_material_model:
                counts["withoutBaseColorTexture"] += 1
                counts["specialMaterialModel"] += 1
            row["glb"] = {
                "file": artifact.get("outputFile"),
                "bytes": artifact.get("bytes"),
                "meshCount": content.get("meshCount"),
                "primitiveCount": content.get("primitiveCount"),
                "materialCount": content.get("materialCount"),
                "referencedMaterialCount": actual_materials,
                "imageCount": content.get("imageCount"),
                "textureCount": content.get("textureCount"),
                "materialTextureUsage": usage,
                "generatedDefaultMaterial": generated_default_material,
                "specialMaterialModel": special_material_model,
                "referencedMaterialNames": exported_material_names if path.is_file() else [],
                "unusedAssignedMaterialCount": unused_assigned_material_count,
            }
        row["issues"] = issues
        row["status"] = "ok" if not issues else ("missing" if artifact is None else "review")
        if artifact is not None and any(issue != "material-slot-count-mismatch" for issue in issues):
            counts["invalid"] += 1
        rows.append(row)

    unexpected = sorted(set(artifacts) - set(static_meshes))
    return {
        "schema": "eve.unreal-static-mesh-parity/1",
        "source": {
            "ueAuditSchema": audit.get("schema"),
            "conversionSchema": manifest.get("schema"),
            "engineVersion": audit.get("engineVersion"),
        },
        "counts": counts,
        "knownLimitation": (
            "UE sockets are not enumerable through the public UE 5.8 Python API; "
            "pivots, bounds, material slots, GLB structure, and hashes are audited."
        ),
        "unexpectedConvertedAssets": unexpected,
        "assets": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ue-audit", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    audit = json.loads(args.ue_audit.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    report = build_report(audit, manifest, args.manifest.parent)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["counts"], ensure_ascii=False, sort_keys=True))
    if report["counts"]["invalid"] or report["counts"]["hashMismatch"]:
        raise SystemExit(1)


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).parents[2] / "scripts"))
    from utf8_stdio import enable_utf8_stdio

    enable_utf8_stdio()
    raise SystemExit(main())
