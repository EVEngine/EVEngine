#!/usr/bin/env python3
"""Embed the built-in grass shaders; no shader files are needed at deployment."""

from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src/modules/graphics/shaders"


def main():
    compiler = shutil.which("glslc")
    if not compiler:
        raise SystemExit("glslc is required to rebuild embedded shader code")
    lines = [
        "// Generated shader code; rebuild with scripts/compile_stylized_grass.py.",
        "#pragma once",
        "namespace eve::graphics::detail::grass_spv {",
    ]
    with tempfile.TemporaryDirectory() as directory:
        for source in sorted((SHADERS / "stylized-grass").iterdir()):
            output = Path(directory) / (source.name + ".spv")
            subprocess.run(
                [compiler, "--target-env=vulkan1.2", str(source), "-o", str(output)], check=True
            )
            data = output.read_bytes()
            words = struct.unpack(f"<{len(data) // 4}I", data)
            name = source.name.replace(".", "_") + "_spv"
            lines.append(f"inline constexpr uint32_t {name}[] = {{")
            for i in range(0, len(words), 8):
                lines.append("    " + ",".join(f"0x{w:08x}u" for w in words[i : i + 8]) + ",")
            lines.append("};")
    lines.append("}\n")
    target = SHADERS / "stylized_grass_spv.inc"
    source = "\n".join(lines)
    if not target.exists() or target.read_text(encoding="utf-8") != source:
        target.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
