"""Main bpy pipeline: import primitive → run converter → bake → export."""

from __future__ import annotations

import os
import sys
from typing import Optional

from .job import Job, Result
from .converters import load_manifest, run_converter, ConverterError


def bpy_available() -> bool:
    try:
        import bpy  # noqa: F401
        return True
    except Exception:
        return False


def require_bpy():
    if not bpy_available():
        raise ConverterError(
            "bpy is not installed/importable. Install it with: pip install bpy\n"
            "The model converter drives Blender through its Python package."
        )
    import bpy  # noqa: F401
    return sys.modules["bpy"]


def _import_model(bpy, path: str):
    if not os.path.isfile(path):
        raise ConverterError(f"input model not found: {path}")
    ext = os.path.splitext(path)[1].lower()
    if ext == ".obj":
        bpy.ops.wm.obj_import(filepath=path)
    elif ext in (".stl",):
        bpy.ops.wm.stl_import(filepath=path)
    elif ext in (".fbx",):
        bpy.ops.wm.import_scene.fbx(filepath=path)
    elif ext in (".glb", ".gltf"):
        bpy.ops.wm.import_scene.gltf(filepath=path)
    else:
        raise ConverterError(f"unsupported input format '{ext}' (use .obj/.stl/.fbx/.glb/.gltf)")

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise ConverterError("no mesh found in input model")
    # Operate on the first mesh; hide the rest as a safeguard.
    target = meshes[0]
    for o in meshes:
        o.select_set(o is target)
    bpy.context.view_layer.objects.active = target
    return target


def _prepare_scene(bpy) -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if bpy.context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def _bake(bpy, obj) -> None:
    from . import mesh_ops
    mesh_ops.apply_modifiers(obj)
    mesh_ops.triangulate(obj)
    mesh_ops.recalc_normals(obj)
    mesh_ops.unwrap_lite(obj)
    mesh_ops.shade_flat(obj, flat=False)


def _export(bpy, obj, output_model: str, fmt: str) -> None:
    out_dir = os.path.dirname(os.path.abspath(output_model))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    # Export only the converted object.
    for o in bpy.context.scene.objects:
        o.select_set(o is obj)
    bpy.context.view_layer.objects.active = obj
    if fmt == "obj":
        bpy.ops.wm.obj_export(filepath=output_model)
    elif fmt == "glb":
        bpy.ops.export_scene.gltf(filepath=output_model, export_format="GLB",
                                  export_yup=True)
    elif fmt == "gltf":
        bpy.ops.export_scene.gltf(filepath=output_model, export_format="GLTF",
                                  export_yup=True)
    elif fmt == "fbx":
        bpy.ops.export_scene.fbx(filepath=output_model)
    else:
        raise ConverterError(f"unsupported output format '{fmt}' (use glb/gltf/obj/fbx)")


def run(job: Job) -> Result:
    result = Result()
    try:
        bpy = require_bpy()
        _prepare_scene(bpy)
        obj = _import_model(bpy, job.input_model)

        manifest = load_manifest(job.converter_dir, job.converter)
        run_converter(manifest, obj, job.params)
        _bake(bpy, obj)
        _export(bpy, obj, job.output_model, job.format or "glb")

        from . import mesh_ops
        result.ok = True
        result.output_model = job.output_model
        result.vertices, result.triangles = mesh_ops.count_stats(obj)
    except Exception as exc:  # noqa: BLE001 - report any failure to the plugin
        result.ok = False
        result.error = str(exc)
    return result
