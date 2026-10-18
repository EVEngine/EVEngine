"""Blender 4.2+: render the authored neutral model; runtime motion proof lives in evidence/."""

from pathlib import Path
import json
import math

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "assets/ironwarden"
OUT = ROOT / "evidence"


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    data = json.loads((ASSET / "scene.json").read_text(encoding="utf-8"))
    materials = {}
    for name in data["palette"]:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        bs = nodes.get("Principled BSDF")
        for suffix, socket in [
            ("basecolor", "Base Color"),
            ("roughness", "Roughness"),
            ("metallic", "Metallic"),
        ]:
            tex = nodes.new("ShaderNodeTexImage")
            tex.image = bpy.data.images.load(str(ASSET / "textures" / f"{name}_{suffix}.png"))
            if suffix != "basecolor":
                tex.image.colorspace_settings.name = "Non-Color"
            links.new(tex.outputs["Color"], bs.inputs[socket])
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(str(ASSET / "textures" / f"{name}_normal.png"))
        tex.image.colorspace_settings.name = "Non-Color"
        normal = nodes.new("ShaderNodeNormalMap")
        normal.inputs["Strength"].default_value = 0.35
        links.new(tex.outputs["Color"], normal.inputs["Color"])
        links.new(normal.outputs["Normal"], bs.inputs["Normal"])
        if name == "cyan":
            bs.inputs["Emission Color"].default_value = (0.4, 0.8, 0.05, 1)
            bs.inputs["Emission Strength"].default_value = 1.3
        materials[name] = mat
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
                vertices=40,
                radius1=p["size"][0],
                radius2=p["topRadius"],
                depth=p["size"][1],
                location=p["pos"],
            )
            obj = bpy.context.object
        obj.name = f"{i:03d}_{p['name']}"
        obj.rotation_euler = [math.radians(v) for v in p["rot"]]
        bevel = obj.modifiers.new("Machined edges", "BEVEL")
        bevel.width = p["bevel"]
        bevel.segments = 2
        bpy.ops.object.modifier_apply(modifier=bevel.name)
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
        obj.data.materials.append(materials[p["mat"]])
        obj.modifiers.new("Weighted normals", "WEIGHTED_NORMAL")
    ground = bpy.data.materials.new("Studio graphite")
    ground.use_nodes = True
    bs = ground.node_tree.nodes.get("Principled BSDF")
    bs.inputs["Base Color"].default_value = (0.055, 0.065, 0.08, 1)
    bs.inputs["Roughness"].default_value = 0.65
    bpy.ops.mesh.primitive_plane_add(size=20000, location=(0, 0, 0))
    bpy.context.object.data.materials.append(ground)
    bpy.context.scene.world = bpy.data.worlds.new("Studio")
    world = bpy.context.scene.world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.23, 0.28, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.5

    def aim(obj, point):
        obj.rotation_euler = (Vector(point) - obj.location).to_track_quat("-Z", "Y").to_euler()

    for name, loc, power, col, size in [
        ("key", (-170, -220, 340), 2400000, (1, 0.95, 0.88), 190),
        ("rim", (130, 120, 260), 2300000, (0.63, 0.78, 1), 160),
        ("fill", (180, -150, 160), 1000000, (0.8, 0.9, 1), 180),
    ]:
        light = bpy.data.lights.new(name, "AREA")
        light.energy = power
        light.color = col
        light.shape = "DISK"
        light.size = size
        obj = bpy.data.objects.new(name, light)
        bpy.context.collection.objects.link(obj)
        obj.location = loc
        aim(obj, (0, 0, 110))
    camdata = bpy.data.cameras.new("Camera")
    cam = bpy.data.objects.new("Camera", camdata)
    bpy.context.collection.objects.link(cam)
    cam.location = (260, -450, 235)
    aim(cam, (0, -3, 109))
    camdata.type = "ORTHO"
    camdata.ortho_scale = 254
    scene = bpy.context.scene
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1500
    scene.render.resolution_y = 1800
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "AgX"
    scene.render.image_settings.file_format = "PNG"
    OUT.mkdir(exist_ok=True)
    for im in bpy.data.images:
        if im.source == "FILE":
            im.pack()
    # Saving the editable studio is optional; the GLB is the separate rigged asset.
    scene.render.filepath = str(OUT / "showcase.png")
    bpy.ops.render.render(write_still=True)
    cam.location = (-260, 440, 230)
    aim(cam, (0, 0, 109))
    scene.render.filepath = str(OUT / "showcase-back.png")
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()
