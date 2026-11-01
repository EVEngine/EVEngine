#!/usr/bin/env bash
# Rebuild canonical binaries and synchronize the standalone example resource copy.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
for stage in vert frag; do
    "${GLSLC:-glslc}" --target-env=vulkan1.2 "$HERE/surface.$stage" -o "$HERE/surface.$stage.spv"
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.2 "$HERE/surface.$stage.spv"
done
# Mode is fixed for the lifetime of each UnitSurfaceEffect. Removing the other
# 25 branches offline bounds cold software-driver compilation without changing
# the 128-byte parameter ABI or any effect's runtime algorithm.
for mode in $(seq -1 25); do
    file="surface-mode-$mode.frag.spv"
    "${GLSLC:-glslc}" --target-env=vulkan1.2 -O -DUNIT_SURFACE_MODE="$mode" "$HERE/surface.frag" -o "$HERE/$file"
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.2 "$HERE/$file"
done
for file in style.nut surface.vert surface.frag surface.vert.spv surface.frag.spv surface-mode-*.frag.spv; do
    for source in "$HERE"/$file; do
        cp "$source" "$ROOT/examples/unit-surface-effects/styles/unit-surface/$(basename "$source")"
    done
done
