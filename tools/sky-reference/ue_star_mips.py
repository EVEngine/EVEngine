"""Run inside UnrealEditor with EVENGINE_STAR_MIP_OUTPUT set to a new directory.

Reads the original runtime texture, including its authored mip filter and platform
compression, through an unlit explicit-LOD material. Never saves the source asset.
"""
import json
import os
from pathlib import Path
import struct
import time
import traceback
import uuid

import unreal


class StarMipReadback:
    def __init__(self):
        self.output = Path(os.environ["EVENGINE_STAR_MIP_OUTPUT"])
        self.output.mkdir(parents=True, exist_ok=False)
        self.texture = unreal.load_asset("/Game/UltraDynamicSky/Textures/Sky/Tiling_Stars")
        if self.texture is None:
            raise RuntimeError("Missing original Tiling_Stars asset")
        self.rows = []
        properties = {key: str(self.texture.get_editor_property(key)) for key in
                      ("mip_gen_settings", "lod_group", "lod_bias", "srgb", "compression_settings", "never_stream", "filter")}
        self.save("settings.json", properties)
        self.material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "StarsMipReadback_" + uuid.uuid4().hex[:12], "/Game/EveSkyReference/RuntimeReadback",
            unreal.Material, unreal.MaterialFactoryNew())
        if self.material is None:
            raise RuntimeError("Could not create diagnostic material")
        self.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        sample = unreal.MaterialEditingLibrary.create_material_expression(self.material, unreal.MaterialExpressionTextureSample)
        sample.set_editor_property("texture", self.texture)
        sample.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
        level = unreal.MaterialEditingLibrary.create_material_expression(self.material, unreal.MaterialExpressionScalarParameter)
        level.set_editor_property("parameter_name", "Mip")
        if not unreal.MaterialEditingLibrary.connect_material_expressions(level, "", sample, "Level"):
            raise RuntimeError("Could not connect explicit mip input")
        if not unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError("Could not connect unlit output")
        unreal.MaterialEditingLibrary.recompile_material(self.material)
        self.world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        self.instance = unreal.MaterialLibrary.create_dynamic_material_instance(self.world, self.material)
        unreal.SystemLibrary.execute_console_command(self.world, "r.Streaming.FullyLoadUsedTextures 1")
        self.start, self.index, self.handle = time.monotonic(), 0, None

    def save(self, name, value):
        (self.output / name).write_text(json.dumps(value, indent=2), encoding="utf-8")

    def finish(self):
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()

    def tick(self, _dt):
        if time.monotonic() - self.start < 15:
            return
        target = None
        try:
            size = max(1, 2048 >> self.index)
            target = unreal.RenderingLibrary.create_render_target2d(
                self.world, size, size, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
            self.instance.set_scalar_parameter_value("Mip", float(self.index))
            unreal.RenderingLibrary.draw_material_to_render_target(self.world, target, self.instance)
            pixels = unreal.RenderingLibrary.read_render_target_raw(self.world, target, False)
            if len(pixels) != size * size:
                raise RuntimeError("Unexpected raw mip dimensions")
            data = bytearray()
            for color in pixels:
                data.extend(struct.pack("<4e", color.r, color.g, color.b, 1.0))
            (self.output / f"mip-{self.index:02d}.rgba16f").write_bytes(data)
            self.rows.append({"mip": self.index, "size": size, "bytes": len(data),
                              "peak": max(max(c.r, c.g, c.b) for c in pixels)})
            self.save("progress.json", self.rows)
            self.index += 1
            self.start = time.monotonic() - 14
            if self.index == 12:
                self.finish()
        except Exception:
            (self.output / "error.txt").write_text(traceback.format_exc(), encoding="utf-8")
            self.finish()
        finally:
            if target is not None:
                unreal.RenderingLibrary.release_render_target2d(target)


readback = StarMipReadback()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
readback.handle = unreal.register_slate_post_tick_callback(readback.tick)
