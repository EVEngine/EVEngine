#!/usr/bin/env python3
"""Compile voxel_rect GLSL to SPIR-V C++ include arrays (uses glslangValidator)."""

from __future__ import annotations

import struct
import subprocess
import sys
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

SHADERS = [
    ("voxel_rect.vert", "vert"),
    ("voxel_rect.frag", "frag"),
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
    lines.append(f"static const size_t {array_name}_count = {len(words)};")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out_path.relative_to(ROOT)} ({len(words)} words)")


def compile_shader(src: Path, stage: str) -> Path:
    out = src.with_suffix(".spv")
    # Prefer glslc, fall back to glslangValidator.
    if shutil.which("glslc"):
        cmd = ["glslc", f"-fshader-stage={stage}", str(src), "-o", str(out)]
    else:
        cmd = ["glslangValidator", "-V", str(src), "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"compile failed for {src.name}:\n{r.stderr or r.stdout}")
    return out


def main() -> int:
    if not SHADER_DIR.is_dir():
        raise SystemExit(f"missing shader dir: {SHADER_DIR}")
    for name, stage in SHADERS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        spv = compile_shader(src, stage)
        stem = src.stem  # voxel_rect
        array = f"{stem}_{stage}_spv"
        spv_to_inc(spv, array, SHADER_DIR / f"{stem}_{stage}_spv.inc")
        spv.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
