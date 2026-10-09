
#include "common/Export.h"
#ifndef EVE_TENSOR_GRAPH_H
#define EVE_TENSOR_GRAPH_H

#include "tensor/Tensor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::tensor {

class Tensor;
class TF;
class Func;
class GpuProgram;
struct OptimizedGraph;

/** @brief OpType public API. */
enum class OpType : uint8_t {
    Placeholder = 0,
    Const,
    // binary (broadcast-capable)
    Add,
    Sub,
    Multiply,
    Divide,
    // scalar / unary elementwise
    AddScalar,
    SubScalar,
    MulScalar,
    DivScalar,
    Neg,
    Abs,
    Sqrt,
    Exp,
    Log,
    Sin,
    Cos,
    Tanh,
    Relu,
    Sigmoid,
    Gelu,
    Silu,
    PowScalar,
    Clamp,
    MaximumScalar,
    MinimumScalar,
    Where,
    // neural / speech / terrain ops
    MatMul,
    Transpose,
    Permute,
    Reshape,
    Flatten,
    Softmax,
    LogSoftmax,
    LayerNorm,
    RMSNorm,
    Conv1d,
    Conv2d,
    MaxPool2d,
    AvgPool2d,
    Embedding,
    Concat,
    Slice,
    ReduceSum,
    ReduceMean,
    ReduceMin,
    ReduceMax,
    ArgMax,
    Cast,
    ScaledDotProductAttention,
    Resize2d,
};

/** @brief GraphNode public API. */
struct GraphNode {
    OpType type = OpType::Const;
    int    dims[Tensor::kMaxRank] = {0, 0, 0, 0, 0, 0};
    int    rank = 0;
    int    size = 0;
    int    in0 = -1;
    int    in1 = -1;
    int    in2 = -1;
    int    in3 = -1;
    int    in4 = -1;
    float  s0 = 0.f;
    float  s1 = 0.f;
    float  s2 = 0.f;
    float  s3 = 0.f;
    // generic int attributes: axis / stride / pad / begin / end / mode / keepdims ...
    int    i0 = 0;
    int    i1 = 0;
    int    i2 = 0;
    int    i3 = 0;
    int    perm[Tensor::kMaxRank] = {0, 1, 2, 3, 4, 5};
    int    permRank = 0;
    int    placeholderSlot = -1;
    int    dtype = static_cast<int>(DType::Float32);
    std::vector<float> constData;
    // Weight-quantized const payload (dtype = Fp16/Fp8E4M3/Fp4E2M1/Int8/Int4).
    std::vector<uint8_t> constBytes;
    std::vector<float> constScales;  // per-group scales (int8/int4)
    int qGroup = 0;                  // elements per scale group
};

/** @brief EVENGINE_API_DOMAINS public API. */
class EVENGINE_API_DOMAINS Graph {
public:
    /** @brief Adds node. */
    int  addNode(GraphNode node);
    /** @brief Node. */
    const GraphNode &node(int id) const { return nodes_[static_cast<size_t>(id)]; }
    /** @brief Node. */
    GraphNode       &node(int id) { return nodes_[static_cast<size_t>(id)]; }
    /** @brief Node count. */
    int  nodeCount() const { return int(nodes_.size()); }
    /** @brief Nodes. */
    const std::vector<GraphNode> &nodes() const { return nodes_; }
    /** @brief Nodes. */
    std::vector<GraphNode>       &nodes() { return nodes_; }

    /** @brief Product. */
    static int product(const int *dims, int rank);

private:
    std::vector<GraphNode> nodes_;
};

/**
 * @brief Trace builder — TF2 `tf.function` analogue (`tf.func` in scripts).
 * While active, TF ops record into this graph.
 */
class EVENGINE_API_DOMAINS Func {
public:
    /** @brief Func. */
    explicit Func(TF *owner);
    /** @brief Func. */
    ~Func();

    /** @brief Input 1. */
    Tensor *input1(int d0);
    /** @brief Input 2. */
    Tensor *input2(int d0, int d1);
    /** @brief Input 3. */
    Tensor *input3(int d0, int d1, int d2);
    /** @brief Input 4. */
    Tensor *input4(int d0, int d1, int d2, int d3);
    /** @brief Input 5. */
    Tensor *input5(int d0, int d1, int d2, int d3, int d4);
    /** @brief Input 6. */
    Tensor *input6(int d0, int d1, int d2, int d3, int d4, int d5);

    /** @brief Sets the output. */
    void setOutput(Tensor *t);

    /** @brief Compiles compile. */
    class CompiledFunction *compile();

    /** @brief Graph. */
    Graph &graph() { return graph_; }
    /** @brief Graph. */
    const Graph &graph() const { return graph_; }
    /** @brief Owner. */
    TF   *owner() const { return owner_; }
    /** @brief True when tracing. */
    bool  isTracing() const { return tracing_; }
    /** @brief Output node. */
    int   outputNode() const { return outputNode_; }
    /** @brief Placeholder count. */
    int   placeholderCount() const { return placeholderCount_; }

    /** @brief Ensure tensor is a node in this graph (Const-capture if eager). */
    int ensureNode(const Tensor *t);

    /** @brief Emit unary. */
    Tensor *emitUnary(OpType type, const Tensor *x);
    /** @brief Emit unary scalar. */
    Tensor *emitUnaryScalar(OpType type, const Tensor *x, float s0, float s1 = 0.f);
    /** @brief Emit binary. */
    Tensor *emitBinary(OpType type, const Tensor *a, const Tensor *b);
    /** @brief Emit ternary. */
    Tensor *emitTernary(OpType type, const Tensor *a, const Tensor *b, const Tensor *c);
    /** @brief Emit mat mul. */
    Tensor *emitMatMul(const Tensor *a, const Tensor *b);
    /** @brief Emit transpose. */
    Tensor *emitTranspose(const Tensor *x);
    /** @brief Emit permute. */
    Tensor *emitPermute(const Tensor *x, const int *order, int rank);
    /** @brief Emit reshape. */
    Tensor *emitReshape(const Tensor *x, const int *dims, int rank);
    /** @brief Emit fill. */
    Tensor *emitFill(const int *dims, int rank, float value);

    /** @brief Emit softmax. */
    Tensor *emitSoftmax(const Tensor *x, int axis, bool logMode);
    /** @brief Emit layer norm. */
    Tensor *emitLayerNorm(const Tensor *x, const Tensor *scale, const Tensor *bias, float eps);
    /** @brief Emit rms norm. */
    Tensor *emitRMSNorm(const Tensor *x, const Tensor *scale, float eps);
    /** @brief Emit conv 1 d. */
    Tensor *emitConv1d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride, int pad);
    /** @brief Emit conv 2 d. */
    Tensor *emitConv2d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride, int pad);
    /** @brief Emit pool. */
    Tensor *emitPool(OpType type, const Tensor *x, int ksize, int stride, int pad);
    /** @brief Emit embedding. */
    Tensor *emitEmbedding(const Tensor *table, const Tensor *indices);
    /** @brief Emit concat. */
    Tensor *emitConcat(const Tensor *const *ins, int n, int axis);
    /** @brief Emit slice. */
    Tensor *emitSlice(const Tensor *x, int axis, int begin, int end);
    /** @brief Emit reduce. */
    Tensor *emitReduce(OpType type, const Tensor *x, int axis, bool keepDims);
    /** @brief Emit arg max. */
    Tensor *emitArgMax(const Tensor *x, int axis, bool keepDims);
    /** @brief Emit cast. */
    Tensor *emitCast(const Tensor *x, DType dtype);
    /** @brief Emit sdpa. */
    Tensor *emitSdpa(const Tensor *q, const Tensor *k, const Tensor *v, const Tensor *mask,
                     float scale);
    /** @brief Emit resize 2 d. */
    Tensor *emitResize2d(const Tensor *x, int outH, int outW, int mode);

private:
    Tensor *makeSymbolicFromNode(int nodeId);
    GraphNode makeShapeNode(OpType type, const int *dims, int rank);

    TF   *owner_ = nullptr;
    Graph graph_;
    int   outputNode_       = -1;
    int   placeholderCount_ = 0;
    bool  tracing_          = true;
};

/**
 * @brief Optimized / scheduled graph ready to run with feeds.
 */
class EVENGINE_API_DOMAINS CompiledFunction {
public:
    /** @brief Compiled function. */
    CompiledFunction();
    /** @brief Compiled function. */
    ~CompiledFunction();

    /** @brief Run 0. */
    Tensor *run0();
    /** @brief Run 1. */
    Tensor *run1(Tensor *in0);
    /** @brief Run 2. */
    Tensor *run2(Tensor *in0, Tensor *in1);
    /** @brief Run 3. */
    Tensor *run3(Tensor *in0, Tensor *in1, Tensor *in2);
    /** @brief Run 4. */
    Tensor *run4(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3);
    /** @brief Run 5. */
    Tensor *run5(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4);
    /** @brief Run 6. */
    Tensor *run6(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4, Tensor *in5);

    /** @brief Returns the placeholder count. */
    int         getPlaceholderCount() const { return placeholderCount_; }
    /** @brief Returns the device. */
    std::string getDevice() const { return device_; }

    /** @brief From func. */
    static CompiledFunction *fromFunc(Func *fn);

private:
    Tensor *runWithFeeds(Tensor *const *feeds, int nFeeds);
    void    executeNode(int nodeId, std::vector<std::vector<float>> &bufs) const;

    Graph                           graph_;
    std::vector<int>                order_;
    std::unique_ptr<OptimizedGraph> optimized_;
    int                             outputNode_       = -1;
    int                             placeholderCount_ = 0;
    std::string                     device_           = "cpu";
    /** @brief Set when the graph could be built for GPU execution (see GpuBackend.cpp). */
    std::unique_ptr<GpuProgram>     gpuProgram_;
};

}  // namespace eve::tensor

#endif  // EVE_TENSOR_GRAPH_H
