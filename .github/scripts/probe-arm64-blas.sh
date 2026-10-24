#!/usr/bin/env bash
set -euo pipefail

g++ -std=c++20 -g .github/scripts/vulkan-probe/blas.cpp -lvulkan -o build/arm64-blas-probe
baseline=0
GALLIVM_DEBUG=symbols gdb --batch --return-child-result \
  -x .github/scripts/vulkan-probe/crash.gdb --args build/arm64-blas-probe || baseline=$?
if [ "$baseline" -ne 0 ]; then
  # Mesa 25.2.8 lp_bld_init_common.c defines nopt to disable LLVM optimization.
  # Compare the independent reproduction; a passing comparison does not mask
  # the required baseline failure or bypass any engine test.
  comparison=0
  GALLIVM_PERF=nopt GALLIVM_DEBUG=symbols gdb --batch --return-child-result \
    -x .github/scripts/vulkan-probe/crash.gdb --args build/arm64-blas-probe || comparison=$?
  echo "Standalone BLAS baseline=$baseline, LLVM optimization disabled=$comparison"
fi
exit "$baseline"
