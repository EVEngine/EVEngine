#ifndef EVE_TENSOR_KERNELGEN_H
#define EVE_TENSOR_KERNELGEN_H

#include "tensor/Optimizer.h"

#include <string>
#include <vector>

namespace eve::tensor {

/**
 * A generated, fully specialized compute kernel (AITemplate-style codegen).
 *
 * Shapes are baked into the GLSL as constants; the only runtime inputs are the
 * storage buffers themselves, so a CompiledFunction is a static executable
 * program with no per-dispatch shape computation.
 *
 * Binding layout:
 *  - single-pass kernels: inputs 0..k-1, output binding k;
 *  - two-pass kernels (softmax / layernorm / rmsnorm): pass1 binds the first
 *    `inputsReadPass1` inputs + `statsCount` stats buffers; pass2 binds all
 *    inputs + stats + the output as the last binding.
 */
struct KernelSpec {
    std::string pass1;   // empty when single-pass
    std::string pass2;   // always set
    int groupsX1 = 0, groupsY1 = 1, groupsZ1 = 1;
    int groupsX2 = 0, groupsY2 = 1, groupsZ2 = 1;
    int inputCount = 0;          // total group input buffers
    int inputsReadPass1 = 0;     // leading inputs bound in pass1
    int statsCount = 0;          // per-row stats buffers (allocated by the runtime)
    int statsSize = 0;           // elements per stats buffer (rows)
    bool twoPass = false;

    // Weight-only quantization of a matmul B / embedding table input.
    // qDtype == 0 means fp32 storage. int8/int4 add a per-group scales buffer.
    int qDtype = 0;              // static_cast<int>(DType)
    int qGroup = 0;              // elements per scale group
    int scalesBinding = -1;      // storage binding of the scales buffer
    int outputBinding = -1;      // storage binding of the output (override)
};

/**
 * Generate the specialized GLSL kernel(s) for a fused group.
 * Returns false when the group cannot be lowered (e.g. too many inputs for the
 * fixed 8-binding descriptor layout) — callers fall back to the CPU interpreter.
 */
bool generateKernel(const Graph &graph, const FusedGroup &group, KernelSpec &out);

/**
 * Generate a specific matmul variant for autotuning (tiled=false: thread-per-
 * output naive; tiled=true: shared-memory 16x16 tiles). Only rank-2 matmuls
 * support the tiled variant.
 */
bool generateMatMulVariant(const Graph &graph, const FusedGroup &group, bool tiled,
                           KernelSpec &out);

/** Max storage bindings available per generated kernel. */
constexpr int kMaxKernelBindings = 8;

}  // namespace eve::tensor

#endif  // EVE_TENSOR_KERNELGEN_H
