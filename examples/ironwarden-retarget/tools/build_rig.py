"""Blender 4.2+: --background --python tools/build_rig.py (paths relative to this file)."""

import json
import math
import struct
from pathlib import Path

import bpy
from mathutils import Matrix, Vector, Quaternion

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "assets" / "ironwarden"


def restore_gltf_rest_axes(records):
    """Blender import may choose new bone display axes. Preserve original glTF rest axes."""

    def read(path):
        raw = path.read_bytes()
        size = struct.unpack_from("<I", raw, 12)[0]
        return json.loads(raw[20 : 20 + size]), bytearray(raw[28 + size :])

    def local(node):
        if "matrix" in node:
            m = node["matrix"]
            return Matrix([[m[c * 4 + r] for c in range(4)] for r in range(4)])
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        return Matrix.LocRotScale(
            Vector(node.get("translation", [0, 0, 0])),
            Quaternion((w, x, y, z)),
            Vector(node.get("scale", [1, 1, 1])),
        )

    def worlds(doc):
        parents = {c: i for i, n in enumerate(doc["nodes"]) for c in n.get("children", [])}
        cache = {}

        def get(i):
            if i not in cache:
                cache[i] = (get(parents[i]) if i in parents else Matrix.Identity(4)) @ local(
                    doc["nodes"][i]
                )
            return cache[i]

        return [get(i) for i in range(len(doc["nodes"]))], parents

    source, _ = read(ROOT / "assets/quaternius/AnimationLibrary.glb")
    source_world, _ = worlds(source)
    source_by_name = {
        n["name"]: source_world[i] for i, n in enumerate(source["nodes"]) if "name" in n
    }
    path = ASSET / "ironwarden.glb"
    doc, binary = read(path)
    original, parents = worlds(doc)
    targets = {r["target"]: r for r in records}
    desired = {}
    for i, node in enumerate(doc["nodes"]):
        if node.get("name") in targets:
            rec = targets[node["name"]]
            frame = source_by_name[rec["source"]].to_quaternion().to_matrix().to_4x4()
            x, y, z = rec["head"]
            frame.translation = Vector((x, z, -y))
            desired[i] = frame
    for i, frame in desired.items():
        parent = parents.get(i)
        parent_world = desired.get(
            parent, original[parent] if parent is not None else Matrix.Identity(4)
        )
        matrix = parent_world.inverted() @ frame
        node = doc["nodes"][i]
        for key in ["translation", "rotation", "scale"]:
            node.pop(key, None)
        node["matrix"] = [matrix[r][c] for c in range(4) for r in range(4)]
    for skin in doc["skins"]:
        accessor = doc["accessors"][skin["inverseBindMatrices"]]
        view = doc["bufferViews"][accessor["bufferView"]]
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        for j, node_index in enumerate(skin["joints"]):
            inverse = desired.get(node_index, original[node_index]).inverted()
            struct.pack_into(
                "<16f", binary, start + j * 64, *[inverse[r][c] for c in range(4) for r in range(4)]
            )
    encoded = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((-len(encoded)) % 4)
    path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(encoded) + len(binary))
        + struct.pack("<I4s", len(encoded), b"JSON")
        + encoded
        + struct.pack("<I4s", len(binary), b"BIN\0")
        + binary
    )


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / "assets/quaternius/AnimationLibrary.glb"))
    source = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    # Import actual human rest frames before constructing the differently named target.
    source_frames = {b.name: source.matrix_world @ b.matrix_local for b in source.data.bones}
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    data = json.loads((ASSET / "scene.json").read_text(encoding="utf-8"))
    joints = data["joints"]
    sh = joints["shoulder"]
    el = joints["elbow"]
    angle = math.atan2(sh[2] - el[2], el[0] - sh[0])
    arm_rot = {side: Matrix.Rotation(-side * angle, 4, "Y") for side in [-1, 1]}

    def mirror(point, side):
        return Vector((point[0] * side, point[1], point[2]))

    def arm_point(point, side):
        pivot = mirror(sh, side)
        return pivot + arm_rot[side] @ (Vector(point) - pivot)

    records = []

    def bone(name, source_name, parent, head, tail):
        records.append(
            dict(
                target=name,
                source=source_name,
                parent=parent,
                head=list(Vector(head) * 0.01),
                tail=list(Vector(tail) * 0.01),
            )
        )

    bone("IW_root", "root", None, (0, 0, 0), (0, 0, 12))
    chain = [
        ("pelvis", "DEF-hips"),
        ("spine1", "DEF-spine.001"),
        ("spine2", "DEF-spine.002"),
        ("chest", "DEF-spine.003"),
        ("neck", "DEF-neck"),
        ("head", "DEF-head"),
    ]
    names = ["IW_pelvis", "IW_spine_01", "IW_spine_02", "IW_chest", "IW_neck", "IW_head"]
    for i, ((key, src), name) in enumerate(zip(chain, names)):
        head = joints[key]
        tail = joints[chain[i + 1][0]] if i + 1 < len(chain) else (0, 0, 204)
        bone(name, src, "IW_root" if i == 0 else names[i - 1], head, tail)
    for side, label in [(1, "L"), (-1, "R")]:
        suffix = "." + label
        shoulder = mirror(sh, side)
        elbow = arm_point(mirror(el, side), side)
        wrist = arm_point(mirror(joints["wrist"], side), side)
        fingers = arm_point(mirror(joints["fingers"], side), side)
        bone(
            "IW_clavicle" + suffix,
            "DEF-shoulder" + suffix,
            "IW_chest",
            (side * 19, 0, 163),
            shoulder,
        )
        bone(
            "IW_upperarm" + suffix,
            "DEF-upper_arm" + suffix,
            "IW_clavicle" + suffix,
            shoulder,
            elbow,
        )
        bone("IW_forearm" + suffix, "DEF-forearm" + suffix, "IW_upperarm" + suffix, elbow, wrist)
        bone("IW_hand" + suffix, "DEF-hand" + suffix, "IW_forearm" + suffix, wrist, fingers)
        bone(
            "IW_thigh" + suffix,
            "DEF-thigh" + suffix,
            "IW_pelvis",
            mirror(joints["hip"], side),
            mirror(joints["knee"], side),
        )
        bone(
            "IW_calf" + suffix,
            "DEF-shin" + suffix,
            "IW_thigh" + suffix,
            mirror(joints["knee"], side),
            mirror(joints["ankle"], side),
        )
        bone(
            "IW_foot" + suffix,
            "DEF-foot" + suffix,
            "IW_calf" + suffix,
            mirror(joints["ankle"], side),
            mirror(joints["toe"], side),
        )
        bone(
            "IW_toe" + suffix,
            "DEF-toe" + suffix,
            "IW_foot" + suffix,
            mirror(joints["toe"], side),
            (side * 18, -25, 7),
        )
    rig_data = bpy.data.armatures.new("IronwardenHumanoid")
    rig = bpy.data.objects.new("IronwardenRig", rig_data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for rec in records:
        b = rig_data.edit_bones.new(rec["target"])
        frame = source_frames[rec["source"]].copy()
        frame.translation = Vector(rec["head"])
        b.matrix = frame
        b.length = (Vector(rec["tail"]) - Vector(rec["head"])).length
        if rec["parent"]:
            b.parent = rig_data.edit_bones[rec["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")

    materials = {}
    for name, color in data["palette"].items():
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bs = mat.node_tree.nodes.get("Principled BSDF")
        for suffix, socket in [
            ("basecolor", "Base Color"),
            ("roughness", "Roughness"),
            ("metallic", "Metallic"),
        ]:
            tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
            tex.image = bpy.data.images.load(str(ASSET / "textures" / f"{name}_{suffix}.png"))
            if suffix != "basecolor":
                tex.image.colorspace_settings.name = "Non-Color"
            mat.node_tree.links.new(tex.outputs["Color"], bs.inputs[socket])
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(str(ASSET / "textures" / f"{name}_normal.png"))
        tex.image.colorspace_settings.name = "Non-Color"
        normal = mat.node_tree.nodes.new("ShaderNodeNormalMap")
        normal.inputs["Strength"].default_value = 0.5
        mat.node_tree.links.new(tex.outputs["Color"], normal.inputs["Color"])
        mat.node_tree.links.new(normal.outputs["Normal"], bs.inputs["Normal"])
        if name == "cyan":
            bs.inputs["Emission Color"].default_value = (0.42, 0.85, 0.07, 1)
            bs.inputs["Emission Strength"].default_value = 2
        materials[name] = mat

    objects = []
    assignments = []
    for i, p in enumerate(data["parts"]):
        if p["kind"] == "mesh":
            geometry = bpy.data.meshes.new(p["name"])
            geometry.from_pydata(p["vertices"], [], p["faces"])
            geometry.update()
            obj = bpy.data.objects.new(p["name"], geometry)
            bpy.context.collection.objects.link(obj)
            bpy.ops.object.select_all(action="DESELECT")
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
            obj.location = p["pos"]
        elif p["kind"] == "box":
            bpy.ops.mesh.primitive_cube_add(size=1, location=p["pos"])
            obj = bpy.context.object
            obj.dimensions = p["size"]
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        else:
            bpy.ops.mesh.primitive_cone_add(
                vertices=32,
                radius1=p["size"][0],
                radius2=p["topRadius"],
                depth=p["size"][1],
                location=p["pos"],
            )
            obj = bpy.context.object
        obj.name = f"{i:03d}_{p['name']}"
        obj.rotation_euler = [math.radians(v) for v in p["rot"]]
        mod = obj.modifiers.new("Machined bevel", "BEVEL")
        mod.width = p["bevel"]
        mod.segments = 2
        bpy.ops.object.modifier_apply(modifier=mod.name)
        # Deterministic per-face box UV, applied before changing the rest pose.
        uv = obj.data.uv_layers.new(name="UVMap")
        lo = Vector(tuple(min(v.co[a] for v in obj.data.vertices) for a in range(3)))
        hi = Vector(tuple(max(v.co[a] for v in obj.data.vertices) for a in range(3)))
        for poly in obj.data.polygons:
            axis = max(range(3), key=lambda a: abs(poly.normal[a]))
            a, b = [k for k in range(3) if k != axis]
            for li in poly.loop_indices:
                co = obj.data.vertices[obj.data.loops[li].vertex_index].co
                uv.data[li].uv = (
                    (co[a] - lo[a]) / max(hi[a] - lo[a], 0.001),
                    (co[b] - lo[b]) / max(hi[b] - lo[b], 0.001),
                )
        bpy.context.view_layer.update()
        world = obj.matrix_world.copy()
        target = p["bone"]
        is_arm = any(tag in target for tag in ["upperarm", "forearm", "hand"])
        for v in obj.data.vertices:
            pos = world @ v.co
            if is_arm:
                pos = arm_point(pos, 1 if target.endswith(".L") else -1)
            v.co = pos * 0.01
        obj.matrix_world = Matrix.Identity(4)
        vg = obj.vertex_groups.new(name=target)
        vg.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
        obj.data.materials.append(materials[p["mat"]])
        objects.append(obj)
        assignments.append(dict(part=i, name=p["name"], bone=target))

    # One mesh per material; all rigid influences remain attached to their named bones.
    merged = []
    groups = {
        name: [o for o in objects if o.data.materials[0] == materials[name]] for name in materials
    }
    for name in materials:
        bpy.ops.object.select_all(action="DESELECT")
        group = groups[name]
        for obj in group:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = group[0]
        bpy.ops.object.join()
        obj = bpy.context.object
        obj.name = "IW_" + name
        triangulate = obj.modifiers.new("Export triangles", "TRIANGULATE")
        bpy.ops.object.modifier_apply(modifier=triangulate.name)
        modifier = obj.modifiers.new("Rigid humanoid skin", "ARMATURE")
        modifier.object = rig
        obj.parent = rig
        merged.append(obj)
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    for obj in merged:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(
        filepath=str(ASSET / "ironwarden.glb"),
        export_format="GLB",
        use_selection=True,
        export_animations=False,
        export_skins=True,
        export_yup=True,
        export_apply=False,
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,
    )
    restore_gltf_rest_axes(records)
    manifest = {
        "schema": "ironwarden-rig",
        "version": 1,
        "source": "Quaternius AnimationLibrary.glb",
        "sourceUnits": "metres",
        "modelUnits": "millimetres",
        "scale": 0.01,
        "skin": "rigid: one bone per vertex",
        "bones": records,
        "attachments": assignments,
    }
    (ASSET / "rig.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"IRONWARDEN_RIG_PASS bones={len(records)} parts={len(assignments)} meshes={len(merged)}")


if __name__ == "__main__":
    main()
