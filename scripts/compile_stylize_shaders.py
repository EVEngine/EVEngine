#!/usr/bin/env python3
"""Compile stylize GLSL fragments to SPIR-V C++ include arrays."""

from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "stylize" / "shaders"

FRAGS = [
    "cartoon_post.frag",
    "watercolor_post.frag",
    "ink_post.frag",
    "pixel_post.frag",
    "ink_mesh.frag",
    "xray_mesh.frag",
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


def compile_frag(src: Path) -> Path:
    out = src.with_suffix(".spv")
    cmd = ["glslc", "-fshader-stage=frag", str(src), "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"glslc failed for {src.name}:\n{r.stderr or r.stdout}")
    return out


def main() -> int:
    if not SHADER_DIR.is_dir():
        raise SystemExit(f"missing shader dir: {SHADER_DIR}")
    for name in FRAGS:
        src = SHADER_DIR / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        spv = compile_frag(src)
        stem = src.stem  # e.g. cartoon_post
        array = f"{stem}_frag_spv"
        spv_to_inc(spv, array, SHADER_DIR / f"{stem}_frag_spv.inc")
        spv.unlink(missing_ok=True)
    # Mesh ink reuses mesh3d_toon.vert SPIR-V from graphics; no vert compile here.
    return 0


if __name__ == "__main__":
    sys.exit(main())
