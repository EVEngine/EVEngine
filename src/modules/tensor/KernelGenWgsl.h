#pragma once

#include "common/Result.h"
#include "tensor/KernelGen.h"

namespace eve::tensor {
/** @brief Internal choice of generated matrix multiplication implementation. */
enum class KernelVariant { Default, TiledMatMul };

/**
 * @brief Lower an optimizer-produced group directly to owning WGSL source and dispatch metadata.
 * @param graph Borrowed, immutable graph; must outlive this synchronous call.
 * @param group Valid group produced by optimizeGraph for graph.
 * @param variant TiledMatMul is supported only for unquantized rank-2 matrix products.
 * @return Owning kernel specification, or Unsupported with a code-generation diagnostic.
 * @note Reentrant and CPU-only; retains no graph references and creates no GPU resources.
 */
[[nodiscard]] Result<KernelSpec> generateWgslKernel(const Graph &graph, const FusedGroup &group,
                                                    KernelVariant variant = KernelVariant::Default);
}  // namespace eve::tensor
