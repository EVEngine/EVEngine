"""Runs inside Unreal Editor and records authored level mesh composition."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


def _vector(value) -> list[float]:
    return [float(value.x), float(value.y), float(value.z)]


def _rotator(value) -> list[float]:
    return [float(value.roll), float(value.pitch), float(value.yaw)]


def _quaternion(value) -> list[float]:
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def _transform(value) -> dict:
    return {
        "locationCentimeters": _vector(value.translation),
        "rotationDegrees": _rotator(value.rotation.rotator()),
        "rotationQuaternion": _quaternion(value.rotation),
        "scale": _vector(value.scale3d),
    }


def _path(value) -> str | None:
    return value.get_path_name() if value is not None else None


def _component_transform(component, actor) -> dict:
    """Read a component world transform across UE Python API variants."""
    getter = getattr(component, "get_component_transform", None)
    if callable(getter):
        return _transform(getter())
    try:
        return _transform(component.get_editor_property("component_to_world"))
    except Exception:
        pass
    location_getter = getattr(component, "get_world_location", None)
    rotation_getter = getattr(component, "get_world_rotation", None)
    scale_getter = getattr(component, "get_world_scale", None)
    if callable(location_getter) and callable(rotation_getter) and callable(scale_getter):
        rotation = rotation_getter()
        return {
            "locationCentimeters": _vector(location_getter()),
            "rotationDegrees": _rotator(rotation),
            "rotationQuaternion": _quaternion(rotation.quaternion()),
            "scale": _vector(scale_getter()),
        }
    # Static and spline mesh components commonly share the actor transform. This
    # final fallback keeps the audit complete while making the approximation explicit.
    result = _transform(actor.get_actor_transform())
    result["source"] = "actorFallback"
    return result


def main() -> None:
    output = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_OUTPUT")
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    if not output or not level:
        raise RuntimeError("EVENGINE_UE_LEVEL_AUDIT_OUTPUT and EVENGINE_UE_LEVEL_AUDIT_MAP are required")
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError(f"level asset does not exist: {level}")
    world = unreal.EditorLoadingAndSavingUtils.load_map(level)
    if world is None:
        raise RuntimeError(f"could not load map: {level}")
    actors = []
    mesh_uses = []
    instance_total = 0
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        actor_record = {
            "label": actor.get_actor_label(),
            "class": actor.get_class().get_name(),
            "path": actor.get_path_name(),
            "transform": _transform(actor.get_actor_transform()),
        }
        components = []
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            if mesh is None:
                continue
            materials = [_path(component.get_material(index)) for index in range(component.get_num_materials())]
            component_record = {
                "name": component.get_name(),
                "class": component.get_class().get_name(),
                "mesh": _path(mesh).split(".", 1)[0],
                "transform": _component_transform(component, actor),
                "materials": materials,
                "instances": [],
            }
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                count = component.get_instance_count()
                instance_total += count
                component_record["instances"] = [
                    _transform(component.get_instance_transform(index, world_space=True))
                    for index in range(count)
                ]
            components.append(component_record)
            mesh_uses.append(component_record["mesh"])
        actor_record["components"] = components
        actors.append(actor_record)
    mesh_component_count = sum(len(actor["components"]) for actor in actors)
    if not actors or mesh_component_count == 0:
        raise RuntimeError(
            f"level audit would be empty: actors={len(actors)} meshComponents={mesh_component_count}"
        )
    result = {
        "schema": "eve.unreal-level-audit/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "level": level,
        "actorCount": len(actors),
        "meshComponentCount": mesh_component_count,
        "instancedMeshInstanceCount": instance_total,
        "uniqueMeshes": sorted(set(mesh_uses)),
        "actors": actors,
    }
    path = Path(output).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
