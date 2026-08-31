#!/usr/bin/env python3
"""Compile the screen-space-reflection fragment to a SPIR-V C++ include array."""

from __future__ import annotations

import struct
import subprocess
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

FRAGS = ["ssr.frag", "ssr_temporal.frag"]


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
    glslang = shutil.which("glslangValidator")
    if not glslc and not glslang:
        raise SystemExit("need glslc or glslangValidator on PATH (Vulkan SDK)")
    for name in FRAGS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        out_spv = src.with_suffix(".spv")
        command = ([glslc, "-fshader-stage=frag", str(src), "-o", str(out_spv)] if glslc else
                   [glslang, "-V", "-S", "frag", "--target-env", "vulkan1.2", str(src),
                    "-o", str(out_spv)])
        r = subprocess.run(command, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"shader compile failed for {name}:\n{r.stderr or r.stdout}")
        array = f"{src.stem}_frag_spv"
        spv_to_inc(out_spv, array, SHADER_DIR / f"{src.stem}_frag_spv.inc")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
