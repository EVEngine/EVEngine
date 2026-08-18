#include "tensor/Graph.h"
#include "tensor/CpuKernels.h"
#include "tensor/GpuBackend.h"
#include "tensor/Optimizer.h"
#include "tensor/Quant.h"
#include "tensor/TF.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eve::tensor {

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

// ---------------------------------------------------------------------------
// Func: tracing graph builder
// ---------------------------------------------------------------------------

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
    for (int i = 0; i < Tensor::kMaxRank; ++i) n.dims[i] = 0;
    for (int i = 0; i < rank; ++i) n.dims[i] = dims[i];
    n.size = Graph::product(dims, rank);
    return n;
}

Tensor *Func::makeSymbolicFromNode(int nodeId) {
    const auto &n = graph_.node(nodeId);
    auto *t       = Tensor::makeSymbolic(&graph_, nodeId, n.dims, n.rank);
    t->setDtype(static_cast<DType>(n.dtype));
    return t;
}

namespace {

GraphNode makeInputNode(Graph &graph, OpType type, const int *dims, int rank, int slot) {
    GraphNode n;
    n.type = type;
    n.rank = rank;
    for (int i = 0; i < Tensor::kMaxRank; ++i) n.dims[i] = 0;
    for (int i = 0; i < rank; ++i) n.dims[i] = dims[i];
    n.size = Graph::product(dims, rank);
    n.placeholderSlot = slot;
    return n;
}

int normalizeAxisChecked(int axis, int rank) {
    if (axis < 0) axis += rank;
    if (axis < 0 || axis >= rank) throw eve::Exception("Func: axis out of range");
    return axis;
}

int convOutSize(int inSize, int kernel, int stride, int pad) {
    const int out = (inSize + 2 * pad - kernel) / stride + 1;
    if (out <= 0) throw eve::Exception("Func: conv output size must be > 0");
    return out;
}

}  // namespace

Tensor *Func::input1(int d0) {
    int d[] = {d0};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 1, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input2(int d0, int d1) {
    int d[] = {d0, d1};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 2, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input3(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 3, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input4(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 4, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input5(int d0, int d1, int d2, int d3, int d4) {
    int d[] = {d0, d1, d2, d3, d4};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 5, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

Tensor *Func::input6(int d0, int d1, int d2, int d3, int d4, int d5) {
    int d[] = {d0, d1, d2, d3, d4, d5};
    int id  = graph_.addNode(makeInputNode(graph_, OpType::Placeholder, d, 6, placeholderCount_++));
    return makeSymbolicFromNode(id);
}

void Func::setOutput(Tensor *t) {
    if (!t) throw eve::Exception("Func.setOutput: null");
    outputNode_ = ensureNode(t);
}

int Func::ensureNode(const Tensor *t) {
    if (!t) throw eve::Exception("Func.ensureNode: null");
    if (t->isSymbolic()) {
        if (t->graph() != &graph_) throw eve::Exception("Func: tensor from another graph");
        return t->nodeId();
    }
    t->ensureEager("capture");
    auto n = makeShapeNode(OpType::Const, t->dims_, t->rank_);
    if (t->isQuantized()) {
        n.constBytes = t->qBytes();
        n.constScales = t->qScales();
        n.qGroup = t->qGroup();
    } else {
        n.constData.assign(t->data(), t->data() + t->getSize());
    }
    n.dtype = static_cast<int>(t->dtype_);
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
    n.dtype         = src.dtype;
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
    n.dtype         = src.dtype;
    int id          = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitBinary(OpType type, const Tensor *a, const Tensor *b) {
    int ia = ensureNode(a);
    int ib = ensureNode(b);
    const auto &na = graph_.node(ia);
    const auto &nb = graph_.node(ib);
    int od[Tensor::kMaxRank] = {};
    int orank = 0;
    if (!kernels::broadcastShape(na.dims, na.rank, nb.dims, nb.rank, od, orank))
        throw eve::Exception("Func binary: broadcast shape mismatch");
    auto n = makeShapeNode(type, od, orank);
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
    if (na.rank == 2 && nb.rank == 2) {
        if (na.dims[1] != nb.dims[0]) throw eve::Exception("Func.matmul: inner dims mismatch");
        int od[] = {na.dims[0], nb.dims[1]};
        auto n   = makeShapeNode(OpType::MatMul, od, 2);
        n.in0    = ia;
        n.in1    = ib;
        int id   = graph_.addNode(std::move(n));
        return makeSymbolicFromNode(id);
    }
    if (na.rank == 3 && nb.rank == 3) {
        if (na.dims[0] != nb.dims[0] || na.dims[2] != nb.dims[1])
            throw eve::Exception("Func.matmul: batched dims mismatch");
        int od[] = {na.dims[0], na.dims[1], nb.dims[2]};
        auto n   = makeShapeNode(OpType::MatMul, od, 3);
        n.in0    = ia;
        n.in1    = ib;
        int id   = graph_.addNode(std::move(n));
        return makeSymbolicFromNode(id);
    }
    throw eve::Exception("Func.matmul: rank 2x2 or 3x3 required");
}

Tensor *Func::emitTranspose(const Tensor *x) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank != 2) throw eve::Exception("Func.transpose: rank 2 required");
    int order[] = {1, 0};
    return emitPermute(x, order, 2);
}

Tensor *Func::emitPermute(const Tensor *x, const int *order, int rank) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (rank != src.rank) throw eve::Exception("Func.permute: rank mismatch");
    int od[Tensor::kMaxRank] = {};
    for (int k = 0; k < rank; ++k) {
        if (order[k] < 0 || order[k] >= rank)
            throw eve::Exception("Func.permute: order out of range");
        od[k] = src.dims[order[k]];
    }
    auto n = makeShapeNode(OpType::Permute, od, rank);
    n.in0  = ix;
    for (int k = 0; k < rank; ++k) n.perm[k] = order[k];
    n.permRank = rank;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitReshape(const Tensor *x, const int *dims, int rank) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    int newSize     = Graph::product(dims, rank);
    if (newSize != src.size) throw eve::Exception("Func.reshape: size mismatch");
    auto n = makeShapeNode(OpType::Reshape, dims, rank);
    n.in0  = ix;
    n.dtype = src.dtype;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitSoftmax(const Tensor *x, int axis, bool logMode) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    axis = normalizeAxisChecked(axis, src.rank);
    auto n = makeShapeNode(logMode ? OpType::LogSoftmax : OpType::Softmax, src.dims, src.rank);
    n.in0  = ix;
    n.i0   = axis;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

namespace {

void checkParamVector(const GraphNode &src, int cols, const char *what) {
    if (src.rank == 1 && src.dims[0] == cols) return;
    if (src.rank == 2 && src.dims[0] == 1 && src.dims[1] == cols) return;
    throw eve::Exception("Func.%s: expected shape [%d]", what, cols);
}

}  // namespace

Tensor *Func::emitLayerNorm(const Tensor *x, const Tensor *scale, const Tensor *bias, float eps) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank < 1) throw eve::Exception("Func.layernorm: rank >= 1 required");
    const int cols = src.dims[src.rank - 1];
    auto n = makeShapeNode(OpType::LayerNorm, src.dims, src.rank);
    n.in0  = ix;
    n.s0   = eps;
    if (scale) {
        int is = ensureNode(scale);
        checkParamVector(graph_.node(is), cols, "layernorm scale");
        n.in1 = is;
    }
    if (bias) {
        int ib = ensureNode(bias);
        checkParamVector(graph_.node(ib), cols, "layernorm bias");
        n.in2 = ib;
    }
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitRMSNorm(const Tensor *x, const Tensor *scale, float eps) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank < 1) throw eve::Exception("Func.rmsnorm: rank >= 1 required");
    const int cols = src.dims[src.rank - 1];
    auto n = makeShapeNode(OpType::RMSNorm, src.dims, src.rank);
    n.in0  = ix;
    n.s0   = eps;
    if (scale) {
        int is = ensureNode(scale);
        checkParamVector(graph_.node(is), cols, "rmsnorm scale");
        n.in1 = is;
    }
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitConv1d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride,
                         int pad) {
    int ix = ensureNode(x);
    int iw = ensureNode(w);
    const auto &nx = graph_.node(ix);
    const auto &nw = graph_.node(iw);
    if (nx.rank != 3 || nw.rank != 3) throw eve::Exception("Func.conv1d: rank 3 required");
    if (nx.dims[1] != nw.dims[1]) throw eve::Exception("Func.conv1d: channel mismatch");
    const int OL = convOutSize(nx.dims[2], nw.dims[2], stride, pad);
    int od[] = {nx.dims[0], nw.dims[0], OL};
    auto n   = makeShapeNode(OpType::Conv1d, od, 3);
    n.in0    = ix;
    n.in1    = iw;
    n.i0     = stride;
    n.i1     = pad;
    if (bias) {
        int ib = ensureNode(bias);
        const auto &nb = graph_.node(ib);
        if (nb.rank != 1 || nb.dims[0] != nw.dims[0])
            throw eve::Exception("Func.conv1d: bias shape mismatch");
        n.in2 = ib;
    }
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitConv2d(const Tensor *x, const Tensor *w, const Tensor *bias, int stride,
                         int pad) {
    int ix = ensureNode(x);
    int iw = ensureNode(w);
    const auto &nx = graph_.node(ix);
    const auto &nw = graph_.node(iw);
    if (nx.rank != 4 || nw.rank != 4) throw eve::Exception("Func.conv2d: rank 4 required");
    if (nx.dims[1] != nw.dims[1]) throw eve::Exception("Func.conv2d: channel mismatch");
    const int OH = convOutSize(nx.dims[2], nw.dims[2], stride, pad);
    const int OW = convOutSize(nx.dims[3], nw.dims[3], stride, pad);
    int od[] = {nx.dims[0], nw.dims[0], OH, OW};
    auto n   = makeShapeNode(OpType::Conv2d, od, 4);
    n.in0    = ix;
    n.in1    = iw;
    n.i0     = stride;
    n.i1     = pad;
    if (bias) {
        int ib = ensureNode(bias);
        const auto &nb = graph_.node(ib);
        if (nb.rank != 1 || nb.dims[0] != nw.dims[0])
            throw eve::Exception("Func.conv2d: bias shape mismatch");
        n.in2 = ib;
    }
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitPool(OpType type, const Tensor *x, int ksize, int stride, int pad) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank != 4) throw eve::Exception("Func.pool: rank 4 required");
    const int OH = convOutSize(src.dims[2], ksize, stride, pad);
    const int OW = convOutSize(src.dims[3], ksize, stride, pad);
    int od[] = {src.dims[0], src.dims[1], OH, OW};
    auto n   = makeShapeNode(type, od, 4);
    n.in0    = ix;
    n.i0     = ksize;
    n.i1     = stride;
    n.i2     = pad;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitEmbedding(const Tensor *table, const Tensor *indices) {
    int it = ensureNode(table);
    int ii = ensureNode(indices);
    const auto &nt = graph_.node(it);
    const auto &ni = graph_.node(ii);
    if (nt.rank != 2) throw eve::Exception("Func.embedding: table rank 2 required");
    if (ni.rank < 1 || ni.rank + 1 > Tensor::kMaxRank)
        throw eve::Exception("Func.embedding: index rank out of range");
    int od[Tensor::kMaxRank] = {};
    for (int k = 0; k < ni.rank; ++k) od[k] = ni.dims[k];
    od[ni.rank] = nt.dims[1];
    auto n = makeShapeNode(OpType::Embedding, od, ni.rank + 1);
    n.in0  = it;
    n.in1  = ii;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitConcat(const Tensor *const *ins, int n, int axis) {
    if (!ins || n < 2 || n > 4) throw eve::Exception("Func.concat: 2..4 inputs required");
    int ids[4] = {};
    const GraphNode *ns[4] = {};
    for (int k = 0; k < n; ++k) {
        ids[k] = ensureNode(ins[k]);
        ns[k]  = &graph_.node(ids[k]);
        if (ns[k]->rank != ns[0]->rank)
            throw eve::Exception("Func.concat: rank mismatch");
    }
    axis = normalizeAxisChecked(axis, ns[0]->rank);
    int od[Tensor::kMaxRank] = {};
    for (int k = 0; k < ns[0]->rank; ++k) {
        if (k == axis) {
            int total = 0;
            for (int t = 0; t < n; ++t) total += ns[t]->dims[k];
            od[k] = total;
        } else {
            od[k] = ns[0]->dims[k];
            for (int t = 1; t < n; ++t)
                if (ns[t]->dims[k] != od[k])
                    throw eve::Exception("Func.concat: dims mismatch on axis %d", k);
        }
    }
    auto g = makeShapeNode(OpType::Concat, od, ns[0]->rank);
    g.in0 = ids[0];
    g.in1 = ids[1];
    if (n > 2) g.in2 = ids[2];
    if (n > 3) g.in3 = ids[3];
    g.i0 = axis;
    int id = graph_.addNode(std::move(g));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitSlice(const Tensor *x, int axis, int begin, int end) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    axis = normalizeAxisChecked(axis, src.rank);
    if (begin < 0 || end < begin || end > src.dims[axis])
        throw eve::Exception("Func.slice: range out of bounds");
    int od[Tensor::kMaxRank] = {};
    for (int k = 0; k < src.rank; ++k) od[k] = src.dims[k];
    od[axis] = end - begin;
    auto n = makeShapeNode(OpType::Slice, od, src.rank);
    n.in0  = ix;
    n.i0   = axis;
    n.i1   = begin;
    n.i2   = end;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitReduce(OpType type, const Tensor *x, int axis, bool keepDims) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    axis = normalizeAxisChecked(axis, src.rank);
    int od[Tensor::kMaxRank] = {};
    int orank = src.rank;
    for (int k = 0; k < src.rank; ++k) od[k] = src.dims[k];
    if (keepDims) {
        od[axis] = 1;
    } else {
        if (orank == 1) {
            od[0] = 1;  // Tensor rank must stay >= 1
        } else {
            for (int k = axis; k < orank - 1; ++k) od[k] = od[k + 1];
            od[orank - 1] = 0;
            --orank;
        }
    }
    auto n = makeShapeNode(type, od, orank);
    n.in0  = ix;
    n.i0   = axis;
    n.i1   = keepDims ? 1 : 0;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitArgMax(const Tensor *x, int axis, bool keepDims) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    axis = normalizeAxisChecked(axis, src.rank);
    int od[Tensor::kMaxRank] = {};
    int orank = src.rank;
    for (int k = 0; k < src.rank; ++k) od[k] = src.dims[k];
    if (keepDims) {
        od[axis] = 1;
    } else {
        if (orank == 1) {
            od[0] = 1;
        } else {
            for (int k = axis; k < orank - 1; ++k) od[k] = od[k + 1];
            od[orank - 1] = 0;
            --orank;
        }
    }
    auto n = makeShapeNode(OpType::ArgMax, od, orank);
    n.in0  = ix;
    n.i0   = axis;
    n.i1   = keepDims ? 1 : 0;
    n.dtype = static_cast<int>(DType::Int32);
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitCast(const Tensor *x, DType dtype) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    auto n = makeShapeNode(OpType::Cast, src.dims, src.rank);
    n.in0  = ix;
    n.dtype = static_cast<int>(dtype);
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitSdpa(const Tensor *q, const Tensor *k, const Tensor *v, const Tensor *mask,
                       float scale) {
    int iq = ensureNode(q);
    int ik = ensureNode(k);
    int iv = ensureNode(v);
    const auto &nq = graph_.node(iq);
    const auto &nk = graph_.node(ik);
    const auto &nv = graph_.node(iv);
    if (nq.rank != 4 || nk.rank != 4 || nv.rank != 4)
        throw eve::Exception("Func.sdpa: rank 4 required");
    if (nq.dims[0] != nk.dims[0] || nq.dims[1] != nk.dims[1] ||
        nq.dims[3] != nk.dims[3] || nk.dims[2] != nv.dims[2] || nq.dims[3] != nv.dims[3])
        throw eve::Exception("Func.sdpa: q/k/v shape mismatch");
    auto n = makeShapeNode(OpType::ScaledDotProductAttention, nq.dims, 4);
    n.in0  = iq;
    n.in1  = ik;
    n.in2  = iv;
    n.s0   = scale;
    if (mask) {
        int im = ensureNode(mask);
        const auto &nm = graph_.node(im);
        if (nm.rank != 4 || nm.dims[0] != nq.dims[0] || nm.dims[1] != nq.dims[1] ||
            nm.dims[2] != nq.dims[2] || nm.dims[3] != nk.dims[2])
            throw eve::Exception("Func.sdpa: mask shape mismatch");
        n.in3 = im;
    }
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

Tensor *Func::emitResize2d(const Tensor *x, int outH, int outW, int mode) {
    int ix = ensureNode(x);
    const auto &src = graph_.node(ix);
    if (src.rank != 4) throw eve::Exception("Func.resize2d: rank 4 required");
    if (outH <= 0 || outW <= 0) throw eve::Exception("Func.resize2d: bad output size");
    int od[] = {src.dims[0], src.dims[1], outH, outW};
    auto n   = makeShapeNode(OpType::Resize2d, od, 4);
    n.in0    = ix;
    n.i0     = mode;
    int id = graph_.addNode(std::move(n));
    return makeSymbolicFromNode(id);
}

// ---------------------------------------------------------------------------
// CompiledFunction
// ---------------------------------------------------------------------------

CompiledFunction::CompiledFunction() = default;
CompiledFunction::~CompiledFunction() = default;

CompiledFunction *Func::compile() {
    if (outputNode_ < 0) throw eve::Exception("Func.compile: setOutput required");
    tracing_ = false;
    if (owner_) owner_->popTrace(this);
    return CompiledFunction::fromFunc(this);
}

CompiledFunction *CompiledFunction::fromFunc(Func *fn) {
    if (!fn) throw eve::Exception("CompiledFunction: null func");
    auto *cf              = new CompiledFunction();
    cf->graph_            = fn->graph();  // copy
    cf->outputNode_       = fn->outputNode();
    cf->placeholderCount_ = fn->placeholderCount();
    cf->device_           = "cpu";
    cf->optimized_        = std::make_unique<OptimizedGraph>(optimizeGraph(cf->graph_, cf->outputNode_));
    cf->order_            = cf->optimized_->order;

    // Best-effort: run on GPU via eve::gpgpu compute shaders when a Vulkan
    // device is available. Falls back to the CPU interpreter below otherwise.
    try {
        cf->gpuProgram_.reset(GpuProgram::tryBuild(cf->graph_, *cf->optimized_, cf->outputNode_));
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

Tensor *CompiledFunction::run5(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4) {
    Tensor *feeds[] = {in0, in1, in2, in3, in4};
    return runWithFeeds(feeds, 5);
}

Tensor *CompiledFunction::run6(Tensor *in0, Tensor *in1, Tensor *in2, Tensor *in3, Tensor *in4,
                               Tensor *in5) {
    Tensor *feeds[] = {in0, in1, in2, in3, in4, in5};
    return runWithFeeds(feeds, 6);
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
        const int slot = nd.placeholderSlot;
        if (slot < 0 || slot >= nFeeds) throw eve::Exception("CompiledFunction: bad placeholder slot");
        Tensor *feed = feeds[slot];
        if (feed->getRank() != nd.rank || feed->getSize() != nd.size)
            throw eve::Exception("CompiledFunction: feed shape mismatch");
        for (int a = 0; a < nd.rank; ++a)
            if (feed->getDim(a) != nd.dims[a])
                throw eve::Exception("CompiledFunction: feed shape mismatch");
    }

    const auto &outN = graph_.node(outputNode_);

    if (gpuProgram_) {
        std::vector<const float *> ptrs(static_cast<size_t>(nFeeds));
        for (int i = 0; i < nFeeds; ++i) ptrs[static_cast<size_t>(i)] = feeds[i]->data();
        std::vector<float> result = gpuProgram_->run(ptrs);
        auto *out = new Tensor(static_cast<DType>(outN.dtype), outN.dims, outN.rank);
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

    auto *out = new Tensor(static_cast<DType>(outN.dtype), outN.dims, outN.rank);
    const auto &src = bufs[static_cast<size_t>(outputNode_)];
    if (int(src.size()) != out->getSize())
        throw eve::Exception("CompiledFunction: output size mismatch");
    std::memcpy(out->data(), src.data(), sizeof(float) * static_cast<size_t>(out->getSize()));
    return out;
}

void CompiledFunction::executeNode(int nodeId, std::vector<std::vector<float>> &bufs) const {
    const auto &nd = graph_.node(nodeId);
    auto &out      = bufs[static_cast<size_t>(nodeId)];
    const auto in  = [&](int id) -> const std::vector<float> & {
        return bufs[static_cast<size_t>(id)];
    };

    switch (nd.type) {
        case OpType::Placeholder:
            return;  // already filled
        case OpType::Const:
            if (!nd.constBytes.empty()) {
                out.assign(static_cast<size_t>(nd.size), 0.f);
                q::dequantizeAll(static_cast<DType>(nd.dtype), nd.constBytes.data(),
                                 nd.constScales.data(), nd.qGroup, nd.size, out.data());
            } else {
                out = nd.constData;
            }
            return;
        case OpType::Add:
        case OpType::Sub:
        case OpType::Multiply:
        case OpType::Divide: {
            const auto &na = graph_.node(nd.in0);
            const auto &nb = graph_.node(nd.in1);
            out.resize(static_cast<size_t>(nd.size));
            kernels::binaryOp(nd.type, in(nd.in0).data(), na.dims, na.rank, in(nd.in1).data(),
                              nb.dims, nb.rank, out.data(), nd.dims, nd.rank);
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
        case OpType::Gelu:
        case OpType::Silu:
        case OpType::AddScalar:
        case OpType::SubScalar:
        case OpType::MulScalar:
        case OpType::DivScalar:
        case OpType::PowScalar:
        case OpType::Clamp:
        case OpType::MaximumScalar:
        case OpType::MinimumScalar: {
            out.resize(static_cast<size_t>(nd.size));
            kernels::unaryOp(nd.type, in(nd.in0).data(), nd.size, out.data(), nd.s0, nd.s1);
            return;
        }
        case OpType::Where: {
            const auto &c = in(nd.in0);
            const auto &a = in(nd.in1);
            const auto &b = in(nd.in2);
            out.resize(static_cast<size_t>(nd.size));
            for (int i = 0; i < nd.size; ++i)
                out[static_cast<size_t>(i)] =
                    c[static_cast<size_t>(i)] > 0.5f ? a[static_cast<size_t>(i)]
                                                      : b[static_cast<size_t>(i)];
            return;
        }
        case OpType::MatMul: {
            const auto &A = graph_.node(nd.in0);
            const auto &B = graph_.node(nd.in1);
            const auto &a = in(nd.in0);
            const auto &b = in(nd.in1);
            out.assign(static_cast<size_t>(nd.size), 0.f);
            if (nd.rank == 2) {
                const int m = A.dims[0], k = A.dims[1], n = B.dims[1];
                for (int i = 0; i < m; ++i) {
                    for (int j = 0; j < n; ++j) {
                        double acc = 0.0;
                        for (int t = 0; t < k; ++t)
                            acc += double(a[static_cast<size_t>(i * k + t)]) *
                                   double(b[static_cast<size_t>(t * n + j)]);
                        out[static_cast<size_t>(i * n + j)] = float(acc);
                    }
                }
            } else {
                const int batch = A.dims[0], m = A.dims[1], k = A.dims[2], n = B.dims[2];
                for (int bb = 0; bb < batch; ++bb) {
                    const float *ap = a.data() + size_t(bb) * m * k;
                    const float *bp = b.data() + size_t(bb) * k * n;
                    float *cp       = out.data() + size_t(bb) * m * n;
                    for (int i = 0; i < m; ++i)
                        for (int j = 0; j < n; ++j) {
                            double acc = 0.0;
                            for (int t = 0; t < k; ++t) acc += double(ap[i * k + t]) * double(bp[t * n + j]);
                            cp[i * n + j] = float(acc);
                        }
                }
            }
            return;
        }
        case OpType::Transpose:
        case OpType::Permute: {
            const auto &X = graph_.node(nd.in0);
            out.resize(static_cast<size_t>(nd.size));
            int order[Tensor::kMaxRank] = {};
            for (int k = 0; k < nd.rank; ++k)
                order[k] = nd.type == OpType::Transpose ? (k == 0 ? 1 : 0) : nd.perm[k];
            kernels::permute(in(nd.in0).data(), X.dims, nd.rank, order, out.data(), nd.dims);
            return;
        }
        case OpType::Reshape:
        case OpType::Flatten:
        case OpType::Cast:
            out = in(nd.in0);
            return;
        case OpType::Softmax:
        case OpType::LogSoftmax: {
            out.resize(static_cast<size_t>(nd.size));
            kernels::softmax(in(nd.in0).data(), nd.dims, nd.rank, nd.i0,
                             nd.type == OpType::LogSoftmax, out.data());
            return;
        }
        case OpType::LayerNorm: {
            out.resize(static_cast<size_t>(nd.size));
            const int cols = nd.dims[nd.rank - 1];
            const int rows = nd.size / cols;
            kernels::layernorm(in(nd.in0).data(), rows, cols,
                               nd.in1 >= 0 ? in(nd.in1).data() : nullptr,
                               nd.in2 >= 0 ? in(nd.in2).data() : nullptr, nd.s0, out.data());
            return;
        }
        case OpType::RMSNorm: {
            out.resize(static_cast<size_t>(nd.size));
            const int cols = nd.dims[nd.rank - 1];
            const int rows = nd.size / cols;
            kernels::rmsnorm(in(nd.in0).data(), rows, cols,
                             nd.in1 >= 0 ? in(nd.in1).data() : nullptr, nd.s0, out.data());
            return;
        }
        case OpType::Conv1d:
        case OpType::Conv2d: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            const auto &W = graph_.node(nd.in1);
            if (nd.type == OpType::Conv1d)
                kernels::conv1d(in(nd.in0).data(), X.dims, in(nd.in1).data(), W.dims,
                                nd.in2 >= 0 ? in(nd.in2).data() : nullptr, nd.i0, nd.i1, out.data());
            else
                kernels::conv2d(in(nd.in0).data(), X.dims, in(nd.in1).data(), W.dims,
                                nd.in2 >= 0 ? in(nd.in2).data() : nullptr, nd.i0, nd.i1, out.data());
            return;
        }
        case OpType::MaxPool2d:
        case OpType::AvgPool2d: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            if (nd.type == OpType::MaxPool2d)
                kernels::maxpool2d(in(nd.in0).data(), X.dims, nd.i0, nd.i1, nd.i2, out.data());
            else
                kernels::avgpool2d(in(nd.in0).data(), X.dims, nd.i0, nd.i1, nd.i2, out.data());
            return;
        }
        case OpType::Embedding: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &T = graph_.node(nd.in0);
            const auto &I = graph_.node(nd.in1);
            kernels::embedding(in(nd.in0).data(), T.dims[0], T.dims[1], in(nd.in1).data(),
                               I.size, out.data());
            return;
        }
        case OpType::Concat: {
            out.resize(static_cast<size_t>(nd.size));
            const float *ins[4] = {};
            const int *dims[4]  = {};
            int ranks[4]        = {};
            int n = 2;
            if (nd.in3 >= 0) n = 4;
            else if (nd.in2 >= 0) n = 3;
            const int ids[4] = {nd.in0, nd.in1, nd.in2, nd.in3};
            for (int k = 0; k < n; ++k) {
                ins[k]  = in(ids[k]).data();
                dims[k] = graph_.node(ids[k]).dims;
                ranks[k] = graph_.node(ids[k]).rank;
            }
            kernels::concat(ins, dims, ranks, n, nd.i0, out.data(), nd.dims, nd.rank);
            return;
        }
        case OpType::Slice: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            kernels::sliceOp(in(nd.in0).data(), X.dims, X.rank, nd.i0, nd.i1, nd.i2, out.data(),
                             nd.dims, nd.rank);
            return;
        }
        case OpType::ReduceSum:
        case OpType::ReduceMean:
        case OpType::ReduceMin:
        case OpType::ReduceMax: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            kernels::reduceAxis(nd.type, in(nd.in0).data(), X.dims, X.rank, nd.i0, out.data(),
                                nd.dims, nd.rank);
            return;
        }
        case OpType::ArgMax: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            kernels::argmax(in(nd.in0).data(), X.dims, X.rank, nd.i0, out.data(), nd.dims,
                            nd.rank);
            return;
        }
        case OpType::ScaledDotProductAttention: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &Q = graph_.node(nd.in0);
            const auto &K = graph_.node(nd.in1);
            const int B = Q.dims[0], H = Q.dims[1], T = Q.dims[2], D = Q.dims[3];
            const int S = K.dims[2];
            kernels::sdpa(in(nd.in0).data(), in(nd.in1).data(), in(nd.in2).data(),
                          nd.in3 >= 0 ? in(nd.in3).data() : nullptr, B, H, T, S, D, nd.s0,
                          out.data());
            return;
        }
        case OpType::Resize2d: {
            out.resize(static_cast<size_t>(nd.size));
            const auto &X = graph_.node(nd.in0);
            kernels::resize2d(in(nd.in0).data(), X.dims, nd.dims[3], nd.dims[2], nd.i0,
                              out.data());
            return;
        }
    }
}

}  // namespace eve::tensor
