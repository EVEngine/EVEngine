"""Runs inside Unreal Editor and audits effective material parameters."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


ROOT = "/Game/Medieval_Docks"


def _path(value) -> str | None:
    if value is None:
        return None
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def _value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if hasattr(value, "r") and hasattr(value, "g") and hasattr(value, "b"):
        result = {"r": float(value.r), "g": float(value.g), "b": float(value.b)}
        if hasattr(value, "a"):
            result["a"] = float(value.a)
        return result
    return _path(value)


def _property(record: dict, source, name: str) -> None:
    try:
        record[name] = _value(source.get_editor_property(name))
    except Exception as error:
        record.setdefault("unavailableProperties", {})[name] = str(error)


def _parameter_map(material, names_method: str, value_method: str) -> dict:
    names = getattr(unreal.MaterialEditingLibrary, names_method)(material)
    result = {}
    getter = getattr(unreal.MaterialEditingLibrary, value_method)
    for name in names:
        result[str(name)] = _value(getter(material, name))
    return dict(sorted(result.items()))


def _material_record(asset_data) -> dict:
    material = asset_data.get_asset()
    record = {
        "asset": f"{asset_data.package_name}.{asset_data.asset_name}",
        "class": str(asset_data.asset_class_path.asset_name),
    }
    try:
        base = material.get_base_material()
    except Exception:
        base = material
    record["baseMaterial"] = _path(base)
    if record["class"] == "MaterialInstanceConstant":
        _property(record, material, "parent")
    for name in (
        "blend_mode",
        "shading_model",
        "two_sided",
        "opacity_mask_clip_value",
        "dithered_lod_transition",
        "cast_dynamic_shadow_as_masked",
    ):
        _property(record, base, name)
    groups = (
        ("scalarParameters", "get_scalar_parameter_names", "get_material_instance_scalar_parameter_value"),
        ("vectorParameters", "get_vector_parameter_names", "get_material_instance_vector_parameter_value"),
        ("textureParameters", "get_texture_parameter_names", "get_material_instance_texture_parameter_value"),
    )
    for output_name, names_method, value_method in groups:
        try:
            record[output_name] = _parameter_map(material, names_method, value_method)
        except Exception as error:
            record.setdefault("unavailableParameterGroups", {})[output_name] = str(error)
    return record


def main() -> None:
    output_value = os.environ.get("EVENGINE_UE_MATERIAL_AUDIT_OUTPUT")
    if not output_value:
        raise RuntimeError("EVENGINE_UE_MATERIAL_AUDIT_OUTPUT is not set")
    output_path = Path(output_value).resolve()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    records = []
    failures = []
    for asset_data in registry.get_assets_by_path(unreal.Name(ROOT), recursive=True):
        class_name = str(asset_data.asset_class_path.asset_name)
        if class_name not in ("Material", "MaterialInstanceConstant"):
            continue
        try:
            records.append(_material_record(asset_data))
        except Exception as error:
            failures.append({
                "asset": f"{asset_data.package_name}.{asset_data.asset_name}",
                "error": str(error),
            })
    result = {
        "schema": "eve.unreal-material-audit/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "root": ROOT,
        "materialCount": len(records),
        "failureCount": len(failures),
        "failures": failures,
        "materials": sorted(records, key=lambda item: item["asset"]),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if failures:
        raise RuntimeError(f"UE material audit failed for {len(failures)} assets")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
