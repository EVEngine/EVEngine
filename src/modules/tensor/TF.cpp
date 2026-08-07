#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "tensor/Graph.h"
#include "tensor/GpuBackend.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>

namespace eve::tensor {
namespace {
// Below this element count the GPU dispatch/readback round-trip costs more
// than the CPU loop saves; only worth trying the compute-shader path above it.
constexpr int kGpuReduceMinSize = 1 << 14;  // 16384 floats
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
        // Const identity
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
    ten.addFunc("getDevice", &Tensor::getDevice);
    ten.addFunc("getDtype", &Tensor::getDtype);

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
    ten.addFunc("flatten", &Tensor::flatten);

    auto fn = table.addClass<Func>(
        "Func", std::function<Func *()>([]() -> Func * { return nullptr; }), true);
    fn.addFunc("input1", &Func::input1);
    fn.addFunc("input2", &Func::input2);
    fn.addFunc("input3", &Func::input3);
    fn.addFunc("input4", &Func::input4);
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
    cf.addFunc("getPlaceholderCount", &CompiledFunction::getPlaceholderCount);
    cf.addFunc("getDevice", &CompiledFunction::getDevice);
}

void TF::expose(ssq::Class &cls) {
    cls.addFunc("getName", &TF::getName);
    cls.addFunc("func", &TF::func);

    cls.addFunc("zeros1", &TF::zeros1);
    cls.addFunc("zeros2", &TF::zeros2);
    cls.addFunc("zeros3", &TF::zeros3);
    cls.addFunc("zeros4", &TF::zeros4);
    cls.addFunc("ones1", &TF::ones1);
    cls.addFunc("ones2", &TF::ones2);
    cls.addFunc("ones3", &TF::ones3);
    cls.addFunc("fill1", &TF::fill1);
    cls.addFunc("fill2", &TF::fill2);
    cls.addFunc("fill3", &TF::fill3);
    cls.addFunc("constantScalar", &TF::constantScalar);
    cls.addFunc("arange", &TF::arange);
    cls.addFunc("linspace", &TF::linspace);
    cls.addFunc("eye", &TF::eye);
    cls.addFunc("randomUniform1", &TF::randomUniform1);
    cls.addFunc("randomUniform2", &TF::randomUniform2);
    cls.addFunc("randomNormal1", &TF::randomNormal1);
    cls.addFunc("randomNormal2", &TF::randomNormal2);
    cls.addFunc("rand1", &TF::rand1);
    cls.addFunc("rand2", &TF::rand2);
    cls.addFunc("randn1", &TF::randn1);
    cls.addFunc("randn2", &TF::randn2);
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
    cls.addFunc("powScalar", &TF::powScalar);
    cls.addFunc("clamp", &TF::clamp);
    cls.addFunc("maximumScalar", &TF::maximumScalar);
    cls.addFunc("minimumScalar", &TF::minimumScalar);
    cls.addFunc("matmul", &TF::matmul);
    cls.addFunc("transpose", &TF::transpose);
    cls.addFunc("reshape1", &TF::reshape1);
    cls.addFunc("reshape2", &TF::reshape2);
    cls.addFunc("reshape3", &TF::reshape3);
    cls.addFunc("reshape4", &TF::reshape4);
    cls.addFunc("flatten", &TF::flatten);
    cls.addFunc("where", &TF::where);
    cls.addFunc("reduceSum", &TF::reduceSum);
    cls.addFunc("reduceMean", &TF::reduceMean);
    cls.addFunc("reduceMin", &TF::reduceMin);
    cls.addFunc("reduceMax", &TF::reduceMax);
}

}  // namespace eve::tensor
