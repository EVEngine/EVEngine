#!/usr/bin/env python3
"""Compile the stage-2 GPU-driven compute shaders (HZB build / cull / emit).

Writes the same artifacts as scripts/compile_mesh3d_shaders.py:
  - <name>.comp.spv  (tracked binary)
  - <name>_comp_spv.inc  (embedded C++ array, consumed by Graphics.cpp)
"""

from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

SHADERS = [
    ("hzb_build.comp", "comp"),
    ("gpu_cull.comp", "comp"),
    ("gpu_emit.comp", "comp"),
    ("vg_main_cull.comp", "comp"),
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
    print(f"wrote {out_path.name} ({len(words)} words)")


def compile_one(src: Path) -> Path:
    out = Path(str(src) + ".spv")
    glslc = shutil.which("glslc")
    glslang = shutil.which("glslangValidator")
    if glslc:
        cmd = [glslc, "-fshader-stage=comp", f"-I{SHADER_DIR}", str(src), "-o", str(out),
               "--target-env=vulkan1.2"]
    elif glslang:
        cmd = [glslang, "-V", "-S", "comp", f"-I{SHADER_DIR}", "--target-env", "vulkan1.2",
               str(src), "-o", str(out)]
    else:
        raise SystemExit("need glslc or glslangValidator on PATH (Vulkan SDK)")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"compile failed for {src.name}:\n{r.stderr or r.stdout or '(no output)'}")
    return out


def main() -> int:
    for name, stage in SHADERS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        spv = compile_one(src)
        array = f"{src.stem}_{stage}_spv"
        spv_to_inc(spv, array, SHADER_DIR / f"{src.stem}_{stage}_spv.inc")
    return 0


if __name__ == "__main__":
    import shutil
    sys.exit(main())
