#!/usr/bin/env bash
# Rebuild canonical binaries and synchronize the standalone example resource copy.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
for stage in vert frag; do
    "${GLSLC:-glslc}" --target-env=vulkan1.2 "$HERE/surface.$stage" -o "$HERE/surface.$stage.spv"
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.2 "$HERE/surface.$stage.spv"
done
for file in style.nut surface.vert surface.frag surface.vert.spv surface.frag.spv; do
    cp "$HERE/$file" "$ROOT/examples/unit-surface-effects/styles/unit-surface/$file"
done
