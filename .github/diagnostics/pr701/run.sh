#!/usr/bin/env bash
set -euo pipefail
cd .github/diagnostics/pr701
for mode in GENERATED VERTEX PUSH UBO SSBO MRT SAMPLER_VERTEX SAMPLER_ALL; do
  glslc -D"$mode" v.vert -o "$mode.vert.spv"
  glslc -D"$mode" f.frag -o "$mode.frag.spv"
done
for mode in GBUFFER GBUFFER_VERTEX; do
  glslc ../../../src/modules/graphics/shaders/mesh3d_gbuffer.vert -o "$mode.vert.spv"
  glslc ../../../src/modules/graphics/shaders/mesh3d_gbuffer.frag -o "$mode.frag.spv"
done
python3 - <<'PYSPV'
from pathlib import Path
import re,struct
for stage in ('vert','frag'):
    p=Path('../../../src/modules/graphics/shaders')/f'mesh3d_gbuffer_{stage}_spv.inc'
    words=[int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',p.read_text(encoding='utf-8'))]
    Path(f'GBUFFER_EMBEDDED.{stage}.spv').write_bytes(struct.pack(f'<{len(words)}I',*words))
PYSPV
clang++ -std=c++20 -O0 -g probe.cpp -I"$VULKAN_SDK/include" -L"$VULKAN_SDK/lib" -lvulkan -Wl,-rpath,"$VULKAN_SDK/lib" -o probe
./probe
