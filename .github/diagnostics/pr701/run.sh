#!/usr/bin/env bash
set -euo pipefail
cd .github/diagnostics/pr701
for mode in GENERATED VERTEX PUSH UBO SSBO MRT SAMPLER_VERTEX SAMPLER_ALL; do
  glslc -D"$mode" v.vert -o "$mode.vert.spv"
  glslc -D"$mode" f.frag -o "$mode.frag.spv"
done
clang++ -std=c++20 -O0 -g probe.cpp -I"$VULKAN_SDK/include" -L"$VULKAN_SDK/lib" -lvulkan -Wl,-rpath,"$VULKAN_SDK/lib" -o probe
./probe
