#include "tensor/Tensor.h"
#include "tensor/CpuKernels.h"
#include "tensor/Graph.h"
#include "tensor/Quant.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eve::tensor {
namespace {

int productLocal(const int *dims, int rank) {
    int n = 1;
    for (int i = 0; i < rank; ++i) {
        if (dims[i] <= 0) throw eve::Exception("Tensor: dims must be > 0");
        n *= dims[i];
    }
    return n;
}

}  // namespace

const char *dtypeName(DType dtype) {
    switch (dtype) {
        case DType::Int32: return "int32";
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        case DType::Float32:
        default: return "float32";
    }
}

bool parseDType(const std::string &name, DType &out) {
    if (name == "float32" || name == "f32") {
        out = DType::Float32;
        return true;
    }
    if (name == "int32" || name == "i32") {
        out = DType::Int32;
        return true;
    }
    if (name == "fp16" || name == "f16") {
        out = DType::Fp16;
        return true;
    }
    if (name == "fp8" || name == "f8") {
        out = DType::Fp8E4M3;
        return true;
    }
    if (name == "fp4" || name == "f4") {
        out = DType::Fp4E2M1;
        return true;
    }
    if (name == "int8" || name == "i8") {
        out = DType::Int8;
        return true;
    }
    if (name == "int4" || name == "i4") {
        out = DType::Int4;
        return true;
    }
    return false;
}

int Tensor::product(const int *dims, int rank) { return productLocal(dims, rank); }

void Tensor::initDims(DType dtype, const int *dims, int rank) {
    if (rank < 1 || rank > kMaxRank)
        throw eve::Exception("Tensor: rank must be 1..%d", kMaxRank);
    rank_ = rank;
    for (int i = 0; i < kMaxRank; ++i) dims_[i] = 0;
    for (int i = 0; i < rank; ++i) dims_[i] = dims[i];
    size_  = productLocal(dims, rank);
    dtype_ = dtype;
    if (q::isQuantDType(dtype)) {
        data_.clear();
    } else {
        data_.assign(static_cast<size_t>(size_), 0.f);
    }
    bytes_.clear();
    qScales_.clear();
    qGroup_ = 0;
    device_ = "cpu";
    kind_   = Kind::Eager;
    graph_  = nullptr;
    nodeId_ = -1;
}

Tensor::Tensor(const int *dims, int rank) { initDims(DType::Float32, dims, rank); }
Tensor::Tensor(DType dtype, const int *dims, int rank) { initDims(dtype, dims, rank); }
Tensor::Tensor(int d0) {
    int d[] = {d0};
    initDims(DType::Float32, d, 1);
}
Tensor::Tensor(int d0, int d1) {
    int d[] = {d0, d1};
    initDims(DType::Float32, d, 2);
}
Tensor::Tensor(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    initDims(DType::Float32, d, 3);
}
Tensor::Tensor(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    initDims(DType::Float32, d, 4);
}
Tensor::Tensor(int d0, int d1, int d2, int d3, int d4) {
    int d[] = {d0, d1, d2, d3, d4};
    initDims(DType::Float32, d, 5);
}
Tensor::Tensor(int d0, int d1, int d2, int d3, int d4, int d5) {
    int d[] = {d0, d1, d2, d3, d4, d5};
    initDims(DType::Float32, d, 6);
}

Tensor *Tensor::makeSymbolic(Graph *graph, int nodeId, const int *dims, int rank) {
    auto *t    = new Tensor();
    t->kind_   = Kind::Symbolic;
    t->graph_  = graph;
    t->nodeId_ = nodeId;
    t->rank_   = rank;
    for (int i = 0; i < kMaxRank; ++i) t->dims_[i] = 0;
    for (int i = 0; i < rank; ++i) t->dims_[i] = dims[i];
    t->size_   = productLocal(dims, rank);
    t->dtype_  = DType::Float32;
    t->device_ = "cpu";
    return t;
}

void Tensor::ensureEager(const char *op) const {
    if (kind_ != Kind::Eager)
        throw eve::Exception("Tensor.%s: symbolic tensor has no value (compile/run first)", op);
}

int Tensor::getDim(int axis) const {
    if (axis < 0 || axis >= rank_) throw eve::Exception("Tensor.getDim: axis out of range");
    return dims_[axis];
}

int Tensor::offset2(int i0, int i1) const {
    if (rank_ != 2) throw eve::Exception("Tensor: expected rank 2");
    if (i0 < 0 || i0 >= dims_[0] || i1 < 0 || i1 >= dims_[1])
        throw eve::Exception("Tensor: index out of range");
    return i0 * dims_[1] + i1;
}

int Tensor::offset3(int i0, int i1, int i2) const {
    if (rank_ != 3) throw eve::Exception("Tensor: expected rank 3");
    if (i0 < 0 || i0 >= dims_[0] || i1 < 0 || i1 >= dims_[1] || i2 < 0 || i2 >= dims_[2])
        throw eve::Exception("Tensor: index out of range");
    return (i0 * dims_[1] + i1) * dims_[2] + i2;
}

int Tensor::offset4(int i0, int i1, int i2, int i3) const {
    if (rank_ != 4) throw eve::Exception("Tensor: expected rank 4");
    if (i0 < 0 || i0 >= dims_[0] || i1 < 0 || i1 >= dims_[1] || i2 < 0 || i2 >= dims_[2] ||
        i3 < 0 || i3 >= dims_[3])
        throw eve::Exception("Tensor: index out of range");
    return ((i0 * dims_[1] + i1) * dims_[2] + i2) * dims_[3] + i3;
}

int Tensor::offset5(int i0, int i1, int i2, int i3, int i4) const {
    if (rank_ != 5) throw eve::Exception("Tensor: expected rank 5");
    if (i0 < 0 || i0 >= dims_[0] || i1 < 0 || i1 >= dims_[1] || i2 < 0 || i2 >= dims_[2] ||
        i3 < 0 || i3 >= dims_[3] || i4 < 0 || i4 >= dims_[4])
        throw eve::Exception("Tensor: index out of range");
    return (((i0 * dims_[1] + i1) * dims_[2] + i2) * dims_[3] + i3) * dims_[4] + i4;
}

int Tensor::offset6(int i0, int i1, int i2, int i3, int i4, int i5) const {
    if (rank_ != 6) throw eve::Exception("Tensor: expected rank 6");
    if (i0 < 0 || i0 >= dims_[0] || i1 < 0 || i1 >= dims_[1] || i2 < 0 || i2 >= dims_[2] ||
        i3 < 0 || i3 >= dims_[3] || i4 < 0 || i4 >= dims_[4] || i5 < 0 || i5 >= dims_[5])
        throw eve::Exception("Tensor: index out of range");
    return ((((i0 * dims_[1] + i1) * dims_[2] + i2) * dims_[3] + i3) * dims_[4] + i4) * dims_[5] +
           i5;
}

float *Tensor::data() {
    ensureEager("data");
    if (isQuantized()) throw eve::Exception("Tensor.data: quantized tensor; dequantized() first");
    return data_.data();
}
const float *Tensor::data() const {
    ensureEager("data");
    if (isQuantized()) throw eve::Exception("Tensor.data: quantized tensor; dequantized() first");
    return data_.data();
}

float Tensor::get(int flatIndex) const {
    ensureEager("get");
    if (flatIndex < 0 || flatIndex >= size_) throw eve::Exception("Tensor.get: index out of range");
    if (isQuantized()) return q::dequantValue(dtype_, bytes_.data(), qScales_.data(), qGroup_, flatIndex);
    return data_[static_cast<size_t>(flatIndex)];
}

void Tensor::set(int flatIndex, float value) {
    ensureEager("set");
    if (flatIndex < 0 || flatIndex >= size_) throw eve::Exception("Tensor.set: index out of range");
    if (isQuantized()) throw eve::Exception("Tensor.set: quantized tensors are immutable");
    data_[static_cast<size_t>(flatIndex)] = value;
}

std::vector<float> Tensor::dequantized() const {
    ensureEager("dequantized");
    if (!isQuantized()) return data_;
    std::vector<float> out(static_cast<size_t>(size_));
    q::dequantizeAll(dtype_, bytes_.data(), qScales_.data(), qGroup_, size_, out.data());
    return out;
}

float Tensor::get1(int i0) const {
    if (rank_ != 1) throw eve::Exception("Tensor.get1: expected rank 1");
    return get(i0);
}
void Tensor::set1(int i0, float value) {
    if (rank_ != 1) throw eve::Exception("Tensor.set1: expected rank 1");
    set(i0, value);
}
float Tensor::get2(int i0, int i1) const {
    ensureEager("get2");
    return data_[static_cast<size_t>(offset2(i0, i1))];
}
void Tensor::set2(int i0, int i1, float value) {
    ensureEager("set2");
    data_[static_cast<size_t>(offset2(i0, i1))] = value;
}
float Tensor::get3(int i0, int i1, int i2) const {
    ensureEager("get3");
    return data_[static_cast<size_t>(offset3(i0, i1, i2))];
}
void Tensor::set3(int i0, int i1, int i2, float value) {
    ensureEager("set3");
    data_[static_cast<size_t>(offset3(i0, i1, i2))] = value;
}
float Tensor::get4(int i0, int i1, int i2, int i3) const {
    ensureEager("get4");
    return data_[static_cast<size_t>(offset4(i0, i1, i2, i3))];
}
void Tensor::set4(int i0, int i1, int i2, int i3, float value) {
    ensureEager("set4");
    data_[static_cast<size_t>(offset4(i0, i1, i2, i3))] = value;
}
float Tensor::get5(int i0, int i1, int i2, int i3, int i4) const {
    ensureEager("get5");
    return data_[static_cast<size_t>(offset5(i0, i1, i2, i3, i4))];
}
void Tensor::set5(int i0, int i1, int i2, int i3, int i4, float value) {
    ensureEager("set5");
    data_[static_cast<size_t>(offset5(i0, i1, i2, i3, i4))] = value;
}
float Tensor::get6(int i0, int i1, int i2, int i3, int i4, int i5) const {
    ensureEager("get6");
    return data_[static_cast<size_t>(offset6(i0, i1, i2, i3, i4, i5))];
}
void Tensor::set6(int i0, int i1, int i2, int i3, int i4, int i5, float value) {
    ensureEager("set6");
    data_[static_cast<size_t>(offset6(i0, i1, i2, i3, i4, i5))] = value;
}

void Tensor::fill(float value) {
    ensureEager("fill");
    std::fill(data_.begin(), data_.end(), value);
}

void Tensor::copyFrom(const Tensor *other) {
    ensureEager("copyFrom");
    if (!other) throw eve::Exception("Tensor.copyFrom: other is null");
    other->ensureEager("copyFrom");
    checkSameShape(other, "copyFrom");
    std::memcpy(data_.data(), other->data_.data(), sizeof(float) * static_cast<size_t>(size_));
    dtype_ = other->dtype_;
}

Tensor *Tensor::clone() const {
    ensureEager("clone");
    auto *out = new Tensor(dtype_, dims_, rank_);
    out->data_   = data_;
    out->device_ = device_;
    return out;
}

void Tensor::checkSameShape(const Tensor *other, const char *op) const {
    if (!other) throw eve::Exception("Tensor.%s: other is null", op);
    if (rank_ != other->rank_ || size_ != other->size_)
        throw eve::Exception("Tensor.%s: shape mismatch", op);
    for (int i = 0; i < rank_; ++i) {
        if (dims_[i] != other->dims_[i]) throw eve::Exception("Tensor.%s: shape mismatch", op);
    }
}

namespace {

Tensor *broadcastBinary(OpType op, const Tensor *a, const Tensor *b, const char *name) {
    a->ensureEager(name);
    b->ensureEager(name);
    int aDims[Tensor::kMaxRank] = {};
    int bDims[Tensor::kMaxRank] = {};
    for (int k = 0; k < a->getRank(); ++k) aDims[k] = a->getDim(k);
    for (int k = 0; k < b->getRank(); ++k) bDims[k] = b->getDim(k);
    int od[Tensor::kMaxRank] = {};
    int orank = 0;
    if (!kernels::broadcastShape(aDims, a->getRank(), bDims, b->getRank(), od, orank))
        throw eve::Exception("Tensor.%s: broadcast shape mismatch", name);
    auto *out = new Tensor(od, orank);
    kernels::binaryOp(op, a->data(), aDims, a->getRank(), b->data(), bDims, b->getRank(),
                      out->data(), od, orank);
    return out;
}

}  // namespace

Tensor *Tensor::add(const Tensor *other) const { return broadcastBinary(OpType::Add, this, other, "add"); }
Tensor *Tensor::sub(const Tensor *other) const { return broadcastBinary(OpType::Sub, this, other, "sub"); }
Tensor *Tensor::multiply(const Tensor *other) const {
    return broadcastBinary(OpType::Multiply, this, other, "multiply");
}
Tensor *Tensor::div(const Tensor *other) const { return broadcastBinary(OpType::Divide, this, other, "div"); }

Tensor *Tensor::addScalar(float s) const {
    ensureEager("addScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::AddScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}
Tensor *Tensor::subScalar(float s) const {
    ensureEager("subScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::SubScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}
Tensor *Tensor::mulScalar(float s) const {
    ensureEager("mulScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::MulScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}
Tensor *Tensor::divScalar(float s) const {
    ensureEager("divScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::DivScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}
Tensor *Tensor::powScalar(float exp) const {
    ensureEager("powScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::PowScalar, data_.data(), size_, out->data_.data(), exp, 0.f);
    return out;
}
Tensor *Tensor::maximumScalar(float s) const {
    ensureEager("maximumScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::MaximumScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}
Tensor *Tensor::minimumScalar(float s) const {
    ensureEager("minimumScalar");
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::MinimumScalar, data_.data(), size_, out->data_.data(), s, 0.f);
    return out;
}

#define EVE_TENSOR_UNARY(name, opType)                                              \
    Tensor *Tensor::name() const {                                                  \
        ensureEager(#name);                                                         \
        auto *out = new Tensor(dtype_, dims_, rank_);                               \
        kernels::unaryOp((opType), data_.data(), size_, out->data_.data(), 0.f, 0.f); \
        return out;                                                                 \
    }

EVE_TENSOR_UNARY(neg, OpType::Neg)
EVE_TENSOR_UNARY(abs, OpType::Abs)
EVE_TENSOR_UNARY(sqrt, OpType::Sqrt)
EVE_TENSOR_UNARY(exp, OpType::Exp)
EVE_TENSOR_UNARY(log, OpType::Log)
EVE_TENSOR_UNARY(sin, OpType::Sin)
EVE_TENSOR_UNARY(cos, OpType::Cos)
EVE_TENSOR_UNARY(tanh, OpType::Tanh)
EVE_TENSOR_UNARY(relu, OpType::Relu)
EVE_TENSOR_UNARY(sigmoid, OpType::Sigmoid)
EVE_TENSOR_UNARY(gelu, OpType::Gelu)
EVE_TENSOR_UNARY(silu, OpType::Silu)

#undef EVE_TENSOR_UNARY

Tensor *Tensor::clamp(float lo, float hi) const {
    ensureEager("clamp");
    if (lo > hi) std::swap(lo, hi);
    auto *out = new Tensor(dtype_, dims_, rank_);
    kernels::unaryOp(OpType::Clamp, data_.data(), size_, out->data_.data(), lo, hi);
    return out;
}

void Tensor::addInPlace(const Tensor *other) {
    ensureEager("addInPlace");
    other->ensureEager("addInPlace");
    checkSameShape(other, "addInPlace");
    float *a       = data_.data();
    const float *b = other->data_.data();
    for (int i = 0; i < size_; ++i) a[i] += b[i];
}

void Tensor::multiplyInPlace(const Tensor *other) {
    ensureEager("multiplyInPlace");
    other->ensureEager("multiplyInPlace");
    checkSameShape(other, "multiplyInPlace");
    float *a       = data_.data();
    const float *b = other->data_.data();
    for (int i = 0; i < size_; ++i) a[i] *= b[i];
}

void Tensor::addScalarInPlace(float s) {
    ensureEager("addScalarInPlace");
    float *a = data_.data();
    for (int i = 0; i < size_; ++i) a[i] += s;
}

void Tensor::mulScalarInPlace(float s) {
    ensureEager("mulScalarInPlace");
    float *a = data_.data();
    for (int i = 0; i < size_; ++i) a[i] *= s;
}

void Tensor::reluInPlace() {
    ensureEager("reluInPlace");
    float *a = data_.data();
    for (int i = 0; i < size_; ++i)
        if (a[i] < 0.f) a[i] = 0.f;
}

float Tensor::reduceSum() const {
    ensureEager("reduceSum");
    double acc = 0.0;
    for (int i = 0; i < size_; ++i) acc += data_[static_cast<size_t>(i)];
    return float(acc);
}

float Tensor::reduceMean() const {
    return size_ > 0 ? reduceSum() / float(size_) : 0.f;
}

float Tensor::reduceMin() const {
    ensureEager("reduceMin");
    if (size_ <= 0) return 0.f;
    float m = data_[0];
    for (int i = 1; i < size_; ++i) m = std::min(m, data_[static_cast<size_t>(i)]);
    return m;
}

float Tensor::reduceMax() const {
    ensureEager("reduceMax");
    if (size_ <= 0) return 0.f;
    float m = data_[0];
    for (int i = 1; i < size_; ++i) m = std::max(m, data_[static_cast<size_t>(i)]);
    return m;
}

float Tensor::dot(const Tensor *other) const {
    ensureEager("dot");
    other->ensureEager("dot");
    checkSameShape(other, "dot");
    double acc     = 0.0;
    const float *a = data_.data();
    const float *b = other->data_.data();
    for (int i = 0; i < size_; ++i) acc += double(a[i]) * double(b[i]);
    return float(acc);
}

Tensor *Tensor::matmul(const Tensor *other) const {
    ensureEager("matmul");
    if (!other) throw eve::Exception("Tensor.matmul: other is null");
    other->ensureEager("matmul");
    std::vector<float> aq, bq;
    const float *a = data_.data();
    const float *b = other->data_.data();
    if (isQuantized()) {
        aq = dequantized();
        a = aq.data();
    }
    if (other->isQuantized()) {
        bq = other->dequantized();
        b = bq.data();
    }
    if (rank_ == 2 && other->rank_ == 2) {
        int m = dims_[0], k = dims_[1], n = other->dims_[1];
        if (k != other->dims_[0]) throw eve::Exception("Tensor.matmul: inner dims mismatch");
        auto *out      = new Tensor(m, n);
        float *c       = out->data_.data();
        for (int i = 0; i < m; ++i) {
            for (int j = 0; j < n; ++j) {
                double acc = 0.0;
                for (int t = 0; t < k; ++t) acc += double(a[i * k + t]) * double(b[t * n + j]);
                c[i * n + j] = float(acc);
            }
        }
        return out;
    }
    if (rank_ == 3 && other->rank_ == 3) {
        const int batch = dims_[0];
        const int m = dims_[1], k = dims_[2], n = other->dims_[2];
        if (batch != other->dims_[0] || k != other->dims_[1])
            throw eve::Exception("Tensor.matmul: batched dims mismatch");
        auto *out = new Tensor(batch, m, n);
        for (int bb = 0; bb < batch; ++bb) {
            const float *ap = a + size_t(bb) * m * k;
            const float *bp = b + size_t(bb) * k * n;
            float *c        = out->data_.data() + size_t(bb) * m * n;
            for (int i = 0; i < m; ++i) {
                for (int j = 0; j < n; ++j) {
                    double acc = 0.0;
                    for (int t = 0; t < k; ++t)
                        acc += double(ap[i * k + t]) * double(bp[t * n + j]);
                    c[i * n + j] = float(acc);
                }
            }
        }
        return out;
    }
    throw eve::Exception("Tensor.matmul: expected rank 2x2 or 3x3 (got %dx%d)", rank_, other->rank_);
}

Tensor *Tensor::transpose() const {
    ensureEager("transpose");
    if (rank_ != 2) throw eve::Exception("Tensor.transpose: expected rank 2");
    if (isQuantized()) {
        // Dequantize so the transposed result is a plain fp32 tensor (the
        // compiled graph path keeps the packed bytes and dequantizes in-kernel).
        const std::vector<float> f = dequantized();
        auto *t = new Tensor(dims_[1], dims_[0]);
        for (int i = 0; i < dims_[1]; ++i)
            for (int j = 0; j < dims_[0]; ++j)
                t->set2(i, j, f[static_cast<size_t>(j * dims_[1] + i)]);
        return t;
    }
    int order[] = {1, 0};
    return permute(order, 2);
}

Tensor *Tensor::permute(const int *order, int rank) const {
    ensureEager("permute");
    if (rank != rank_) throw eve::Exception("Tensor.permute: rank mismatch");
    if (isQuantized())
        throw eve::Exception("Tensor.permute: quantized tensor; dequantize first");
    int od[Tensor::kMaxRank] = {};
    for (int k = 0; k < rank; ++k) {
        if (order[k] < 0 || order[k] >= rank)
            throw eve::Exception("Tensor.permute: order out of range");
        od[k] = dims_[order[k]];
    }
    auto *out = new Tensor(dtype_, od, rank);
    kernels::permute(data_.data(), dims_, rank, order, out->data_.data(), od);
    return out;
}

Tensor *Tensor::reshape1(int d0) const {
    ensureEager("reshape1");
    if (d0 != size_) throw eve::Exception("Tensor.reshape1: size mismatch");
    auto *out  = new Tensor(dtype_, &d0, 1);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape2(int d0, int d1) const {
    ensureEager("reshape2");
    if (d0 * d1 != size_) throw eve::Exception("Tensor.reshape2: size mismatch");
    int d[] = {d0, d1};
    auto *out  = new Tensor(dtype_, d, 2);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape3(int d0, int d1, int d2) const {
    ensureEager("reshape3");
    if (d0 * d1 * d2 != size_) throw eve::Exception("Tensor.reshape3: size mismatch");
    int d[] = {d0, d1, d2};
    auto *out  = new Tensor(dtype_, d, 3);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape4(int d0, int d1, int d2, int d3) const {
    ensureEager("reshape4");
    if (d0 * d1 * d2 * d3 != size_) throw eve::Exception("Tensor.reshape4: size mismatch");
    int d[] = {d0, d1, d2, d3};
    auto *out  = new Tensor(dtype_, d, 4);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape5(int d0, int d1, int d2, int d3, int d4) const {
    ensureEager("reshape5");
    if (d0 * d1 * d2 * d3 * d4 != size_) throw eve::Exception("Tensor.reshape5: size mismatch");
    int d[] = {d0, d1, d2, d3, d4};
    auto *out  = new Tensor(dtype_, d, 5);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape6(int d0, int d1, int d2, int d3, int d4, int d5) const {
    ensureEager("reshape6");
    if (d0 * d1 * d2 * d3 * d4 * d5 != size_)
        throw eve::Exception("Tensor.reshape6: size mismatch");
    int d[] = {d0, d1, d2, d3, d4, d5};
    auto *out  = new Tensor(dtype_, d, 6);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::flatten() const { return reshape1(size_); }

}  // namespace eve::tensor
