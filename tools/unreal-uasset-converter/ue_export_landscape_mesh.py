"""Runs inside Unreal Editor and exports an audited Landscape mesh to USD."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal
from pxr import Usd, UsdGeom
from usd_unreal.exporting_utils import UsdConversionContext


def main() -> None:
    output = os.environ.get("EVENGINE_UE_LANDSCAPE_OUTPUT")
    report_output = os.environ.get("EVENGINE_UE_LANDSCAPE_REPORT")
    crop_output = os.environ.get("EVENGINE_UE_LANDSCAPE_CROP_OUTPUT")
    level = os.environ.get("EVENGINE_UE_LEVEL_AUDIT_MAP")
    lod = int(os.environ.get("EVENGINE_UE_LANDSCAPE_LOD", "1"))
    if not output or not report_output or not level:
        raise RuntimeError("landscape output, report and level environment variables are required")
    world = unreal.EditorLoadingAndSavingUtils.load_map(level)
    if world is None:
        raise RuntimeError(f"could not load map: {level}")
    landscapes = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if isinstance(actor, unreal.LandscapeProxy) and actor.get_actor_label() == "Landscape"
    ]
    if len(landscapes) != 1:
        raise RuntimeError(f"expected one primary Landscape actor, found {len(landscapes)}")

    path = Path(output).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    stage = Usd.Stage.CreateNew(str(path))
    prim = stage.DefinePrim("/Landscape", "Mesh")
    stage.SetDefaultPrim(prim)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
    UsdGeom.SetStageMetersPerUnit(stage, 0.01)
    stage.GetRootLayer().Save()
    with UsdConversionContext(str(path)) as converter:
        if not converter.convert_landscape_proxy_actor_mesh(landscapes[0], "/Landscape", lod, lod):
            raise RuntimeError("ConvertLandscapeProxyActorMesh returned false")
    stage.GetRootLayer().Save()

    stage = Usd.Stage.Open(str(path))
    mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Landscape"))
    points = mesh.GetPointsAttr().Get() or []
    # Landscape conversion writes heightfield-local coordinates and does not
    # author the actor transform on the USD prim. Apply UE's actor translation
    # and XY scale explicitly; USD conversion mirrors Unreal's Y axis.
    actor_location = landscapes[0].get_actor_location()
    actor_scale = landscapes[0].get_actor_scale3d()
    world_points = [
        (
            actor_location.x + float(point[0]) * actor_scale.x,
            actor_location.y - float(point[1]) * actor_scale.y,
            actor_location.z + float(point[2]) * actor_scale.z,
        )
        for point in points
    ]
    xs = [float(point[0]) for point in points]
    ys = [float(point[1]) for point in points]
    zs = [float(point[2]) for point in points]
    world_xs = [float(point[0]) for point in world_points]
    world_ys = [float(point[1]) for point in world_points]
    world_zs = [float(point[2]) for point in world_points]
    crop_summary = None
    if crop_output:
        center_x = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_X_CM", "-1908.8040098"))
        center_y = float(os.environ.get("EVENGINE_UE_LANDSCAPE_CENTER_Y_CM", "9039.1233941"))
        half_x = float(os.environ.get("EVENGINE_UE_LANDSCAPE_HALF_X_CM", "10000"))
        half_y = float(os.environ.get("EVENGINE_UE_LANDSCAPE_HALF_Y_CM", "10000"))
        counts = mesh.GetFaceVertexCountsAttr().Get() or []
        indices = mesh.GetFaceVertexIndicesAttr().Get() or []
        selected_faces = []
        cursor = 0
        for count in counts:
            face = [int(value) for value in indices[cursor:cursor + count]]
            cursor += count
            if all(
                abs(float(world_points[index][0]) - center_x) <= half_x
                and abs(float(world_points[index][1]) - center_y) <= half_y
                for index in face
            ):
                selected_faces.append(face)
        used = sorted({index for face in selected_faces for index in face})
        remap = {source: target + 1 for target, source in enumerate(used)}
        lines = [
            "# UE Landscape crop exported by ue_export_landscape_mesh.py",
            "mtllib market-landscape.mtl",
            "usemtl market_ground",
        ]
        for source in used:
            point = world_points[source]
            local_x = (float(point[0]) - center_x) / 100.0
            local_z = (float(point[1]) - center_y) / 100.0
            lines.append(
                "v %.6f %.6f %.6f"
                % (
                    local_x,
                    float(point[2]) / 100.0,
                    local_z,
                )
            )
        # The UE top-down capture points along -Z with yaw zero: image-right is
        # +UE Y and image-up is +UE X. OBJ V=0 addresses the bottom of the image,
        # so the baked orthophoto maps as U=Y and V=X over the audited crop.
        for source in used:
            point = world_points[source]
            lines.append(
                "vt %.6f %.6f"
                % (
                    (float(point[1]) - (center_y - half_y)) / (2.0 * half_y),
                    (float(point[0]) - (center_x - half_x)) / (2.0 * half_x),
                )
            )
        for face in selected_faces:
            lines.append(
                "f " + " ".join(f"{remap[index]}/{remap[index]}" for index in face)
            )
        crop_path = Path(crop_output).resolve()
        crop_path.parent.mkdir(parents=True, exist_ok=True)
        crop_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        crop_path.with_name("market-landscape.mtl").write_text(
            "newmtl market_ground\n"
            "Ka 0.015000 0.012000 0.008000\n"
            "Kd 1.000000 1.000000 1.000000\n"
            "map_Kd landscape-baked-basecolor.png\n"
            "Ks 0.010000 0.010000 0.010000\n"
            "Ns 4.000000\n"
            "d 1.000000\n"
            "illum 2\n",
            encoding="utf-8",
        )
        crop_summary = {
            "path": str(crop_path),
            "centerCentimeters": [center_x, center_y],
            "halfExtentCentimeters": [half_x, half_y],
            "pointCount": len(used),
            "faceCount": len(selected_faces),
        }
    report = {
        "schema": "eve.unreal-landscape-mesh-audit/1",
        "level": level,
        "actor": landscapes[0].get_path_name(),
        "actorTransform": {
            "locationCentimeters": [
                float(landscapes[0].get_actor_location().x),
                float(landscapes[0].get_actor_location().y),
                float(landscapes[0].get_actor_location().z),
            ],
            "scale": [
                float(landscapes[0].get_actor_scale3d().x),
                float(landscapes[0].get_actor_scale3d().y),
                float(landscapes[0].get_actor_scale3d().z),
            ],
        },
        "lod": lod,
        "pointCount": len(points),
        "faceCount": len(mesh.GetFaceVertexCountsAttr().Get() or []),
        "bounds": {
            "min": [min(xs), min(ys), min(zs)],
            "max": [max(xs), max(ys), max(zs)],
        },
        "worldBounds": {
            "min": [min(world_xs), min(world_ys), min(world_zs)],
            "max": [max(world_xs), max(world_ys), max(world_zs)],
        },
        "crop": crop_summary,
        "samplePoints": [[float(v) for v in point] for point in points[:8]],
    }
    report_path = Path(report_output).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
