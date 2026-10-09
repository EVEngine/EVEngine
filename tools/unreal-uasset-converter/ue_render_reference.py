"""Render a deterministic UE reference frame using the EVEngine sample camera."""

from __future__ import annotations

import json
import math
import os
import traceback
from pathlib import Path

import unreal


def _vec3(values: list[float]) -> unreal.Vector:
    return unreal.Vector(float(values[0]), float(values[1]), float(values[2]))


def main() -> None:
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    output_text = os.environ.get("EVENGINE_UE_REFERENCE_OUTPUT")
    report_text = os.environ.get("EVENGINE_UE_REFERENCE_REPORT")
    if not level or not output_text or not report_text:
        raise RuntimeError("level, reference output, and report path are required")
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError(f"level asset does not exist: {level}")

    unreal.EditorLoadingAndSavingUtils.load_map(level)
    unreal.AutomationLibrary.finish_loading_before_screenshot()

    width = int(os.environ.get("EVENGINE_UE_REFERENCE_WIDTH", "1280"))
    height = int(os.environ.get("EVENGINE_UE_REFERENCE_HEIGHT", "760"))
    vertical_fov = float(os.environ.get("EVENGINE_UE_REFERENCE_VERTICAL_FOV", "49.0"))
    center_x = float(os.environ.get("EVENGINE_UE_REFERENCE_CENTER_X_CM", "-1908.8040098"))
    center_y = float(os.environ.get("EVENGINE_UE_REFERENCE_CENTER_Y_CM", "9039.1233941"))

    # EV(X,Y,Z) maps to UE(centerX + 100X, centerY - 100Z, 100Y).
    eye_ev = json.loads(os.environ.get("EVENGINE_UE_REFERENCE_EYE", "[0.0, 5.2, 0.0]"))
    target_ev = json.loads(os.environ.get("EVENGINE_UE_REFERENCE_TARGET", "[18.0, 1.5, 0.0]"))
    eye_ue = [center_x + eye_ev[0] * 100.0, center_y - eye_ev[2] * 100.0, eye_ev[1] * 100.0]
    target_ue = [
        center_x + target_ev[0] * 100.0,
        center_y - target_ev[2] * 100.0,
        target_ev[1] * 100.0,
    ]
    rotation = unreal.MathLibrary.find_look_at_rotation(_vec3(eye_ue), _vec3(target_ue))
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SceneCapture2D, _vec3(eye_ue), rotation
    )
    if actor is None:
        raise RuntimeError("could not spawn SceneCapture2D")
    component = actor.get_editor_property("capture_component2d")

    target = unreal.RenderingLibrary.create_render_target2d(
        actor,
        width,
        height,
        unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
        unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
        False,
        False,
    )
    if target is None:
        raise RuntimeError("could not create reference render target")
    horizontal_fov = math.degrees(
        2.0 * math.atan(math.tan(math.radians(vertical_fov) * 0.5) * (width / height))
    )
    component.set_editor_property("texture_target", target)
    component.set_editor_property("fov_angle", horizontal_fov)
    component.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    component.set_editor_property("capture_every_frame", False)
    component.set_editor_property("capture_on_movement", False)
    component.capture_scene()

    # Readback synchronizes the render thread before the PNG export.
    center_sample = unreal.RenderingLibrary.read_render_target_pixel(
        actor, target, width // 2, height // 2
    )
    output = Path(output_text).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    unreal.RenderingLibrary.export_render_target(actor, target, str(output.parent), output.name)
    if not output.is_file():
        raise RuntimeError(f"UE reference render produced no file: {output}")

    report = {
        "schema": "eve.unreal-controlled-reference/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "level": level,
        "resolution": [width, height],
        "evCamera": {"eye": eye_ev, "target": target_ev, "verticalFovDegrees": vertical_fov},
        "ueCamera": {
            "eyeCentimeters": eye_ue,
            "targetCentimeters": target_ue,
            "horizontalFovDegrees": horizontal_fov,
            "rotationDegrees": [rotation.roll, rotation.pitch, rotation.yaw],
        },
        "centerSample": [
            int(center_sample.r),
            int(center_sample.g),
            int(center_sample.b),
            int(center_sample.a),
        ],
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
