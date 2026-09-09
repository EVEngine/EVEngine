"""Run in Blender's Text Editor once per session; Ctrl+S then publishes GLB.

Bootstrap assets: blender --background --python blender_bridge.py -- --create
The v1 manifest contains one basename, no newline; unknown names are rejected.
"""
from pathlib import Path
import os
import sys
import uuid

import bpy
from bpy.app.handlers import persistent

ROOT = Path(__file__).resolve().parent
TAG = "evengine_blender_hot_reload_v1"


def export_glb(destination):
    result = bpy.ops.export_scene.gltf(
        filepath=str(destination), export_format="GLB", use_selection=False,
        export_animations=False, export_cameras=False, export_lights=False,
    )
    if "FINISHED" not in result or not destination.is_file():
        raise RuntimeError("Blender GLB export did not finish")


@persistent
def publish_after_save(_):
    # A different .blend opened in this session must not replace this demo.
    if Path(bpy.data.filepath).resolve() != ROOT / "scene.blend":
        return
    generation = "scene-" + uuid.uuid4().hex + ".glb"
    temporary = ROOT / (".pending-" + generation)
    try:
        export_glb(temporary)
        os.replace(temporary, ROOT / generation)
        # Publish only after the complete, self-contained GLB exists.
        manifest = ROOT / ".current-v1.tmp"
        manifest.write_text(generation, encoding="ascii")
        os.replace(manifest, ROOT / "current-v1.txt")
        print("[blender] published " + generation)
    except Exception as error:
        print("[blender] export failed; previous generation retained:", error)
    finally:
        temporary.unlink(missing_ok=True)


publish_after_save.evengine_tag = TAG


def register():
    # Re-running the Text Editor script replaces our handler, never duplicates it.
    for handler in list(bpy.app.handlers.save_post):
        if getattr(handler, "evengine_tag", None) == TAG:
            bpy.app.handlers.save_post.remove(handler)
    bpy.app.handlers.save_post.append(publish_after_save)
    print("[blender] save bridge enabled for", ROOT / "scene.blend")


def create_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name, location, scale, color in [
        ("Ground", (0, 0, -0.2), (5, 5, 0.2), (0.25, 0.35, 0.3, 1)),
        ("Cube", (-1.6, 0, 1), (1, 1, 1), (0.85, 0.25, 0.12, 1)),
    ]:
        bpy.ops.mesh.primitive_cube_add(size=2, location=location)
        obj = bpy.context.object
        obj.name, obj.scale = name, scale
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.diffuse_color = color
        shader = mat.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = color
        shader.inputs["Roughness"].default_value = 0.8
        obj.data.materials.append(mat)
    bpy.ops.mesh.primitive_monkey_add(location=(1.7, 0, 1))
    bpy.context.object.name = "Suzanne"
    mat = bpy.data.materials.new("Suzanne Blue")
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (0.1, 0.45, 0.85, 1)
    shader.inputs["Roughness"].default_value = 0.65
    bpy.context.object.data.materials.append(mat)
    # Keep the bridge visible in the saved .blend; execution is explicitly manual.
    bpy.data.texts.load(str(ROOT / "blender_bridge.py"))
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "scene.blend"))
    export_glb(ROOT / "scene.glb")


if __name__ == "__main__":
    if "--" in sys.argv and "--create" in sys.argv[sys.argv.index("--") + 1:]:
        create_scene()
    register()
