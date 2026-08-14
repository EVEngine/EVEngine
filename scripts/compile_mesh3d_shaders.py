#!/usr/bin/env python3
"""Compile mesh3d / mesh3d_clustered GLSL to SPIR-V C++ include arrays.

Uses glslangValidator (or glslc if present). Supports #include via
GL_GOOGLE_include_directive and -I on the shaders directory.

Writes both:
  - mesh3d.frag.spv (tracked binary artifact)
  - mesh3d_frag_spv.inc (embedded in the engine)
"""

from __future__ import annotations

import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

SHADERS = [
    ("mesh3d.vert", "vert"),
    ("mesh3d.frag", "frag"),
    ("mesh3d_clustered.vert", "vert"),
    ("mesh3d_clustered.frag", "frag"),
    ("mesh3d_hair.vert", "vert"),
    ("mesh3d_hair.frag", "frag"),
    ("mesh3d_gbuffer.vert", "vert"),
    ("mesh3d_gbuffer.frag", "frag"),
    ("mesh3d_toon.vert", "vert"),
]


def spv_to_inc(spv_path: Path, array_name: str, out_path: Path) -> None:
    data = spv_path.read_bytes()
    if len(data) % 4 != 0:
        raise SystemExit(f"SPIR-V size not multiple of 4: {spv_path}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if not words or words[0] != 0x07230203:
        raise SystemExit(f"bad SPIR-V magic in {spv_path}")
    lines = [f"static const uint32_t {array_name}[] = {{"]
    row: list[str] = []
    for i, w in enumerate(words):
        row.append(f"0x{w:08x}")
        if len(row) == 8:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append(
        f"static const size_t {array_name}_count = sizeof({array_name})/sizeof({array_name}[0]);"
    )
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out_path.relative_to(ROOT)} ({len(words)} words)")


def compile_one(src: Path, stage: str) -> Path:
    # Repo convention: mesh3d.frag.spv alongside the GLSL source.
    out = Path(str(src) + ".spv")
    glslc = shutil.which("glslc")
    if glslc:
        cmd = [
            glslc,
            f"-fshader-stage={stage}",
            f"-I{SHADER_DIR}",
            str(src),
            "-o",
            str(out),
        ]
    else:
        validator = shutil.which("glslangValidator")
        if not validator:
            raise SystemExit("need glslc or glslangValidator on PATH")
        cmd = [
            validator,
            "-V",
            "-S",
            stage,
            f"-I{SHADER_DIR}",
            str(src),
            "-o",
            str(out),
        ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(
            f"compile failed for {src.name}:\n{r.stderr or r.stdout or '(no output)'}"
        )
    return out


def main() -> int:
    if not SHADER_DIR.is_dir():
        raise SystemExit(f"missing shader dir: {SHADER_DIR}")
    for name, stage in SHADERS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        spv = compile_one(src, stage)
        stem = src.stem  # mesh3d / mesh3d_clustered
        array = f"{stem}_{stage}_spv"
        spv_to_inc(spv, array, SHADER_DIR / f"{stem}_{stage}_spv.inc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
