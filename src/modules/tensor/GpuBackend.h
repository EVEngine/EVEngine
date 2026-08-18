#pragma once

#include <vector>

namespace eve::tensor {

class Graph;

/**
 * @brief GPU execution of a compiled tensor Graph via eve::gpgpu compute shaders.
 *
 * Built once per CompiledFunction (shapes are static at compile time): every
 * graph node gets a GPU storage buffer, ops are mapped to a handful of shared
 * compute kernels (binary / unary / matmul / transpose / where), and the
 * dispatch order is precomputed. run() only needs to re-upload placeholder
 * feeds, replay the dispatch sequence, and download the output.
 *
 * tryBuild() never throws: it returns nullptr when Vulkan/gpgpu isn't
 * available (no window yet, missing glslc, unsupported driver, ...), in
 * which case the caller should fall back to the CPU interpreter.
 */
class GpuProgram {
public:
    ~GpuProgram();

    static GpuProgram *tryBuild(const Graph &graph, const std::vector<int> &order, int outputNode);

    /** @brief feeds[slot] must point to `placeholderSize(slot)` floats. Returns the output buffer. */
    std::vector<float> run(const std::vector<const float *> &feeds) const;

private:
    GpuProgram();
    GpuProgram(const GpuProgram &) = delete;
    GpuProgram &operator=(const GpuProgram &) = delete;

    struct Impl;
    Impl *impl_ = nullptr;
};

/**
 * @brief GPU-accelerated reduction for large eager tensors.
 * op: 0 = sum, 1 = min, 2 = max. Returns false (caller should fall back to CPU)
 * when Vulkan/gpgpu isn't available.
 */
bool gpuReduce(const float *data, int size, int op, float &outResult);

}  // namespace eve::tensor
