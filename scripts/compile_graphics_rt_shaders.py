#!/usr/bin/env python3
"""Compile Vulkan ray-tracing reflection shaders to SPIR-V C++ include arrays."""

from __future__ import annotations

import struct
import subprocess
import shutil
from pathlib import Path
import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

SHADERS = [
    ("rt_reflection.rgen", "rgen", "rt_reflection_rgen_spv"),
    ("rt_reflection.rmiss", "rmiss", "rt_reflection_rmiss_spv"),
    ("rt_reflection.rchit", "rchit", "rt_reflection_rchit_spv"),
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


def main() -> int:
    glslc = shutil.which("glslc")
    if not glslc:
        raise SystemExit("need glslc on PATH (Vulkan SDK)")
    for name, stage, array in SHADERS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        out_spv = src.with_suffix(".spv")
        command = [
            glslc,
            f"-fshader-stage={stage}",
            "--target-env=vulkan1.2",
            str(src),
            "-o",
            str(out_spv),
        ]
        r = subprocess.run(command, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"shader compile failed for {name}:\n{r.stderr or r.stdout}")
        spv_to_inc(out_spv, array, SHADER_DIR / f"{array}.inc")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
