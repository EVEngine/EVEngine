#include "tensor/Tensor.h"
#include "tensor/Graph.h"

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

void Tensor::initDims(const int *dims, int rank) {
    if (rank < 1 || rank > kMaxRank)
        throw eve::Exception("Tensor: rank must be 1..%d", kMaxRank);
    rank_ = rank;
    for (int i = 0; i < kMaxRank; ++i) dims_[i] = 0;
    for (int i = 0; i < rank; ++i) dims_[i] = dims[i];
    size_ = productLocal(dims, rank);
    data_.assign(static_cast<size_t>(size_), 0.f);
    device_ = "cpu";
    kind_   = Kind::Eager;
    graph_  = nullptr;
    nodeId_ = -1;
}

Tensor::Tensor(const int *dims, int rank) { initDims(dims, rank); }
Tensor::Tensor(int d0) {
    int d[] = {d0};
    initDims(d, 1);
}
Tensor::Tensor(int d0, int d1) {
    int d[] = {d0, d1};
    initDims(d, 2);
}
Tensor::Tensor(int d0, int d1, int d2) {
    int d[] = {d0, d1, d2};
    initDims(d, 3);
}
Tensor::Tensor(int d0, int d1, int d2, int d3) {
    int d[] = {d0, d1, d2, d3};
    initDims(d, 4);
}

Tensor *Tensor::makeSymbolic(Graph *graph, int nodeId, const int *dims, int rank) {
    auto *t   = new Tensor();
    t->kind_  = Kind::Symbolic;
    t->graph_ = graph;
    t->nodeId_ = nodeId;
    t->rank_  = rank;
    for (int i = 0; i < kMaxRank; ++i) t->dims_[i] = 0;
    for (int i = 0; i < rank; ++i) t->dims_[i] = dims[i];
    t->size_   = productLocal(dims, rank);
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

float *Tensor::data() {
    ensureEager("data");
    return data_.data();
}
const float *Tensor::data() const {
    ensureEager("data");
    return data_.data();
}

float Tensor::get(int flatIndex) const {
    ensureEager("get");
    if (flatIndex < 0 || flatIndex >= size_) throw eve::Exception("Tensor.get: index out of range");
    return data_[static_cast<size_t>(flatIndex)];
}

void Tensor::set(int flatIndex, float value) {
    ensureEager("set");
    if (flatIndex < 0 || flatIndex >= size_) throw eve::Exception("Tensor.set: index out of range");
    data_[static_cast<size_t>(flatIndex)] = value;
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
}

Tensor *Tensor::clone() const {
    ensureEager("clone");
    auto *out = new Tensor(dims_, rank_);
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

#define EVE_TENSOR_BINARY_OP(name, expr)                     \
    Tensor *Tensor::name(const Tensor *other) const {        \
        ensureEager(#name);                                  \
        other->ensureEager(#name);                           \
        checkSameShape(other, #name);                        \
        auto *out      = new Tensor(dims_, rank_);           \
        const float *a = data_.data();                       \
        const float *b = other->data_.data();                \
        float *o       = out->data_.data();                  \
        for (int i = 0; i < size_; ++i) {                    \
            float x = a[i];                                  \
            float y = b[i];                                  \
            o[i]    = (expr);                                \
        }                                                    \
        return out;                                          \
    }

EVE_TENSOR_BINARY_OP(add, x + y)
EVE_TENSOR_BINARY_OP(sub, x - y)
EVE_TENSOR_BINARY_OP(multiply, x * y)
EVE_TENSOR_BINARY_OP(div, x / y)

#undef EVE_TENSOR_BINARY_OP

#define EVE_TENSOR_UNARY_SCALAR(name, expr)     \
    Tensor *Tensor::name(float s) const {       \
        ensureEager(#name);                     \
        auto *out      = new Tensor(dims_, rank_); \
        const float *a = data_.data();          \
        float *o       = out->data_.data();     \
        for (int i = 0; i < size_; ++i) {       \
            float x = a[i];                     \
            o[i]    = (expr);                   \
        }                                       \
        return out;                             \
    }

EVE_TENSOR_UNARY_SCALAR(addScalar, x + s)
EVE_TENSOR_UNARY_SCALAR(subScalar, x - s)
EVE_TENSOR_UNARY_SCALAR(mulScalar, x * s)
EVE_TENSOR_UNARY_SCALAR(divScalar, x / s)
EVE_TENSOR_UNARY_SCALAR(powScalar, std::pow(x, s))
EVE_TENSOR_UNARY_SCALAR(maximumScalar, std::max(x, s))
EVE_TENSOR_UNARY_SCALAR(minimumScalar, std::min(x, s))

#undef EVE_TENSOR_UNARY_SCALAR

#define EVE_TENSOR_UNARY(name, expr)            \
    Tensor *Tensor::name() const {              \
        ensureEager(#name);                     \
        auto *out      = new Tensor(dims_, rank_); \
        const float *a = data_.data();          \
        float *o       = out->data_.data();     \
        for (int i = 0; i < size_; ++i) {       \
            float x = a[i];                     \
            o[i]    = (expr);                   \
        }                                       \
        return out;                             \
    }

EVE_TENSOR_UNARY(neg, -x)
EVE_TENSOR_UNARY(abs, std::fabs(x))
EVE_TENSOR_UNARY(sqrt, std::sqrt(x))
EVE_TENSOR_UNARY(exp, std::exp(x))
EVE_TENSOR_UNARY(log, std::log(x))
EVE_TENSOR_UNARY(sin, std::sin(x))
EVE_TENSOR_UNARY(cos, std::cos(x))
EVE_TENSOR_UNARY(tanh, std::tanh(x))
EVE_TENSOR_UNARY(relu, x > 0.f ? x : 0.f)
EVE_TENSOR_UNARY(sigmoid, 1.f / (1.f + std::exp(-x)))

#undef EVE_TENSOR_UNARY

Tensor *Tensor::clamp(float lo, float hi) const {
    ensureEager("clamp");
    if (lo > hi) std::swap(lo, hi);
    auto *out      = new Tensor(dims_, rank_);
    const float *a = data_.data();
    float *o       = out->data_.data();
    for (int i = 0; i < size_; ++i) o[i] = std::clamp(a[i], lo, hi);
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
    if (rank_ != 2 || other->rank_ != 2) throw eve::Exception("Tensor.matmul: both must be rank 2");
    int m = dims_[0], k = dims_[1], n = other->dims_[1];
    if (k != other->dims_[0]) throw eve::Exception("Tensor.matmul: inner dims mismatch");
    auto *out      = new Tensor(m, n);
    const float *a = data_.data();
    const float *b = other->data_.data();
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

Tensor *Tensor::transpose() const {
    ensureEager("transpose");
    if (rank_ != 2) throw eve::Exception("Tensor.transpose: expected rank 2");
    int r = dims_[0], c = dims_[1];
    auto *out = new Tensor(c, r);
    for (int i = 0; i < r; ++i)
        for (int j = 0; j < c; ++j) out->set2(j, i, get2(i, j));
    return out;
}

Tensor *Tensor::reshape1(int d0) const {
    ensureEager("reshape1");
    if (d0 != size_) throw eve::Exception("Tensor.reshape1: size mismatch");
    auto *out  = new Tensor(d0);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape2(int d0, int d1) const {
    ensureEager("reshape2");
    if (d0 * d1 != size_) throw eve::Exception("Tensor.reshape2: size mismatch");
    auto *out  = new Tensor(d0, d1);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape3(int d0, int d1, int d2) const {
    ensureEager("reshape3");
    if (d0 * d1 * d2 != size_) throw eve::Exception("Tensor.reshape3: size mismatch");
    auto *out  = new Tensor(d0, d1, d2);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::reshape4(int d0, int d1, int d2, int d3) const {
    ensureEager("reshape4");
    if (d0 * d1 * d2 * d3 != size_) throw eve::Exception("Tensor.reshape4: size mismatch");
    auto *out  = new Tensor(d0, d1, d2, d3);
    out->data_ = data_;
    return out;
}

Tensor *Tensor::flatten() const { return reshape1(size_); }

}  // namespace eve::tensor
