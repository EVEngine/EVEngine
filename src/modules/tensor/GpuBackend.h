#ifndef EVE_TENSOR_GPUBACKEND_H
#define EVE_TENSOR_GPUBACKEND_H

#include <vector>

namespace eve::tensor {

class Graph;
struct OptimizedGraph;

/**
 * @brief GPU execution of a compiled tensor Graph via generated compute shaders.
 *
 * Built once per CompiledFunction (shapes are static at compile time):
 *  - every fused group is lowered by KernelGen into a fully specialized GLSL
 *    kernel (shapes baked as constants, no per-dispatch shape work);
 *  - a static memory plan reuses arena buffers whenever node lifetimes don't
 *    overlap (AITemplate-style memory planning);
 *  - rank-2 matmuls are autotuned between a naive thread-per-output kernel and
 *    a shared-memory 16x16 tiled kernel.
 *
 * tryBuild() never throws: it returns nullptr when Vulkan/gpgpu isn't
 * available or a group cannot be lowered, in which case the caller falls back
 * to the CPU interpreter.
 */
class GpuProgram {
public:
    ~GpuProgram();

    static GpuProgram *tryBuild(const Graph &graph, const OptimizedGraph &opt, int outputNode);

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

#endif  // EVE_TENSOR_GPUBACKEND_H
