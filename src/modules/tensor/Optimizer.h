#ifndef EVE_TENSOR_OPTIMIZER_H
#define EVE_TENSOR_OPTIMIZER_H

#include "tensor/Graph.h"

#include <vector>

namespace eve::tensor {

/**
 * Kernel group produced by the optimizer (AITemplate-style fusion).
 *
 * A group is either:
 *  - an Elementwise chain: multiple graph nodes fused into ONE generated
 *    kernel; nodes inside the chain never materialize a buffer;
 *  - a MatMul / Conv group with an optional bias + elementwise epilogue
 *    fused into the same kernel;
 *  - a single specialized op kernel (softmax, layernorm, attention, ...);
 *  - an Alias (reshape / flatten / cast): no kernel, output aliases input.
 */
enum class GroupKind : uint8_t {
    Elementwise,
    MatMul,
    Conv1d,
    Conv2d,
    MaxPool2d,
    AvgPool2d,
    Softmax,
    LayerNorm,
    RMSNorm,
    Reduce,
    ArgMax,
    Embedding,
    Concat,
    Slice,
    Permute,
    Sdpa,
    Resize2d,
    Alias,
};

struct FusedGroup {
    GroupKind kind = GroupKind::Elementwise;
    /** Actual op for polymorphic groups (Reduce sum/mean/min/max, Softmax/LogSoftmax). */
    OpType op = OpType::Const;
    /** Graph node ids in execution order (elementwise chains: all chain nodes). */
    std::vector<int> nodes;
    /** External producer node ids (deduped, ordered by first use). */
    std::vector<int> inputs;
    int  outputNode = -1;

    // attributes copied from the root node for convenience
    float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;
    int   i0 = 0, i1 = 0, i2 = 0, i3 = 0;
    int   perm[Tensor::kMaxRank] = {0, 1, 2, 3, 4, 5};
    int   permRank = 0;
    int   dtype = static_cast<int>(DType::Float32);

    // MatMul / Conv epilogue: bias node id (or -1) + elementwise chain node ids
    int              biasNode = -1;
    std::vector<int> epilogue;
    bool             hasScale = false;
    bool             hasBias  = false;
    bool             masked   = false;
    bool             logMode  = false;
};

/**
 * Result of the graph optimizer: execution order, fused groups, and a static
 * memory plan that reuses buffers when node lifetimes don't overlap.
 */
struct OptimizedGraph {
    /** Topological order of all live nodes (CPU interpreter uses this). */
    std::vector<int> order;
    /** Fused groups in execution order (GPU codegen uses this). */
    std::vector<FusedGroup> groups;
    /** Execution order: indices into `groups` (topological by output position). */
    std::vector<int> groupOrder;
    /** nodeId -> arena slot id; -1 = fused into a group (no buffer). */
    std::vector<int> nodeSlot;
    /** slot id -> capacity in floats. */
    std::vector<int> slotSize;
    /** Slots that live for the whole program (consts, placeholders, output). */
    std::vector<int> persistentSlots;
    int outputNode = -1;
};

/**
 * Optimize a traced graph:
 *  1. dead-code elimination + topological sort;
 *  2. constant folding of elementwise chains over Const nodes;
 *  3. elementwise chain fusion into FusedGroups;
 *  4. matmul/conv bias + activation epilogue fusion;
 *  5. static memory planning with buffer reuse.
 */
OptimizedGraph optimizeGraph(const Graph &graph, int outputNode);

/** Number of groups that need GPU kernels (i.e. not Alias). */
int groupKernelCount(const OptimizedGraph &opt);

}  // namespace eve::tensor

#endif  // EVE_TENSOR_OPTIMIZER_H
