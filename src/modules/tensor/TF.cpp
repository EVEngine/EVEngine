#include "tensor/TF.h"
#include "tensor/CpuKernels.h"
#include "tensor/GpuBackend.h"
#include "tensor/Graph.h"
#include "tensor/Quant.h"
#include "tensor/Tensor.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace eve::tensor {
namespace {
// Below this element count the GPU dispatch/readback round-trip costs more
// than the CPU loop saves; only worth trying the compute-shader path above it.
constexpr int kGpuReduceMinSize = 1 << 14;  // 16384 floats

int convOutSize(int inSize, int kernel, int stride, int pad) {
    const int out = (inSize + 2 * pad - kernel) / stride + 1;
    if (out <= 0) throw Exception("TF.conv: output size must be > 0");
    return out;
}

int normalizeAxis(int axis, int rank) {
    if (axis < 0) axis += rank;
    if (axis < 0 || axis >= rank) throw Exception("TF: axis out of range");
    return axis;
}

void reduceOutDims(const int *dims, int rank, int axis, int keepDims, int *out, int &outRank) {
    outRank = rank;
    for (int k = 0; k < rank; ++k) out[k] = dims[k];
    if (keepDims) {
        out[axis] = 1;
    } else if (rank == 1) {
        out[0] = 1;
    } else {
        for (int k = axis; k < rank - 1; ++k) out[k] = out[k + 1];
        out[rank - 1] = 0;
        --outRank;
    }
}

}  // namespace

Module_IMPL(TF, new TF());

TF::TF() : seed_(1), rngState_(1) {}

float TF::nextUniform() const {
    rngState_ = rngState_ * 1664525u + 1013904223u;
    return float(rngState_ >> 8) * (1.f / 16777216.f);
}

float TF::nextGaussian() const {
    float u1 = nextUniform();
    float u2 = nextUniform();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return std::sqrt(-2.f * std::log(u1)) * std::cos(6.28318530718f * u2);
}

void TF::setRandomSeed(uint32_t seed) {
    seed_     = seed == 0 ? 1u : seed;
    rngState_ = seed_;
}

uint32_t TF::getRandomSeed() const { return seed_; }

void TF::pushTrace(Func *f) {
    if (f) traceStack_.push_back(f);
}

void TF::popTrace(Func *f) {
    if (traceStack_.empty()) return;
    if (traceStack_.back() == f) {
        traceStack_.pop_back();
        return;
    }
    for (auto it = traceStack_.begin(); it != traceStack_.end(); ++it) {
        if (*it == f) {
            traceStack_.erase(it);
            return;
        }
    }
}

Func *TF::tracing() const {
    return traceStack_.empty() ? nullptr : traceStack_.back();
}

Func *TF::func() { return new Func(this); }

Tensor *TF::filled(const int *dims, int rank, float value) {
    if (Func *f = tracing()) return f->emitFill(dims, rank, value);
    auto *t = new Tensor(dims, rank);
    t->fill(value);
    return t;
}

Tensor *TF::zeros1(int d0) {
    int d[] = {d0};
    return filled(d, 1, 0.f);
}
Tensor *TF::zeros2(int d0, int d1) {
    int d[] = {d0, d1};
    return filled(d, 2, 0.f);
}
Tensor *TF::zeros3(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    return filled(d, 3, 0.f);
}
Tensor *TF::zeros4(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    return filled(d, 4, 0.f);
}
Tensor *TF::zeros5(int d0, int d1, int d2, int d3, int d4) {
    int d[] = {d0, d1, d2, d3, d4};
    return filled(d, 5, 0.f);
}
Tensor *TF::zeros6(int d0, int d1, int d2, int d3, int d4, int d5) {
    int d[] = {d0, d1, d2, d3, d4, d5};
    return filled(d, 6, 0.f);
}

Tensor *TF::ones1(int d0) {
    int d[] = {d0};
    return filled(d, 1, 1.f);
}
Tensor *TF::ones2(int d0, int d1) {
    int d[] = {d0, d1};
    return filled(d, 2, 1.f);
}
Tensor *TF::ones3(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    return filled(d, 3, 1.f);
}
Tensor *TF::ones4(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    return filled(d, 4, 1.f);
}
Tensor *TF::ones5(int d0, int d1, int d2, int d3, int d4) {
    int d[] = {d0, d1, d2, d3, d4};
    return filled(d, 5, 1.f);
}
Tensor *TF::ones6(int d0, int d1, int d2, int d3, int d4, int d5) {
    int d[] = {d0, d1, d2, d3, d4, d5};
    return filled(d, 6, 1.f);
}

Tensor *TF::fill1(int d0, float value) {
    int d[] = {d0};
    return filled(d, 1, value);
}
Tensor *TF::fill2(int d0, int d1, float value) {
    int d[] = {d0, d1};
    return filled(d, 2, value);
}
Tensor *TF::fill3(int d0, int d1, int d2, float value) {
    int d[] = {d0, d1, d2};
    return filled(d, 3, value);
}
Tensor *TF::fill4(int d0, int d1, int d2, int d3, float value) {
    int d[] = {d0, d1, d2, d3};
    return filled(d, 4, value);
}

Tensor *TF::constantScalar(float value) {
    int d[] = {1};
    return filled(d, 1, value);
}

Tensor *TF::arange(int n) {
    if (n <= 0) throw Exception("TF.arange: n must be > 0");
    if (tracing()) throw Exception("TF.arange: not supported while tracing (use inputs/constants)");
    auto *t = new Tensor(n);
    for (int i = 0; i < n; ++i) t->set1(i, float(i));
    return t;
}

Tensor *TF::linspace(float start, float end, int n) {
    if (n <= 0) throw Exception("TF.linspace: n must be > 0");
    if (tracing()) throw Exception("TF.linspace: not supported while tracing");
    auto *t = new Tensor(n);
    if (n == 1) {
        t->set1(0, start);
        return t;
    }
    float step = (end - start) / float(n - 1);
    for (int i = 0; i < n; ++i) t->set1(i, start + step * float(i));
    return t;
}

Tensor *TF::eye(int n) {
    if (n <= 0) throw Exception("TF.eye: n must be > 0");
    if (Func *f = tracing()) {
        auto *eager = new Tensor(n, n);
        for (int i = 0; i < n; ++i) eager->set2(i, i, 1.f);
        int id = f->ensureNode(eager);
        delete eager;
        return Tensor::makeSymbolic(&f->graph(), id, f->graph().node(id).dims,
                                    f->graph().node(id).rank);
    }
    auto *t = new Tensor(n, n);
    for (int i = 0; i < n; ++i) t->set2(i, i, 1.f);
    return t;
}

Tensor *TF::randomUniform1(int d0) {
    if (tracing()) throw Exception("TF.randomUniform: not supported while tracing");
    auto *t = zeros1(d0);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextUniform());
    return t;
}

Tensor *TF::randomUniform2(int d0, int d1) {
    if (tracing()) throw Exception("TF.randomUniform: not supported while tracing");
    auto *t = zeros2(d0, d1);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextUniform());
    return t;
}

Tensor *TF::randomUniform3(int d0, int d1, int d2) {
    if (tracing()) throw Exception("TF.randomUniform: not supported while tracing");
    auto *t = zeros3(d0, d1, d2);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextUniform());
    return t;
}

Tensor *TF::randomUniform4(int d0, int d1, int d2, int d3) {
    if (tracing()) throw Exception("TF.randomUniform: not supported while tracing");
    auto *t = zeros4(d0, d1, d2, d3);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextUniform());
    return t;
}

Tensor *TF::randomNormal1(int d0) {
    if (tracing()) throw Exception("TF.randomNormal: not supported while tracing");
    auto *t = zeros1(d0);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextGaussian());
    return t;
}

Tensor *TF::randomNormal2(int d0, int d1) {
    if (tracing()) throw Exception("TF.randomNormal: not supported while tracing");
    auto *t = zeros2(d0, d1);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextGaussian());
    return t;
}

Tensor *TF::randomNormal3(int d0, int d1, int d2) {
    if (tracing()) throw Exception("TF.randomNormal: not supported while tracing");
    auto *t = zeros3(d0, d1, d2);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextGaussian());
    return t;
}

Tensor *TF::randomNormal4(int d0, int d1, int d2, int d3) {
    if (tracing()) throw Exception("TF.randomNormal: not supported while tracing");
    auto *t = zeros4(d0, d1, d2, d3);
    for (int i = 0; i < t->getSize(); ++i) t->set(i, nextGaussian());
    return t;
}

#define EVE_TF_UNARY(name, method, opType)                \
    Tensor *TF::name(Tensor *a) {                         \
        if (!a) throw Exception("TF." #name ": null");    \
        if (Func *f = tracing()) return f->emitUnary(opType, a); \
        a->ensureEager(#name);                            \
        return a->method();                               \
    }

EVE_TF_UNARY(neg, neg, OpType::Neg)
EVE_TF_UNARY(abs, abs, OpType::Abs)
EVE_TF_UNARY(sqrt, sqrt, OpType::Sqrt)
EVE_TF_UNARY(exp, exp, OpType::Exp)
EVE_TF_UNARY(log, log, OpType::Log)
EVE_TF_UNARY(sin, sin, OpType::Sin)
EVE_TF_UNARY(cos, cos, OpType::Cos)
EVE_TF_UNARY(tanh, tanh, OpType::Tanh)
EVE_TF_UNARY(relu, relu, OpType::Relu)
EVE_TF_UNARY(sigmoid, sigmoid, OpType::Sigmoid)
EVE_TF_UNARY(gelu, gelu, OpType::Gelu)
EVE_TF_UNARY(silu, silu, OpType::Silu)

#undef EVE_TF_UNARY

#define EVE_TF_BINARY(name, method, opType)                             \
    Tensor *TF::name(Tensor *a, Tensor *b) {                            \
        if (!a || !b) throw Exception("TF." #name ": null");            \
        if (Func *f = tracing()) return f->emitBinary(opType, a, b);    \
        a->ensureEager(#name);                                          \
        b->ensureEager(#name);                                          \
        return a->method(b);                                            \
    }

EVE_TF_BINARY(add, add, OpType::Add)
EVE_TF_BINARY(sub, sub, OpType::Sub)
EVE_TF_BINARY(multiply, multiply, OpType::Multiply)
EVE_TF_BINARY(div, div, OpType::Divide)

#undef EVE_TF_BINARY

Tensor *TF::addScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.addScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::AddScalar, a, s);
    return a->addScalar(s);
}
Tensor *TF::subScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.subScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::SubScalar, a, s);
    return a->subScalar(s);
}
Tensor *TF::mulScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.mulScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::MulScalar, a, s);
    return a->mulScalar(s);
}
Tensor *TF::divScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.divScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::DivScalar, a, s);
    return a->divScalar(s);
}
Tensor *TF::powScalar(Tensor *a, float exp) {
    if (!a) throw Exception("TF.powScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::PowScalar, a, exp);
    return a->powScalar(exp);
}
Tensor *TF::clamp(Tensor *a, float lo, float hi) {
    if (!a) throw Exception("TF.clamp: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::Clamp, a, lo, hi);
    return a->clamp(lo, hi);
}
Tensor *TF::maximumScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.maximumScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::MaximumScalar, a, s);
    return a->maximumScalar(s);
}
Tensor *TF::minimumScalar(Tensor *a, float s) {
    if (!a) throw Exception("TF.minimumScalar: null");
    if (Func *f = tracing()) return f->emitUnaryScalar(OpType::MinimumScalar, a, s);
    return a->minimumScalar(s);
}

Tensor *TF::matmul(Tensor *a, Tensor *b) {
    if (!a || !b) throw Exception("TF.matmul: null");
    if (Func *f = tracing()) return f->emitMatMul(a, b);
    return a->matmul(b);
}

Tensor *TF::transpose(Tensor *a) {
    if (!a) throw Exception("TF.transpose: null");
    if (Func *f = tracing()) return f->emitTranspose(a);
    return a->transpose();
}

Tensor *TF::permute2(Tensor *a, int a0, int a1) {
    if (!a) throw Exception("TF.permute2: null");
    int order[] = {a0, a1};
    if (Func *f = tracing()) return f->emitPermute(a, order, 2);
    return a->permute(order, 2);
}
Tensor *TF::permute3(Tensor *a, int a0, int a1, int a2) {
    if (!a) throw Exception("TF.permute3: null");
    int order[] = {a0, a1, a2};
    if (Func *f = tracing()) return f->emitPermute(a, order, 3);
    return a->permute(order, 3);
}
Tensor *TF::permute4(Tensor *a, int a0, int a1, int a2, int a3) {
    if (!a) throw Exception("TF.permute4: null");
    int order[] = {a0, a1, a2, a3};
    if (Func *f = tracing()) return f->emitPermute(a, order, 4);
    return a->permute(order, 4);
}
Tensor *TF::permute5(Tensor *a, int a0, int a1, int a2, int a3, int a4) {
    if (!a) throw Exception("TF.permute5: null");
    int order[] = {a0, a1, a2, a3, a4};
    if (Func *f = tracing()) return f->emitPermute(a, order, 5);
    return a->permute(order, 5);
}
Tensor *TF::permute6(Tensor *a, int a0, int a1, int a2, int a3, int a4, int a5) {
    if (!a) throw Exception("TF.permute6: null");
    int order[] = {a0, a1, a2, a3, a4, a5};
    if (Func *f = tracing()) return f->emitPermute(a, order, 6);
    return a->permute(order, 6);
}

Tensor *TF::reshape1(Tensor *a, int d0) {
    if (!a) throw Exception("TF.reshape1: null");
    int d[] = {d0};
    if (Func *f = tracing()) return f->emitReshape(a, d, 1);
    return a->reshape1(d0);
}
Tensor *TF::reshape2(Tensor *a, int d0, int d1) {
    if (!a) throw Exception("TF.reshape2: null");
    int d[] = {d0, d1};
    if (Func *f = tracing()) return f->emitReshape(a, d, 2);
    return a->reshape2(d0, d1);
}
Tensor *TF::reshape3(Tensor *a, int d0, int d1, int d2) {
    if (!a) throw Exception("TF.reshape3: null");
    int d[] = {d0, d1, d2};
    if (Func *f = tracing()) return f->emitReshape(a, d, 3);
    return a->reshape3(d0, d1, d2);
}
Tensor *TF::reshape4(Tensor *a, int d0, int d1, int d2, int d3) {
    if (!a) throw Exception("TF.reshape4: null");
    int d[] = {d0, d1, d2, d3};
    if (Func *f = tracing()) return f->emitReshape(a, d, 4);
    return a->reshape4(d0, d1, d2, d3);
}
Tensor *TF::reshape5(Tensor *a, int d0, int d1, int d2, int d3, int d4) {
    if (!a) throw Exception("TF.reshape5: null");
    int d[] = {d0, d1, d2, d3, d4};
    if (Func *f = tracing()) return f->emitReshape(a, d, 5);
    return a->reshape5(d0, d1, d2, d3, d4);
}
Tensor *TF::reshape6(Tensor *a, int d0, int d1, int d2, int d3, int d4, int d5) {
    if (!a) throw Exception("TF.reshape6: null");
    int d[] = {d0, d1, d2, d3, d4, d5};
    if (Func *f = tracing()) return f->emitReshape(a, d, 6);
    return a->reshape6(d0, d1, d2, d3, d4, d5);
}

Tensor *TF::flatten(Tensor *a) {
    if (!a) throw Exception("TF.flatten: null");
    int d[] = {a->getSize()};
    if (Func *f = tracing()) return f->emitReshape(a, d, 1);
    return a->flatten();
}

Tensor *TF::where(Tensor *cond, Tensor *a, Tensor *b) {
    if (!cond || !a || !b) throw Exception("TF.where: null");
    if (Func *f = tracing()) return f->emitTernary(OpType::Where, cond, a, b);
    if (cond->getRank() != a->getRank() || a->getRank() != b->getRank() ||
        cond->getSize() != a->getSize() || a->getSize() != b->getSize())
        throw Exception("TF.where: shape mismatch");
    for (int i = 0; i < cond->getRank(); ++i) {
        if (cond->getDim(i) != a->getDim(i) || a->getDim(i) != b->getDim(i))
            throw Exception("TF.where: shape mismatch");
    }
    int dims[Tensor::kMaxRank] = {};
    for (int i = 0; i < a->getRank(); ++i) dims[i] = a->getDim(i);
    auto *out = new Tensor(dims, a->getRank());
    for (int i = 0; i < a->getSize(); ++i)
        out->set(i, cond->get(i) > 0.5f ? a->get(i) : b->get(i));
    return out;
}

// --- neural / speech / terrain ops ---------------------------------------

Tensor *TF::softmax(Tensor *a, int axis) {
    if (!a) throw Exception("TF.softmax: null");
    if (Func *f = tracing()) return f->emitSoftmax(a, axis, false);
    a->ensureEager("softmax");
    axis = normalizeAxis(axis, a->getRank());
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::softmax(a->data(), dims, a->getRank(), axis, false, out->data());
    return out;
}

Tensor *TF::logSoftmax(Tensor *a, int axis) {
    if (!a) throw Exception("TF.logSoftmax: null");
    if (Func *f = tracing()) return f->emitSoftmax(a, axis, true);
    a->ensureEager("logSoftmax");
    axis = normalizeAxis(axis, a->getRank());
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::softmax(a->data(), dims, a->getRank(), axis, true, out->data());
    return out;
}

Tensor *TF::layernorm(Tensor *a, float eps) {
    if (!a) throw Exception("TF.layernorm: null");
    if (Func *f = tracing()) return f->emitLayerNorm(a, nullptr, nullptr, eps);
    a->ensureEager("layernorm");
    const int cols = a->getDim(a->getRank() - 1);
    const int rows = a->getSize() / cols;
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::layernorm(a->data(), rows, cols, nullptr, nullptr, eps, out->data());
    return out;
}

Tensor *TF::layernormWB(Tensor *a, Tensor *scale, Tensor *bias, float eps) {
    if (!a || !scale || !bias) throw Exception("TF.layernormWB: null");
    if (Func *f = tracing()) return f->emitLayerNorm(a, scale, bias, eps);
    a->ensureEager("layernormWB");
    const int cols = a->getDim(a->getRank() - 1);
    if (scale->getSize() != cols || bias->getSize() != cols)
        throw Exception("TF.layernormWB: scale/bias must match last dim");
    const int rows = a->getSize() / cols;
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::layernorm(a->data(), rows, cols, scale->data(), bias->data(), eps, out->data());
    return out;
}

Tensor *TF::rmsnorm(Tensor *a, float eps) {
    if (!a) throw Exception("TF.rmsnorm: null");
    if (Func *f = tracing()) return f->emitRMSNorm(a, nullptr, eps);
    a->ensureEager("rmsnorm");
    const int cols = a->getDim(a->getRank() - 1);
    const int rows = a->getSize() / cols;
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::rmsnorm(a->data(), rows, cols, nullptr, eps, out->data());
    return out;
}

Tensor *TF::rmsnormW(Tensor *a, Tensor *scale, float eps) {
    if (!a || !scale) throw Exception("TF.rmsnormW: null");
    if (Func *f = tracing()) return f->emitRMSNorm(a, scale, eps);
    a->ensureEager("rmsnormW");
    const int cols = a->getDim(a->getRank() - 1);
    if (scale->getSize() != cols) throw Exception("TF.rmsnormW: scale must match last dim");
    const int rows = a->getSize() / cols;
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dims, a->getRank());
    kernels::rmsnorm(a->data(), rows, cols, scale->data(), eps, out->data());
    return out;
}

Tensor *TF::conv1d(Tensor *x, Tensor *w, int stride, int pad) {
    return conv1dBias(x, w, nullptr, stride, pad);
}

Tensor *TF::conv1dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad) {
    if (!x || !w) throw Exception("TF.conv1d: null");
    if (Func *f = tracing()) return f->emitConv1d(x, w, bias, stride, pad);
    if (x->getRank() != 3 || w->getRank() != 3) throw Exception("TF.conv1d: rank 3 required");
    if (x->getDim(1) != w->getDim(1)) throw Exception("TF.conv1d: channel mismatch");
    if (bias && bias->getSize() != w->getDim(0)) throw Exception("TF.conv1d: bias mismatch");
    const int OL = convOutSize(x->getDim(2), w->getDim(2), stride, pad);
    auto *out = new Tensor(x->getDim(0), w->getDim(0), OL);
    int xd[3] = {x->getDim(0), x->getDim(1), x->getDim(2)};
    int wd[3] = {w->getDim(0), w->getDim(1), w->getDim(2)};
    kernels::conv1d(x->data(), xd, w->data(), wd, bias ? bias->data() : nullptr, stride, pad,
                    out->data());
    return out;
}

Tensor *TF::conv2d(Tensor *x, Tensor *w, int stride, int pad) {
    return conv2dBias(x, w, nullptr, stride, pad);
}

Tensor *TF::conv2dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad) {
    if (!x || !w) throw Exception("TF.conv2d: null");
    if (Func *f = tracing()) return f->emitConv2d(x, w, bias, stride, pad);
    if (x->getRank() != 4 || w->getRank() != 4) throw Exception("TF.conv2d: rank 4 required");
    if (x->getDim(1) != w->getDim(1)) throw Exception("TF.conv2d: channel mismatch");
    if (bias && bias->getSize() != w->getDim(0)) throw Exception("TF.conv2d: bias mismatch");
    const int OH = convOutSize(x->getDim(2), w->getDim(2), stride, pad);
    const int OW = convOutSize(x->getDim(3), w->getDim(3), stride, pad);
    auto *out = new Tensor(x->getDim(0), w->getDim(0), OH, OW);
    int xd[4] = {x->getDim(0), x->getDim(1), x->getDim(2), x->getDim(3)};
    int wd[4] = {w->getDim(0), w->getDim(1), w->getDim(2), w->getDim(3)};
    kernels::conv2d(x->data(), xd, w->data(), wd, bias ? bias->data() : nullptr, stride, pad,
                    out->data());
    return out;
}

Tensor *TF::maxpool2d(Tensor *x, int ksize, int stride, int pad) {
    if (!x) throw Exception("TF.maxpool2d: null");
    if (Func *f = tracing()) return f->emitPool(OpType::MaxPool2d, x, ksize, stride, pad);
    if (x->getRank() != 4) throw Exception("TF.maxpool2d: rank 4 required");
    const int OH = convOutSize(x->getDim(2), ksize, stride, pad);
    const int OW = convOutSize(x->getDim(3), ksize, stride, pad);
    auto *out = new Tensor(x->getDim(0), x->getDim(1), OH, OW);
    int xd[4] = {x->getDim(0), x->getDim(1), x->getDim(2), x->getDim(3)};
    kernels::maxpool2d(x->data(), xd, ksize, stride, pad, out->data());
    return out;
}

Tensor *TF::avgpool2d(Tensor *x, int ksize, int stride, int pad) {
    if (!x) throw Exception("TF.avgpool2d: null");
    if (Func *f = tracing()) return f->emitPool(OpType::AvgPool2d, x, ksize, stride, pad);
    if (x->getRank() != 4) throw Exception("TF.avgpool2d: rank 4 required");
    const int OH = convOutSize(x->getDim(2), ksize, stride, pad);
    const int OW = convOutSize(x->getDim(3), ksize, stride, pad);
    auto *out = new Tensor(x->getDim(0), x->getDim(1), OH, OW);
    int xd[4] = {x->getDim(0), x->getDim(1), x->getDim(2), x->getDim(3)};
    kernels::avgpool2d(x->data(), xd, ksize, stride, pad, out->data());
    return out;
}

Tensor *TF::embedding(Tensor *table, Tensor *indices) {
    if (!table || !indices) throw Exception("TF.embedding: null");
    if (Func *f = tracing()) return f->emitEmbedding(table, indices);
    if (table->getRank() != 2) throw Exception("TF.embedding: table rank 2 required");
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < indices->getRank(); ++k) dims[k] = indices->getDim(k);
    dims[indices->getRank()] = table->getDim(1);
    auto *out = new Tensor(dims, indices->getRank() + 1);
    if (table->isQuantized()) {
        const std::vector<float> tf32 = table->dequantized();
        kernels::embedding(tf32.data(), table->getDim(0), table->getDim(1), indices->data(),
                           indices->getSize(), out->data());
    } else {
        kernels::embedding(table->data(), table->getDim(0), table->getDim(1), indices->data(),
                           indices->getSize(), out->data());
    }
    return out;
}

Tensor *TF::quantizeWeight(Tensor *a, const std::string &dtype, int group) {
    if (!a) throw Exception("TF.quantizeWeight: null");
    if (tracing()) throw Exception("TF.quantizeWeight: quantize eager weights before tracing");
    DType dt = DType::Float32;
    if (!parseDType(dtype, dt) || !q::isQuantDType(dt))
        throw Exception("TF.quantizeWeight: expected fp16/fp8/fp4/int8/int4, got '%s'",
                        dtype.c_str());
    a->ensureEager("quantizeWeight");
    if (a->isQuantized()) throw Exception("TF.quantizeWeight: input is already quantized");
    q::QuantPayload p = q::quantize(a->data(), a->getSize(), dt, group);
    auto *out = new Tensor(dt, a->getRank() > 0 ? a->dims_ : nullptr, a->getRank());
    out->bytes_ = std::move(p.bytes);
    out->qScales_ = std::move(p.scales);
    out->qGroup_ = p.group;
    return out;
}

Tensor *TF::concat2(Tensor *a, Tensor *b, int axis) {
    Tensor *ins[] = {a, b};
    return concatN(ins, 2, axis);
}

Tensor *TF::concat3(Tensor *a, Tensor *b, Tensor *c, int axis) {
    Tensor *ins[] = {a, b, c};
    return concatN(ins, 3, axis);
}

Tensor *TF::concat4(Tensor *a, Tensor *b, Tensor *c, Tensor *d, int axis) {
    Tensor *ins[] = {a, b, c, d};
    return concatN(ins, 4, axis);
}

namespace {

Tensor *concatEager(Tensor *const *ins, int n, int axis) {
    for (int k = 0; k < n; ++k)
        if (!ins[k]) throw Exception("TF.concat: null");
    const int rank = ins[0]->getRank();
    axis = normalizeAxis(axis, rank);
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < rank; ++k) {
        if (k == axis) {
            int total = 0;
            for (int t = 0; t < n; ++t) total += ins[t]->getDim(k);
            dims[k] = total;
        } else {
            dims[k] = ins[0]->getDim(k);
            for (int t = 1; t < n; ++t)
                if (ins[t]->getDim(k) != dims[k]) throw Exception("TF.concat: dims mismatch");
        }
    }
    auto *out = new Tensor(dims, rank);
    const float *ptrs[4] = {};
    int inDims[4][Tensor::kMaxRank] = {};
    const int *dimsPtr[4] = {};
    int inRanks[4] = {};
    for (int k = 0; k < n; ++k) {
        ptrs[k] = ins[k]->data();
        for (int d = 0; d < rank; ++d) inDims[k][d] = ins[k]->getDim(d);
        dimsPtr[k] = inDims[k];
        inRanks[k] = rank;
    }
    kernels::concat(ptrs, dimsPtr, inRanks, n, axis, out->data(), dims, rank);
    return out;
}

}  // namespace

Tensor *TF::concatN(Tensor *const *ins, int n, int axis) {
    if (Func *f = tracing()) return f->emitConcat(const_cast<const Tensor *const *>(ins), n, axis);
    return concatEager(ins, n, axis);
}

Tensor *TF::slice(Tensor *a, int axis, int begin, int end) {
    if (!a) throw Exception("TF.slice: null");
    if (Func *f = tracing()) return f->emitSlice(a, axis, begin, end);
    a->ensureEager("slice");
    axis = normalizeAxis(axis, a->getRank());
    if (begin < 0 || end < begin || end > a->getDim(axis))
        throw Exception("TF.slice: range out of bounds");
    int srcDims[Tensor::kMaxRank] = {};
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) {
        srcDims[k] = a->getDim(k);
        dims[k] = a->getDim(k);
    }
    dims[axis] = end - begin;
    auto *out = new Tensor(dims, a->getRank());
    kernels::sliceOp(a->data(), srcDims, a->getRank(), axis, begin, end, out->data(), dims,
                     a->getRank());
    return out;
}

#define EVE_TF_AXIS_REDUCE(name, opType)                                       \
    Tensor *TF::name(Tensor *a, int axis, int keepDims) {                      \
        if (!a) throw Exception("TF." #name ": null");                         \
        if (Func *f = tracing()) return f->emitReduce(opType, a, axis, keepDims != 0); \
        a->ensureEager(#name);                                                 \
        axis = normalizeAxis(axis, a->getRank());                              \
        int srcDims[Tensor::kMaxRank] = {};                                    \
        for (int k = 0; k < a->getRank(); ++k) srcDims[k] = a->getDim(k);      \
        int od[Tensor::kMaxRank] = {};                                         \
        int outRank = 0;                                                       \
        reduceOutDims(srcDims, a->getRank(), axis, keepDims != 0, od, outRank); \
        auto *out = new Tensor(od, outRank);                                   \
        kernels::reduceAxis(opType, a->data(), srcDims, a->getRank(), axis, out->data(), od, \
                            outRank);                                          \
        return out;                                                            \
    }

EVE_TF_AXIS_REDUCE(sumAxis, OpType::ReduceSum)
EVE_TF_AXIS_REDUCE(meanAxis, OpType::ReduceMean)
EVE_TF_AXIS_REDUCE(minAxis, OpType::ReduceMin)
EVE_TF_AXIS_REDUCE(maxAxis, OpType::ReduceMax)

#undef EVE_TF_AXIS_REDUCE

Tensor *TF::argmax(Tensor *a, int axis, int keepDims) {
    if (!a) throw Exception("TF.argmax: null");
    if (Func *f = tracing()) return f->emitArgMax(a, axis, keepDims != 0);
    a->ensureEager("argmax");
    axis = normalizeAxis(axis, a->getRank());
    int srcDims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) srcDims[k] = a->getDim(k);
    int od[Tensor::kMaxRank] = {};
    int outRank = 0;
    reduceOutDims(srcDims, a->getRank(), axis, keepDims != 0, od, outRank);
    auto *out = new Tensor(DType::Int32, od, outRank);
    kernels::argmax(a->data(), srcDims, a->getRank(), axis, out->data(), od, outRank);
    return out;
}

Tensor *TF::cast(Tensor *a, const std::string &dtype) {
    if (!a) throw Exception("TF.cast: null");
    DType dt = DType::Float32;
    if (!parseDType(dtype, dt)) throw Exception("TF.cast: unknown dtype '%s'", dtype.c_str());
    if (Func *f = tracing()) return f->emitCast(a, dt);
    a->ensureEager("cast");
    int dims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) dims[k] = a->getDim(k);
    auto *out = new Tensor(dt, dims, a->getRank());
    std::memcpy(out->data(), a->data(), sizeof(float) * static_cast<size_t>(a->getSize()));
    return out;
}

Tensor *TF::sdpa(Tensor *q, Tensor *k, Tensor *v, float scale) {
    return sdpaMasked(q, k, v, nullptr, scale);
}

Tensor *TF::sdpaMasked(Tensor *q, Tensor *k, Tensor *v, Tensor *mask, float scale) {
    if (!q || !k || !v) throw Exception("TF.sdpa: null");
    if (Func *f = tracing()) return f->emitSdpa(q, k, v, mask, scale);
    if (q->getRank() != 4 || k->getRank() != 4 || v->getRank() != 4)
        throw Exception("TF.sdpa: rank 4 required");
    if (q->getDim(0) != k->getDim(0) || q->getDim(1) != k->getDim(1) ||
        q->getDim(3) != k->getDim(3) || k->getDim(2) != v->getDim(2) ||
        q->getDim(3) != v->getDim(3))
        throw Exception("TF.sdpa: q/k/v shape mismatch");
    if (mask && (mask->getRank() != 4 || mask->getDim(0) != q->getDim(0) ||
                 mask->getDim(1) != q->getDim(1) || mask->getDim(2) != q->getDim(2) ||
                 mask->getDim(3) != k->getDim(2)))
        throw Exception("TF.sdpa: mask shape mismatch");
    auto *out = new Tensor(q->getDim(0), q->getDim(1), q->getDim(2), q->getDim(3));
    kernels::sdpa(q->data(), k->data(), v->data(), mask ? mask->data() : nullptr, q->getDim(0),
                  q->getDim(1), q->getDim(2), k->getDim(2), q->getDim(3), scale, out->data());
    return out;
}

Tensor *TF::resize2d(Tensor *a, int outW, int outH, int mode) {
    if (!a) throw Exception("TF.resize2d: null");
    if (Func *f = tracing()) return f->emitResize2d(a, outH, outW, mode);
    if (a->getRank() != 4) throw Exception("TF.resize2d: rank 4 required");
    if (outW <= 0 || outH <= 0) throw Exception("TF.resize2d: bad output size");
    auto *out = new Tensor(a->getDim(0), a->getDim(1), outH, outW);
    int xd[4] = {a->getDim(0), a->getDim(1), a->getDim(2), a->getDim(3)};
    kernels::resize2d(a->data(), xd, outW, outH, mode, out->data());
    return out;
}

float TF::reduceSum(Tensor *a) {
    if (!a) throw Exception("TF.reduceSum: null");
    if (tracing()) throw Exception("TF.reduceSum: not supported while tracing");
    a->ensureEager("reduceSum");
    float gpuResult = 0.f;
    if (a->getSize() >= kGpuReduceMinSize && gpuReduce(a->data(), a->getSize(), 0, gpuResult))
        return gpuResult;
    return a->reduceSum();
}
float TF::reduceMean(Tensor *a) {
    if (!a) throw Exception("TF.reduceMean: null");
    if (tracing()) throw Exception("TF.reduceMean: not supported while tracing");
    a->ensureEager("reduceMean");
    if (a->getSize() <= 0) return 0.f;
    return reduceSum(a) / float(a->getSize());
}
float TF::reduceMin(Tensor *a) {
    if (!a) throw Exception("TF.reduceMin: null");
    if (tracing()) throw Exception("TF.reduceMin: not supported while tracing");
    a->ensureEager("reduceMin");
    float gpuResult = 0.f;
    if (a->getSize() >= kGpuReduceMinSize && gpuReduce(a->data(), a->getSize(), 1, gpuResult))
        return gpuResult;
    return a->reduceMin();
}
float TF::reduceMax(Tensor *a) {
    if (!a) throw Exception("TF.reduceMax: null");
    if (tracing()) throw Exception("TF.reduceMax: not supported while tracing");
    a->ensureEager("reduceMax");
    float gpuResult = 0.f;
    if (a->getSize() >= kGpuReduceMinSize && gpuReduce(a->data(), a->getSize(), 2, gpuResult))
        return gpuResult;
    return a->reduceMax();
}

void TF::expose(ssq::Table &table) {
    auto cls = table.addClass(name, TF::create, false);
    expose(cls);

    auto ten = table.addClass<Tensor>(
        "Tensor", std::function<Tensor *()>([]() -> Tensor * { return nullptr; }), true);

    ten.addFunc("isSymbolic", &Tensor::isSymbolic);
    ten.addFunc("isEager", &Tensor::isEager);
    ten.addFunc("getRank", &Tensor::getRank);
    ten.addFunc("getSize", &Tensor::getSize);
    ten.addFunc("getDim", &Tensor::getDim);
    ten.addFunc("getDim0", &Tensor::getDim0);
    ten.addFunc("getDim1", &Tensor::getDim1);
    ten.addFunc("getDim2", &Tensor::getDim2);
    ten.addFunc("getDim3", &Tensor::getDim3);
    ten.addFunc("getDim4", &Tensor::getDim4);
    ten.addFunc("getDim5", &Tensor::getDim5);
    ten.addFunc("getDevice", &Tensor::getDevice);
    ten.addFunc("getDtype", &Tensor::getDtype);
    ten.addFunc("isQuantized", &Tensor::isQuantized);

    ten.addFunc("get", &Tensor::get);
    ten.addFunc("set", &Tensor::set);
    ten.addFunc("get1", &Tensor::get1);
    ten.addFunc("set1", &Tensor::set1);
    ten.addFunc("get2", &Tensor::get2);
    ten.addFunc("set2", &Tensor::set2);
    ten.addFunc("get3", &Tensor::get3);
    ten.addFunc("set3", &Tensor::set3);
    ten.addFunc("get4", &Tensor::get4);
    ten.addFunc("set4", &Tensor::set4);
    ten.addFunc("get5", &Tensor::get5);
    ten.addFunc("set5", &Tensor::set5);
    ten.addFunc("get6", &Tensor::get6);
    ten.addFunc("set6", &Tensor::set6);

    ten.addFunc("fill", &Tensor::fill);
    ten.addFunc("copyFrom", &Tensor::copyFrom);
    ten.addFunc("clone", &Tensor::clone);

    ten.addFunc("add", &Tensor::add);
    ten.addFunc("sub", &Tensor::sub);
    ten.addFunc("multiply", &Tensor::multiply);
    ten.addFunc("div", &Tensor::div);
    ten.addFunc("addScalar", &Tensor::addScalar);
    ten.addFunc("subScalar", &Tensor::subScalar);
    ten.addFunc("mulScalar", &Tensor::mulScalar);
    ten.addFunc("divScalar", &Tensor::divScalar);
    ten.addFunc("neg", &Tensor::neg);
    ten.addFunc("abs", &Tensor::abs);
    ten.addFunc("sqrt", &Tensor::sqrt);
    ten.addFunc("exp", &Tensor::exp);
    ten.addFunc("log", &Tensor::log);
    ten.addFunc("sin", &Tensor::sin);
    ten.addFunc("cos", &Tensor::cos);
    ten.addFunc("tanh", &Tensor::tanh);
    ten.addFunc("relu", &Tensor::relu);
    ten.addFunc("sigmoid", &Tensor::sigmoid);
    ten.addFunc("gelu", &Tensor::gelu);
    ten.addFunc("silu", &Tensor::silu);
    ten.addFunc("powScalar", &Tensor::powScalar);
    ten.addFunc("clamp", &Tensor::clamp);
    ten.addFunc("maximumScalar", &Tensor::maximumScalar);
    ten.addFunc("minimumScalar", &Tensor::minimumScalar);

    ten.addFunc("addInPlace", &Tensor::addInPlace);
    ten.addFunc("multiplyInPlace", &Tensor::multiplyInPlace);
    ten.addFunc("addScalarInPlace", &Tensor::addScalarInPlace);
    ten.addFunc("mulScalarInPlace", &Tensor::mulScalarInPlace);
    ten.addFunc("reluInPlace", &Tensor::reluInPlace);

    ten.addFunc("reduceSum", &Tensor::reduceSum);
    ten.addFunc("reduceMean", &Tensor::reduceMean);
    ten.addFunc("reduceMin", &Tensor::reduceMin);
    ten.addFunc("reduceMax", &Tensor::reduceMax);
    ten.addFunc("dot", &Tensor::dot);

    ten.addFunc("matmul", &Tensor::matmul);
    ten.addFunc("transpose", &Tensor::transpose);
    ten.addFunc("reshape1", &Tensor::reshape1);
    ten.addFunc("reshape2", &Tensor::reshape2);
    ten.addFunc("reshape3", &Tensor::reshape3);
    ten.addFunc("reshape4", &Tensor::reshape4);
    ten.addFunc("reshape5", &Tensor::reshape5);
    ten.addFunc("reshape6", &Tensor::reshape6);
    ten.addFunc("flatten", &Tensor::flatten);

    auto fn = table.addClass<Func>(
        "Func", std::function<Func *()>([]() -> Func * { return nullptr; }), true);
    fn.addFunc("input1", &Func::input1);
    fn.addFunc("input2", &Func::input2);
    fn.addFunc("input3", &Func::input3);
    fn.addFunc("input4", &Func::input4);
    fn.addFunc("input5", &Func::input5);
    fn.addFunc("input6", &Func::input6);
    fn.addFunc("setOutput", &Func::setOutput);
    fn.addFunc("compile", &Func::compile);

    auto cf = table.addClass<CompiledFunction>(
        "CompiledFunction",
        std::function<CompiledFunction *()>([]() -> CompiledFunction * { return nullptr; }), true);
    cf.addFunc("run0", &CompiledFunction::run0);
    cf.addFunc("run1", &CompiledFunction::run1);
    cf.addFunc("run2", &CompiledFunction::run2);
    cf.addFunc("run3", &CompiledFunction::run3);
    cf.addFunc("run4", &CompiledFunction::run4);
    cf.addFunc("run5", &CompiledFunction::run5);
    cf.addFunc("run6", &CompiledFunction::run6);
    cf.addFunc("getPlaceholderCount", &CompiledFunction::getPlaceholderCount);
    cf.addFunc("getDevice", &CompiledFunction::getDevice);
}

void TF::expose(ssq::Class &cls) {
    cls.addFunc("getName", &TF::getName);
    cls.addFunc("func", &TF::func);
    cls.addFunc("quantizeWeight", &TF::quantizeWeight);

    cls.addFunc("zeros1", &TF::zeros1);
    cls.addFunc("zeros2", &TF::zeros2);
    cls.addFunc("zeros3", &TF::zeros3);
    cls.addFunc("zeros4", &TF::zeros4);
    cls.addFunc("zeros5", &TF::zeros5);
    cls.addFunc("zeros6", &TF::zeros6);
    cls.addFunc("ones1", &TF::ones1);
    cls.addFunc("ones2", &TF::ones2);
    cls.addFunc("ones3", &TF::ones3);
    cls.addFunc("ones4", &TF::ones4);
    cls.addFunc("ones5", &TF::ones5);
    cls.addFunc("ones6", &TF::ones6);
    cls.addFunc("fill1", &TF::fill1);
    cls.addFunc("fill2", &TF::fill2);
    cls.addFunc("fill3", &TF::fill3);
    cls.addFunc("fill4", &TF::fill4);
    cls.addFunc("constantScalar", &TF::constantScalar);
    cls.addFunc("arange", &TF::arange);
    cls.addFunc("linspace", &TF::linspace);
    cls.addFunc("eye", &TF::eye);
    cls.addFunc("randomUniform1", &TF::randomUniform1);
    cls.addFunc("randomUniform2", &TF::randomUniform2);
    cls.addFunc("randomUniform3", &TF::randomUniform3);
    cls.addFunc("randomUniform4", &TF::randomUniform4);
    cls.addFunc("randomNormal1", &TF::randomNormal1);
    cls.addFunc("randomNormal2", &TF::randomNormal2);
    cls.addFunc("randomNormal3", &TF::randomNormal3);
    cls.addFunc("randomNormal4", &TF::randomNormal4);
    cls.addFunc("rand1", &TF::rand1);
    cls.addFunc("rand2", &TF::rand2);
    cls.addFunc("rand3", &TF::rand3);
    cls.addFunc("rand4", &TF::rand4);
    cls.addFunc("randn1", &TF::randn1);
    cls.addFunc("randn2", &TF::randn2);
    cls.addFunc("randn3", &TF::randn3);
    cls.addFunc("randn4", &TF::randn4);
    cls.addFunc("setRandomSeed", &TF::setRandomSeed);
    cls.addFunc("getRandomSeed", &TF::getRandomSeed);

    cls.addFunc("add", &TF::add);
    cls.addFunc("sub", &TF::sub);
    cls.addFunc("multiply", &TF::multiply);
    cls.addFunc("div", &TF::div);
    cls.addFunc("addScalar", &TF::addScalar);
    cls.addFunc("subScalar", &TF::subScalar);
    cls.addFunc("mulScalar", &TF::mulScalar);
    cls.addFunc("divScalar", &TF::divScalar);
    cls.addFunc("neg", &TF::neg);
    cls.addFunc("abs", &TF::abs);
    cls.addFunc("sqrt", &TF::sqrt);
    cls.addFunc("exp", &TF::exp);
    cls.addFunc("log", &TF::log);
    cls.addFunc("sin", &TF::sin);
    cls.addFunc("cos", &TF::cos);
    cls.addFunc("tanh", &TF::tanh);
    cls.addFunc("relu", &TF::relu);
    cls.addFunc("sigmoid", &TF::sigmoid);
    cls.addFunc("gelu", &TF::gelu);
    cls.addFunc("silu", &TF::silu);
    cls.addFunc("powScalar", &TF::powScalar);
    cls.addFunc("clamp", &TF::clamp);
    cls.addFunc("maximumScalar", &TF::maximumScalar);
    cls.addFunc("minimumScalar", &TF::minimumScalar);
    cls.addFunc("matmul", &TF::matmul);
    cls.addFunc("transpose", &TF::transpose);
    cls.addFunc("permute2", &TF::permute2);
    cls.addFunc("permute3", &TF::permute3);
    cls.addFunc("permute4", &TF::permute4);
    cls.addFunc("permute5", &TF::permute5);
    cls.addFunc("permute6", &TF::permute6);
    cls.addFunc("reshape1", &TF::reshape1);
    cls.addFunc("reshape2", &TF::reshape2);
    cls.addFunc("reshape3", &TF::reshape3);
    cls.addFunc("reshape4", &TF::reshape4);
    cls.addFunc("reshape5", &TF::reshape5);
    cls.addFunc("reshape6", &TF::reshape6);
    cls.addFunc("flatten", &TF::flatten);
    cls.addFunc("where", &TF::where);
    cls.addFunc("reduceSum", &TF::reduceSum);
    cls.addFunc("reduceMean", &TF::reduceMean);
    cls.addFunc("reduceMin", &TF::reduceMin);
    cls.addFunc("reduceMax", &TF::reduceMax);

    cls.addFunc("softmax", &TF::softmax);
    cls.addFunc("logSoftmax", &TF::logSoftmax);
    cls.addFunc("layernorm", &TF::layernorm);
    cls.addFunc("layernormWB", &TF::layernormWB);
    cls.addFunc("rmsnorm", &TF::rmsnorm);
    cls.addFunc("rmsnormW", &TF::rmsnormW);
    cls.addFunc("conv1d", &TF::conv1d);
    cls.addFunc("conv1dBias", &TF::conv1dBias);
    cls.addFunc("conv2d", &TF::conv2d);
    cls.addFunc("conv2dBias", &TF::conv2dBias);
    cls.addFunc("maxpool2d", &TF::maxpool2d);
    cls.addFunc("avgpool2d", &TF::avgpool2d);
    cls.addFunc("embedding", &TF::embedding);
    cls.addFunc("concat2", &TF::concat2);
    cls.addFunc("concat3", &TF::concat3);
    cls.addFunc("concat4", &TF::concat4);
    cls.addFunc("slice", &TF::slice);
    cls.addFunc("sumAxis", &TF::sumAxis);
    cls.addFunc("meanAxis", &TF::meanAxis);
    cls.addFunc("minAxis", &TF::minAxis);
    cls.addFunc("maxAxis", &TF::maxAxis);
    cls.addFunc("argmax", &TF::argmax);
    cls.addFunc("cast", &TF::cast);
    cls.addFunc("sdpa", &TF::sdpa);
    cls.addFunc("sdpaMasked", &TF::sdpaMasked);
    cls.addFunc("resize2d", &TF::resize2d);
}

}  // namespace eve::tensor
