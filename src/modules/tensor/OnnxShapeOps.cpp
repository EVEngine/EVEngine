#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include "tensor/OnnxInternal.h"

namespace eve::tensor::onnx_detail {
std::vector<int64_t> broadcast(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
    std::vector<int64_t> out(std::max(a.size(), b.size()), 1);
    for (size_t i = 0; i < out.size(); ++i) {
        const auto x = i < a.size() ? a[a.size() - 1 - i] : 1, y = i < b.size() ? b[b.size() - 1 - i] : 1;
        if (x != y && x != 1 && y != 1) throw Failure("Broadcast dimension mismatch");
        out[out.size() - 1 - i] = x == 1 ? y : x;
    }
    count(out);
    return out;
}
size_t broadcastIndex(size_t i, const std::vector<int64_t>& shape, const std::vector<int64_t>& output) {
    size_t index = 0, stride = 1;
    for (size_t j = 0; j < output.size(); ++j) {
        const auto coordinate = i % output[output.size() - 1 - j];
        i /= output[output.size() - 1 - j];
        if (j < shape.size()) {
            const auto d = shape[shape.size() - 1 - j];
            if (d != 1) index += coordinate * stride;
            stride *= d;
        }
    }
    return index;
}
std::optional<RuntimeTensor> executeShape(const Node& n, const std::vector<const RuntimeTensor*>& in) {
    const auto& x = required(in, 0);
    if (n.op == "Slice") {
        auto starts = ints(required(in, 1)), ends = ints(required(in, 2));
        auto axes = in.size() > 3 && in[3] ? ints(*in[3]) : std::vector<int64_t>(starts.size());
        if (in.size() <= 3 || !in[3]) std::iota(axes.begin(), axes.end(), 0);
        auto steps = in.size() > 4 && in[4] ? ints(*in[4]) : std::vector<int64_t>(starts.size(), 1);
        if (starts.size() != ends.size() || starts.size() != axes.size() || starts.size() != steps.size())
            throw Failure("Slice parameter lengths mismatch");
        auto                 shape = x.shape;
        std::vector<int64_t> begin(shape.size(), 0), step(shape.size(), 1);
        std::set<int>        seen;
        for (size_t j = 0; j < axes.size(); ++j) {
            int a = axis(axes[j], shape.size());
            if (!seen.insert(a).second || !steps[j] || steps[j] == INT64_MIN) throw Failure("Invalid Slice axis/step");
            const auto d = x.shape[a], st = steps[j];
            auto       norm = [d, st](int64_t v, bool end) {
                if (v < 0) v = v < -d ? (st < 0 ? -1 : 0) : v + d;
                return std::clamp(v, st < 0 && end ? int64_t(-1) : int64_t(0),
                                  st < 0 ? std::max(int64_t(0), d - 1) : d);
            };
            begin[a]               = norm(starts[j], false);
            const auto e           = norm(ends[j], true);
            step[a]                = st;
            const int64_t distance = st > 0 ? e - begin[a] : begin[a] - e, magnitude = st > 0 ? st : -st;
            shape[a] = d == 0 || distance <= 0 ? 0 : 1 + (distance - 1) / magnitude;
        }
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i) {
            size_t rest = i, source = 0, stride = 1;
            for (size_t j = shape.size(); j > 0; --j) {
                auto c = rest % shape[j - 1];
                rest /= shape[j - 1];
                source += (begin[j - 1] + c * step[j - 1]) * stride;
                stride *= x.shape[j - 1];
            }
            std::memcpy(out.bytes.data() + i * elementSize(x.element), x.bytes.data() + source * elementSize(x.element),
                        elementSize(x.element));
        }
        return out;
    }
    if (n.op == "Concat") {
        int  a     = axis(attr(n, "axis", 0), x.shape.size());
        auto shape = x.shape;
        shape[a]   = 0;
        for (auto* t : in) {
            if (!t || t->element != x.element || t->shape.size() != shape.size())
                throw Failure("Concat type/rank mismatch");
            for (size_t j = 0; j < shape.size(); ++j)
                if (j != a && t->shape[j] != shape[j]) throw Failure("Concat dimension mismatch");
            shape[a] += t->shape[a];
        }
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        size_t        outer = 1, inner = elementSize(x.element);
        for (int j = 0; j < a; ++j) outer *= shape[j];
        for (size_t j = a + 1; j < shape.size(); ++j) inner *= shape[j];
        size_t offset = 0;
        for (size_t o = 0; o < outer; ++o)
            for (auto* t : in) {
                const size_t size = t->shape[a] * inner;
                if (size) std::memcpy(out.bytes.data() + offset, t->bytes.data() + o * size, size);
                offset += size;
            }
        return out;
    }
    if (n.op == "Expand") {
        auto requested = ints(required(in, 1));
        count(requested);
        auto          shape = broadcast(x.shape, requested);
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i)
            std::memcpy(out.bytes.data() + i * elementSize(x.element),
                        x.bytes.data() + broadcastIndex(i, x.shape, shape) * elementSize(x.element),
                        elementSize(x.element));
        return out;
    }
    if (n.op == "ConstantOfShape") {
        auto          shape = ints(x);
        RuntimeTensor value = make(OnnxElement::Float32, {}, std::vector<float>{0});
        if (n.attrs.contains("value")) {
            if (!n.attrs.at("value").tensor) throw Failure("Missing fill value");
            value = *n.attrs.at("value").tensor;
        }
        if (count(value.shape) != 1) throw Failure("ConstantOfShape requires one fill value");
        RuntimeTensor out{value.element, shape, std::vector<uint8_t>(count(shape) * elementSize(value.element))};
        for (size_t i = 0; i < out.bytes.size(); i += value.bytes.size())
            std::memcpy(out.bytes.data() + i, value.bytes.data(), value.bytes.size());
        return out;
    }
    if (n.op == "Where") {
        const auto& a = required(in, 1);
        const auto& b = required(in, 2);
        if (x.element != OnnxElement::Bool || a.element != b.element) throw Failure("Where type mismatch");
        auto          shape = broadcast(broadcast(x.shape, a.shape), b.shape);
        const auto    size  = elementSize(a.element);
        RuntimeTensor out{a.element, shape, std::vector<uint8_t>(count(shape) * size)};
        for (size_t i = 0; i < count(shape); ++i) {
            const auto& v = x.bytes[broadcastIndex(i, x.shape, shape)] ? a : b;
            std::memcpy(out.bytes.data() + i * size, v.bytes.data() + broadcastIndex(i, v.shape, shape) * size, size);
        }
        return out;
    }
    if (n.op == "Not") {
        if (x.element != OnnxElement::Bool) throw Failure("Not requires boolean");
        auto out = x;
        for (auto& b : out.bytes) b = !b;
        return out;
    }
    if (n.op == "Equal" || n.op == "Less" || n.op == "Greater" || n.op == "And") {
        const auto& y = required(in, 1);
        if (x.element != y.element || (n.op == "And" && x.element != OnnxElement::Bool))
            throw Failure("Comparison type mismatch");
        auto          shape = broadcast(x.shape, y.shape);
        RuntimeTensor out{OnnxElement::Bool, shape, std::vector<uint8_t>(count(shape))};
        for (size_t i = 0; i < out.bytes.size(); ++i) {
            auto a = broadcastIndex(i, x.shape, shape), b = broadcastIndex(i, y.shape, shape);
            auto compare = [&](auto u, auto v) {
                return n.op == "Equal"     ? u == v
                       : n.op == "Less"    ? u < v
                       : n.op == "Greater" ? u > v
                                           : bool(u) && bool(v);
            };
            out.bytes[i] = x.element == OnnxElement::Float32 ? compare(read<float>(x, a), read<float>(y, b))
                                                             : compare(integer(x, a), integer(y, b));
        }
        return out;
    }
    if ((n.op == "Add" || n.op == "Sub" || n.op == "Mul" || n.op == "Div") && x.element != OnnxElement::Float32) {
        const auto& y = required(in, 1);
        if (x.element != y.element || (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32))
            throw Failure("Integer arithmetic dtype mismatch");
        auto          shape = broadcast(x.shape, y.shape);
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i) {
            int64_t a = integer(x, broadcastIndex(i, x.shape, shape)),
                    b = integer(y, broadcastIndex(i, y.shape, shape)), v = 0;
            if (n.op == "Add") {
                if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
                    throw Failure("Integer addition overflow");
                v = a + b;
            }
            if (n.op == "Sub") {
                if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
                    throw Failure("Integer subtraction overflow");
                v = a - b;
            }
            if (n.op == "Mul") {
                if (a && b &&
                    (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
                           : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)))
                    throw Failure("Integer multiplication overflow");
                v = a * b;
            }
            if (n.op == "Div") {
                if (!b || (a == INT64_MIN && b == -1)) throw Failure("Invalid integer division");
                v = a / b;
            }
            if (x.element == OnnxElement::Int32 && (v < INT32_MIN || v > INT32_MAX))
                throw Failure("Int32 arithmetic overflow");
            std::memcpy(out.bytes.data() + i * elementSize(x.element), &v, elementSize(x.element));
        }
        return out;
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail
