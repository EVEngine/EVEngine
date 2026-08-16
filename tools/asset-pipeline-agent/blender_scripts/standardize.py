"""Headless Blender 自动化处理脚本（bpy）。

由 asset_pipeline_agent.blender_pipeline 以子进程方式调用：
    <bpy-python> standardize.py --job job.json

job.json:
{
  "archive":   "path/to/asset.zip",       # 已缓存并 MD5 校验过的原始压缩包
  "work_dir":  "path/to/extract",         # 解压 + 中间产物目录
  "output":    "path/to/asset.glb",       # 导出目标（引擎格式）
  "license":   "cc0",                     # 合规授权（写入元数据）
  "attribution": "Poly Haven 'x' (CC0) by ...",   # 永久留存的版权信息
  "params": {
    "up_axis": "Y", "scale": 1.0, "max_triangles": 200000, "decimate": true,
    "recalc_normals": true, "fix_pivot": true, "clean_mesh": true,
    "standardize_material": true, "compress_textures": true,
    "texture_max_size": 2048, "output_format": "glb"
  }
}

处理后写 result.json（同目录）。
"""

from __future__ import annotations

import json
import os
import shutil
import sys
import time
import zipfile


def log(msg: str) -> None:
    print(f"[standardize] {msg}", file=sys.stderr, flush=True)


# ---------------------------------------------------------------- import
def import_mesh(bpy, path: str):
    """按扩展名导入首个网格对象；返回该对象（无网格则报错）。"""
    ext = os.path.splitext(path)[1].lower()
    if ext in (".glb", ".gltf"):
        bpy.ops.wm.import_scene.gltf(filepath=path)
    elif ext == ".obj":
        bpy.ops.wm.obj_import(filepath=path)
    elif ext == ".fbx":
        bpy.ops.wm.import_scene.fbx(filepath=path)
    elif ext == ".stl":
        bpy.ops.wm.stl_import(filepath=path)
    elif ext == ".blend":
        with bpy.data.libraries.load(path, link=False) as (data_from, data_to):
            meshes = [o for o in data_from.objects if o.type == "MESH"]
            if not meshes:
                raise RuntimeError("blend 文件无网格对象")
            data_to.objects = meshes
        for o in data_to.objects:
            if o:
                bpy.context.collection.objects.link(o)
    else:
        raise RuntimeError(f"不支持的输入格式 {ext}")

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError("输入中未找到网格对象")
    target = meshes[0]
    for o in meshes:
        o.select_set(o is target)
    bpy.context.view_layer.objects.active = target
    return target


def find_import_file(work_dir: str) -> str:
    """在工作目录里按优先级找主网格文件。"""
    priorities = (".glb", ".gltf", ".obj", ".fbx", ".stl", ".blend")
    found: dict = {}
    for root, _dirs, files in os.walk(work_dir):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in priorities:
                found.setdefault(ext, []).append(os.path.join(root, f))
    for ext in priorities:
        if ext in found:
            return found[ext][0]
    raise RuntimeError("工作目录中未找到可导入的模型文件")


# ---------------------------------------------------------------- clean
def clean_mesh(bpy, bmesh, obj) -> None:
    me = obj.data
    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.update_edit_mesh(me)
    bpy.ops.object.mode_set(mode="OBJECT")


def apply_transform(bpy, obj) -> None:
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    try:
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    except RuntimeError:
        pass


def fix_pivot(bpy, obj) -> None:
    """轴心校正：将原点对齐到几何体包围盒底面中心，并把局部旋转归零。"""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.origin_set(type="ORIGIN_CENTER_OF_VOLUME")
    # 可选：降到包围盒底面（地面零平面），便于引擎摆放。
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")


def orient_up_axis(bpy, obj, up_axis: str) -> None:
    """坐标系校正：确保局部 +Y/+Z 指向上。"""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if up_axis.upper() == "Y":
        # glTF 约定 +Y up；若资产是 +Z up 则绕 X 转 -90 度。
        bpy.ops.transform.rotate(value=-1.5708, orient_axis="X")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def recalc_normals(bpy, obj) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def decimate_to(bpy, obj, max_triangles: int) -> None:
    """超面数则三角化 + 精简到上限。"""
    me = obj.data
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    if n <= max_triangles:
        return
    ratio = max(0.05, min(0.95, max_triangles / max(1, n)))
    mod = obj.modifiers.new(name="EVE_Decimate", type="DECIMATE")
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    try:
        bpy.ops.object.modifier_apply(modifier="EVE_Decimate")
    except RuntimeError:
        pass


# ---------------------------------------------------------------- material
def standardize_materials(bpy, obj, max_size: int) -> int:
    """材质标准化：确保 PBR 材质可被引擎读取，并压缩纹理到 max_size。"""
    changed = 0
    if obj.data.materials:
        for mat in list(obj.data.materials):
            if mat is None or mat.use_nodes:
                continue
            # 无节点材质 -> 建 Principled BSDF。
            mat.use_nodes = True
            nodes = mat.node_tree.nodes
            bsdf = nodes.get("Principled BSDF")
            if bsdf is None:
                bsdf = nodes.new("ShaderNodeBsdfPrincipled")
            mat.node_tree.links.new(
                bsdf.outputs["BSDF"],
                mat.node_tree.nodes.get("Material Output").inputs["Surface"],
            )
            changed += 1

    seen = set()
    for img in bpy.data.images:
        if img.name in seen or not getattr(img, "source", None) in ("FILE",):
            continue
        seen.add(img.name)
        if max_size and (img.size[0] > max_size or img.size[1] > max_size):
            img.scale(max_size, max_size)  # 纹理压缩
    return changed


# ---------------------------------------------------------------- export
def export(bpy, obj, output_path: str, fmt: str) -> None:
    out_dir = os.path.dirname(os.path.abspath(output_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if fmt == "glb":
        bpy.ops.export_scene.gltf(filepath=output_path, export_format="GLB", export_yup=True)
    elif fmt == "gltf":
        bpy.ops.export_scene.gltf(filepath=output_path, export_format="GLTF", export_yup=True)
    elif fmt == "fbx":
        bpy.ops.export_scene.fbx(filepath=output_path)
    elif fmt == "obj":
        bpy.ops.wm.obj_export(filepath=output_path)
    else:
        raise RuntimeError(f"不支持的输出格式 {fmt}")


def _count(obj) -> "tuple[int, int, int]":
    me = obj.data
    me.calc_loop_triangles()
    return len(me.vertices), len(me.loop_triangles), len(obj.data.materials)


def run(job: dict) -> dict:
    t0 = time.time()
    params = job.get("params", {}) or {}
    warnings: list = []
    import bpy  # noqa: F401  (必须在子进程内 import)
    bpy.ops.wm.read_factory_settings(use_empty=True)

    archive = job.get("archive", "")
    work_dir = job.get("work_dir", "")
    output = job.get("output", "")

    # 解压
    if archive and os.path.isfile(archive):
        os.makedirs(work_dir, exist_ok=True)
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(work_dir)
    elif os.path.isdir(work_dir):
        pass
    else:
        raise RuntimeError("缺少可处理的输入（archive 或 work_dir）")

    src = find_import_file(work_dir)
    obj = import_mesh(bpy, src)

    if params.get("clean_mesh", True):
        import bmesh
        clean_mesh(bpy, bmesh, obj)
    apply_transform(bpy, obj)
    if params.get("fix_pivot", True):
        fix_pivot(bpy, obj)
    if params.get("up_axis", "Y"):
        orient_up_axis(bpy, obj, params.get("up_axis", "Y"))
    apply_transform(bpy, obj)
    if params.get("recalc_normals", True):
        recalc_normals(bpy, obj)
    if params.get("decimate", True) and params.get("max_triangles"):
        before = _count(obj)[1]
        decimate_to(bpy, obj, int(params["max_triangles"]))
        after = _count(obj)[1]
        if after < before:
            warnings.append(f"面数精简 {before} -> {after}")

    mats = 0
    if params.get("standardize_material", True):
        mats = standardize_materials(bpy, obj, int(params.get("texture_max_size", 2048)))

    fmt = params.get("output_format", "glb")
    export(bpy, obj, output, fmt)
    vertices, triangles, _ = _count(obj)

    # 版权信息落盘（永久留存）。
    meta_path = os.path.splitext(output)[0] + ".attribution.json"
    with open(meta_path, "w", encoding="utf-8") as fh:
        json.dump({
            "license": job.get("license", "cc0"),
            "attribution": job.get("attribution", ""),
            "author_url": job.get("author_url", ""),
            "generated_by": "EVEngine asset-pipeline-agent",
        }, fh, ensure_ascii=False, indent=2)

    return {
        "ok": True,
        "output": output,
        "vertices": vertices,
        "triangles": triangles,
        "materials": mats,
        "textures": len(bpy.data.images),
        "duration_s": round(time.time() - t0, 2),
        "warnings": warnings,
    }


def main(argv) -> int:
    if len(argv) < 2:
        print("usage: python standardize.py --job job.json", file=sys.stderr)
        return 2
    job_path = argv[1] if argv[1] != "--job" else argv[2]
    with open(job_path, "r", encoding="utf-8") as fh:
        job = json.load(fh)
    result_path = job.get("result_path", os.path.join(os.path.dirname(job_path), "result.json"))
    try:
        result = run(job)
    except Exception as exc:  # noqa: BLE001 - 向父进程上报任何失败
        result = {"ok": False, "error": str(exc), "warnings": []}
    with open(result_path, "w", encoding="utf-8") as fh:
        json.dump(result, fh, ensure_ascii=False, indent=2)
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
