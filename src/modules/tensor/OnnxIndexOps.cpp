#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include "tensor/OnnxInternal.h"

namespace eve::tensor::onnx_detail {
std::optional<std::vector<RuntimeTensor>> executeIndex(const Node& n, const std::vector<const RuntimeTensor*>& in) {
    const auto& x   = required(in, 0);
    auto        one = [](RuntimeTensor t) { return std::vector<RuntimeTensor>{std::move(t)}; };
    if (n.op == "Range") {
        const auto& end   = required(in, 1);
        const auto& delta = required(in, 2);
        if (count(x.shape) != 1 || count(end.shape) != 1 || count(delta.shape) != 1 || x.element != end.element ||
            x.element != delta.element)
            throw Failure("Range requires matching scalars");
        if (x.element == OnnxElement::Float32) {
            const double start = read<float>(x, 0), stop = read<float>(end, 0), step = read<float>(delta, 0);
            if (!std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(step) || step == 0)
                throw Failure("Invalid Range");
            const double length = std::max(0., std::ceil((stop - start) / step));
            if (length > 128u * 1024u * 1024u) throw Failure("Range too large");
            std::vector<float> v(static_cast<size_t>(length));
            for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(start + i * step);
            return one(make(x.element, {static_cast<int64_t>(v.size())}, v));
        }
        if (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32)
            throw Failure("Range integer dtype unsupported");
        const int64_t start = integer(x), stop = integer(end), step = integer(delta);
        if (!step || step == INT64_MIN) throw Failure("Invalid Range step");
        uint64_t length = 0;
        if (step > 0 && stop > start) length = 1 + (uint64_t(stop) - uint64_t(start) - 1) / uint64_t(step);
        if (step < 0 && stop < start) length = 1 + (uint64_t(start) - uint64_t(stop) - 1) / uint64_t(-step);
        if (length > 128u * 1024u * 1024u) throw Failure("Range too large");
        RuntimeTensor out{
            x.element, {static_cast<int64_t>(length)}, std::vector<uint8_t>(length * elementSize(x.element))};
        int64_t v = start;
        for (size_t i = 0; i < length; ++i) {
            std::memcpy(out.bytes.data() + i * elementSize(x.element), &v, elementSize(x.element));
            if (i + 1 < length) v += step;
        }
        return one(std::move(out));
    }
    if (n.op == "TopK") {
        const auto& kt = required(in, 1);
        if (count(kt.shape) != 1) throw Failure("TopK requires one K");
        int           a = axis(attr(n, "axis", -1), x.shape.size());
        const int64_t k = integer(kt), size = x.shape[a];
        if (k < 0 || k > size) throw Failure("Invalid TopK K");
        size_t outer = 1, inner = 1;
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        auto shape = x.shape;
        shape[a]   = k;
        RuntimeTensor        values{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        std::vector<int64_t> indices(count(shape));
        for (size_t o = 0; o < outer; ++o)
            for (size_t i = 0; i < inner; ++i) {
                std::vector<int64_t> order(size);
                std::iota(order.begin(), order.end(), 0);
                std::stable_sort(order.begin(), order.end(), [&](int64_t aa, int64_t bb) {
                    auto         compare = [&](auto u, auto v) { return attr(n, "largest", 1) ? u > v : u < v; };
                    const size_t ai = (o * size + aa) * inner + i, bi = (o * size + bb) * inner + i;
                    return x.element == OnnxElement::Float32 ? compare(read<float>(x, ai), read<float>(x, bi))
                                                             : compare(integer(x, ai), integer(x, bi));
                });
                for (int64_t j = 0; j < k; ++j) {
                    size_t dst = (o * k + j) * inner + i, src = (o * size + order[j]) * inner + i;
                    indices[dst] = order[j];
                    std::memcpy(values.bytes.data() + dst * elementSize(x.element),
                                x.bytes.data() + src * elementSize(x.element), elementSize(x.element));
                }
            }
        return std::vector<RuntimeTensor>{std::move(values), make(OnnxElement::Int64, shape, indices)};
    }
    if (n.op == "ScatterElements" || n.op == "ScatterND") {
        const auto& idx     = required(in, 1);
        const auto& updates = required(in, 2);
        if (updates.element != x.element || (idx.element != OnnxElement::Int64 && idx.element != OnnxElement::Int32))
            throw Failure("Scatter dtype mismatch");
        auto       out   = x;
        const auto bytes = elementSize(x.element);
        if (n.op == "ScatterElements") {
            if (idx.shape != updates.shape || idx.shape.size() != x.shape.size())
                throw Failure("ScatterElements shape mismatch");
            int a = axis(attr(n, "axis", 0), x.shape.size());
            for (size_t j = 0; j < x.shape.size(); ++j)
                if (j != a && idx.shape[j] > x.shape[j]) throw Failure("Scatter index shape exceeds data");
            for (size_t i = 0; i < count(idx.shape); ++i) {
                size_t rest = i, source = 0, stride = 1;
                for (size_t j = x.shape.size(); j > 0; --j) {
                    int64_t c = rest % idx.shape[j - 1];
                    rest /= idx.shape[j - 1];
                    if (j - 1 == a) {
                        c = integer(idx, i);
                        if (c < 0) c += x.shape[j - 1];
                    }
                    if (c < 0 || c >= x.shape[j - 1]) throw Failure("Scatter index out of range");
                    source += c * stride;
                    stride *= x.shape[j - 1];
                }
                std::memcpy(out.bytes.data() + source * bytes, updates.bytes.data() + i * bytes, bytes);
            }
        } else {
            if (idx.shape.empty() || idx.shape.back() < 1 || idx.shape.back() > x.shape.size())
                throw Failure("ScatterND index rank mismatch");
            const size_t         depth = idx.shape.back();
            std::vector<int64_t> expected(idx.shape.begin(), idx.shape.end() - 1);
            expected.insert(expected.end(), x.shape.begin() + depth, x.shape.end());
            if (expected != updates.shape) throw Failure("ScatterND updates shape mismatch");
            size_t chunk = 1;
            for (size_t j = depth; j < x.shape.size(); ++j) chunk *= x.shape[j];
            for (size_t i = 0; i < count(idx.shape) / depth; ++i) {
                size_t target = 0;
                for (size_t j = 0; j < depth; ++j) {
                    auto c = integer(idx, i * depth + j);
                    if (c < 0) c += x.shape[j];
                    if (c < 0 || c >= x.shape[j]) throw Failure("ScatterND index out of range");
                    target = target * x.shape[j] + c;
                }
                if (chunk)
                    std::memcpy(out.bytes.data() + target * chunk * bytes, updates.bytes.data() + i * chunk * bytes,
                                chunk * bytes);
            }
        }
        return one(std::move(out));
    }
    if (n.op == "ReduceProd" || n.op == "ReduceMax") {
        auto axes = attrs(n, "axes", {});
        if (axes.empty())
            for (size_t i = 0; i < x.shape.size(); ++i) axes.push_back(i);
        std::set<int> reduced;
        for (auto a : axes)
            if (!reduced.insert(axis(a, x.shape.size())).second) throw Failure("Duplicate reduction axis");
        auto                 mapShape = x.shape;
        std::vector<int64_t> shape;
        for (size_t j = 0; j < x.shape.size(); ++j) {
            if (reduced.contains(static_cast<int>(j))) {
                mapShape[j] = 1;
                if (attr(n, "keepdims", 1)) shape.push_back(1);
            } else
                shape.push_back(x.shape[j]);
        }
        if (x.element == OnnxElement::Float32) {
            std::vector<float> out(count(shape), n.op == "ReduceProd" ? 1.f : -std::numeric_limits<float>::infinity());
            for (size_t i = 0; i < count(x.shape); ++i) {
                size_t      t = broadcastIndex(i, mapShape, x.shape);
                const float v = read<float>(x, i);
                out[t]        = n.op == "ReduceProd" ? out[t] * v : std::max(out[t], v);
            }
            return one(make(x.element, shape, out));
        }
        if (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32)
            throw Failure("Integer reduction dtype unsupported");
        std::vector<int64_t> out(count(shape), n.op == "ReduceProd" ? 1 : INT64_MIN);
        for (size_t i = 0; i < count(x.shape); ++i) {
            auto&      a = out[broadcastIndex(i, mapShape, x.shape)];
            const auto b = integer(x, i);
            if (n.op == "ReduceMax")
                a = std::max(a, b);
            else {
                if (a && b &&
                    (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
                           : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)))
                    throw Failure("ReduceProd overflow");
                a *= b;
            }
        }
        if (x.element == OnnxElement::Int64) return one(make(x.element, shape, out));
        std::vector<int32_t> narrow;
        for (auto v : out) {
            if (v < INT32_MIN || v > INT32_MAX) throw Failure("Int32 reduction overflow");
            narrow.push_back(static_cast<int32_t>(v));
        }
        return one(make(x.element, shape, narrow));
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail
