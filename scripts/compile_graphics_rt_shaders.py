#!/usr/bin/env python3
"""Compile and validate Vulkan reflection/path-tracing shaders as C++ arrays."""

from __future__ import annotations

import struct
import subprocess
import shutil
import tempfile
from pathlib import Path
import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "src" / "modules" / "graphics" / "shaders"

SHADERS = [
    ("rt_reflection.rgen", "rgen", "rt_reflection_rgen_spv"),
    ("rt_reflection.rmiss", "rmiss", "rt_reflection_rmiss_spv"),
    ("rt_reflection.rchit", "rchit", "rt_reflection_rchit_spv"),
    ("pt_path.rgen", "rgen", "pt_path_rgen_spv"),
    ("pt_path.rmiss", "rmiss", "pt_path_rmiss_spv"),
    ("pt_path.rahit", "rahit", "pt_path_rahit_spv"),
    ("pt_path.rchit", "rchit", "pt_path_rchit_spv"),
    ("pt_denoise.comp", "compute", "pt_denoise_spv"),
    ("pt_temporal.comp", "compute", "pt_temporal_spv"),
]
SAMPLING_TEST = ROOT / "test" / "integration" / "render-pipeline"


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
    validator = shutil.which("spirv-val")
    if not validator:
        raise SystemExit("need spirv-val on PATH (Vulkan SDK)")
    with tempfile.TemporaryDirectory(prefix="eve-rt-shaders-") as temporary:
        pending: list[tuple[Path, str, Path]] = []
        jobs = [(SHADER_DIR / name, stage, array, SHADER_DIR) for name, stage, array in SHADERS]
        jobs.append((SAMPLING_TEST / "path_sampling.comp", "compute", "pt_sampling_spv", SAMPLING_TEST))
        for src, stage, array, destination in jobs:
            if not src.is_file():
                raise SystemExit(f"missing {src}")
            out_spv = Path(temporary) / f"{array}.spv"
            command = [
                glslc,
                f"-fshader-stage={stage}",
                "--target-env=vulkan1.2",
                "-I", str(SHADER_DIR),
                str(src),
                "-o",
                str(out_spv),
            ]
            for step in (command, [validator, "--target-env", "vulkan1.2", str(out_spv)]):
                r = subprocess.run(step, capture_output=True, text=True, encoding="utf-8")
                if r.returncode != 0:
                    raise SystemExit(f"shader build failed for {src.name}:\n{r.stderr or r.stdout}")
            pending.append((out_spv, array, destination))
        # A compilation failure must not publish only part of a shader family.
        for out_spv, array, destination in pending:
            spv_to_inc(out_spv, array, destination / f"{array}.inc")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
