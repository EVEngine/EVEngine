"""Runs inside Unreal Editor and exports blended Landscape weightmaps for a crop."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


def main() -> None:
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    output_text = os.environ.get("EVENGINE_UE_LANDSCAPE_WEIGHTMAP_OUTPUT")
    report_text = os.environ.get("EVENGINE_UE_LANDSCAPE_WEIGHTMAP_REPORT")
    if not level or not output_text or not report_text:
        raise RuntimeError("level, weightmap output, and report path are required")
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError(f"level asset does not exist: {level}")

    unreal.EditorLoadingAndSavingUtils.load_map(level)
    landscapes = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if isinstance(actor, unreal.Landscape) and actor.get_actor_label() == "Landscape"
    ]
    if len(landscapes) != 1:
        raise RuntimeError(f"expected one primary Landscape actor, found {len(landscapes)}")
    landscape = landscapes[0]

    layer_names = [str(name) for name in landscape.get_target_layer_names(False)]
    if not layer_names:
        raise RuntimeError("Landscape reported no target layers")
    requested = json.loads(
        os.environ.get("EVENGINE_UE_LANDSCAPE_WEIGHTMAP_LAYERS", "[]")
    )
    if not requested:
        requested = layer_names[:4]
    missing = sorted(set(requested) - set(layer_names))
    if missing:
        raise RuntimeError(f"requested Landscape layers do not exist: {missing}")
    if len(requested) > 4:
        raise RuntimeError("RGBA8 output can contain at most four Landscape layers")

    center_x = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_X_CM", "-1908.8040098"))
    center_y = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_Y_CM", "9039.1233941"))
    half_x = float(os.environ.get("EVENGINE_UE_LANDSCAPE_HALF_X_CM", "4500"))
    half_y = float(os.environ.get("EVENGINE_UE_LANDSCAPE_HALF_Y_CM", "3600"))
    resolution = int(os.environ.get("EVENGINE_UE_LANDSCAPE_WEIGHTMAP_RESOLUTION", "2048"))

    target = unreal.RenderingLibrary.create_render_target2d(
        landscape,
        resolution,
        resolution,
        unreal.TextureRenderTargetFormat.RTF_RGBA8,
        unreal.LinearColor(0.0, 0.0, 0.0, 0.0),
        False,
        False,
    )
    if target is None:
        raise RuntimeError("could not create Landscape weightmap render target")
    render_transform = unreal.Transform(
        location=unreal.Vector(0.0, 0.0, 0.0),
        rotation=unreal.Rotator(0.0, 0.0, 0.0),
        scale=unreal.Vector(1.0, 1.0, 1.0),
    )
    extents = unreal.Box2D(
        min=unreal.Vector2D(center_x - half_x, center_y - half_y),
        max=unreal.Vector2D(center_x + half_x, center_y + half_y),
    )
    if not landscape.render_weightmaps(render_transform, extents, requested, target):
        raise RuntimeError("Landscape RenderWeightmaps returned false")

    # RenderWeightmaps enqueues GPU work. A synchronous readback guarantees that
    # the merge has completed before ExportRenderTarget serializes the texture.
    center_sample = unreal.RenderingLibrary.read_render_target_pixel(
        landscape, target, resolution // 2, resolution // 2
    )

    output = Path(output_text).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    unreal.RenderingLibrary.export_render_target(
        landscape, target, str(output.parent), output.name
    )
    if not output.is_file():
        raise RuntimeError(f"Landscape weightmap export produced no file: {output}")

    report = {
        "schema": "eve.unreal-landscape-weightmap-export/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "level": level,
        "actor": landscape.get_path_name(),
        "availableLayers": layer_names,
        "channels": {"RGBA"[index]: name for index, name in enumerate(requested)},
        "centerCentimeters": [center_x, center_y],
        "halfExtentCentimeters": [half_x, half_y],
        "resolution": [resolution, resolution],
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
