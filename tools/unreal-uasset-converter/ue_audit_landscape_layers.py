"""Runs inside Unreal Editor and audits Landscape weightmap layer allocation."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


def _value(obj, name: str):
    try:
        return obj.get_editor_property(name)
    except Exception as error:
        return {"unavailable": str(error)}


def _path(value) -> str | None:
    return value.get_path_name() if value is not None else None


def main() -> None:
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    output = os.environ.get("EVENGINE_UE_LANDSCAPE_LAYER_REPORT")
    if not level or not output:
        raise RuntimeError("level and Landscape layer report are required")
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError(f"level asset does not exist: {level}")
    unreal.EditorLoadingAndSavingUtils.load_map(level)
    landscapes = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if isinstance(actor, unreal.LandscapeProxy) and actor.get_actor_label() == "Landscape"
    ]
    if len(landscapes) != 1:
        raise RuntimeError(f"expected one primary Landscape actor, found {len(landscapes)}")
    components = []
    for component in landscapes[0].get_components_by_class(unreal.LandscapeComponent):
        textures = _value(component, "weightmap_textures")
        allocations = _value(component, "weightmap_layer_allocations")
        record = {
            "name": component.get_name(),
            "sectionBaseX": _value(component, "section_base_x"),
            "sectionBaseY": _value(component, "section_base_y"),
            "componentSizeQuads": _value(component, "component_size_quads"),
            "weightmapScaleBias": str(_value(component, "weightmap_scale_bias")),
            "weightmapSubsectionOffset": _value(component, "weightmap_subsection_offset"),
            "weightmapTextures": (
                [_path(texture) for texture in textures]
                if isinstance(textures, (list, tuple)) else textures
            ),
            "allocations": [],
        }
        if isinstance(allocations, (list, tuple)):
            for allocation in allocations:
                layer = _value(allocation, "layer_info")
                record["allocations"].append(
                    {
                        "layer": _path(layer) if not isinstance(layer, dict) else layer,
                        "textureIndex": _value(allocation, "weightmap_texture_index"),
                        "textureChannel": _value(allocation, "weightmap_texture_channel"),
                    }
                )
        else:
            record["allocationError"] = allocations
        components.append(record)
    if not components:
        raise RuntimeError("Landscape layer audit found no components")
    report = {
        "schema": "eve.unreal-landscape-layer-audit/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "level": level,
        "actor": landscapes[0].get_path_name(),
        "componentCount": len(components),
        "components": components,
    }
    Path(output).resolve().write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
