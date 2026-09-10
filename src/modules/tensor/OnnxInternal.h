#pragma once
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include "tensor/OnnxByteStorage.h"
#include "tensor/OnnxModel.h"

namespace eve::tensor::onnx_detail {
struct Failure : std::runtime_error {
    DiagnosticCode code;
    std::string    path;
    explicit Failure(std::string message, DiagnosticCode c = DiagnosticCode::InvalidArgument, std::string p = {})
        : std::runtime_error(std::move(message)), code(c), path(std::move(p)) {}
};
struct ModelData;
struct Attribute {
    int                          type    = 0;
    int64_t                      integer = 0;
    float                        real    = 0;
    std::string                  text;
    std::vector<int64_t>         integers;
    std::vector<float>           reals;
    std::vector<std::string>     strings;
    std::optional<RuntimeTensor> tensor;
    std::shared_ptr<ModelData>   graph;
};
struct Node {
    std::string                      name, op, domain;
    std::vector<std::string>         inputs, outputs;
    std::map<std::string, Attribute> attrs;
};
struct Input {
    int                  type = 0;
    std::vector<int64_t> shape;
};
struct ModelData {
    OnnxModelInfo                                  info;
    std::vector<Node>                              nodes;
    std::unordered_map<std::string, RuntimeTensor> constants;
    std::map<std::string, Input>                   inputs;
    std::map<std::string, int64_t>                 opsets;
};
inline size_t elementSize(OnnxElement e) {
    switch (e) {
        case OnnxElement::Float32:
        case OnnxElement::Int32: return 4;
        case OnnxElement::Int64: return 8;
        case OnnxElement::UInt8:
        case OnnxElement::Int8:
        case OnnxElement::Bool: return 1;
    }
    throw Failure("Unsupported ONNX element type", DiagnosticCode::Unsupported);
}
inline size_t count(const std::vector<int64_t>& shape) {
    if (shape.size() > 6) throw Failure("ONNX rank exceeds six", DiagnosticCode::Unsupported);
    size_t n = 1;
    for (int64_t d : shape) {
        if (d < 0 || d > INT32_MAX || (d && n > (128u * 1024u * 1024u) / static_cast<size_t>(d)))
            throw Failure("Invalid or excessive tensor shape");
        n *= static_cast<size_t>(d);
    }
    return n;
}
inline void validate(const RuntimeTensor& v) {
    if (count(v.shape) * elementSize(v.element) != v.bytes.size()) throw Failure("Tensor byte count mismatch");
}
template <class T>
T read(const RuntimeTensor& v, size_t i) {
    T x;
    std::memcpy(&x, v.bytes.data() + i * sizeof(T), sizeof(T));
    return x;
}
template <class T>
RuntimeTensor make(OnnxElement e, std::vector<int64_t> shape, const std::vector<T>& values) {
    RuntimeTensor v{e, std::move(shape), std::vector<uint8_t>(values.size() * sizeof(T))};
    if (!values.empty()) std::memcpy(v.bytes.data(), values.data(), v.bytes.size());
    validate(v);
    return v;
}
inline int64_t integer(const RuntimeTensor& v, size_t i = 0) {
    if (i >= count(v.shape)) throw Failure("Integer index out of bounds");
    switch (v.element) {
        case OnnxElement::Int64: return read<int64_t>(v, i);
        case OnnxElement::Int32: return read<int32_t>(v, i);
        case OnnxElement::Int8: return v.bytes[i] >= 128 ? int(v.bytes[i]) - 256 : v.bytes[i];
        case OnnxElement::UInt8:
        case OnnxElement::Bool: return v.bytes[i];
        default: throw Failure("Expected integer tensor");
    }
}
inline std::vector<float> floats(const RuntimeTensor& v) {
    if (v.element != OnnxElement::Float32) throw Failure("Expected FP32 tensor");
    std::vector<float> out(count(v.shape));
    if (!out.empty()) std::memcpy(out.data(), v.bytes.data(), v.bytes.size());
    return out;
}
inline std::vector<int64_t> ints(const RuntimeTensor& v) {
    std::vector<int64_t> out(count(v.shape));
    for (size_t i = 0; i < out.size(); ++i) out[i] = integer(v, i);
    return out;
}
inline int64_t attr(const Node& n, const char* key, int64_t fallback) {
    auto it = n.attrs.find(key);
    return it == n.attrs.end() ? fallback : it->second.integer;
}
inline std::vector<int64_t> attrs(const Node& n, const char* key, std::vector<int64_t> fallback) {
    auto it = n.attrs.find(key);
    return it == n.attrs.end() ? fallback : it->second.integers;
}
inline const RuntimeTensor& required(const std::vector<const RuntimeTensor*>& in, size_t i) {
    if (i >= in.size() || !in[i]) throw Failure("Missing required input");
    return *in[i];
}
inline int axis(int64_t a, size_t rank) {
    if (a < 0) a += rank;
    if (a < 0 || a >= static_cast<int64_t>(rank)) throw Failure("Invalid axis");
    return static_cast<int>(a);
}
std::vector<int64_t> broadcast(const std::vector<int64_t>& a, const std::vector<int64_t>& b);
size_t               broadcastIndex(size_t i, const std::vector<int64_t>& shape, const std::vector<int64_t>& output);
std::optional<std::vector<RuntimeTensor>> executeIndex(const Node&, const std::vector<const RuntimeTensor*>&);
std::optional<RuntimeTensor>              executeShape(const Node&, const std::vector<const RuntimeTensor*>&);
RuntimeTensor dispatchFloat(OnnxCompute&, const std::vector<const RuntimeTensor*>&, const std::vector<int64_t>&,
                            const std::string&, size_t work = 0);
std::optional<RuntimeTensor> executeMisc(const Node&, const std::vector<const RuntimeTensor*>&, OnnxCompute*);
std::optional<RuntimeTensor> executeNeural(const Node&, const std::vector<const RuntimeTensor*>&, OnnxCompute*);
std::optional<RuntimeTensor> executeNumeric(const Node&, const std::vector<const RuntimeTensor*>&, OnnxCompute*);
std::vector<OnnxNamedTensor> evaluate(const ModelData&, std::span<const OnnxNamedTensor>,
                                      const std::vector<std::string>&, OnnxCompute*, OnnxRunOptions options);
ModelData                    parse(std::span<const uint8_t> bytes);
bool                         isSupported(const Node& node);
std::vector<RuntimeTensor>   execute(const Node& node, const std::vector<const RuntimeTensor*>& inputs,
                                     OnnxCompute* compute = nullptr);
std::vector<RuntimeTensor>   executeQuant(const Node& node, const std::vector<const RuntimeTensor*>& inputs,
                                          OnnxCompute* compute = nullptr);
std::vector<RuntimeTensor>   executeQuantLstm(const Node& node, const std::vector<const RuntimeTensor*>& inputs,
                                              OnnxCompute* compute = nullptr);
}  // namespace eve::tensor::onnx_detail
