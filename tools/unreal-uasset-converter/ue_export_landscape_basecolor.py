"""Render the primary UE Landscape material to a deterministic orthographic PNG."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


def _rgba(sample: unreal.Color) -> list[int]:
    return [int(sample.r), int(sample.g), int(sample.b), int(sample.a)]


def main() -> None:
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    output_text = os.environ.get("EVENGINE_UE_LANDSCAPE_BASECOLOR_OUTPUT")
    report_text = os.environ.get("EVENGINE_UE_LANDSCAPE_BASECOLOR_REPORT")
    if not level or not output_text or not report_text:
        raise RuntimeError("level, landscape base-color output, and report path are required")
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError(f"level asset does not exist: {level}")

    world = unreal.EditorLoadingAndSavingUtils.load_map(level)
    if world is None:
        raise RuntimeError(f"could not load map: {level}")
    unreal.AutomationLibrary.finish_loading_before_screenshot()
    landscapes = [
        actor
        for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if isinstance(actor, unreal.LandscapeProxy) and actor.get_actor_label() == "Landscape"
    ]
    if len(landscapes) != 1:
        raise RuntimeError(f"expected one primary Landscape actor, found {len(landscapes)}")

    resolution = int(os.environ.get("EVENGINE_UE_LANDSCAPE_BASECOLOR_SIZE", "4096"))
    center_x = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_X_CM", "-1908.8040098"))
    center_y = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_Y_CM", "9039.1233941"))
    half_extent = float(os.environ.get("EVENGINE_UE_LANDSCAPE_HALF_X_CM", "10000"))
    camera_z = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CAPTURE_Z_CM", "30000"))

    camera_location = unreal.Vector(center_x, center_y, camera_z)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(
        camera_location, unreal.Vector(center_x, center_y, 0.0)
    )
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SceneCapture2D,
        camera_location,
        camera_rotation,
    )
    if actor is None:
        raise RuntimeError("could not spawn Landscape SceneCapture2D")
    component = actor.get_editor_property("capture_component2d")
    target = unreal.RenderingLibrary.create_render_target2d(
        actor,
        resolution,
        resolution,
        unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
        unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
        False,
        False,
    )
    if target is None:
        raise RuntimeError("could not create Landscape base-color render target")

    component.set_editor_property("texture_target", target)
    component.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
    component.set_editor_property("ortho_width", half_extent * 2.0)
    component.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    component.set_editor_property(
        "show_flag_settings",
        [unreal.EngineShowFlagsSetting(show_flag_name="Lighting", enabled=False)],
    )
    component.set_editor_property(
        "primitive_render_mode",
        unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST,
    )
    component.show_only_actor_components(landscapes[0], True)
    component.set_editor_property("capture_every_frame", False)
    component.set_editor_property("capture_on_movement", False)
    component.capture_scene()

    sample_locations = {
        "bottomLeft": (0, 0),
        "bottomRight": (resolution - 1, 0),
        "center": (resolution // 2, resolution // 2),
        "topLeft": (0, resolution - 1),
        "topRight": (resolution - 1, resolution - 1),
    }
    samples = {
        name: _rgba(unreal.RenderingLibrary.read_render_target_pixel(actor, target, x, y))
        for name, (x, y) in sample_locations.items()
    }
    output = Path(output_text).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    unreal.RenderingLibrary.export_render_target(actor, target, str(output.parent), output.name)
    if not output.is_file():
        raise RuntimeError(f"Landscape base-color capture produced no file: {output}")

    report = {
        "schema": "eve.unreal-landscape-basecolor/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "level": level,
        "landscape": landscapes[0].get_path_name(),
        "resolution": [resolution, resolution],
        "worldCropCentimeters": {
            "center": [center_x, center_y],
            "halfExtent": [half_extent, half_extent],
        },
        "camera": {
            "locationCentimeters": [center_x, center_y, camera_z],
            "rotationDegrees": [
                camera_rotation.roll,
                camera_rotation.pitch,
                camera_rotation.yaw,
            ],
            "orthographicWidthCentimeters": half_extent * 2.0,
        },
        "captureSource": "FinalColorLDR with Lighting show flag disabled",
        "primitiveSelection": "primary Landscape only",
        "samples": samples,
        "path": str(output),
        "bytes": output.stat().st_size,
    }
    report_path = Path(report_text).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
