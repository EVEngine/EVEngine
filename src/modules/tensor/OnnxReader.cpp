#include "tensor/OnnxInternal.h"

#include <bit>
#include <functional>
#include <set>

namespace eve::tensor::onnx_detail {
namespace {
struct Field {
    int                      id = 0, wire = 0;
    uint64_t                 number = 0;
    std::span<const uint8_t> bytes;
    std::string              text() const { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }
};
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    bool     empty() const { return offset_ == data_.size(); }
    uint64_t varint() {
        uint64_t n = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (offset_ == data_.size()) throw Failure("Truncated protobuf varint", DiagnosticCode::ParseError);
            const uint8_t b = data_[offset_++];
            if (shift == 63 && b > 1) throw Failure("Overflowing protobuf varint", DiagnosticCode::ParseError);
            n |= static_cast<uint64_t>(b & 127) << shift;
            if (!(b & 128)) return n;
        }
        throw Failure("Invalid protobuf varint", DiagnosticCode::ParseError);
    }
    std::span<const uint8_t> take(uint64_t n) {
        if (n > data_.size() - offset_) throw Failure("Truncated protobuf field", DiagnosticCode::ParseError);
        auto result = data_.subspan(offset_, static_cast<size_t>(n));
        offset_ += static_cast<size_t>(n);
        return result;
    }
    Field next() {
        const uint64_t tag = varint();
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffff)
            throw Failure("Invalid protobuf tag", DiagnosticCode::ParseError);
        Field f;
        f.id   = static_cast<int>(tag >> 3);
        f.wire = tag & 7;
        switch (f.wire) {
            case 0: f.number = varint(); break;
            case 1: f.bytes = take(8); break;
            case 2: f.bytes = take(varint()); break;
            case 5: f.bytes = take(4); break;
            default: throw Failure("Unsupported protobuf wire type", DiagnosticCode::ParseError);
        }
        return f;
    }

private:
    std::span<const uint8_t> data_;
    size_t                   offset_ = 0;
};
void wire(const Field& f, int expected) {
    if (f.wire != expected) throw Failure("Unexpected protobuf field type", DiagnosticCode::ParseError);
}
void integers(const Field& f, std::vector<int64_t>& out) {
    if (f.wire == 0)
        out.push_back(std::bit_cast<int64_t>(f.number));
    else {
        wire(f, 2);
        Reader r(f.bytes);
        while (!r.empty()) out.push_back(std::bit_cast<int64_t>(r.varint()));
    }
}
void reals(const Field& f, std::vector<float>& out) {
    if (f.wire != 2 && f.wire != 5) throw Failure("Invalid float wire type", DiagnosticCode::ParseError);
    if (f.bytes.size() % 4) throw Failure("Truncated float payload", DiagnosticCode::ParseError);
    for (size_t i = 0; i < f.bytes.size(); i += 4) {
        float x;
        std::memcpy(&x, f.bytes.data() + i, 4);
        out.push_back(x);
    }
}
std::pair<std::string, RuntimeTensor> tensor(std::span<const uint8_t> data) {
    Reader               r(data);
    RuntimeTensor        v;
    std::string          name;
    std::vector<int64_t> iv;
    std::vector<float>   fv;
    bool                 raw = false, type = false;
    while (!r.empty()) {
        auto f = r.next();
        switch (f.id) {
            case 1: integers(f, v.shape); break;
            case 2:
                wire(f, 0);
                if (f.number > 9) throw Failure("Unsupported tensor element type", DiagnosticCode::Unsupported);
                v.element = static_cast<OnnxElement>(f.number);
                type      = true;
                break;
            case 4: reals(f, fv); break;
            case 5:
            case 7: integers(f, iv); break;
            case 8:
                wire(f, 2);
                name = f.text();
                break;
            case 9:
                wire(f, 2);
                if (raw) throw Failure("Duplicate raw tensor data", DiagnosticCode::ParseError);
                v.bytes.assign(f.bytes.begin(), f.bytes.end());
                raw = true;
                break;
            case 14:
                wire(f, 0);
                if (f.number != 0) throw Failure("External tensor data unsupported", DiagnosticCode::Unsupported);
                break;
            case 3:
            case 6:
            case 10:
            case 11:
            case 13: throw Failure("External, segmented or unsupported tensor data", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (!type) throw Failure("Tensor has no dtype", DiagnosticCode::ParseError);
    const size_t n = count(v.shape), size = elementSize(v.element);
    if (raw && (!iv.empty() || !fv.empty())) throw Failure("Ambiguous tensor payload", DiagnosticCode::ParseError);
    if (!raw) {
        if (v.element == OnnxElement::Float32) {
            if (fv.size() != n || !iv.empty())
                throw Failure("Invalid float tensor payload", DiagnosticCode::ParseError);
            v.bytes.resize(n * size);
            if (n) std::memcpy(v.bytes.data(), fv.data(), v.bytes.size());
        } else {
            if (iv.size() != n || !fv.empty())
                throw Failure("Invalid integer tensor payload", DiagnosticCode::ParseError);
            v.bytes.resize(n * size);
            for (size_t i = 0; i < n; ++i) {
                const auto x = iv[i];
                if ((v.element == OnnxElement::UInt8 && (x < 0 || x > 255)) ||
                    (v.element == OnnxElement::Int8 && (x < -128 || x > 127)) ||
                    (v.element == OnnxElement::Bool && x != 0 && x != 1) ||
                    (v.element == OnnxElement::Int32 && (x < INT32_MIN || x > INT32_MAX)))
                    throw Failure("Integer initializer out of range", DiagnosticCode::ParseError);
                std::memcpy(v.bytes.data() + i * size, &x, size);
            }
        }
    }
    validate(v);
    return {std::move(name), std::move(v)};
}
ModelData                         parseGraph(std::span<const uint8_t> bytes, size_t depth);
std::pair<std::string, Attribute> attribute(std::span<const uint8_t> bytes, size_t depth) {
    Reader      r(bytes);
    Attribute   a;
    std::string name;
    while (!r.empty()) {
        auto f = r.next();
        switch (f.id) {
            case 1:
                wire(f, 2);
                name = f.text();
                break;
            case 20:
                wire(f, 0);
                a.type = static_cast<int>(f.number);
                break;
            case 2: {
                wire(f, 5);
                std::memcpy(&a.real, f.bytes.data(), 4);
                break;
            }
            case 3:
                wire(f, 0);
                a.integer = std::bit_cast<int64_t>(f.number);
                break;
            case 4:
                wire(f, 2);
                a.text = f.text();
                break;
            case 5:
                wire(f, 2);
                a.tensor = tensor(f.bytes).second;
                break;
            case 7: reals(f, a.reals); break;
            case 8: integers(f, a.integers); break;
            case 9:
                wire(f, 2);
                a.strings.push_back(f.text());
                break;
            case 6:
                wire(f, 2);
                a.graph = std::make_shared<ModelData>(parseGraph(f.bytes, depth + 1));
                break;
            case 11: throw Failure("Multiple graph attributes unsupported", DiagnosticCode::Unsupported);
            case 21: throw Failure("Function attribute references unsupported", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (name.empty() || !a.type) throw Failure("Incomplete attribute", DiagnosticCode::ParseError);
    return {std::move(name), std::move(a)};
}
Node node(std::span<const uint8_t> bytes, size_t depth) {
    Reader r(bytes);
    Node   n;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id >= 1 && f.id <= 5) wire(f, 2);
        switch (f.id) {
            case 1: n.inputs.push_back(f.text()); break;
            case 2: n.outputs.push_back(f.text()); break;
            case 3: n.name = f.text(); break;
            case 4: n.op = f.text(); break;
            case 5:
                if (!n.attrs.insert(attribute(f.bytes, depth)).second)
                    throw Failure("Duplicate attribute", DiagnosticCode::ParseError);
                break;
            case 7:
                wire(f, 2);
                n.domain = f.text();
                break;
            case 8: throw Failure("Function overloads unsupported", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (n.op.empty() || n.outputs.empty()) throw Failure("Incomplete node", DiagnosticCode::ParseError);
    return n;
}
std::pair<std::string, Input> input(std::span<const uint8_t> bytes) {
    Reader      r(bytes);
    std::string name;
    Input       value;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id == 1) {
            wire(f, 2);
            name = f.text();
        }
        if (f.id == 2) {
            wire(f, 2);
            Reader type(f.bytes);
            while (!type.empty()) {
                auto t = type.next();
                if (t.id == 4) {
                    value.type = 0;
                    continue;
                }
                if (t.id != 1) throw Failure("Non-tensor graph input", DiagnosticCode::Unsupported);
                wire(t, 2);
                Reader tt(t.bytes);
                while (!tt.empty()) {
                    auto p = tt.next();
                    if (p.id == 1) {
                        wire(p, 0);
                        value.type = static_cast<int>(p.number);
                    }
                    if (p.id == 2) {
                        wire(p, 2);
                        Reader shape(p.bytes);
                        while (!shape.empty()) {
                            auto d = shape.next();
                            if (d.id != 1) continue;
                            wire(d, 2);
                            Reader  dim(d.bytes);
                            int64_t v = -1;
                            while (!dim.empty()) {
                                auto x = dim.next();
                                if (x.id == 1) {
                                    wire(x, 0);
                                    v = std::bit_cast<int64_t>(x.number);
                                }
                            }
                            value.shape.push_back(v);
                        }
                    }
                }
            }
        }
    }
    if (name.empty()) throw Failure("Empty value name", DiagnosticCode::ParseError);
    if (value.shape.size() > 6) throw Failure("Input rank exceeds six", DiagnosticCode::Unsupported);
    for (auto d : value.shape)
        if (d < -1 || d > INT32_MAX) throw Failure("Invalid interface dimension", DiagnosticCode::ParseError);
    return {name, value};
}
ModelData parseGraph(std::span<const uint8_t> bytes, size_t depth) {
    ModelData model;
    if (depth > 16) throw Failure("ONNX graph nesting limit exceeded", DiagnosticCode::ParseError);
    Reader g(bytes);
    while (!g.empty()) {
        auto f = g.next();
        if (f.id == 1) {
            wire(f, 2);
            if (model.nodes.size() >= 100000) throw Failure("Node limit exceeded", DiagnosticCode::ParseError);
            model.nodes.push_back(node(f.bytes, depth));
        }
        if (f.id == 5) {
            wire(f, 2);
            auto t = tensor(f.bytes);
            t.second.bytes.retainAcrossRuns();
            if (t.first.empty() || !model.constants.emplace(std::move(t)).second)
                throw Failure("Duplicate/empty initializer", DiagnosticCode::ParseError);
        }
        if (f.id == 11) {
            wire(f, 2);
            auto v = input(f.bytes);
            model.info.inputs.push_back(v.first);
            if (!model.inputs.emplace(std::move(v)).second)
                throw Failure("Duplicate graph input", DiagnosticCode::ParseError);
        }
        if (f.id == 12) {
            wire(f, 2);
            model.info.outputs.push_back(input(f.bytes).first);
        }
        if (f.id == 15) throw Failure("Sparse initializers unsupported", DiagnosticCode::Unsupported);
    }
    return model;
}
}  // namespace

ModelData parse(std::span<const uint8_t> bytes) {
    if constexpr (std::endian::native != std::endian::little)
        throw Failure("ONNX requires little-endian host", DiagnosticCode::Unsupported);
    if (bytes.empty() || bytes.size() > 512u * 1024u * 1024u)
        throw Failure("Model size exceeds import limit", DiagnosticCode::ParseError);
    ModelData                model;
    Reader                   r(bytes);
    std::span<const uint8_t> graph;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id == 1) {
            wire(f, 0);
            model.info.irVersion = static_cast<int64_t>(f.number);
        }
        if (f.id == 7) {
            wire(f, 2);
            if (!graph.empty()) throw Failure("Duplicate graph", DiagnosticCode::ParseError);
            graph = f.bytes;
        }
        if (f.id == 8) {
            wire(f, 2);
            Reader      ops(f.bytes);
            std::string domain;
            int64_t     version = 0;
            while (!ops.empty()) {
                auto x = ops.next();
                if (x.id == 1) {
                    wire(x, 2);
                    domain = x.text();
                }
                if (x.id == 2) {
                    wire(x, 0);
                    version = x.number;
                }
            }
            if (!model.opsets.emplace(domain, version).second)
                throw Failure("Duplicate opset", DiagnosticCode::ParseError);
        }
        if (f.id == 20 || f.id == 25 || f.id == 26)
            throw Failure("Training/functions/device configuration unsupported", DiagnosticCode::Unsupported);
    }
    if (model.info.irVersion < 3 || model.info.irVersion > 10 || !model.opsets.contains("") ||
        model.opsets.at("") < 13 || model.opsets.at("") > 17)
        throw Failure("Unsupported ONNX IR/opset version", DiagnosticCode::UnknownVersion);
    if (model.opsets.contains("com.microsoft") && model.opsets.at("com.microsoft") != 1)
        throw Failure("Unsupported Microsoft opset", DiagnosticCode::UnknownVersion);
    if (graph.empty()) throw Failure("Missing ONNX graph", DiagnosticCode::ParseError);
    auto parsed        = parseGraph(graph, 0);
    model.nodes        = std::move(parsed.nodes);
    model.constants    = std::move(parsed.constants);
    model.inputs       = std::move(parsed.inputs);
    model.info.inputs  = std::move(parsed.info.inputs);
    model.info.outputs = std::move(parsed.info.outputs);
    std::set<std::string> names;
    for (const auto& [name, v] : model.constants) {
        names.insert(name);
        model.info.initializerBytes += v.bytes.size();
    }
    for (const auto& [name, v] : model.inputs) names.insert(name);
    for (const auto& n : model.nodes) {
        if (!model.opsets.contains(n.domain)) throw Failure("Node domain has no opset", DiagnosticCode::ParseError);
        for (const auto& name : n.inputs)
            if (!name.empty() && !names.contains(name))
                throw Failure("Input not in topological order: " + name, DiagnosticCode::ParseError);
        for (const auto& name : n.outputs)
            if (!name.empty() && !names.insert(name).second)
                throw Failure("Duplicate graph value: " + name, DiagnosticCode::ParseError);
        if (!isSupported(n)) model.info.unsupportedNodes.push_back(n.name + " [" + n.domain + "::" + n.op + "]");
    }
    for (const auto& out : model.info.outputs)
        if (!names.contains(out)) throw Failure("Undefined graph output", DiagnosticCode::ParseError);
    size_t                                                                    recursiveNodes = 0;
    std::function<void(const ModelData&, const std::set<std::string>&, bool)> validateNested;
    validateNested = [&](const ModelData& g, const std::set<std::string>& outer, bool root) {
        recursiveNodes += g.nodes.size();
        if (recursiveNodes > 100000) throw Failure("Recursive node limit exceeded", DiagnosticCode::ParseError);
        auto                  available = outer;
        std::set<std::string> locals;
        for (const auto& [name, t] : g.constants) locals.insert(name);
        for (const auto& [name, t] : g.inputs) locals.insert(name);
        for (const auto& n : g.nodes)
            for (const auto& out : n.outputs)
                if (!out.empty() && !locals.insert(out).second)
                    throw Failure("Duplicate nested graph value: " + out, DiagnosticCode::ParseError);
        available.insert(locals.begin(), locals.end());
        for (const auto& n : g.nodes) {
            if (!model.opsets.contains(n.domain))
                throw Failure("Nested node domain has no opset", DiagnosticCode::ParseError);
            for (const auto& input : n.inputs)
                if (!input.empty() && !available.contains(input))
                    throw Failure("Undefined lexical capture: " + input, DiagnosticCode::ParseError);
            if (!root && !isSupported(n))
                model.info.unsupportedNodes.push_back(n.name + " [" + n.domain + "::" + n.op + "]");
            for (const auto& [key, a] : n.attrs)
                if (a.graph) validateNested(*a.graph, available, false);
        }
        for (const auto& out : g.info.outputs)
            if (!available.contains(out)) throw Failure("Undefined nested output: " + out, DiagnosticCode::ParseError);
    };
    validateNested(model, {}, true);
    model.info.nodeCount = model.nodes.size();
    return model;
}
}  // namespace eve::tensor::onnx_detail
