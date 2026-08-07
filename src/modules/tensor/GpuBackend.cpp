#include "tensor/GpuBackend.h"
#include "tensor/Graph.h"

#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"

#include "common/Exception.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>

namespace eve::tensor {
namespace {

// --- shared compute kernels (compiled once, reused by every GpuProgram) ---
//
// All kernels use set=0 storage-buffer bindings + a 32-float push constant
// block (matches eve::gpgpu::ComputeShader's fixed layout).

constexpr int kLocalSize = 256;

const char *kBinaryGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer A { float a[]; };
layout(set = 0, binding = 1) readonly buffer B { float b[]; };
layout(set = 0, binding = 2) writeonly buffer C { float c[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint size = uint(pc.data[0] + 0.5);
    if (i >= size) return;
    int op = int(pc.data[1] + 0.5);
    float x = a[i];
    float y = b[i];
    float r = x;
    if (op == 0) r = x + y;
    else if (op == 1) r = x - y;
    else if (op == 2) r = x * y;
    else r = x / y;
    c[i] = r;
}
)";

const char *kUnaryGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer In { float a[]; };
layout(set = 0, binding = 1) writeonly buffer Out { float c[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint size = uint(pc.data[0] + 0.5);
    if (i >= size) return;
    int op = int(pc.data[1] + 0.5);
    float s0 = pc.data[2];
    float s1 = pc.data[3];
    float x = a[i];
    float r = x;
    if (op == 0) r = -x;
    else if (op == 1) r = abs(x);
    else if (op == 2) r = sqrt(x);
    else if (op == 3) r = exp(x);
    else if (op == 4) r = log(x);
    else if (op == 5) r = sin(x);
    else if (op == 6) r = cos(x);
    else if (op == 7) r = tanh(x);
    else if (op == 8) r = x > 0.0 ? x : 0.0;
    else if (op == 9) r = 1.0 / (1.0 + exp(-x));
    else if (op == 10) r = x + s0;
    else if (op == 11) r = x - s0;
    else if (op == 12) r = x * s0;
    else if (op == 13) r = x / s0;
    else if (op == 14) r = pow(x, s0);
    else if (op == 15) { float lo = min(s0, s1); float hi = max(s0, s1); r = clamp(x, lo, hi); }
    else if (op == 16) r = max(x, s0);
    else if (op == 17) r = min(x, s0);
    c[i] = r;
}
)";

const char *kMatMulGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer A { float a[]; };
layout(set = 0, binding = 1) readonly buffer B { float b[]; };
layout(set = 0, binding = 2) writeonly buffer C { float c[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint m = uint(pc.data[0] + 0.5);
    uint k = uint(pc.data[1] + 0.5);
    uint n = uint(pc.data[2] + 0.5);
    if (idx >= m * n) return;
    uint i = idx / n;
    uint j = idx % n;
    float acc = 0.0;
    for (uint t = 0u; t < k; ++t) acc += a[i * k + t] * b[t * n + j];
    c[idx] = acc;
}
)";

const char *kTransposeGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer In { float a[]; };
layout(set = 0, binding = 1) writeonly buffer Out { float b[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint idx = gl_GlobalInvocationID.x;
    uint rows = uint(pc.data[0] + 0.5);
    uint cols = uint(pc.data[1] + 0.5);
    if (idx >= rows * cols) return;
    uint i = idx / cols;
    uint j = idx % cols;
    b[j * rows + i] = a[idx];
}
)";

const char *kWhereGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer Cond { float cnd[]; };
layout(set = 0, binding = 1) readonly buffer A { float a[]; };
layout(set = 0, binding = 2) readonly buffer B { float b[]; };
layout(set = 0, binding = 3) writeonly buffer Out { float o[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint size = uint(pc.data[0] + 0.5);
    if (i >= size) return;
    o[i] = cnd[i] > 0.5 ? a[i] : b[i];
}
)";

// Partial (per-workgroup) reduction; final combine happens on the CPU over
// the small `partial[]` output (one float per workgroup).
const char *kReduceGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer In { float a[]; };
layout(set = 0, binding = 1) writeonly buffer Out { float partial[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
shared float sdata[256];
void main() {
    uint tid = gl_LocalInvocationID.x;
    uint gid = gl_GlobalInvocationID.x;
    uint size = uint(pc.data[0] + 0.5);
    int op = int(pc.data[1] + 0.5);
    float ident = (op == 0) ? 0.0 : (op == 1 ? 3.402823e38 : -3.402823e38);
    sdata[tid] = (gid < size) ? a[gid] : ident;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) {
            if (op == 0) sdata[tid] += sdata[tid + s];
            else if (op == 1) sdata[tid] = min(sdata[tid], sdata[tid + s]);
            else sdata[tid] = max(sdata[tid], sdata[tid + s]);
        }
        barrier();
    }
    if (tid == 0u) partial[gl_WorkGroupID.x] = sdata[0];
}
)";

struct Kernels {
    gpgpu::Gpgpu *gpgpu = nullptr;
    gpgpu::ComputeShader *binary = nullptr;
    gpgpu::ComputeShader *unary = nullptr;
    gpgpu::ComputeShader *matmul = nullptr;
    gpgpu::ComputeShader *transpose = nullptr;
    gpgpu::ComputeShader *where = nullptr;
    gpgpu::ComputeShader *reduce = nullptr;
};

/** Lazily compiled, process-lifetime singleton. Returns nullptr if unavailable. */
Kernels *getKernels() {
    static Kernels *kernels = nullptr;
    static bool failed = false;
    if (kernels) return kernels;
    if (failed) return nullptr;
    try {
        auto *gp = gpgpu::Gpgpu::create();
        if (!gp || !gp->isAvailable()) {
            failed = true;
            return nullptr;
        }
        auto *k = new Kernels();
        k->gpgpu = gp;
        k->binary = gp->newShader(kBinaryGlsl);
        k->unary = gp->newShader(kUnaryGlsl);
        k->matmul = gp->newShader(kMatMulGlsl);
        k->transpose = gp->newShader(kTransposeGlsl);
        k->where = gp->newShader(kWhereGlsl);
        k->reduce = gp->newShader(kReduceGlsl);
        kernels = k;
        return kernels;
    } catch (...) {
        failed = true;
        return nullptr;
    }
}

int groupsFor(int count) { return (count + kLocalSize - 1) / kLocalSize; }

int binaryOpCode(OpType t) {
    switch (t) {
        case OpType::Add: return 0;
        case OpType::Sub: return 1;
        case OpType::Multiply: return 2;
        case OpType::Divide: return 3;
        default: return -1;
    }
}

int unaryOpCode(OpType t) {
    switch (t) {
        case OpType::Neg: return 0;
        case OpType::Abs: return 1;
        case OpType::Sqrt: return 2;
        case OpType::Exp: return 3;
        case OpType::Log: return 4;
        case OpType::Sin: return 5;
        case OpType::Cos: return 6;
        case OpType::Tanh: return 7;
        case OpType::Relu: return 8;
        case OpType::Sigmoid: return 9;
        case OpType::AddScalar: return 10;
        case OpType::SubScalar: return 11;
        case OpType::MulScalar: return 12;
        case OpType::DivScalar: return 13;
        case OpType::PowScalar: return 14;
        case OpType::Clamp: return 15;
        case OpType::MaximumScalar: return 16;
        case OpType::MinimumScalar: return 17;
        default: return -1;
    }
}

}  // namespace

struct GpuProgram::Impl {
    struct Step {
        enum class Kind { Binary, Unary, MatMul, Transpose, Where } kind;
        int opCode = 0;
        float s0 = 0.f, s1 = 0.f;
        int size = 0;                 // elementwise / where element count
        int m = 0, k = 0, n = 0;      // matmul
        int rows = 0, cols = 0;       // transpose
        gpgpu::GpuBuffer *in0 = nullptr;
        gpgpu::GpuBuffer *in1 = nullptr;
        gpgpu::GpuBuffer *in2 = nullptr;
        gpgpu::GpuBuffer *out = nullptr;
    };

    Kernels *kernels = nullptr;
    std::vector<std::unique_ptr<gpgpu::GpuBuffer>> ownedBuffers;
    std::vector<Step> steps;
    std::vector<gpgpu::GpuBuffer *> placeholderBuffers;
    std::vector<int> placeholderSizes;
    gpgpu::GpuBuffer *outputBuffer = nullptr;
    int outputSize = 0;

    void executeStep(const Step &st) const {
        switch (st.kind) {
            case Step::Kind::Binary: {
                auto *sh = kernels->binary;
                sh->bindBuffer(0, st.in0);
                sh->bindBuffer(1, st.in1);
                sh->bindBuffer(2, st.out);
                sh->setFloat(0, float(st.size));
                sh->setFloat(1, float(st.opCode));
                kernels->gpgpu->dispatch(sh, groupsFor(st.size));
                break;
            }
            case Step::Kind::Unary: {
                auto *sh = kernels->unary;
                sh->bindBuffer(0, st.in0);
                sh->bindBuffer(1, st.out);
                sh->setFloat(0, float(st.size));
                sh->setFloat(1, float(st.opCode));
                sh->setFloat(2, st.s0);
                sh->setFloat(3, st.s1);
                kernels->gpgpu->dispatch(sh, groupsFor(st.size));
                break;
            }
            case Step::Kind::MatMul: {
                auto *sh = kernels->matmul;
                sh->bindBuffer(0, st.in0);
                sh->bindBuffer(1, st.in1);
                sh->bindBuffer(2, st.out);
                sh->setFloat(0, float(st.m));
                sh->setFloat(1, float(st.k));
                sh->setFloat(2, float(st.n));
                kernels->gpgpu->dispatch(sh, groupsFor(st.m * st.n));
                break;
            }
            case Step::Kind::Transpose: {
                auto *sh = kernels->transpose;
                sh->bindBuffer(0, st.in0);
                sh->bindBuffer(1, st.out);
                sh->setFloat(0, float(st.rows));
                sh->setFloat(1, float(st.cols));
                kernels->gpgpu->dispatch(sh, groupsFor(st.rows * st.cols));
                break;
            }
            case Step::Kind::Where: {
                auto *sh = kernels->where;
                sh->bindBuffer(0, st.in0);
                sh->bindBuffer(1, st.in1);
                sh->bindBuffer(2, st.in2);
                sh->bindBuffer(3, st.out);
                sh->setFloat(0, float(st.size));
                kernels->gpgpu->dispatch(sh, groupsFor(st.size));
                break;
            }
        }
    }
};

GpuProgram::GpuProgram() : impl_(new Impl()) {}

GpuProgram::~GpuProgram() { delete impl_; }

GpuProgram *GpuProgram::tryBuild(const Graph &graph, const std::vector<int> &order, int outputNode) {
    Kernels *kernels = getKernels();
    if (!kernels) return nullptr;
    if (outputNode < 0 || outputNode >= graph.nodeCount()) return nullptr;

    auto *prog = new GpuProgram();
    prog->impl_->kernels = kernels;
    std::unordered_map<int, int> alias;  // Reshape/Flatten -> source node id
    std::unordered_map<int, gpgpu::GpuBuffer *> bufOf;

    std::function<int(int)> resolve = [&](int id) {
        auto it = alias.find(id);
        while (it != alias.end()) {
            id = it->second;
            it = alias.find(id);
        }
        return id;
    };
    auto bufferFor = [&](int id) -> gpgpu::GpuBuffer * { return bufOf.at(resolve(id)); };
    auto newBuf = [&](int elemCount) -> gpgpu::GpuBuffer * {
        auto *buf = kernels->gpgpu->newBuffer(elemCount * int(sizeof(float)), "storage");
        prog->impl_->ownedBuffers.emplace_back(buf);
        return buf;
    };

    try {
        for (int id : order) {
            const GraphNode &nd = graph.node(id);
            switch (nd.type) {
                case OpType::Placeholder: {
                    auto *buf = newBuf(nd.size);
                    bufOf[id] = buf;
                    int slot = nd.placeholderSlot;
                    if (slot < 0) throw eve::Exception("GpuProgram: bad placeholder slot");
                    if (int(prog->impl_->placeholderBuffers.size()) <= slot) {
                        prog->impl_->placeholderBuffers.resize(size_t(slot) + 1, nullptr);
                        prog->impl_->placeholderSizes.resize(size_t(slot) + 1, 0);
                    }
                    prog->impl_->placeholderBuffers[size_t(slot)] = buf;
                    prog->impl_->placeholderSizes[size_t(slot)] = nd.size;
                    break;
                }
                case OpType::Const: {
                    auto *buf = newBuf(nd.size);
                    buf->uploadBytes(nd.constData.data(), sizeof(float) * size_t(nd.size));
                    bufOf[id] = buf;
                    break;
                }
                case OpType::Reshape:
                case OpType::Flatten: {
                    alias[id] = nd.in0;
                    break;
                }
                case OpType::MatMul: {
                    const auto &A = graph.node(nd.in0);
                    const auto &B = graph.node(nd.in1);
                    Impl::Step st;
                    st.kind = Impl::Step::Kind::MatMul;
                    st.m = A.dims[0];
                    st.k = A.dims[1];
                    st.n = B.dims[1];
                    st.in0 = bufferFor(nd.in0);
                    st.in1 = bufferFor(nd.in1);
                    st.out = newBuf(nd.size);
                    bufOf[id] = st.out;
                    prog->impl_->steps.push_back(st);
                    break;
                }
                case OpType::Transpose: {
                    const auto &X = graph.node(nd.in0);
                    Impl::Step st;
                    st.kind = Impl::Step::Kind::Transpose;
                    st.rows = X.dims[0];
                    st.cols = X.dims[1];
                    st.in0 = bufferFor(nd.in0);
                    st.out = newBuf(nd.size);
                    bufOf[id] = st.out;
                    prog->impl_->steps.push_back(st);
                    break;
                }
                case OpType::Where: {
                    Impl::Step st;
                    st.kind = Impl::Step::Kind::Where;
                    st.size = nd.size;
                    st.in0 = bufferFor(nd.in0);
                    st.in1 = bufferFor(nd.in1);
                    st.in2 = bufferFor(nd.in2);
                    st.out = newBuf(nd.size);
                    bufOf[id] = st.out;
                    prog->impl_->steps.push_back(st);
                    break;
                }
                default: {
                    int bop = binaryOpCode(nd.type);
                    if (bop >= 0) {
                        Impl::Step st;
                        st.kind = Impl::Step::Kind::Binary;
                        st.opCode = bop;
                        st.size = nd.size;
                        st.in0 = bufferFor(nd.in0);
                        st.in1 = bufferFor(nd.in1);
                        st.out = newBuf(nd.size);
                        bufOf[id] = st.out;
                        prog->impl_->steps.push_back(st);
                        break;
                    }
                    int uop = unaryOpCode(nd.type);
                    if (uop >= 0) {
                        Impl::Step st;
                        st.kind = Impl::Step::Kind::Unary;
                        st.opCode = uop;
                        st.s0 = nd.s0;
                        st.s1 = nd.s1;
                        st.size = nd.size;
                        st.in0 = bufferFor(nd.in0);
                        st.out = newBuf(nd.size);
                        bufOf[id] = st.out;
                        prog->impl_->steps.push_back(st);
                        break;
                    }
                    // Unknown / unsupported op type — bail out to the CPU interpreter.
                    throw eve::Exception("GpuProgram: unsupported op");
                }
            }
        }
    } catch (...) {
        delete prog;
        return nullptr;
    }

    prog->impl_->outputBuffer = bufOf.count(resolve(outputNode)) ? bufOf.at(resolve(outputNode)) : nullptr;
    prog->impl_->outputSize = graph.node(outputNode).size;
    if (!prog->impl_->outputBuffer) {
        delete prog;
        return nullptr;
    }
    return prog;
}

std::vector<float> GpuProgram::run(const std::vector<const float *> &feeds) const {
    for (size_t i = 0; i < feeds.size(); ++i) {
        if (i >= impl_->placeholderBuffers.size() || !impl_->placeholderBuffers[i]) continue;
        impl_->placeholderBuffers[i]->uploadBytes(feeds[i], sizeof(float) * size_t(impl_->placeholderSizes[i]));
    }
    for (const auto &st : impl_->steps) impl_->executeStep(st);
    std::vector<float> out(size_t(impl_->outputSize));
    impl_->outputBuffer->downloadBytes(out.data(), sizeof(float) * size_t(impl_->outputSize));
    return out;
}

bool gpuReduce(const float *data, int size, int op, float &outResult) {
    if (!data || size <= 0) return false;
    Kernels *kernels = getKernels();
    if (!kernels) return false;
    try {
        std::unique_ptr<gpgpu::GpuBuffer> in(kernels->gpgpu->newBuffer(size * int(sizeof(float)), "storage"));
        in->uploadBytes(data, sizeof(float) * size_t(size));

        const int groups = groupsFor(size);
        std::unique_ptr<gpgpu::GpuBuffer> partial(kernels->gpgpu->newBuffer(groups * int(sizeof(float)), "storage"));

        kernels->reduce->bindBuffer(0, in.get());
        kernels->reduce->bindBuffer(1, partial.get());
        kernels->reduce->setFloat(0, float(size));
        kernels->reduce->setFloat(1, float(op));
        kernels->gpgpu->dispatch(kernels->reduce, groups);

        std::vector<float> parts(static_cast<size_t>(groups));
        partial->downloadBytes(parts.data(), sizeof(float) * size_t(groups));

        float acc = op == 0 ? 0.f : (op == 1 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max());
        for (float v : parts) {
            if (op == 0) acc += v;
            else if (op == 1) acc = std::min(acc, v);
            else acc = std::max(acc, v);
        }
        outResult = acc;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace eve::tensor
