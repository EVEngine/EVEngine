"""Common, deterministic bpy mesh helpers used by script converters."""

from __future__ import annotations

import math
import random

import bpy
import bmesh


def deselect_all() -> None:
    if bpy.context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")


def select_only(obj) -> None:
    deselect_all()
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def remesh_quads(obj, subdivisions: int) -> None:
    """Subdivide the mesh into quads (Catmull-Clark if the mesh allows)."""
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.subdivide(number_cuts=max(0, int(subdivisions)))
    bpy.ops.object.mode_set(mode="OBJECT")


def weighted_random_group(obj, seed: int, group_name: str, weight_min: float = 0.0,
                          weight_max: float = 1.0) -> None:
    """Assign a seeded random weight to every vertex in a vertex group.

    Adjacent vertices can end up with very different weights; scripts usually
    smooth this afterwards (see smooth_group).
    """
    group = obj.vertex_groups.get(group_name)
    if group is None:
        group = obj.vertex_groups.new(name=group_name)
    rng = random.Random(seed)
    data = obj.data
    try:
        bpy.ops.object.mode_set(mode="OBJECT")
        for v in data.vertices:
            w = rng.uniform(weight_min, weight_max)
            group.add([v.index], w, "REPLACE")
    except RuntimeError:
        # Some Blender builds require EDIT mode for weight painting.
        for v in data.vertices:
            w = rng.uniform(weight_min, weight_max)
            group.add([v.index], w, "REPLACE")


def smooth_group(obj, group_name: str, iterations: int = 3) -> None:
    """Blur vertex-group weights across the mesh for softer displacement."""

    def _neighbors(me):
        import collections
        nbr: "collections.defaultdict[int, set[int]]" = collections.defaultdict(set)
        for p in me.polygons:
            for i in range(len(p.vertices)):
                a = p.vertices[i]
                b = p.vertices[(i + 1) % len(p.vertices)]
                nbr[a].add(b)
                nbr[b].add(a)
        return nbr

    group = obj.vertex_groups.get(group_name)
    if group is None:
        return
    me = obj.data
    nbr = _neighbors(me)
    weights = {v.index: group.weight(v.index) for v in me.vertices}
    for _ in range(max(1, int(iterations))):
        new_weights = {}
        for vid, val in weights.items():
            n = nbr.get(vid)
            if not n:
                new_weights[vid] = val
                continue
            total = 0.0
            cnt = 0
            for nb in n:
                total += weights.get(nb, group.weight(nb))
                cnt += 1
            new_weights[vid] = total / cnt if cnt else val
        weights = new_weights
    for vid, w in weights.items():
        group.add([vid], w, "REPLACE")


def displace_vertex_weights(obj, seed: int, strength: float, group_name: str,
                            use_edit_mode: bool = False) -> None:
    """Displace vertices along their normals by a seeded fbm, weighted by a
    vertex group. Produces an organic (e.g. rock) surface.
    """
    group = obj.vertex_groups.get(group_name)
    if group is None:
        group = obj.vertex_groups.new(name=group_name)
    strength = float(strength)

    # Deterministic 3D value-noise (no global random state).
    def _hash3(x, y, z, s):
        return ((x * 374761393) ^ (y * 668265263) ^ (z * 1274126177) ^ (s * 2246822519)) & 0x7FFFFFFF

    def _fbm(x, y, z, s, octaves=4):
        total = 0.0
        amp = 0.5
        freq = 1.0
        norm = 0.0
        for _ in range(octaves):
            h = _hash3(int(x * freq), int(y * freq), int(z * freq), s)
            total += ((h / 0x7FFFFFFF) * 2.0 - 1.0) * amp
            norm += amp
            amp *= 0.5
            freq *= 2.0
        return total / norm if norm else 0.0

    me = obj.data
    if use_edit_mode:
        # Operate via bmesh so history/normals stay consistent.
        bm = bmesh.new()
        bm.from_mesh(me)
        bm.verts.ensure_lookup_table()
        layer = bm.verts.layers.deform.get(group_name)
        if layer is None:
            layer = bm.verts.layers.deform.new(group_name)
        for v in bm.verts:
            w = v[layer].get(group.index, 0.0) if layer is not None else 0.0
            disp = _fbm(v.co.x, v.co.y, v.co.z, seed) * strength * w
            v.co.x += v.normal.x * disp
            v.co.y += v.normal.y * disp
            v.co.z += v.normal.z * disp
        bm.normal_update()
        bm.to_mesh(me)
        bm.free()
    else:
        me.update(calc_edges=True)
        for v in me.vertices:
            w = group.weight(v.index)
            disp = _fbm(v.co.x, v.co.y, v.co.z, seed) * strength * w
            v.co.x += v.normal.x * disp
            v.co.y += v.normal.y * disp
            v.co.z += v.normal.z * disp
    me.update()


def apply_modifiers(obj) -> None:
    deps = bpy.context.evaluated_depsgraph_get()
    for modifier in list(obj.modifiers):
        try:
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        except RuntimeError:
            # Apply through evaluated geometry if direct apply fails.
            if hasattr(obj, "evaluated_get"):
                ev = obj.evaluated_get(deps)
                if ev.data and ev.data.name:
                    new_data = ev.data.copy()
                    obj.data.user_clear()
                    obj.data = new_data
                    obj.modifiers.clear()


def triangulate(obj) -> None:
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="SHORTEST_DIAGONAL")
    bpy.ops.object.mode_set(mode="OBJECT")


def recalc_normals(obj) -> None:
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def shade_flat(obj, flat: bool = True) -> None:
    select_only(obj)
    if flat:
        bpy.ops.object.shade_flat()
    else:
        bpy.ops.object.shade_smooth()


def unwrap_lite(obj, margin: float = 0.02) -> None:
    """Smart-projection UV unwrap; safe fallback for simple boxes."""
    select_only(obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    try:
        bpy.ops.uv.smart_project()
    except RuntimeError:
        bpy.ops.uv.unwrap()
    bpy.ops.object.mode_set(mode="OBJECT")


def solidify(obj, thickness: float, offset: float = -1.0) -> None:
    mod = obj.modifiers.new(name="EVE_Solidify", type="SOLIDIFY")
    mod.thickness = float(thickness)
    mod.offset = float(offset)


def bevel(obj, width: float, segments: int = 1) -> None:
    mod = obj.modifiers.new(name="EVE_Bevel", type="BEVEL")
    mod.width = float(width)
    mod.segments = max(1, int(segments))
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(30)


def scale_object(obj, sx: float, sy: float, sz: float) -> None:
    select_only(obj)
    bpy.ops.transform.resize(value=(sx, sy, sz))
    bpy.ops.object.transform_apply(scale=True)


def count_stats(obj) -> "tuple[int, int]":
    me = obj.data
    me.calc_loop_triangles()
    return len(me.vertices), len(me.loop_triangles)
