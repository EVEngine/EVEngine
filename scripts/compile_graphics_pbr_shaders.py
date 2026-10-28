"""Compile canonical PBR shaders, including the Vulkan separated-sampler binding variant."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from compile_graphics_water_shaders import SHADER_DIR, spv_to_inc
import utf8_stdio


def separated_sampler_source(source: str) -> str:
    """Keep all PBR textures while mapping normal samplers through fifteen shared slots.

    Binding 16 contains the normal sampler table; binding 17 contains five uvec4
    role-to-slot records. The comparison sampler retains its original binding.
    Static switch operands avoid requiring dynamic sampler-array indexing.
    """
    s = source
    for old, new in [
        ("sampler2D maps", "texture2D maps"),
        ("sampler2D detailMaps", "texture2D detailMaps"),
        ("samplerCube environmentMap", "textureCube environmentMap"),
        ("sampler2DArray extrasMap", "texture2DArray extrasMap"),
        ("sampler2DArray colorsMap", "texture2DArray colorsMap"),
        ("sampler3D vegetationFadeNoise", "texture3D vegetationFadeNoise"),
    ]:
        if s.count(old) != 1:
            raise ValueError(f"expected one canonical PBR declaration: {old}")
        s = s.replace(old, new)
    s = s.replace(
        "textureQueryLevels(environmentMap)",
        "textureQueryLevels(samplerCube(environmentMap,pbrSamplers[0]))",
    )
    s = re.sub(
        r"texture\(maps\[(\d+)\],", lambda m: f"pbrSample2D(maps[{m[1]}],pbrSampler({m[1]}u),", s
    )
    s = re.sub(
        r"texture\(detailMaps\[(\d+)\],",
        lambda m: f"pbrSample2D(detailMaps[{m[1]}],pbrSampler({11 + int(m[1])}u),",
        s,
    )
    for name, slot, call in [
        ("environmentMap", 14, "CubeLod"),
        ("extrasMap", 15, "ArrayLod"),
        ("colorsMap", 16, "ArrayLod"),
    ]:
        s = s.replace("textureLod(" + name + ",", f"pbrSample{call}({name},pbrSampler({slot}u),")
    s = s.replace(
        "texture(vegetationFadeNoise,", "pbrSample3D(vegetationFadeNoise,pbrSampler(17u),"
    )
    helpers = "layout(set=0,binding=16) uniform sampler pbrSamplers[15];\nlayout(set=0,binding=17,std140) uniform SamplerSlots {uvec4 values[5];} samplerSlots;\nuint pbrSampler(uint role){return samplerSlots.values[role/4u][role%4u];}\n"
    for name, itype, coord, constructor, lod in [
        ("2D", "texture2D", "vec2", "sampler2D", False),
        ("3D", "texture3D", "vec3", "sampler3D", False),
        ("CubeLod", "textureCube", "vec3", "samplerCube", True),
        ("ArrayLod", "texture2DArray", "vec3", "sampler2DArray", True),
    ]:
        helpers += (
            f"vec4 pbrSample{name}({itype} image,uint slot,{coord} uv"
            + (",float lod" if lod else "")
            + "){switch(slot){\n"
        )
        for i in range(15):
            helpers += (
                ("default:" if i == 14 else f"case {i}u:")
                + "return "
                + ("textureLod" if lod else "texture")
                + f"({constructor}(image,pbrSamplers[{i}]),uv"
                + (",lod" if lod else "")
                + ");\n"
            )
        helpers += "}}\n"
    s = s.replace("void main() {", helpers + "\nvoid main() {")
    return s


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--separated-only", action="store_true")
    args = parser.parse_args()
    if not args.separated_only:
        for stage in ("vert", "frag"):
            source = SHADER_DIR / f"pbr_surface.{stage}"
            binary = source.with_suffix(source.suffix + ".spv")
            subprocess.run(["glslc", str(source), "-o", str(binary)], check=True)
            spv_to_inc(
                binary, f"pbr_surface_{stage}_spv", SHADER_DIR / f"pbr_surface_{stage}_spv.inc"
            )
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "pbr_surface_separated.frag"
        binary = Path(temporary) / "pbr_surface_separated.spv"
        source.write_text(
            separated_sampler_source((SHADER_DIR / "pbr_surface.frag").read_text(encoding="utf-8")),
            encoding="utf-8",
        )
        subprocess.run(["glslc", str(source), "-o", str(binary)], check=True)
        spv_to_inc(
            binary,
            "pbr_surface_separated_frag_spv",
            SHADER_DIR / "pbr_surface_separated_frag_spv.inc",
        )
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
