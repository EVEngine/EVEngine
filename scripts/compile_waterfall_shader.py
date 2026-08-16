#!/usr/bin/env python3
"""Compile the waterfall mesh fragment GLSL to a SPIR-V C++ include array.

Source of truth: src/modules/graphics/shaders/waterfall_mesh.frag
Output:          src/modules/graphics/shaders/waterfall_mesh_frag_spv.inc
"""

from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "modules" / "graphics" / "shaders" / "waterfall_mesh.frag"
OUT = ROOT / "src" / "modules" / "graphics" / "shaders" / "waterfall_mesh_frag_spv.inc"
ARRAY = "waterfall_mesh_frag_spv"


def spv_to_inc(spv_path: Path) -> None:
    data = spv_path.read_bytes()
    if len(data) % 4 != 0:
        raise SystemExit(f"SPIR-V size not multiple of 4: {spv_path}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if not words or words[0] != 0x07230203:
        raise SystemExit(f"bad SPIR-V magic in {spv_path}")
    lines = [f"static const uint32_t {ARRAY}[] = {{"]
    row: list[str] = []
    for w in words:
        row.append(f"0x{w:08x}")
        if len(row) == 8:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append(f"static const size_t {ARRAY}_count = {len(words)};")
    lines.append("")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(words)} words)")


def main() -> int:
    if not SRC.is_file():
        raise SystemExit(f"missing {SRC}")
    spv = SRC.with_suffix(".spv")
    r = subprocess.run(["glslc", "-fshader-stage=frag", str(SRC), "-o", str(spv)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"glslc failed for {SRC.name}:\n{r.stderr or r.stdout}")
    try:
        spv_to_inc(spv)
    finally:
        spv.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
