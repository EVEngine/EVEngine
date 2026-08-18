#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::tensor {

class Tensor;
class TF;
class Func;
class GpuProgram;

enum class OpType : uint8_t {
    Placeholder = 0,
    Const,
    Add,
    Sub,
    Multiply,
    Divide,
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
    PowScalar,
    Clamp,
    MaximumScalar,
    MinimumScalar,
    MatMul,
    Transpose,
    Reshape,
    Flatten,
    Where,
};

struct GraphNode {
    OpType type = OpType::Const;
    int    dims[4] = {0, 0, 0, 0};
    int    rank = 0;
    int    size = 0;
    int    in0 = -1;
    int    in1 = -1;
    int    in2 = -1;
    float  s0 = 0.f;
    float  s1 = 0.f;
    int    placeholderSlot = -1;
    std::vector<float> constData;
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
    Tensor *emitReshape(const Tensor *x, const int *dims, int rank);
    Tensor *emitFill(const int *dims, int rank, float value);

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

    int         getPlaceholderCount() const { return placeholderCount_; }
    std::string getDevice() const { return device_; }

    static CompiledFunction *fromFunc(Func *fn);

private:
    Tensor *runWithFeeds(Tensor *const *feeds, int nFeeds);
    void    executeNode(int nodeId, std::vector<std::vector<float>> &bufs) const;

    Graph              graph_;
    std::vector<int>   order_;
    int                outputNode_       = -1;
    int                placeholderCount_ = 0;
    std::string        device_           = "cpu";
    /** @brief Set when the graph could be built for GPU execution (see GpuBackend.cpp). */
    std::unique_ptr<GpuProgram> gpuProgram_;
};

/** @brief Cheap unary-chain fusion: collapse A->unary->unary into one fused pass marker via rewriting. */
void optimizeGraph(Graph &g, int outputNode, std::vector<int> &order);

}  // namespace eve::tensor
