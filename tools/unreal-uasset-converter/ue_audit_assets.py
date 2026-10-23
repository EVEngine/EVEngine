"""Runs inside Unreal Editor and writes a source-of-truth asset audit."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


ROOT = "/Game/Medieval_Docks"


def _asset_path(value) -> str | None:
    if value is None:
        return None
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def _bounds(mesh) -> dict:
    box = mesh.get_bounding_box()
    minimum = box.min
    maximum = box.max
    return {
        "min": [minimum.x, minimum.y, minimum.z],
        "max": [maximum.x, maximum.y, maximum.z],
        "size": [maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z],
    }


def _static_mesh_details(mesh, subsystem) -> dict:
    materials = []
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        interface = slot.get_editor_property("material_interface")
        materials.append(
            {
                "index": index,
                "slotName": str(slot.get_editor_property("material_slot_name")),
                "importedSlotName": str(slot.get_editor_property("imported_material_slot_name")),
                "material": _asset_path(interface),
            }
        )
    lod_count = mesh.get_num_lods()
    return {
        "lodCount": lod_count,
        "materialSlots": materials,
        # UE 5.8 exposes socket lookup helpers but protects the StaticMesh
        # sockets array from Python. Do not turn that API limitation into a
        # false claim that the asset has zero sockets.
        "socketEnumeration": "unavailable-via-public-ue5.8-python-api",
        "boundsCentimeters": _bounds(mesh),
        "allowCpuAccess": bool(mesh.get_editor_property("allow_cpu_access")),
    }


def main() -> None:
    output_value = os.environ.get("EVENGINE_UE_AUDIT_OUTPUT")
    if not output_value:
        raise RuntimeError("EVENGINE_UE_AUDIT_OUTPUT is not set")
    output_path = Path(output_value).resolve()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    dependency_options = unreal.AssetRegistryDependencyOptions()
    dependency_options.set_editor_property("include_soft_package_references", True)
    dependency_options.set_editor_property("include_hard_package_references", True)
    dependency_options.set_editor_property("include_searchable_names", False)
    dependency_options.set_editor_property("include_soft_management_references", False)
    dependency_options.set_editor_property("include_hard_management_references", False)
    records = []
    failures = []
    class_counts: dict[str, int] = {}
    assets = registry.get_assets_by_path(unreal.Name(ROOT), recursive=True)
    for asset_data in assets:
        package_name = str(asset_data.package_name)
        asset_name = str(asset_data.asset_name)
        object_path = f"{package_name}.{asset_name}"
        class_name = str(asset_data.asset_class_path.asset_name)
        class_counts[class_name] = class_counts.get(class_name, 0) + 1
        record = {
            "asset": object_path,
            "package": str(asset_data.package_name),
            "class": class_name,
        }
        try:
            dependencies = registry.get_dependencies(asset_data.package_name, dependency_options)
            record["dependencies"] = sorted(str(value) for value in dependencies)
            if class_name == "StaticMesh":
                mesh = asset_data.get_asset()
                record.update(_static_mesh_details(mesh, None))
        except Exception as error:
            record["auditError"] = str(error)
            failures.append({"asset": object_path, "error": str(error)})
        records.append(record)
    result = {
        "schema": "eve.unreal-asset-audit/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "root": ROOT,
        "assetCount": len(records),
        "classCounts": dict(sorted(class_counts.items())),
        "failureCount": len(failures),
        "failures": failures,
        "assets": sorted(records, key=lambda item: item["asset"]),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if failures:
        raise RuntimeError(f"UE asset audit failed for {len(failures)} assets")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
