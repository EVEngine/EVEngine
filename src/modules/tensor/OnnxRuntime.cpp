#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <random>
#include <set>
#include "tensor/OnnxInternal.h"

namespace eve::tensor::onnx_detail {
namespace {
struct Value {
    const RuntimeTensor*                 borrowed = nullptr;
    std::shared_ptr<const RuntimeTensor> owned;
    std::shared_ptr<std::vector<Value>>  sequence;
    OnnxElement                          sequenceElement = OnnxElement::Float32;
    const RuntimeTensor&                 tensor() const {
        if (owned) return *owned;
        if (borrowed) return *borrowed;
        throw Failure("Expected tensor, received sequence");
    }
};
using Values = std::unordered_map<std::string, Value>;
struct Context {
    OnnxCompute*                                  compute;
    uint64_t                                      seed;
    bool                                          finite;
    size_t                                        steps = 0, liveBytes = 0;
    std::unordered_map<const Node*, std::mt19937> randomStreams;
};
Value hold(RuntimeTensor t, Context& c) {
    validate(t);
    if (c.finite && t.element == OnnxElement::Float32)
        for (size_t i = 0; i < count(t.shape); ++i)
            if (!std::isfinite(read<float>(t, i))) throw Failure("Nonfinite float output");
    const auto size = t.bytes.size();
    if (size > 512u * 1024u * 1024u - c.liveBytes) throw Failure("ONNX live tensor memory limit exceeded");
    // Context encloses every runtime Value; escaping public outputs are owning copies.
    auto* raw = new RuntimeTensor(std::move(t));
    c.liveBytes += size;
    return {nullptr,
            std::shared_ptr<const RuntimeTensor>(raw,
                                                 [&c, size](const RuntimeTensor* p) {
                                                     delete p;
                                                     c.liveBytes -= size;
                                                 }),
            {}};
}
std::set<std::string> captures(const ModelData& graph);
std::set<std::string> dependencies(const Node& n) {
    std::set<std::string> result;
    for (const auto& i : n.inputs)
        if (!i.empty()) result.insert(i);
    for (const auto& [key, a] : n.attrs)
        if (a.graph) {
            auto names = captures(*a.graph);
            result.insert(names.begin(), names.end());
        }
    return result;
}
std::set<std::string> captures(const ModelData& graph) {
    std::set<std::string> locals, result;
    for (const auto& [name, t] : graph.constants) locals.insert(name);
    for (const auto& [name, t] : graph.inputs) locals.insert(name);
    for (const auto& n : graph.nodes)
        for (const auto& out : n.outputs)
            if (!out.empty()) locals.insert(out);
    for (const auto& n : graph.nodes) {
        auto deps = dependencies(n);
        for (const auto& d : deps)
            if (!locals.contains(d)) result.insert(d);
    }
    for (const auto& out : graph.info.outputs)
        if (!locals.contains(out)) result.insert(out);
    return result;
}
void admit(const Node& n) {
    if (!isSupported(n))
        throw Failure(n.name + ": Unsupported ONNX node " + n.domain + "::" + n.op, DiagnosticCode::Unsupported,
                      n.name);
    for (const auto& [key, a] : n.attrs)
        if (a.graph)
            for (const auto& child : a.graph->nodes) admit(child);
}
std::vector<size_t> plan(const ModelData& g, const std::vector<std::string>& requested) {
    std::unordered_map<std::string, size_t> producers;
    for (size_t i = 0; i < g.nodes.size(); ++i)
        for (const auto& name : g.nodes[i].outputs)
            if (!name.empty() && !producers.emplace(name, i).second) throw Failure("Duplicate graph output");
    std::vector<bool>        selected(g.nodes.size(), false);
    std::vector<std::string> pending = requested;
    while (!pending.empty()) {
        auto name = std::move(pending.back());
        pending.pop_back();
        auto p = producers.find(name);
        if (p == producers.end() || selected[p->second]) continue;
        selected[p->second] = true;
        admit(g.nodes[p->second]);
        auto deps = dependencies(g.nodes[p->second]);
        pending.insert(pending.end(), deps.begin(), deps.end());
    }
    std::vector<size_t>                                                    indegree(g.nodes.size());
    std::vector<std::vector<size_t>>                                       users(g.nodes.size());
    size_t                                                                 total = 0;
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t i = 0; i < g.nodes.size(); ++i)
        if (selected[i]) {
            ++total;
            std::set<size_t> parents;
            for (const auto& name : dependencies(g.nodes[i])) {
                auto p = producers.find(name);
                if (p != producers.end()) parents.insert(p->second);
            }
            indegree[i] = parents.size();
            for (auto p : parents) users[p].push_back(i);
            if (parents.empty()) ready.push(i);
        }
    std::vector<size_t> order;
    while (!ready.empty()) {
        size_t i = ready.top();
        ready.pop();
        order.push_back(i);
        for (auto user : users[i])
            if (--indegree[user] == 0) ready.push(user);
    }
    if (order.size() != total) throw Failure("ONNX graph has cyclic lexical dependencies", DiagnosticCode::ParseError);
    return order;
}
std::vector<Value> run(const ModelData&, Values, const std::vector<std::string>&, Context&, size_t);
std::vector<Value> control(const Node& n, const std::vector<Value>& in, const Values& values, Context& c,
                           size_t depth) {
    auto scalar = [&](size_t i) {
        const auto& t = in.at(i).tensor();
        if (count(t.shape) != 1) throw Failure("Control input must contain one element");
        return integer(t);
    };
    auto seq = [&](size_t i) -> const std::vector<Value>& {
        if (!in.at(i).sequence) throw Failure("Expected sequence input");
        return *in[i].sequence;
    };
    auto sequence = [](std::vector<Value> v, OnnxElement type) {
        return Value{nullptr, {}, std::make_shared<std::vector<Value>>(std::move(v)), type};
    };
    if (n.op == "SequenceEmpty") {
        auto type = static_cast<OnnxElement>(attr(n, "dtype", 1));
        elementSize(type);
        return {sequence({}, type)};
    }
    if (n.op == "SequenceAt") {
        const auto& s = seq(0);
        int64_t     i = scalar(1);
        if (i < 0) i += static_cast<int64_t>(s.size());
        if (i < 0 || static_cast<size_t>(i) >= s.size()) throw Failure("SequenceAt out of bounds");
        return {s[i]};
    }
    if (n.op == "SequenceInsert") {
        auto s = seq(0);
        if (s.size() >= 100000) throw Failure("Sequence length limit exceeded");
        int64_t i = in.size() > 2 ? scalar(2) : static_cast<int64_t>(s.size());
        if (i < 0) i += static_cast<int64_t>(s.size());
        if (i < 0 || static_cast<size_t>(i) > s.size()) throw Failure("SequenceInsert out of bounds");
        const auto& t = in.at(1).tensor();
        if (in[0].sequenceElement != t.element) throw Failure("Sequence element dtype mismatch");
        s.insert(s.begin() + i, in[1]);
        return {sequence(std::move(s), in[0].sequenceElement)};
    }
    if (n.op == "SplitToSequence") {
        const auto&          x = in.at(0).tensor();
        int                  a = axis(attr(n, "axis", 0), x.shape.size());
        std::vector<int64_t> lengths;
        if (in.size() > 1 && (in[1].borrowed || in[1].owned)) {
            const auto& split = in[1].tensor();
            lengths           = ints(split);
            if (split.shape.empty()) {
                if (lengths[0] <= 0) throw Failure("Invalid split size");
                int64_t width = lengths[0];
                lengths.clear();
                for (int64_t i = 0; i < x.shape[a]; i += width) {
                    if (lengths.size() >= 100000) throw Failure("Sequence length limit exceeded");
                    lengths.push_back(std::min(width, x.shape[a] - i));
                }
            }
        } else
            lengths.assign(x.shape[a], 1);
        if (lengths.size() > 100000) throw Failure("Sequence length limit exceeded");
        int64_t total = 0;
        for (auto size : lengths) {
            if (size < 0 || size > x.shape[a] - total) throw Failure("Invalid split lengths");
            total += size;
        }
        if (total != x.shape[a]) throw Failure("Split lengths do not cover axis");
        size_t outer = 1, inner = elementSize(x.element);
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        std::vector<Value> s;
        size_t             offset = 0;
        for (auto length : lengths) {
            auto shape = x.shape;
            shape[a]   = length;
            if (!attr(n, "keepdims", 1)) {
                if (in.size() > 1 && (in[1].borrowed || in[1].owned))
                    throw Failure("keepdims=0 with explicit split unsupported", DiagnosticCode::Unsupported);
                shape.erase(shape.begin() + a);
            }
            RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
            for (size_t o = 0; o < outer; ++o)
                if (length)
                    std::memcpy(out.bytes.data() + o * length * inner,
                                x.bytes.data() + (o * x.shape[a] + offset) * inner, length * inner);
            s.push_back(hold(std::move(out), c));
            offset += length;
        }
        return {sequence(std::move(s), x.element)};
    }
    if (n.op == "ConcatFromSequence") {
        const auto& s = seq(0);
        if (s.empty()) throw Failure("Cannot concatenate empty sequence");
        std::vector<RuntimeTensor>        tensors;
        std::vector<const RuntimeTensor*> inputs;
        for (const auto& v : s) {
            tensors.push_back(v.tensor());
            if (attr(n, "new_axis", 0)) {
                const int a = axis(attr(n, "axis", 0), tensors.back().shape.size() + 1);
                tensors.back().shape.insert(tensors.back().shape.begin() + a, 1);
            }
        }
        for (const auto& t : tensors) inputs.push_back(&t);
        Node concat = n;
        concat.op   = "Concat";
        auto out    = executeShape(concat, inputs);
        if (!out) throw Failure("ConcatFromSequence dispatch failed");
        return {hold(std::move(*out), c)};
    }
    if (n.op == "If") {
        if (in.at(0).tensor().element != OnnxElement::Bool) throw Failure("If condition must be boolean");
        auto        key = scalar(0) ? "then_branch" : "else_branch";
        const auto& g   = n.attrs.at(key).graph;
        if (!g) throw Failure("Missing If branch");
        return run(*g, values, g->info.outputs, c, depth + 1);
    }
    if (n.op == "Loop") {
        const auto& body = n.attrs.at("body").graph;
        if (!body) throw Failure("Missing Loop body");
        if (in.size() < 2 || body->info.inputs.size() != in.size() || body->info.outputs.size() != in.size() - 1 ||
            n.outputs.size() != in.size() - 2)
            throw Failure("Loop scan outputs unsupported", DiagnosticCode::Unsupported);
        const int64_t trips = (in[0].borrowed || in[0].owned) ? scalar(0) : 10000;
        if (trips < 0 || trips > 10000) throw Failure("Loop trip budget exceeded");
        bool               condition = (in[1].borrowed || in[1].owned) ? scalar(1) != 0 : true;
        std::vector<Value> state(in.begin() + 2, in.end());
        for (int64_t i = 0; i < trips && condition; ++i) {
            auto scope                  = values;
            scope[body->info.inputs[0]] = hold(make(OnnxElement::Int64, {}, std::vector<int64_t>{i}), c);
            scope[body->info.inputs[1]] =
                hold(make(OnnxElement::Bool, {}, std::vector<uint8_t>{uint8_t(condition)}), c);
            for (size_t j = 0; j < state.size(); ++j) scope[body->info.inputs[j + 2]] = state[j];
            auto        result = run(*body, std::move(scope), body->info.outputs, c, depth + 1);
            const auto& cond   = result[0].tensor();
            if (cond.element != OnnxElement::Bool || count(cond.shape) != 1)
                throw Failure("Loop returned invalid condition");
            condition = integer(cond) != 0;
            state.assign(result.begin() + 1, result.end());
        }
        return state;
    }
    if (n.op == "RandomUniformLike" || n.op == "RandomNormalLike") {
        const auto& x = in.at(0).tensor();
        if (attr(n, "dtype", static_cast<int>(x.element)) != 1)
            throw Failure("Random output requires FP32", DiagnosticCode::Unsupported);
        uint64_t seed = c.seed;
        for (unsigned char ch : n.name) seed = (seed ^ ch) * 1099511628211ull;
        if (n.attrs.contains("seed")) {
            uint32_t bits;
            std::memcpy(&bits, &n.attrs.at("seed").real, 4);
            seed ^= bits;
        }
        auto [stream, inserted]    = c.randomStreams.try_emplace(&n, static_cast<uint32_t>(seed ^ (seed >> 32)));
        auto&              rng     = stream->second;
        auto               uniform = [&]() { return (double(rng()) + .5) / 4294967296.; };
        std::vector<float> v(count(x.shape));
        const float        low = n.attrs.contains("low") ? n.attrs.at("low").real : 0,
                    high       = n.attrs.contains("high") ? n.attrs.at("high").real : 1,
                    mean       = n.attrs.contains("mean") ? n.attrs.at("mean").real : 0,
                    scale      = n.attrs.contains("scale") ? n.attrs.at("scale").real : 1;
        if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(mean) || !std::isfinite(scale) ||
            high < low || scale < 0)
            throw Failure("Invalid random distribution parameters");
        for (auto& f : v)
            f = n.op == "RandomUniformLike" ? static_cast<float>(low + (high - low) * uniform())
                                            : static_cast<float>(mean + scale * std::sqrt(-2 * std::log(uniform())) *
                                                                            std::cos(6.283185307179586 * uniform()));
        return {hold(make(OnnxElement::Float32, x.shape, v), c)};
    }
    std::vector<const RuntimeTensor*> tensors;
    for (const auto& v : in) tensors.push_back(v.borrowed || v.owned ? &v.tensor() : nullptr);
    auto               outputs = execute(n, tensors, c.compute);
    std::vector<Value> result;
    for (auto& t : outputs) result.push_back(hold(std::move(t), c));
    return result;
}
std::vector<Value> run(const ModelData& g, Values values, const std::vector<std::string>& requested, Context& c,
                       size_t depth) {
    if (depth > 16) throw Failure("Execution graph nesting limit exceeded");
    for (const auto& [name, t] : g.constants)
        if (!g.inputs.contains(name) || !values.contains(name)) values[name] = {&t, {}, {}};
    auto                                    order = plan(g, requested);
    std::unordered_map<std::string, size_t> uses;
    for (auto i : order)
        for (const auto& dep : dependencies(g.nodes[i])) ++uses[dep];
    for (const auto& name : requested) ++uses[name];
    for (auto i : order) {
        const auto& n = g.nodes[i];
        if (++c.steps > 1000000) throw Failure("ONNX execution step budget exceeded");
        try {
            std::vector<Value> in;
            for (const auto& name : n.inputs) {
                if (name.empty()) {
                    in.push_back({});
                    continue;
                }
                auto it = values.find(name);
                if (it == values.end()) throw Failure("Missing feed or lexical capture: " + name);
                in.push_back(it->second);
            }
            auto outputs = control(n, in, values, c, depth);
            if (outputs.size() < n.outputs.size()) throw Failure("Operator output arity mismatch");
            for (size_t j = 0; j < n.outputs.size(); ++j)
                if (!n.outputs[j].empty() && uses[n.outputs[j]]) values[n.outputs[j]] = std::move(outputs[j]);
            for (const auto& dep : dependencies(n))
                if (--uses[dep] == 0) values.erase(dep);
        } catch (const Failure& e) {
            throw Failure(n.name + ": " + e.what(), e.code, e.path.empty() ? n.name : e.path);
        }
    }
    std::vector<Value> out;
    for (const auto& name : requested) {
        auto it = values.find(name);
        if (it == values.end()) throw Failure("Missing output: " + name, DiagnosticCode::NotFound);
        out.push_back(it->second);
    }
    return out;
}
}  // namespace
std::vector<OnnxNamedTensor> evaluate(const ModelData& model, std::span<const OnnxNamedTensor> feeds,
                                      const std::vector<std::string>& requested, OnnxCompute* compute,
                                      OnnxRunOptions options) {
    Context c{compute, options.seed, options.requireFinite};
    Values  inputs;
    for (const auto& f : feeds) inputs[f.name] = hold({f.tensor.element, f.tensor.shape, f.tensor.bytes}, c);
    auto                         outputs = run(model, std::move(inputs), requested, c, 0);
    std::vector<OnnxNamedTensor> result;
    size_t                       bytes = c.liveBytes;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& t = outputs[i].tensor();
        if (t.bytes.size() > 512u * 1024u * 1024u - bytes) throw Failure("ONNX output memory limit exceeded");
        bytes += t.bytes.size();
        result.push_back({requested[i], {t.element, t.shape, static_cast<std::vector<uint8_t>>(t.bytes)}});
    }
    return result;
}
}  // namespace eve::tensor::onnx_detail
