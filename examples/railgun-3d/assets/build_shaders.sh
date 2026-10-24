#!/usr/bin/env bash
# Rebuild the shipped Vulkan stages; runtime needs no shader compiler.
set -euo pipefail
cd "$(dirname "$0")"
compiler="${GLSLANG_VALIDATOR:-glslangValidator}"
"$compiler" -V rail-batch.vert -o smoke.vert.spv
"$compiler" -V rail-batch.frag -o smoke.frag.spv
"$compiler" -V -DSPARK=1 rail-batch.vert -o spark.vert.spv
"$compiler" -V -DSPARK=1 rail-batch.frag -o spark.frag.spv
"$compiler" -V -DBEAM=1 rail-batch.vert -o beam.vert.spv
"$compiler" -V -DBEAM=1 rail-batch.frag -o beam.frag.spv
