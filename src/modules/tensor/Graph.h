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

class Graph {
public:
    int  addNode(GraphNode node);
    const GraphNode &node(int id) const { return nodes_[static_cast<size_t>(id)]; }
    GraphNode       &node(int id) { return nodes_[static_cast<size_t>(id)]; }
    int  nodeCount() const { return int(nodes_.size()); }
    const std::vector<GraphNode> &nodes() const { return nodes_; }
    std::vector<GraphNode>       &nodes() { return nodes_; }

    static int product(const int *dims, int rank);

private:
    std::vector<GraphNode> nodes_;
};

/**
 * @brief Trace builder — TF2 `tf.function` analogue (`tf.func` in scripts).
 * While active, TF ops record into this graph.
 */
class Func {
public:
    explicit Func(TF *owner);
    ~Func();

    Tensor *input1(int d0);
    Tensor *input2(int d0, int d1);
    Tensor *input3(int d0, int d1, int d2);
    Tensor *input4(int d0, int d1, int d2, int d3);
    Tensor *input5(int d0, int d1, int d2, int d3, int d4);
    Tensor *input6(int d0, int d1, int d2, int d3, int d4, int d5);

    void setOutput(Tensor *t);

    class CompiledFunction *compile();

    Graph &graph() { return graph_; }
    const Graph &graph() const { return graph_; }
    TF   *owner() const { return owner_; }
    bool  isTracing() const { return tracing_; }
    int   outputNode() const { return outputNode_; }
    int   placeholderCount() const { return placeholderCount_; }

    /** @brief Ensure tensor is a node in this graph (Const-capture if eager). */
    int ensureNode(const Tensor *t);

    Tensor *emitUnary(OpType type, const Tensor *x);
    Tensor *emitUnaryScalar(OpType type, const Tensor *x, float s0, float s1 = 0.f);
    Tensor *emitBinary(OpType type, const Tensor *a, const Tensor *b);
    Tensor *emitTernary(OpType type, const Tensor *a, const Tensor *b, const Tensor *c);
    Tensor *emitMatMul(const Tensor *a, const Tensor *b);
    Tensor *emitTranspose(const Tensor *x);
    Tensor *emitPermute(const Tensor *x, const int *order, int rank);
    Tensor *emitReshape(const Tensor *x, const int *dims, int rank);
    Tensor *emitFill(const int *dims, int rank, float value);

    Tensor *emitSoftmax(const Tensor *x, int axis, bool logMode);
    Tensor *emitLayerNorm(const Tensor *x, const Tensor *scale, const Tensor *bias, float eps);
    Tensor *emitRMSNorm(const Tensor *x, const Tensor *scale, float eps);
    Tensor *emitConv1d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride, int pad);
    Tensor *emitConv2d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride, int pad);
    Tensor *emitPool(OpType type, const Tensor *x, int ksize, int stride, int pad);
    Tensor *emitEmbedding(const Tensor *table, const Tensor *indices);
    Tensor *emitConcat(const Tensor *const *ins, int n, int axis);
    Tensor *emitSlice(const Tensor *x, int axis, int begin, int end);
    Tensor *emitReduce(OpType type, const Tensor *x, int axis, bool keepDims);
    Tensor *emitArgMax(const Tensor *x, int axis, bool keepDims);
    Tensor *emitCast(const Tensor *x, DType dtype);
    Tensor *emitSdpa(const Tensor *q, const Tensor *k, const Tensor *v, const Tensor *mask,
                     float scale);
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
class CompiledFunction {
public:
    CompiledFunction();
    ~CompiledFunction();

    Tensor *run0();
    Tensor *run1(Tensor *in0);
    Tensor *run2(Tensor *in0, Tensor *in1);
    Tensor *run3(Tensor *in0, Tensor *in1, Tensor *in2);
    Tensor *run4(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3);
    Tensor *run5(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4);
    Tensor *run6(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4, Tensor *in5);

    int         getPlaceholderCount() const { return placeholderCount_; }
    std::string getDevice() const { return device_; }

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
