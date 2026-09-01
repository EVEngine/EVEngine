#!/usr/bin/env python3
"""Compile the Vulkan reflection-probe compute filter and embed SPIR-V."""

import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "src/modules/graphics/shaders/reflection_probe_filter.comp"

glslc = shutil.which("glslc")
spv = Path(str(SHADER) + ".spv")
if glslc:
    command = [glslc, "-fshader-stage=comp", "--target-env=vulkan1.2", str(SHADER),
               "-o", str(spv)]
else:
    glslang = shutil.which("glslangValidator")
    if not glslang:
        raise SystemExit("need glslc or glslangValidator on PATH")
    command = [glslang, "-V", "--target-env", "vulkan1.2", "-S", "comp", str(SHADER),
               "-o", str(spv)]
subprocess.run(command, check=True)
data = spv.read_bytes()
words = struct.unpack(f"<{len(data) // 4}I", data)
name = "reflection_probe_filter_comp_spv"
lines = [f"static const uint32_t {name}[] = {{"]
for offset in range(0, len(words), 8):
    lines.append("    " + ", ".join(f"0x{word:08x}" for word in words[offset:offset + 8]) + ",")
lines += ["};", f"static const size_t {name}_count = sizeof({name})/sizeof({name}[0]);", ""]
(SHADER.parent / "reflection_probe_filter_comp_spv.inc").write_text("\n".join(lines), encoding="utf-8")
