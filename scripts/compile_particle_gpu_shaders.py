#!/usr/bin/env python3
"""Compile resident-particle compute/draw shaders and embed their SPIR-V."""

from __future__ import annotations

import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"
SHADERS = [
    ("particle_resident.comp", "comp"),
    ("particle_resident.vert", "vert"),
    ("particle_resident.frag", "frag"),
]


def spv_to_inc(spv_path: Path, array_name: str, out_path: Path) -> None:
    data = spv_path.read_bytes()
    if len(data) % 4 != 0:
        raise SystemExit(f"SPIR-V size not multiple of 4: {spv_path}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if not words or words[0] != 0x07230203:
        raise SystemExit(f"bad SPIR-V magic in {spv_path}")
    lines = [f"static const uint32_t {array_name}[] = {{"]
    for offset in range(0, len(words), 8):
        row = ", ".join(f"0x{word:08x}" for word in words[offset : offset + 8])
        lines.append(f"    {row},")
    lines.append("};")
    lines.append(
        f"static const size_t {array_name}_count = "
        f"sizeof({array_name}) / sizeof({array_name}[0]);"
    )
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out_path.name} ({len(words)} words)")


def main() -> int:
    glslc = shutil.which("glslc")
    if not glslc:
        raise SystemExit("need glslc on PATH (Vulkan SDK)")
    for name, stage in SHADERS:
        src = SHADER_DIR / name
        spv = Path(f"{src}.spv")
        result = subprocess.run(
            [glslc, f"-fshader-stage={stage}", str(src), "-o", str(spv),
             "--target-env=vulkan1.2"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise SystemExit(
                f"compile failed for {src.name}:\n"
                f"{result.stderr or result.stdout or '(no output)'}"
            )
        array_name = f"particle_resident_{stage}_spv"
        spv_to_inc(spv, array_name, SHADER_DIR / f"particle_resident_{stage}_spv.inc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
