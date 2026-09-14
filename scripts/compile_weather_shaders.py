#!/usr/bin/env python3
"""Regenerate embedded weather SPIR-V from GLSL (requires glslc on PATH)."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src/modules/weather/shaders"


def main():
    compiler = shutil.which("glslc")
    if not compiler:
        raise SystemExit("glslc is required; add the Vulkan SDK Bin directory to PATH")
    with tempfile.TemporaryDirectory() as temp:
        for effect in ("weather", "bolt"):
            for stage in ("vert", "frag"):
                name = f"{effect}_{stage}_spv"
                output = Path(temp) / f"{name}.spv"
                subprocess.run([compiler, f"-fshader-stage={stage}",
                                str(SHADERS / f"{effect}_{stage}.glsl"),
                                "-o", str(output)], check=True)
                data = output.read_bytes()
                words = struct.unpack(f"<{len(data) // 4}I", data)
                lines = [f"static const uint32_t {name}[] = {{"]
                lines += ["    " + ", ".join(f"0x{w:08x}" for w in words[i:i + 8]) + ","
                          for i in range(0, len(words), 8)]
                lines += ["};", f"static const size_t {name}_count = sizeof({name})/sizeof({name}[0]);", ""]
                (SHADERS / f"{name}.inc").write_text("\n".join(lines), encoding="utf-8")
                print(f"Compiled {effect} {stage}: {len(words)} words")


if __name__ == "__main__":
    main()
