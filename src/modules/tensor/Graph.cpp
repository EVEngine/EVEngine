#include "tensor/Graph.h"
#include "tensor/Tensor.h"
#include "tensor/TF.h"
#include "tensor/GpuBackend.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>

namespace eve::tensor {
namespace {

bool isUnaryElementwise(OpType t) {
    switch (t) {
        case OpType::Neg:
        case OpType::Abs:
        case OpType::Sqrt:
        case OpType::Exp:
        case OpType::Log:
        case OpType::Sin:
        case OpType::Cos:
        case OpType::Tanh:
        case OpType::Relu:
        case OpType::Sigmoid:
        case OpType::AddScalar:
        case OpType::SubScalar:
        case OpType::MulScalar:
        case OpType::DivScalar:
        case OpType::PowScalar:
        case OpType::Clamp:
        case OpType::MaximumScalar:
        case OpType::MinimumScalar:
            return true;
        default:
            return false;
    }
}

float applyUnary(OpType t, float x, float s0, float s1) {
    switch (t) {
        case OpType::Neg: return -x;
        case OpType::Abs: return std::fabs(x);
        case OpType::Sqrt: return std::sqrt(x);
        case OpType::Exp: return std::exp(x);
        case OpType::Log: return std::log(x);
        case OpType::Sin: return std::sin(x);
        case OpType::Cos: return std::cos(x);
        case OpType::Tanh: return std::tanh(x);
        case OpType::Relu: return x > 0.f ? x : 0.f;
        case OpType::Sigmoid: return 1.f / (1.f + std::exp(-x));
        case OpType::AddScalar: return x + s0;
        case OpType::SubScalar: return x - s0;
        case OpType::MulScalar: return x * s0;
        case OpType::DivScalar: return x / s0;
        case OpType::PowScalar: return std::pow(x, s0);
        case OpType::Clamp: {
            float lo = s0, hi = s1;
            if (lo > hi) std::swap(lo, hi);
            return std::clamp(x, lo, hi);
        }
        case OpType::MaximumScalar: return std::max(x, s0);
        case OpType::MinimumScalar: return std::min(x, s0);
        default: return x;
    }
}

}  // namespace

int Graph::product(const int *dims, int rank) {
    int n = 1;
    for (int i = 0; i < rank; ++i) {
        if (dims[i] <= 0) throw eve::Exception("Graph: dims must be > 0");
        n *= dims[i];
    }
    return n;
}

int Graph::addNode(GraphNode node) {
    if (node.rank > 0 && node.size <= 0) node.size = product(node.dims, node.rank);
    nodes_.push_back(std::move(node));
    return int(nodes_.size()) - 1;
}

void optimizeGraph(Graph &g, int outputNode, std::vector<int> &order) {
    const int n = g.nodeCount();
    if (n <= 0 || outputNode < 0 || outputNode >= n)
        throw eve::Exception("optimizeGraph: invalid output");

    std::vector<char> need(static_cast<size_t>(n), 0);
    std::vector<int> stack = {outputNode};
    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        if (u < 0 || u >= n || need[static_cast<size_t>(u)]) continue;
        need[static_cast<size_t>(u)] = 1;
        const auto &nd = g.node(u);
        if (nd.in0 >= 0) stack.push_back(nd.in0);
        if (nd.in1 >= 0) stack.push_back(nd.in1);
        if (nd.in2 >= 0) stack.push_back(nd.in2);
    }

    std::vector<int> indeg(static_cast<size_t>(n), 0);
    std::vector<std::vector<int>> outs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (!need[static_cast<size_t>(i)]) continue;
        const auto &nd = g.node(i);
        auto link = [&](int from) {
            if (from < 0 || !need[static_cast<size_t>(from)]) return;
            outs[static_cast<size_t>(from)].push_back(i);
            indeg[static_cast<size_t>(i)]++;
        };
        link(nd.in0);
        link(nd.in1);
        link(nd.in2);
    }

    std::queue<int> q;
    for (int i = 0; i < n; ++i) {
        if (need[static_cast<size_t>(i)] && indeg[static_cast<size_t>(i)] == 0) q.push(i);
    }

    order.clear();
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        order.push_back(u);
        for (int v : outs[static_cast<size_t>(u)]) {
            if (--indeg[static_cast<size_t>(v)] == 0) q.push(v);
        }
    }
    if (int(order.size()) != int(std::count(need.begin(), need.end(), char(1))))
        throw eve::Exception("optimizeGraph: cycle in graph");
    // Unary elementwise chains are fused at execute time (see executeNode).
}

Func::Func(TF *owner) : owner_(owner) {
    if (!owner_) throw eve::Exception("Func: null owner");
    owner_->pushTrace(this);
}

Func::~Func() {
    if (tracing_ && owner_) owner_->popTrace(this);
}

GraphNode Func::makeShapeNode(OpType type, const int *dims, int rank) {
    GraphNode n;
    n.type = type;
    n.rank = rank;
    for (int i = 0; i < 4; ++i) n.dims[i] = 0;
    for (int i = 0; i < rank; ++i) n.dims[i] = dims[i];
    n.size = Graph::product(dims, rank);
    return n;
}

Tensor *Func::makeSymbolicFromNode(int nodeId) {
    const auto &n = graph_.node(nodeId);
    return Tensor::makeSymbolic(&graph_, nodeId, n.dims, n.rank);
}

Tensor *Func::input1(int d0) {
    int d[] = {d0};
    auto n  = makeShapeNode(OpType::Placeholder, d, 1);
    n.placeholderSlot = placeholderCount_++;
    int id            = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input2(int d0, int d1) {
    int d[] = {d0, d1};
    auto n  = makeShapeNode(OpType::Placeholder, d, 2);
    n.placeholderSlot = placeholderCount_++;
    int id            = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input3(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    auto n  = makeShapeNode(OpType::Placeholder, d, 3);
    n.placeholderSlot = placeholderCount_++;
    int id            = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input4(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    auto n  = makeShapeNode(OpType::Placeholder, d, 4);
    n.placeholderSlot = placeholderCount_++;
    int id            = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

void Func::setOutput(Tensor *t) {
    if (!t) throw eve::Exception("Func.setOutput: null");
    outputNode_ = ensureNode(t);
}

int Func::ensureNode(const Tensor *t) {
    if (!t) throw eve::Exception("Func.ensureNode: null");
    if (t->isSymbolic()) {
        if (t->graph() != &graph_)
            throw eve::Exception("Func: tensor from another graph");
        return t->nodeId();
    }
    t->ensureEager("capture");
    auto n = makeShapeNode(OpType::Const, t->dims_, t->rank_);
    n.constData.assign(t->data(), t->data() + t->getSize());
    return graph_.addNode(std::move(n));
}

Tensor *Func::emitFill(const int *dims, int rank, float value) {
    auto n = makeShapeNode(OpType::Const, dims, rank);
    n.constData.assign(static_cast<size_t>(n.size), value);
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitUnary(OpType type, const Tensor *x) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    auto n          = makeShapeNode(type, src.dims, src.rank);
    n.in0           = ix;
    int id          = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitUnaryScalar(OpType type, const Tensor *x, float s0, float s1) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    auto n          = makeShapeNode(type, src.dims, src.rank);
    n.in0           = ix;
    n.s0            = s0;
    n.s1            = s1;
    int id          = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitBinary(OpType type, const Tensor *a, const Tensor *b) {
    int ia = ensureNode(a);
    int ib = ensureNode(b);
    const auto &na = graph_.node(ia);
    const auto &nb = graph_.node(ib);
    if (na.rank != nb.rank || na.size != nb.size)
        throw eve::Exception("Func binary: shape mismatch");
    for (int i = 0; i < na.rank; ++i)
        if (na.dims[i] != nb.dims[i]) throw eve::Exception("Func binary: shape mismatch");
    auto n = makeShapeNode(type, na.dims, na.rank);
    n.in0  = ia;
    n.in1  = ib;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitTernary(OpType type, const Tensor *a, const Tensor *b, const Tensor *c) {
    int ia = ensureNode(a);
    int ib = ensureNode(b);
    int ic = ensureNode(c);
    const auto &na = graph_.node(ia);
    auto n         = makeShapeNode(type, na.dims, na.rank);
    n.in0 = ia;
    n.in1 = ib;
    n.in2 = ic;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitMatMul(const Tensor *a, const Tensor *b) {
    int ia = ensureNode(a);
    int ib = ensureNode(b);
    const auto &na = graph_.node(ia);
    const auto &nb = graph_.node(ib);
    if (na.rank != 2 || nb.rank != 2) throw eve::Exception("Func.matmul: rank 2 required");
    if (na.dims[1] != nb.dims[0]) throw eve::Exception("Func.matmul: inner dims mismatch");
    int od[] = {na.dims[0], nb.dims[1]};
    auto n   = makeShapeNode(OpType::MatMul, od, 2);
    n.in0    = ia;
    n.in1    = ib;
    int id   = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitTranspose(const Tensor *x) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank != 2) throw eve::Exception("Func.transpose: rank 2 required");
    int od[] = {src.dims[1], src.dims[0]};
    auto n   = makeShapeNode(OpType::Transpose, od, 2);
    n.in0    = ix;
    int id   = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitReshape(const Tensor *x, const int *dims, int rank) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    int newSize     = Graph::product(dims, rank);
    if (newSize != src.size) throw eve::Exception("Func.reshape: size mismatch");
    auto n = makeShapeNode(OpType::Reshape, dims, rank);
    n.in0  = ix;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

CompiledFunction *Func::compile() {
    if (outputNode_ < 0) throw eve::Exception("Func.compile: setOutput required");
    tracing_ = false;
    if (owner_) owner_->popTrace(this);
    return CompiledFunction::fromFunc(this);
}

CompiledFunction::CompiledFunction() = default;
CompiledFunction::~CompiledFunction() = default;

CompiledFunction *CompiledFunction::fromFunc(Func *fn) {
    if (!fn) throw eve::Exception("CompiledFunction: null func");
    auto *cf              = new CompiledFunction();
    cf->graph_            = fn->graph();  // copy
    cf->outputNode_       = fn->outputNode();
    cf->placeholderCount_ = fn->placeholderCount();
    cf->device_           = "cpu";
    optimizeGraph(cf->graph_, cf->outputNode_, cf->order_);

    // Best-effort: run on GPU via eve::gpgpu compute shaders when a Vulkan
    // device is available (window already created) and glslc can compile the
    // shared kernels. Falls back to the CPU interpreter below otherwise.
    try {
        cf->gpuProgram_.reset(GpuProgram::tryBuild(cf->graph_, cf->order_, cf->outputNode_));
    } catch (...) {
        cf->gpuProgram_.reset();
    }
    if (cf->gpuProgram_) cf->device_ = "gpu";
    return cf;
}

Tensor *CompiledFunction::run0() { return runWithFeeds(nullptr, 0); }

Tensor *CompiledFunction::run1(Tensor *in0) {
    Tensor *feeds[] = {in0};
    return runWithFeeds(feeds, 1);
}

Tensor *CompiledFunction::run2(Tensor *in0, Tensor *in1) {
    Tensor *feeds[] = {in0, in1};
    return runWithFeeds(feeds, 2);
}

Tensor *CompiledFunction::run3(Tensor *in0, Tensor *in1, Tensor *in2) {
    Tensor *feeds[] = {in0, in1, in2};
    return runWithFeeds(feeds, 3);
}

Tensor *CompiledFunction::run4(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3) {
    Tensor *feeds[] = {in0, in1, in2, in3};
    return runWithFeeds(feeds, 4);
}

Tensor *CompiledFunction::runWithFeeds(Tensor *const *feeds, int nFeeds) {
    if (nFeeds != placeholderCount_)
        throw eve::Exception("CompiledFunction.run: expected %d feeds, got %d", placeholderCount_,
                             nFeeds);
    for (int i = 0; i < nFeeds; ++i) {
        if (!feeds[i]) throw eve::Exception("CompiledFunction.run: null feed");
        feeds[i]->ensureEager("run");
    }

    const int n = graph_.nodeCount();

    // Validate placeholder shapes (shared by the GPU and CPU execution paths).
    for (int i = 0; i < n; ++i) {
        const auto &nd = graph_.node(i);
        if (nd.type != OpType::Placeholder) continue;
        int slot = nd.placeholderSlot;
        if (slot < 0 || slot >= nFeeds) throw eve::Exception("CompiledFunction: bad placeholder slot");
        Tensor *feed = feeds[slot];
        if (feed->getRank() != nd.rank || feed->getSize() != nd.size)
            throw eve::Exception("CompiledFunction: feed shape mismatch");
        for (int a = 0; a < nd.rank; ++a)
            if (feed->getDim(a) != nd.dims[a])
                throw eve::Exception("CompiledFunction: feed shape mismatch");
    }

    if (gpuProgram_) {
        std::vector<const float *> ptrs(static_cast<size_t>(nFeeds));
        for (int i = 0; i < nFeeds; ++i) ptrs[static_cast<size_t>(i)] = feeds[i]->data();
        std::vector<float> result = gpuProgram_->run(ptrs);

        const auto &outN = graph_.node(outputNode_);
        auto *out         = new Tensor(outN.dims, outN.rank);
        if (int(result.size()) != out->getSize())
            throw eve::Exception("CompiledFunction: gpu output size mismatch");
        std::memcpy(out->data(), result.data(), sizeof(float) * static_cast<size_t>(out->getSize()));
        return out;
    }

    std::vector<std::vector<float>> bufs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto &nd = graph_.node(i);
        if (nd.type != OpType::Placeholder) continue;
        Tensor *feed = feeds[nd.placeholderSlot];
        bufs[static_cast<size_t>(i)].assign(feed->data(), feed->data() + feed->getSize());
    }

    for (int nodeId : order_) executeNode(nodeId, bufs);

    const auto &outN = graph_.node(outputNode_);
    auto *out        = new Tensor(outN.dims, outN.rank);
    const auto &src  = bufs[static_cast<size_t>(outputNode_)];
    if (int(src.size()) != out->getSize())
        throw eve::Exception("CompiledFunction: output size mismatch");
    std::memcpy(out->data(), src.data(), sizeof(float) * static_cast<size_t>(out->getSize()));
    return out;
}

void CompiledFunction::executeNode(int nodeId, std::vector<std::vector<float>> &bufs) const {
    const auto &nd = graph_.node(nodeId);
    auto &out      = bufs[static_cast<size_t>(nodeId)];

    switch (nd.type) {
        case OpType::Placeholder:
            return;  // already filled
        case OpType::Const:
            out = nd.constData;
            return;
        case OpType::Add:
        case OpType::Sub:
        case OpType::Multiply:
        case OpType::Divide: {
            const auto &a = bufs[static_cast<size_t>(nd.in0)];
            const auto &b = bufs[static_cast<size_t>(nd.in1)];
            out.resize(static_cast<size_t>(nd.size));
            for (int i = 0; i < nd.size; ++i) {
                float x = a[static_cast<size_t>(i)];
                float y = b[static_cast<size_t>(i)];
                float r = 0.f;
                switch (nd.type) {
                    case OpType::Add: r = x + y; break;
                    case OpType::Sub: r = x - y; break;
                    case OpType::Multiply: r = x * y; break;
                    case OpType::Divide: r = x / y; break;
                    default: break;
                }
                out[static_cast<size_t>(i)] = r;
            }
            return;
        }
        case OpType::Neg:
        case OpType::Abs:
        case OpType::Sqrt:
        case OpType::Exp:
        case OpType::Log:
        case OpType::Sin:
        case OpType::Cos:
        case OpType::Tanh:
        case OpType::Relu:
        case OpType::Sigmoid:
        case OpType::AddScalar:
        case OpType::SubScalar:
        case OpType::MulScalar:
        case OpType::DivScalar:
        case OpType::PowScalar:
        case OpType::Clamp:
        case OpType::MaximumScalar:
        case OpType::MinimumScalar: {
            // Fuse unary chain: walk back through single-use unary parents in order
            // For simplicity execute this node; optional multi-op fusion:
            const auto &a = bufs[static_cast<size_t>(nd.in0)];
            out.resize(static_cast<size_t>(nd.size));
            // Collect chain of unary ops ending at this node (from root input)
            std::vector<const GraphNode *> chain;
            int cur = nodeId;
            while (true) {
                const auto &cn = graph_.node(cur);
                if (!isUnaryElementwise(cn.type)) break;
                chain.push_back(&cn);
                // only fuse if input is also unary we will process — stop at non-unary buffer
                int pred = cn.in0;
                if (pred < 0) break;
                const auto &pn = graph_.node(pred);
                if (!isUnaryElementwise(pn.type)) break;
                // fuse only single-consumer chains roughly: always include immediate preds that are unary
                cur = pred;
            }
            std::reverse(chain.begin(), chain.end());
            // Input buffer is the in0 of the first chain node
            int inputId = chain.front()->in0;
            // If we walked into another unary, input is that unary's input
            // Actually chain includes only unaries; first's in0 is the data source
            const auto &src = bufs[static_cast<size_t>(inputId)];
            for (int i = 0; i < nd.size; ++i) {
                float v = src[static_cast<size_t>(i)];
                for (const GraphNode *step : chain) v = applyUnary(step->type, v, step->s0, step->s1);
                out[static_cast<size_t>(i)] = v;
            }
            return;
        }
        case OpType::MatMul: {
            const auto &A = graph_.node(nd.in0);
            const auto &B = graph_.node(nd.in1);
            const auto &a = bufs[static_cast<size_t>(nd.in0)];
            const auto &b = bufs[static_cast<size_t>(nd.in1)];
            int m = A.dims[0], k = A.dims[1], n = B.dims[1];
            out.assign(static_cast<size_t>(m * n), 0.f);
            for (int i = 0; i < m; ++i) {
                for (int j = 0; j < n; ++j) {
                    double acc = 0.0;
                    for (int t = 0; t < k; ++t)
                        acc += double(a[static_cast<size_t>(i * k + t)]) *
                               double(b[static_cast<size_t>(t * n + j)]);
                    out[static_cast<size_t>(i * n + j)] = float(acc);
                }
            }
            return;
        }
        case OpType::Transpose: {
            const auto &X = graph_.node(nd.in0);
            const auto &a = bufs[static_cast<size_t>(nd.in0)];
            int r = X.dims[0], c = X.dims[1];
            out.resize(static_cast<size_t>(r * c));
            for (int i = 0; i < r; ++i)
                for (int j = 0; j < c; ++j)
                    out[static_cast<size_t>(j * r + i)] = a[static_cast<size_t>(i * c + j)];
            return;
        }
        case OpType::Reshape:
        case OpType::Flatten: {
            out = bufs[static_cast<size_t>(nd.in0)];
            return;
        }
        case OpType::Where: {
            const auto &c = bufs[static_cast<size_t>(nd.in0)];
            const auto &a = bufs[static_cast<size_t>(nd.in1)];
            const auto &b = bufs[static_cast<size_t>(nd.in2)];
            out.resize(static_cast<size_t>(nd.size));
            for (int i = 0; i < nd.size; ++i)
                out[static_cast<size_t>(i)] =
                    c[static_cast<size_t>(i)] > 0.5f ? a[static_cast<size_t>(i)]
                                                     : b[static_cast<size_t>(i)];
            return;
        }
    }
}

}  // namespace eve::tensor
