#!/usr/bin/env python3
"""Compile the EVEngine GLSL shader sources to WGSL C-arrays for the WebGPU
backend.

For every `*.vert` / `*.frag` / `*.glsl` under src/modules/graphics/shaders,
this script runs the classic GLSL -> SPIR-V toolchain (glslc, or
glslangValidator as fallback) and then converts the SPIR-V to WGSL with Tint,
emitting a `<name>_wgsl.inc` file holding a `static const uint32_t` array (UTF-8
WGSL is embedded word-aligned via uint32_t storage, mirroring the existing
`*_spv.inc` convention).

Existing files are only rewritten when the WGSL content actually changes, so the
committed `.inc` files stay stable across builds.

Usage:
    python3 scripts/compile_webgpu_shaders.py --shaders-dir <dir> [--tint <path>] [--glslc <path>]

Environment overrides: EVENGINE_GLSLC, EVENGINE_TINT.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

STAGE_EXT = {".vert": "vertex", ".frag": "fragment", ".glsl": "fragment"}


def expand_opaque_sampler_arrays(src: str, name: str) -> str:
    """Expand fixed sampler arrays that WGSL cannot represent as handle arrays."""
    if name != "pbr_surface.frag":
        return src
    replacements = (("maps", 11, 20), ("detailMaps", 3, 40))
    for array_name, count, first_binding in replacements:
        declaration = re.compile(
            rf"layout\(set=0,binding=\d+\) uniform sampler2D {array_name}\[{count}\];"
        )
        expanded = "\n".join(
            f"layout(set=0,binding={first_binding + index}) uniform sampler2D {array_name[:-1]}{index};"
            for index in range(count)
        )
        src, changed = declaration.subn(expanded, src, count=1)
        if changed != 1:
            raise ValueError(f"expected one {array_name}[{count}] declaration")
        src = re.sub(
            rf"\b{array_name}\[(\d+)\]",
            lambda match: f"{array_name[:-1]}{match.group(1)}",
            src,
        )
    return src


def remap_pbr_vertex_bindings(src: str, name: str) -> str:
    """Keep split WebGPU texture/sampler bindings disjoint from the fragment stage."""
    if name != "pbr_surface.vert":
        return src
    for old, new in ((10, 50), (11, 52), (12, 54)):
        marker = f"layout(set=0,binding={old})"
        if src.count(marker) != 1:
            raise ValueError(f"expected one PBR vertex binding {old}")
        src = src.replace(marker, f"layout(set=0,binding={new})", 1)
    return src


def find_tool(name, env_name, extra_hints):
    if env_name and os.environ.get(env_name):
        return os.environ[env_name]
    p = shutil.which(name)
    if p:
        return p
    for hint in extra_hints:
        if hint and os.path.exists(hint):
            return hint
    return None


def emit_wgsl_inc(src_path: Path, wgsl: bytes) -> Path:
    out = src_path.with_suffix(src_path.suffix + "_wgsl.inc")
    # Include the stage suffix: foo.vert and foo.frag are commonly included by
    # the same translation unit and must not emit the same C++ symbol.
    stem = src_path.name.replace("-", "_").replace(".", "_")
    array_name = f"{stem}_wgsl"

    # Pack the WGSL source so the C++ side can memcpy it as an opaque blob.
    padded = wgsl + b"\x00" * (-len(wgsl) % 4)
    words = struct.unpack(f"<{len(padded) // 4}I", padded)
    lines = [f"static const uint32_t {array_name}[] = {{"]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08x}" for w in words[i : i + 8]) + ",")
    lines.append("};")
    lines.append(f"static const size_t {array_name}_count = {len(words)};")
    content = "\n".join(lines) + "\n"

    if out.exists() and out.read_bytes() == content.encode("utf-8"):
        return out
    out.write_text(content, encoding="utf-8")
    print(f"[webgpu-shaders] wrote {out}")
    return out


def compile_one(glsl_path: Path, glslc, glslang, tint, scratch: Path):
    ext = glsl_path.suffix.lower()
    stage = STAGE_EXT.get(ext)
    if stage is None:
        return

    # Fragment includes (parallax_map.glsl / tex_cell_bomb.glsl / tonemap.glsl)
    # are pulled into a main shader via #include; they have no entry point and
    # cannot be compiled standalone. Skip them.
    try:
        src = glsl_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return
    if "void main" not in src:
        print(f"[webgpu-shaders] SKIP {glsl_path.name}: include fragment (no main)")
        return

    spv_path = scratch / (glsl_path.stem + ".spv")
    compile_path = glsl_path
    expanded = expand_opaque_sampler_arrays(src, glsl_path.name)
    expanded = remap_pbr_vertex_bindings(expanded, glsl_path.name)
    if expanded != src:
        compile_path = scratch / glsl_path.name
        compile_path.write_text(expanded, encoding="utf-8")

    # 1) GLSL -> SPIR-V
    if glslc:
        cmd = [glslc, f"-fshader-stage={stage}", "-I", str(glsl_path.parent), "-o", str(spv_path), str(compile_path)]
    elif glslang:
        cmd = [glslang, f"-S{stage}", "-V", "-I" + str(glsl_path.parent), "-o", str(spv_path), str(compile_path)]
    else:
        print(f"[webgpu-shaders] SKIP {glsl_path.name}: no glslc/glslangValidator on PATH")
        return
    subprocess.run(cmd, check=True, capture_output=True)

    # 2) SPIR-V -> WGSL
    if not tint:
        print(f"[webgpu-shaders] SKIP {glsl_path.name}: no tint on PATH")
        return
    wgsl_path = scratch / (glsl_path.stem + ".wgsl")
    subprocess.run([tint, str(spv_path), "--format", "wgsl", "--allow-non-uniform-derivatives", "true",
                    "-o", str(wgsl_path)],
                   check=True, capture_output=True)
    wgsl = wgsl_path.read_bytes()
    emit_wgsl_inc(glsl_path, wgsl)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shaders-dir", required=True)
    ap.add_argument("--tint", default="")
    ap.add_argument("--glslc", default="")
    args = ap.parse_args()

    shaders_dir = Path(args.shaders_dir)
    if not shaders_dir.is_dir():
        print(f"[webgpu-shaders] missing shaders dir {shaders_dir}")
        return 1

    glslc = args.glslc or find_tool("glslc", "EVENGINE_GLSLC", [])
    glslang = args.glslc or find_tool("glslangValidator", "EVENGINE_GLSLANG", [])
    tint = args.tint or find_tool("tint", "EVENGINE_TINT", [])

    if not (glslc or glslang):
        print("[webgpu-shaders] WARNING: no GLSL compiler (glslc/glslangValidator); "
              "WebGPU shader .inc files will not be (re)generated.")
    if not tint:
        print("[webgpu-shaders] WARNING: no tint found; WGSL .inc files will not be regenerated.")

    with tempfile.TemporaryDirectory(prefix="eve_wgsl_") as td:
        scratch = Path(td)
        for glsl_path in sorted(shaders_dir.glob("*")):
            if glsl_path.suffix.lower() in STAGE_EXT:
                try:
                    compile_one(glsl_path, glslc, glslang, tint, scratch)
                except (subprocess.CalledProcessError, OSError) as e:
                    print(f"[webgpu-shaders] ERROR {glsl_path.name}: {e}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
