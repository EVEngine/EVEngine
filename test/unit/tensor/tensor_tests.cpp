// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "tensor_llm.cpp",
    "tensor.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor_llm)
#include "PrivateTestScope.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "tensor/AffineQuant.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/OnnxByteStorage.h"
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxModel.h"
#include "tensor/Optimizer.h"
#include "tensor/Quant.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor_llm.cpp
namespace test_tensor_llm_unit {
// tensor_llm.cpp
using namespace eve::tensor;

namespace {

constexpr int kVocab   = 50257;
constexpr int kEmbDim  = 64;
constexpr int kHeads   = 16;
constexpr int kHeadDim = kEmbDim / kHeads;  // 4
constexpr int kLayers  = 8;
constexpr int kInner   = 256;
constexpr int kEos     = 50256;
constexpr int kGenToks = 24;

[[maybe_unused]] std::string assetPath(const std::string &name) {
    return std::string(EVENGINE_SOURCE_DIR) + "/test/assets/mini_llm/" + name;
}

struct Weight {
    std::vector<int>   dims;
    std::vector<float> data;
};

struct Model {
    std::map<std::string, Weight> weights;
    std::vector<std::string>      tokens;  // id -> decoded text
    std::vector<float>            prompt;
    std::vector<float>            reference;

    const Weight &w(const std::string &name) const { return weights.at(name); }
};

[[maybe_unused]] bool loadFile(const std::string &path, std::vector<char> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    f.read(out.data(), n);
    return !f.fail() || n == 0;
}

[[maybe_unused]] bool loadModel(Model &m) {
    std::vector<char> buf;
    if (!loadFile(assetPath("tinystories.bin"), buf)) return false;
    const char *p   = buf.data();
    const char *end = p + buf.size();
    if (end - p < 7 || std::memcmp(p, "EVLLM11", 7) != 0) {
        std::fprintf(stderr, "[llm] bad magic %.7s\n", p);
        return false;
    }
    p += 7;
    int count = 0;
    if (end - p < 4) return false;
    std::memcpy(&count, p, 4);
    p += 4;
    for (int i = 0; i < count; ++i) {
        int nameLen = 0;
        if (end - p < 4) return false;
        std::memcpy(&nameLen, p, 4);
        p += 4;
        if (end - p < nameLen) return false;
        std::string name(p, static_cast<size_t>(nameLen));
        p += nameLen;
        int rank = 0;
        if (end - p < 4) return false;
        std::memcpy(&rank, p, 4);
        p += 4;
        Weight wt;
        wt.dims.resize(static_cast<size_t>(rank));
        if (end - p < rank * 4) return false;
        std::memcpy(wt.dims.data(), p, static_cast<size_t>(rank) * 4);
        p += static_cast<size_t>(rank) * 4;
        long long bytes = 0;
        if (end - p < 8) return false;
        std::memcpy(&bytes, p, 8);
        p += 8;
        if (bytes <= 0 || end - p < bytes || (bytes % 4) != 0) return false;
        wt.data.resize(static_cast<size_t>(bytes) / 4);
        std::memcpy(wt.data.data(), p, static_cast<size_t>(bytes));
        p += bytes;
        m.weights[name] = std::move(wt);
    }

    if (!loadFile(assetPath("tokens.bin"), buf)) return false;
    p            = buf.data();
    end          = p + buf.size();
    int tokCount = 0;
    std::memcpy(&tokCount, p, 4);
    p += 4;
    m.tokens.reserve(static_cast<size_t>(tokCount));
    for (int i = 0; i < tokCount; ++i) {
        int len = 0;
        std::memcpy(&len, p, 4);
        p += 4;
        m.tokens.emplace_back(p, static_cast<size_t>(len));
        p += len;
    }
    return true;
}

[[maybe_unused]] std::vector<float> readCsvInts(const std::string &path) {
    std::vector<char> buf;
    if (!loadFile(path, buf)) return {};
    std::string        s(buf.begin(), buf.end());
    std::vector<float> out;
    size_t             i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(static_cast<float>(std::atoi(s.substr(i, j - i).c_str())));
        i = j + 1;
    }
    return out;
}

[[maybe_unused]] Tensor *toTensor(const std::vector<float> &data) {
    const int dims[1] = {static_cast<int>(data.size())};
    auto     *t       = new Tensor(dims, 1);
    for (size_t i = 0; i < data.size(); ++i) t->set(static_cast<int>(i), data[i]);
    return t;
}

[[maybe_unused]] Tensor *toTensor(const Weight &wt) {
    auto *t = new Tensor(wt.dims.data(), static_cast<int>(wt.dims.size()));
    for (size_t i = 0; i < wt.data.size(); ++i) t->set(static_cast<int>(i), wt.data[i]);
    return t;
}

/** Position ids [0..n) as a plain eager tensor (traceable as a constant). */
[[maybe_unused]] Tensor *arangeTensor(int n) {
    auto *t = new Tensor(n);
    for (int i = 0; i < n; ++i) t->set1(i, float(i));
    return t;
}

/** Causal mask [1, H, T, T]: 0 for s<=t, -1e4 for s>t. */
[[maybe_unused]] Tensor *causalMask(TF *tf, int T, int H) {
    const int dims[4] = {1, H, T, T};
    auto     *m       = new Tensor(dims, 4);
    m->fill(-1e4f);
    for (int h = 0; h < H; ++h)
        for (int t = 0; t < T; ++t)
            for (int s = 0; s <= t; ++s) m->set(((h * T + t) * T + s), 0.f);
    (void)tf;
    return m;
}

/**
 * GPT-2 hidden state after all transformer layers + final layernorm.
 * `ids` is [T] (eager float ids, or a traced placeholder).
 */
using WeightMap = std::map<std::string, Tensor *>;

[[maybe_unused]] WeightMap buildWeightTensors(const Model &m) {
    WeightMap out;
    for (const auto &kv : m.weights) out[kv.first] = toTensor(kv.second);
    return out;
}

[[maybe_unused]] const char *quantTag(DType dt) {
    switch (dt) {
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        default: return "float32";
    }
}

[[maybe_unused]] WeightMap buildQuantWeightTensors(TF *tf, const Model &m, DType dt, int group) {
    WeightMap out;
    for (const auto &kv : m.weights) {
        // Weight-only quantization: matrices (incl. embeddings / lm_head) are
        // packed; layernorm scales/biases and linear biases stay fp32 (they are
        // a tiny fraction of the bytes and keep the dequant path simple).
        const std::string &n      = kv.first;
        const bool         matrix = n.find(".bias") == std::string::npos && n.find("ln_") == std::string::npos;
        if (!matrix) {
            out[kv.first] = toTensor(kv.second);
            continue;
        }
        Tensor *t     = toTensor(kv.second);
        out[kv.first] = tf->quantizeWeight(t, quantTag(dt), group);
    }
    return out;
}

[[maybe_unused]] Tensor *buildHidden(TF *tf, Tensor *ids, const WeightMap &wm) {
    const int T   = ids->getSize();
    Tensor   *wte = wm.at("transformer.wte.weight");
    Tensor   *wpe = wm.at("transformer.wpe.weight");
    Tensor   *tok = tf->embedding(wte, ids);
    Tensor   *pos = arangeTensor(T);
    Tensor   *pe  = tf->embedding(wpe, pos);
    Tensor   *x   = tf->add(tok, pe);

    for (int i = 0; i < kLayers; ++i) {
        const std::string p    = "transformer.h." + std::to_string(i) + ".";
        Tensor           *ln1w = wm.at(p + "ln_1.weight");
        Tensor           *ln1b = wm.at(p + "ln_1.bias");
        Tensor           *h    = tf->layernormWB(x, ln1w, ln1b, 1e-5f);

        Tensor *qkv = tf->add(tf->matmul(h, wm.at(p + "attn.c_attn.weight")),
                              wm.at(p + "attn.c_attn.bias"));  // [T,3D]
        Tensor *q   = tf->slice(qkv, 1, 0, kEmbDim);
        Tensor *k   = tf->slice(qkv, 1, kEmbDim, 2 * kEmbDim);
        Tensor *v   = tf->slice(qkv, 1, 2 * kEmbDim, 3 * kEmbDim);
        q           = tf->reshape3(q, T, kHeads, kHeadDim);
        k           = tf->reshape3(k, T, kHeads, kHeadDim);
        v           = tf->reshape3(v, T, kHeads, kHeadDim);
        q           = tf->permute3(q, 1, 0, 2);  // [H,T,Dh]
        k           = tf->permute3(k, 1, 0, 2);
        v           = tf->permute3(v, 1, 0, 2);
        q           = tf->reshape4(q, 1, kHeads, T, kHeadDim);
        k           = tf->reshape4(k, 1, kHeads, T, kHeadDim);
        v           = tf->reshape4(v, 1, kHeads, T, kHeadDim);
        Tensor *att = tf->sdpaMasked(q, k, v, causalMask(tf, T, kHeads), 1.f / std::sqrt(float(kHeadDim)));
        att         = tf->permute4(tf->reshape4(att, 1, kHeads, T, kHeadDim), 0, 2, 1, 3);
        att         = tf->reshape2(att, T, kEmbDim);
        Tensor *out = tf->add(tf->matmul(att, wm.at(p + "attn.c_proj.weight")), wm.at(p + "attn.c_proj.bias"));
        x           = tf->add(x, out);

        h = tf->layernormWB(x, wm.at(p + "ln_2.weight"), wm.at(p + "ln_2.bias"), 1e-5f);
        h = tf->gelu(tf->add(tf->matmul(h, wm.at(p + "mlp.c_fc.weight")), wm.at(p + "mlp.c_fc.bias")));
        h = tf->add(tf->matmul(h, wm.at(p + "mlp.c_proj.weight")), wm.at(p + "mlp.c_proj.bias"));
        x = tf->add(x, h);
    }
    return tf->layernormWB(x, wm.at("transformer.ln_f.weight"), wm.at("transformer.ln_f.bias"), 1e-5f);
}

/** Full logits [T, vocab] (used for the numpy cross-check / compiled check). */
[[maybe_unused]] Tensor *fullLogits(TF *tf, Tensor *hidden, const WeightMap &wm) {
    Tensor *head = wm.at("lm_head.weight");
    return tf->matmul(hidden, tf->transpose(head));
}

[[maybe_unused]] int greedyNext(TF *tf, const std::vector<float> &ids, const WeightMap &wm) {
    Tensor   *in      = toTensor(ids);
    Tensor   *hidden  = buildHidden(tf, in, wm);
    Tensor   *last    = tf->slice(hidden, 0, static_cast<int>(ids.size()) - 1, static_cast<int>(ids.size()));
    Tensor   *logits1 = fullLogits(tf, last, wm);  // [1, vocab]
    Tensor   *am      = tf->argmax(logits1, 1, 0);
    const int token   = static_cast<int>(am->get(0));
    delete am;
    delete logits1;
    delete last;
    delete hidden;
    delete in;
    return token;
}

[[maybe_unused]] void softmaxRows(float *data, int rows, int cols) {
    for (int r = 0; r < rows; ++r) {
        float  mx  = -std::numeric_limits<float>::infinity();
        float *row = data + size_t(r) * cols;
        for (int c = 0; c < cols; ++c) mx = std::max(mx, row[c]);
        double sum = 0.0;
        for (int c = 0; c < cols; ++c) {
            row[c] = static_cast<float>(std::exp(double(row[c] - mx)));
            sum += row[c];
        }
        for (int c = 0; c < cols; ++c) row[c] = float(row[c] / sum);
    }
}

[[maybe_unused]] std::string decode(const Model &m, const std::vector<float> &ids) {
    std::string s;
    for (float f : ids) {
        const int id = static_cast<int>(f);
        if (id >= 0 && id < static_cast<int>(m.tokens.size())) s += m.tokens[static_cast<size_t>(id)];
    }
    return s;
}

[[maybe_unused]] Model *model() {
    static Model *m = [] {
        auto *mm = new Model();
        if (!loadModel(*mm)) {
            std::fprintf(stderr,
                         "[llm] skip: test/assets/mini_llm missing (see scripts/"
                         "export_tiny_gpt2.py)\n");
            delete mm;
            return static_cast<Model *>(nullptr);
        }
        return mm;
    }();
    return m;
}

[[maybe_unused]] bool ensureReference(Model &m) {
    if (m.reference.empty()) m.reference = readCsvInts(assetPath("reference_tokens.txt"));
    if (m.prompt.empty()) m.prompt = readCsvInts(assetPath("prompt.txt"));
    return !m.reference.empty() && !m.prompt.empty();
}

}  // namespace


// tensor_onnx.cpp
namespace {
using Bytes = std::vector<uint8_t>;
[[maybe_unused]] void varint(Bytes &b, uint64_t n) {
    while (n >= 128) {
        b.push_back(static_cast<uint8_t>(n) | 128);
        n >>= 7;
    }
    b.push_back(static_cast<uint8_t>(n));
}
[[maybe_unused]] void number(Bytes &b, int id, uint64_t n) {
    varint(b, id * 8);
    varint(b, n);
}
[[maybe_unused]] void field(Bytes &b, int id, const Bytes &value) {
    varint(b, id * 8 + 2);
    varint(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
[[maybe_unused]] Bytes text(const char *s) { return Bytes(s, s + std::char_traits<char>::length(s)); }
[[maybe_unused]] Bytes unknownModel() {
    Bytes node, graph, output, opset, model;
    field(node, 2, text("y"));
    field(node, 3, text("unsupported-node"));
    field(node, 4, text("UnknownOperator"));
    field(graph, 1, node);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    return model;
}
}  // namespace

TEST_CASE("tensor.onnx.affine.roundingAndRange") {
    using namespace eve::tensor::affine;
    const std::vector<float> input{-1000, -1.25f, -.75f, -.25f, .25f, .75f, 1.25f, 1000};
    auto                     q = quantize(input, .5f, 0, true);
    REQUIRE(q.ok());
    CHECK(q.value() == Bytes({128, 254, 254, 0, 0, 2, 2, 127}));
    auto zero = dynamicQuantize(std::vector<float>{0, 0});
    REQUIRE(zero.ok());
    CHECK(zero.value().scale == 1);
    auto invalid = quantize(input, 0, 0, false);
    CHECK(!invalid.ok());
    auto nonfinite = dynamicQuantize(std::vector<float>{std::numeric_limits<float>::infinity()});
    CHECK(!nonfinite.ok());
}

TEST_CASE("tensor.onnx.affine.exactAccumulators") {
    using namespace eve::tensor::affine;
    Bytes a(1025, 255), b(1025, 127);
    auto  product = matmul({a, false}, {b, true}, 1, 1025, 1);
    REQUIRE(product.ok());
    CHECK(product.value()[0] == 33194625);
    auto mismatch = matmul({a, false}, {b, true}, 2, 1025, 1);
    CHECK(!mismatch.ok());
    auto zero = matmul({a, false}, {b, true}, 1, 1025, 1, 256);
    CHECK(!zero.ok());
    Bytes largeA(40000, 255), largeB(40000, 255);
    auto  overflow = matmul({largeA, false}, {largeB, false}, 1, 40000, 1);
    CHECK(!overflow.ok());
}

TEST_CASE("tensor.onnx.affine.convolutionValidation") {
    using namespace eve::tensor::affine;
    ConvShape shape;
    shape.width    = 3;
    shape.kernelW  = 3;
    shape.padLeft  = 1;
    shape.padRight = 1;
    const Bytes                x{11, 12, 13}, w{1, 1, 1};
    const std::vector<int32_t> z{0};
    auto                       result = conv({x, false}, {w, false}, shape, 10, z);
    REQUIRE(result.ok());
    CHECK(result.value() == std::vector<int32_t>({3, 6, 5}));
    shape.groups = 2;
    auto invalid = conv({x, false}, {w, false}, shape, 10, z);
    CHECK(!invalid.ok());
}


TEST_CASE("tensor.onnx.import.owningOutputs") {
    Bytes tensor, attribute, node, graph, output, opset, encoded;
    number(tensor, 2, 1);
    const float expected = 1.5f;
    Bytes       payload(sizeof(float));
    std::memcpy(payload.data(), &expected, sizeof(float));
    field(tensor, 9, payload);
    field(attribute, 1, text("value"));
    number(attribute, 20, 4);
    field(attribute, 5, tensor);
    field(node, 2, text("y"));
    field(node, 4, text("Constant"));
    field(node, 5, attribute);
    field(graph, 1, node);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(encoded, 1, 7);
    field(encoded, 8, opset);
    field(encoded, 7, graph);
    auto imported = eve::tensor::OnnxModel::load(encoded);
    REQUIRE(imported.ok());
    encoded.clear();
    auto result = imported.value()->run({});
    REQUIRE(result.ok());
    imported.value().reset();
    REQUIRE(result.value().size() == 1);
    REQUIRE(result.value()[0].tensor.bytes.size() == 4);
    float actual;
    std::memcpy(&actual, result.value()[0].tensor.bytes.data(), 4);
    CHECK(actual == expected);
}


TEST_CASE("tensor.onnx.storage.aliasAndCpuMutation") {
    using eve::tensor::onnx_detail::ByteStorage;
    class Deferred final : public eve::tensor::OnnxDeviceStorage {
    public:
        mutable int                       reads = 0;
        eve::Result<std::vector<uint8_t>> readback() const override {
            ++reads;
            return eve::Result<std::vector<uint8_t>>::success({1, 2, 3, 4});
        }
    };
    auto        deferred = std::make_shared<Deferred>();
    ByteStorage original(eve::tensor::OnnxBuffer{{}, deferred, 4});
    ByteStorage alias = original;
    CHECK(alias.size() == 4);
    CHECK(deferred->reads == 0);
    alias.data()[0] = 9;
    CHECK(deferred->reads == 1);
    CHECK(static_cast<const ByteStorage &>(alias)[0] == 9);
    CHECK(static_cast<const ByteStorage &>(original)[0] == 1);
    CHECK(deferred->reads == 2);
    ByteStorage copy = alias;
    copy.data()[1]   = 8;
    CHECK(static_cast<const ByteStorage &>(alias)[1] == 2);
    CHECK(static_cast<const ByteStorage &>(copy)[1] == 8);
}

// tensor_optimizer.cpp
using namespace eve::tensor;

TEST_CASE("tensor.optimizer.residualAttentionDependenciesAndSlots") {
    auto                   *tf = TF::create();
    const int               T = 4, D = 8;
    std::unique_ptr<Tensor> W(tf->fill2(D, D, -0.1f));
    for (int i = 0; i < D; ++i) W->set2(i, i, 0.7f + float(i) * 0.1f);
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in = fn->input3(1, T, D);
    Tensor               *l  = tf->layernorm(in, 1e-5f);
    Tensor               *m  = tf->reshape2(l, T, D);
    Tensor               *mm = tf->matmul(m, W.get());
    Tensor               *q  = tf->reshape4(mm, 1, 1, T, D);
    Tensor               *a  = tf->sdpa(q, q, q, 0.353553f);
    Tensor               *a3 = tf->reshape3(a, 1, T, D);
    fn->setOutput(tf->add(in, a3));

    OptimizedGraph opt = optimizeGraph(fn->graph(), fn->outputNode());
    REQUIRE(!opt.order.empty());
    REQUIRE(opt.outputNode == fn->outputNode());
    REQUIRE(opt.nodeSlot.size() == size_t(fn->graph().nodeCount()));
    std::vector<bool> visited(size_t(fn->graph().nodeCount()), false);
    for (int id : opt.order) {
        REQUIRE(id >= 0);
        REQUIRE(id < fn->graph().nodeCount());
        REQUIRE(!visited[size_t(id)]);
        const auto &node = fn->graph().node(id);
        for (int input : {node.in0, node.in1, node.in2, node.in3, node.in4}) {
            if (input >= 0) REQUIRE(visited[size_t(input)]);
        }
        visited[size_t(id)] = true;
    }
    for (const auto &group : opt.groups) {
        REQUIRE(group.outputNode >= 0);
        const int slot = opt.nodeSlot[size_t(group.outputNode)];
        REQUIRE(slot >= 0);
        REQUIRE(size_t(slot) < opt.slotSize.size());
        REQUIRE(opt.slotSize[size_t(slot)] >= fn->graph().node(group.outputNode).size);
        if (group.kind == GroupKind::Elementwise) {
            KernelSpec spec;
            generateKernel(fn->graph(), group, spec);
            REQUIRE(!spec.pass2.empty());
        }
    }
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor>           input(tf->zeros3(1, T, D));
    for (int i = 0; i < input->getSize(); ++i) input->set(i, float(i % 7) * 0.2f);
    std::unique_ptr<Tensor> normalized(tf->layernorm(input.get(), 1e-5f));
    std::unique_ptr<Tensor> matrix(tf->reshape2(normalized.get(), T, D));
    std::unique_ptr<Tensor> multiplied(tf->matmul(matrix.get(), W.get()));
    std::unique_ptr<Tensor> query(tf->reshape4(multiplied.get(), 1, 1, T, D));
    std::unique_ptr<Tensor> attended(tf->sdpa(query.get(), query.get(), query.get(), 0.353553f));
    std::unique_ptr<Tensor> reshaped(tf->reshape3(attended.get(), 1, T, D));
    std::unique_ptr<Tensor> expected(tf->add(input.get(), reshaped.get()));
    std::unique_ptr<Tensor> actual(compiled->run1(input.get()));
    REQUIRE(actual->getSize() == expected->getSize());
    for (int i = 0; i < actual->getSize(); ++i) REQUIRE(std::fabs(actual->get(i) - expected->get(i)) < 1e-4f);
}

// tensor_quant.cpp
using namespace eve::tensor;

namespace {

[[maybe_unused]] std::vector<float> randData(int n, unsigned seed) {
    std::vector<float> v(static_cast<size_t>(n));
    unsigned           s = seed;
    for (int i = 0; i < n; ++i) {
        s                         = s * 1664525u + 1013904223u;
        v[static_cast<size_t>(i)] = float(int(s % 2001) - 1000) / 1000.f;  // [-1, 1]
    }
    return v;
}

[[maybe_unused]] Tensor *fromData(const std::vector<float> &d, int d0, int d1) {
    auto *t = new Tensor(d0, d1);
    for (size_t i = 0; i < d.size(); ++i) t->set(int(i), d[static_cast<size_t>(i)]);
    return t;
}

[[maybe_unused]] float maxAbsDiff(const Tensor *a, const Tensor *b) {
    float m = 0.f;
    for (int i = 0; i < a->getSize(); ++i) m = std::max(m, std::fabs(a->get(i) - b->get(i)));
    return m;
}

[[maybe_unused]] const char *dtypeTag(DType dt) {
    switch (dt) {
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        default: return "?";
    }
}

}  // namespace

TEST_CASE("tensor.quant.roundtrip") {
    auto                    *tf  = TF::create();
    const std::vector<float> src = randData(32 * 48, 7);
    Tensor                  *ref = fromData(src, 32, 48);

    const struct {
        DType dt;
        float bound;
    } cases[] = {
        {DType::Fp16, 1e-3f}, {DType::Int8, 0.03f},    {DType::Fp8E4M3, 0.08f},
        {DType::Int4, 0.3f},  {DType::Fp4E2M1, 0.35f},
    };
    for (const auto &c : cases) {
        Tensor *q = tf->quantizeWeight(ref, dtypeTag(c.dt), 16);
        REQUIRE(q != nullptr);
        CHECK(q->isQuantized());
        CHECK_EQ(std::string(q->getDtype()), std::string(dtypeTag(c.dt)));
        float maxErr = 0.f;
        for (int i = 0; i < q->getSize(); ++i)
            maxErr = std::max(maxErr, std::fabs(q->get(i) - src[static_cast<size_t>(i)]));
        std::fprintf(stderr, "[quant] %s round-trip max err = %.4f (bound %.3f)\n", dtypeTag(c.dt), maxErr, c.bound);
        CHECK(maxErr < c.bound);
        delete q;
    }
    delete ref;
}

TEST_CASE("tensor.quant.eagerMatmul") {
    auto                    *tf       = TF::create();
    const std::vector<float> ad       = randData(4 * 8, 11);
    const std::vector<float> bd       = randData(8 * 16, 23);
    Tensor                  *a        = fromData(ad, 4, 8);
    Tensor                  *b        = fromData(bd, 8, 16);
    Tensor                  *baseline = a->matmul(b);

    const struct {
        DType dt;
        float bound;
    } cases[] = {
        {DType::Fp16, 0.01f}, {DType::Int8, 0.15f},   {DType::Fp8E4M3, 0.35f},
        {DType::Int4, 1.2f},  {DType::Fp4E2M1, 1.4f},
    };
    for (const auto &c : cases) {
        Tensor     *qb   = tf->quantizeWeight(b, dtypeTag(c.dt), 8);
        Tensor     *out  = a->matmul(qb);
        const float diff = maxAbsDiff(out, baseline);
        std::fprintf(stderr, "[quant] eager %s matmul max diff = %.4f (bound %.2f)\n", dtypeTag(c.dt), diff, c.bound);
        CHECK(diff < c.bound);
        delete qb;
        delete out;
    }
    delete a;
    delete b;
    delete baseline;
}

TEST_CASE("tensor.quant.compiledMatmul") {
    auto                    *tf = TF::create();
    const std::vector<float> ad = randData(4 * 8, 31);
    const std::vector<float> bd = randData(8 * 16, 43);
    Tensor                  *a  = fromData(ad, 4, 8);
    Tensor                  *b  = fromData(bd, 8, 16);

    for (const char *tag : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        Tensor *qb    = tf->quantizeWeight(b, tag, 8);
        Tensor *eager = a->matmul(qb);

        auto   *fn = tf->func();
        Tensor *ph = fn->input2(4, 8);
        fn->setOutput(tf->matmul(ph, qb));  // quantized const captured as a graph const
        std::unique_ptr<CompiledFunction> cf(fn->compile());
        Tensor                           *out  = cf->run1(a);
        const float                       diff = maxAbsDiff(out, eager);
        std::fprintf(stderr, "[quant] compiled %s matmul vs eager diff = %.6f\n", tag, diff);
        // CPU: identical dequant+double-accumulate loops -> ~0. GPU: fp32
        // accumulation differs from eager's double accumulation by ~1e-3.
        CHECK(diff < 0.01f);
        delete qb;
        delete eager;
        delete out;
    }
    delete a;
    delete b;
}

TEST_CASE("tensor.quant.embedding") {
    auto                    *tf      = TF::create();
    const std::vector<float> td      = randData(10 * 6, 51);
    Tensor                  *table   = fromData(td, 10, 6);
    const float              idxv[4] = {0.f, 3.f, 9.f, 5.f};
    Tensor                  *idx     = new Tensor(4);
    for (int i = 0; i < 4; ++i) idx->set1(i, idxv[i]);
    Tensor *baseline = tf->embedding(table, idx);

    for (const char *tag : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        Tensor     *qt   = tf->quantizeWeight(table, tag, 6);
        Tensor     *out  = tf->embedding(qt, idx);
        const float diff = maxAbsDiff(out, baseline);
        std::fprintf(stderr, "[quant] eager %s embedding max diff = %.4f\n", tag, diff);
        CHECK(diff < 0.4f);

        auto   *fn = tf->func();
        Tensor *ph = fn->input1(4);
        fn->setOutput(tf->embedding(qt, ph));
        std::unique_ptr<CompiledFunction> cf(fn->compile());
        Tensor                           *cout  = cf->run1(idx);
        const float                       cdiff = maxAbsDiff(cout, out);
        CHECK(cdiff < 0.01f);
        delete qt;
        delete out;
        delete cout;
    }
    delete table;
    delete idx;
    delete baseline;
}

TEST_CASE("tensor.quant.guards") {
    auto   *tf    = TF::create();
    Tensor *t     = tf->fill2(2, 2, 1.f);
    Tensor *q     = tf->quantizeWeight(t, "int8", 2);
    bool    threw = false;
    try {
        (void)q->data();
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        q->set(0, 1.f);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    delete q;
    delete t;
}
}  // namespace test_tensor_llm_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include "common/Exception.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor.cpp
namespace test_tensor_unit {
using namespace eve::tensor;

namespace {

/** GPU tensor tests need a live Vulkan device; headless init is enough. */
[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace

TEST_CASE("tensor.create.zerosOnes") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->zeros2(3, 4));
    CHECK_EQ(a->getRank(), 2);
    CHECK_EQ(a->getDim0(), 3);
    CHECK_EQ(a->getDim1(), 4);
    CHECK_EQ(a->getSize(), 12);
    CHECK_EQ(a->getDevice(), std::string("cpu"));
    CHECK(a->isEager());
    CHECK(std::fabs(a->reduceSum()) < 1e-6f);

    std::unique_ptr<Tensor> b(tf->ones2(2, 2));
    CHECK(std::fabs(b->reduceSum() - 4.f) < 1e-5f);
    CHECK_EQ(b->getDtype(), std::string("float32"));
}

TEST_CASE("tensor.elementwise.matmul") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->fill2(2, 3, 2.f));
    std::unique_ptr<Tensor> b(tf->fill2(2, 3, 3.f));
    std::unique_ptr<Tensor> c(tf->multiply(a.get(), b.get()));
    CHECK(std::fabs(c->get2(0, 0) - 6.f) < 1e-5f);

    std::unique_ptr<Tensor> m(tf->zeros2(2, 3));
    m->set2(0, 0, 1.f);
    m->set2(0, 1, 2.f);
    m->set2(0, 2, 3.f);
    m->set2(1, 0, 4.f);
    m->set2(1, 1, 5.f);
    m->set2(1, 2, 6.f);
    std::unique_ptr<Tensor> n(tf->zeros2(3, 2));
    n->set2(0, 0, 1.f);
    n->set2(1, 0, 1.f);
    n->set2(2, 0, 1.f);
    n->set2(0, 1, 0.f);
    n->set2(1, 1, 0.f);
    n->set2(2, 1, 0.f);
    std::unique_ptr<Tensor> p(tf->matmul(m.get(), n.get()));
    CHECK_EQ(p->getDim0(), 2);
    CHECK_EQ(p->getDim1(), 2);
    CHECK(std::fabs(p->get2(0, 0) - 6.f) < 1e-4f);
    CHECK(std::fabs(p->get2(1, 0) - 15.f) < 1e-4f);
}

TEST_CASE("tensor.inplace.reduce") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->arange(5));
    CHECK(std::fabs(tf->reduceSum(a.get()) - 10.f) < 1e-5f);
    CHECK(std::fabs(tf->reduceMean(a.get()) - 2.f) < 1e-5f);
    a->mulScalarInPlace(2.f);
    CHECK(std::fabs(a->get1(4) - 8.f) < 1e-5f);
    a->reluInPlace();
}

TEST_CASE("tensor.reshape.transpose") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->arange(6));
    std::unique_ptr<Tensor> b(tf->reshape2(a.get(), 2, 3));
    CHECK_EQ(b->get2(1, 2), 5.f);
    std::unique_ptr<Tensor> c(tf->transpose(b.get()));
    CHECK_EQ(c->getDim0(), 3);
    CHECK_EQ(c->getDim1(), 2);
    CHECK_EQ(c->get2(2, 1), 5.f);
}

TEST_CASE("tensor.activations.where") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->fill1(3, -1.f));
    x->set1(1, 2.f);
    std::unique_ptr<Tensor> y(tf->relu(x.get()));
    CHECK(std::fabs(y->get1(0)) < 1e-6f);
    CHECK(std::fabs(y->get1(1) - 2.f) < 1e-5f);

    std::unique_ptr<Tensor> cond(tf->zeros1(3));
    cond->set1(0, 1.f);
    std::unique_ptr<Tensor> a(tf->fill1(3, 10.f));
    std::unique_ptr<Tensor> b(tf->fill1(3, 20.f));
    std::unique_ptr<Tensor> w(tf->where(cond.get(), a.get(), b.get()));
    CHECK(std::fabs(w->get1(0) - 10.f) < 1e-5f);
    CHECK(std::fabs(w->get1(1) - 20.f) < 1e-5f);
}

TEST_CASE("tensor.random.seeded") {
    auto *tf = TF::create();
    tf->setRandomSeed(7);
    std::unique_ptr<Tensor> a(tf->randomUniform2(4, 4));
    tf->setRandomSeed(7);
    std::unique_ptr<Tensor> b(tf->rand2(4, 4));
    CHECK(std::fabs(a->get(0) - b->get(0)) < 1e-7f);
    CHECK_GE(a->reduceMin(), 0.f);
    CHECK_LE(a->reduceMax(), 1.f);
}

TEST_CASE("tensor.func.compileRun") {
    auto *tf = TF::create();

    // Eager reference
    std::unique_ptr<Tensor> xEager(tf->zeros2(2, 3));
    xEager->set2(0, 0, 1.f);
    xEager->set2(0, 1, 2.f);
    xEager->set2(0, 2, 3.f);
    xEager->set2(1, 0, 4.f);
    xEager->set2(1, 1, 5.f);
    xEager->set2(1, 2, 6.f);
    std::unique_ptr<Tensor> w(tf->zeros2(3, 2));
    w->set2(0, 0, 1.f);
    w->set2(1, 0, 1.f);
    w->set2(2, 0, 1.f);
    w->set2(0, 1, 0.f);
    w->set2(1, 1, 0.f);
    w->set2(2, 1, 0.f);
    std::unique_ptr<Tensor> mm(tf->matmul(xEager.get(), w.get()));
    std::unique_ptr<Tensor> eagerOut(tf->relu(mm.get()));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x = fn->input2(2, 3);
    Tensor               *y = tf->matmul(x, w.get());  // captures w as Const
    y                       = tf->relu(y);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    CHECK_EQ(out->getDim0(), 2);
    CHECK_EQ(out->getDim1(), 2);
    CHECK(std::fabs(out->get2(0, 0) - eagerOut->get2(0, 0)) < 1e-4f);
    CHECK(std::fabs(out->get2(1, 0) - eagerOut->get2(1, 0)) < 1e-4f);
    CHECK_EQ(compiled->getDevice(), std::string("cpu"));
}

TEST_CASE("tensor.func.symbolicReadThrows") {
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x = fn->input1(4);
    CHECK(x->isSymbolic());
    bool threw = false;
    try {
        (void)x->get1(0);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    // Finish tracing cleanly
    fn->setOutput(x);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor>           feed(tf->arange(4));
    std::unique_ptr<Tensor>           out(compiled->run1(feed.get()));
    CHECK(std::fabs(out->get1(3) - 3.f) < 1e-5f);
}

// --- GPU (compute shader) path ---------------------------------------------
// These skip (return) when no Vulkan device / glslc is available (e.g. some CI
// images), matching test/gpgpu.cpp's tolerant pattern. When the environment
// does support it, CompiledFunction::getDevice() should report "gpu".


// --- AITemplate-style refactor: new op coverage ---------------------------

TEST_CASE("tensor.broadcast.binary") {
    auto *tf = TF::create();
    // [2,3] + [3] -> [2,3]; [2,3] * [2,1] -> [2,3]
    std::unique_ptr<Tensor> a(tf->zeros2(2, 3));
    a->set2(0, 0, 1.f);
    a->set2(0, 1, 2.f);
    a->set2(0, 2, 3.f);
    a->set2(1, 0, 4.f);
    a->set2(1, 1, 5.f);
    a->set2(1, 2, 6.f);
    std::unique_ptr<Tensor> b(tf->fill1(3, 10.f));
    std::unique_ptr<Tensor> c(tf->add(a.get(), b.get()));
    REQUIRE_EQ(c->getRank(), 2);
    CHECK(std::fabs(c->get2(0, 0) - 11.f) < 1e-5f);
    CHECK(std::fabs(c->get2(1, 2) - 16.f) < 1e-5f);

    std::unique_ptr<Tensor> d(tf->fill2(2, 1, 2.f));
    std::unique_ptr<Tensor> e(tf->multiply(a.get(), d.get()));
    CHECK(std::fabs(e->get2(0, 0) - 2.f) < 1e-5f);
    CHECK(std::fabs(e->get2(1, 0) - 8.f) < 1e-5f);
    CHECK(std::fabs(e->get2(1, 2) - 12.f) < 1e-5f);

    // shape mismatch must throw
    bool                    threw = false;
    std::unique_ptr<Tensor> bad(tf->fill2(3, 3, 1.f));
    try {
        tf->add(a.get(), bad.get());
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("tensor.softmax.logsoftmax") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 3));
    x->set2(0, 0, 1.f);
    x->set2(0, 1, 2.f);
    x->set2(0, 2, 3.f);
    x->set2(1, 0, 0.f);
    x->set2(1, 1, 0.f);
    x->set2(1, 2, 0.f);

    std::unique_ptr<Tensor> s(tf->softmax(x.get(), -1));
    const float             s00 = std::exp(1.f) / (std::exp(1.f) + std::exp(2.f) + std::exp(3.f));
    CHECK(std::fabs(s->get2(0, 0) - s00) < 1e-5f);
    CHECK(std::fabs(s->reduceSum() - 2.f) < 1e-4f);

    std::unique_ptr<Tensor> ls(tf->logSoftmax(x.get(), 1));
    CHECK(std::fabs(ls->get2(0, 0) - std::log(s00)) < 1e-4f);
    CHECK(std::fabs(ls->get2(1, 0) - std::log(1.f / 3.f)) < 1e-4f);

    // axis-0 softmax
    std::unique_ptr<Tensor> s0(tf->softmax(x.get(), 0));
    const float             e1 = std::exp(1.f), e0 = std::exp(0.f);
    CHECK(std::fabs(s0->get2(0, 0) - e1 / (e1 + e0)) < 1e-5f);
    CHECK(std::fabs(s0->get2(1, 0) - e0 / (e1 + e0)) < 1e-5f);
}

TEST_CASE("tensor.layernorm.rmsnorm") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 4));
    x->set2(0, 0, 1.f);
    x->set2(0, 1, 2.f);
    x->set2(0, 2, 3.f);
    x->set2(0, 3, 4.f);
    x->set2(1, 0, -1.f);
    x->set2(1, 1, 0.f);
    x->set2(1, 2, 1.f);
    x->set2(1, 3, 2.f);

    std::unique_ptr<Tensor> ln(tf->layernorm(x.get(), 1e-5f));
    const float             mean0 = 2.5f, inv0 = 1.f / std::sqrt(1.25f + 1e-5f);
    CHECK(std::fabs(ln->get2(0, 0) - (1.f - mean0) * inv0) < 1e-4f);
    CHECK(std::fabs(ln->get2(0, 3) - (4.f - mean0) * inv0) < 1e-4f);
    // zero-mean row -> output is a centered unit vector
    CHECK(std::fabs(ln->get2(1, 0) - (-1.5f) * inv0) < 1e-4f);

    std::unique_ptr<Tensor> scale(tf->fill1(4, 2.f));
    std::unique_ptr<Tensor> bias(tf->fill1(4, 1.f));
    std::unique_ptr<Tensor> lnwb(tf->layernormWB(x.get(), scale.get(), bias.get(), 1e-5f));
    CHECK(std::fabs(lnwb->get2(0, 0) - (ln->get2(0, 0) * 2.f + 1.f)) < 1e-4f);

    std::unique_ptr<Tensor> rn(tf->rmsnorm(x.get(), 1e-5f));
    const double            rms0 = std::sqrt((1.0 + 4.0 + 9.0 + 16.0) / 4.0 + 1e-5);
    CHECK(std::fabs(rn->get2(0, 0) - float(1.0 / rms0)) < 1e-4f);
    std::unique_ptr<Tensor> rnw(tf->rmsnormW(x.get(), scale.get(), 1e-5f));
    CHECK(std::fabs(rnw->get2(0, 0) - rn->get2(0, 0) * 2.f) < 1e-4f);
}

TEST_CASE("tensor.gelu.silu") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->linspace(-2.f, 2.f, 5));
    std::unique_ptr<Tensor> g(tf->gelu(x.get()));
    // gelu(0)=0, gelu(2) ≈ 1.9546 (tanh approx)
    CHECK(std::fabs(g->get1(2) - 0.f) < 1e-5f);
    CHECK(std::fabs(g->get1(4) - 1.9546f) < 1e-3f);
    std::unique_ptr<Tensor> s(tf->silu(x.get()));
    CHECK(std::fabs(s->get1(4) - (2.f / (1.f + std::exp(-2.f)))) < 1e-5f);
    std::unique_ptr<Tensor> g2(x->gelu());
    CHECK(std::fabs(g2->get1(4) - g->get1(4)) < 1e-6f);
}

namespace {

[[maybe_unused]] float conv2dRef(const float *x, int N, int C, int H, int W, const float *w, int F, int KH, int KW,
                                 int stride, int pad, int oh, int ow) {
    float acc = 0.f;
    for (int c = 0; c < C; ++c)
        for (int kh = 0; kh < KH; ++kh)
            for (int kw = 0; kw < KW; ++kw) {
                const int ih = oh * stride + kh - pad;
                const int iw = ow * stride + kw - pad;
                if (ih < 0 || ih >= H || iw < 0 || iw >= W) continue;
                acc += x[((0 * C + c) * H + ih) * W + iw] * w[((0 * F + c) * KH + kh) * KW + kw];
            }
    return acc;
}

}  // namespace

TEST_CASE("tensor.conv2d.pool") {
    auto *tf = TF::create();
    // 1x1x3x3 input, single 3x3 kernel = all-ones -> sum of 3x3 patch
    std::unique_ptr<Tensor> x(tf->zeros4(1, 1, 3, 3));
    for (int i = 0; i < 9; ++i) x->set(i, float(i + 1));
    std::unique_ptr<Tensor> w(tf->fill4(1, 1, 3, 3, 1.f));
    std::unique_ptr<Tensor> y(tf->conv2d(x.get(), w.get(), 1, 0));
    REQUIRE_EQ(y->getDim2(), 1);
    REQUIRE_EQ(y->getDim3(), 1);
    CHECK(std::fabs(y->get4(0, 0, 0, 0) - 45.f) < 1e-4f);

    std::unique_ptr<Tensor> w2(tf->fill4(1, 1, 2, 2, 1.f));
    std::unique_ptr<Tensor> y2(tf->conv2d(x.get(), w2.get(), 1, 0));
    REQUIRE_EQ(y2->getDim2(), 2);
    CHECK(std::fabs(y2->get4(0, 0, 0, 0) - (1.f + 2.f + 4.f + 5.f)) < 1e-4f);
    CHECK(std::fabs(y2->get4(0, 0, 1, 1) - (5.f + 6.f + 8.f + 9.f)) < 1e-4f);

    // bias
    std::unique_ptr<Tensor> bias(tf->fill1(1, 5.f));
    std::unique_ptr<Tensor> yb(tf->conv2dBias(x.get(), w2.get(), bias.get(), 1, 0));
    CHECK(std::fabs(yb->get4(0, 0, 0, 0) - (1.f + 2.f + 4.f + 5.f + 5.f)) < 1e-4f);

    // conv1d: length-4 input, kernel [1,2,1]
    std::unique_ptr<Tensor> x1(tf->zeros3(1, 1, 4));
    x1->set3(0, 0, 0, 1.f);
    x1->set3(0, 0, 1, 2.f);
    x1->set3(0, 0, 2, 3.f);
    x1->set3(0, 0, 3, 4.f);
    std::unique_ptr<Tensor> w1(tf->zeros3(1, 1, 3));
    w1->set3(0, 0, 0, 1.f);
    w1->set3(0, 0, 1, 2.f);
    w1->set3(0, 0, 2, 1.f);
    std::unique_ptr<Tensor> y1(tf->conv1d(x1.get(), w1.get(), 1, 0));
    REQUIRE_EQ(y1->getDim2(), 2);
    CHECK(std::fabs(y1->get3(0, 0, 0) - (1.f + 4.f + 3.f)) < 1e-4f);
    CHECK(std::fabs(y1->get3(0, 0, 1) - (2.f + 6.f + 4.f)) < 1e-4f);

    // max/avg pool on 2x2 from 3x3
    std::unique_ptr<Tensor> p(tf->maxpool2d(x.get(), 2, 1, 0));
    CHECK(std::fabs(p->get4(0, 0, 0, 0) - 5.f) < 1e-5f);
    std::unique_ptr<Tensor> a(tf->avgpool2d(x.get(), 2, 1, 0));
    CHECK(std::fabs(a->get4(0, 0, 0, 0) - (1.f + 2.f + 4.f + 5.f) / 4.f) < 1e-5f);
}

TEST_CASE("tensor.embedding.concat.slice.permute") {
    auto *tf = TF::create();
    // table 4x2
    std::unique_ptr<Tensor> table(tf->zeros2(4, 2));
    table->set2(0, 0, 10.f);
    table->set2(0, 1, 11.f);
    table->set2(1, 0, 20.f);
    table->set2(1, 1, 21.f);
    table->set2(2, 0, 30.f);
    table->set2(2, 1, 31.f);
    table->set2(3, 0, 40.f);
    table->set2(3, 1, 41.f);
    std::unique_ptr<Tensor> idx(tf->zeros1(3));
    idx->set1(0, 0.f);
    idx->set1(1, 2.f);
    idx->set1(2, 3.f);
    std::unique_ptr<Tensor> emb(tf->embedding(table.get(), idx.get()));
    REQUIRE_EQ(emb->getRank(), 2);
    REQUIRE_EQ(emb->getDim1(), 2);
    CHECK(std::fabs(emb->get2(0, 0) - 10.f) < 1e-5f);
    CHECK(std::fabs(emb->get2(1, 1) - 31.f) < 1e-5f);
    CHECK(std::fabs(emb->get2(2, 0) - 40.f) < 1e-5f);

    // concat along last axis
    std::unique_ptr<Tensor> a(tf->zeros2(2, 2));
    a->set2(0, 0, 1.f);
    a->set2(0, 1, 2.f);
    a->set2(1, 0, 3.f);
    a->set2(1, 1, 4.f);
    std::unique_ptr<Tensor> b(tf->fill2(2, 3, 9.f));
    std::unique_ptr<Tensor> c(tf->concat2(a.get(), b.get(), 1));
    REQUIRE_EQ(c->getDim1(), 5);
    CHECK(std::fabs(c->get2(0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(c->get2(0, 4) - 9.f) < 1e-5f);

    // concat3 along axis 0
    std::unique_ptr<Tensor> d(tf->fill2(1, 2, 7.f));
    std::unique_ptr<Tensor> e(tf->concat3(a.get(), a.get(), d.get(), 0));
    REQUIRE_EQ(e->getDim0(), 5);
    CHECK(std::fabs(e->get2(4, 1) - 7.f) < 1e-5f);

    // slice
    std::unique_ptr<Tensor> s(tf->slice(c.get(), 1, 1, 4));
    REQUIRE_EQ(s->getDim1(), 3);
    CHECK(std::fabs(s->get2(0, 0) - 2.f) < 1e-5f);
    CHECK(std::fabs(s->get2(0, 2) - 9.f) < 1e-5f);

    // permute3: [2,3,4] -> [4,2,3]
    std::unique_ptr<Tensor> t3(tf->zeros3(2, 3, 4));
    t3->set3(1, 2, 3, 42.f);
    std::unique_ptr<Tensor> perm(tf->permute3(t3.get(), 2, 0, 1));
    REQUIRE_EQ(perm->getDim0(), 4);
    REQUIRE_EQ(perm->getDim1(), 2);
    REQUIRE_EQ(perm->getDim2(), 3);
    CHECK(std::fabs(perm->get3(3, 1, 2) - 42.f) < 1e-5f);
}

TEST_CASE("tensor.reduceAxis.argmax.cast") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 3));
    x->set2(0, 0, 1.f);
    x->set2(0, 1, 3.f);
    x->set2(0, 2, 2.f);
    x->set2(1, 0, 4.f);
    x->set2(1, 1, 1.f);
    x->set2(1, 2, 5.f);

    std::unique_ptr<Tensor> sum(tf->sumAxis(x.get(), 1, 0));
    REQUIRE_EQ(sum->getRank(), 1);
    CHECK(std::fabs(sum->get1(0) - 6.f) < 1e-5f);
    CHECK(std::fabs(sum->get1(1) - 10.f) < 1e-5f);

    std::unique_ptr<Tensor> mean(tf->meanAxis(x.get(), 1, 1));
    REQUIRE_EQ(mean->getDim1(), 1);
    CHECK(std::fabs(mean->get2(0, 0) - 2.f) < 1e-5f);

    std::unique_ptr<Tensor> mx(tf->maxAxis(x.get(), 1, 0));
    CHECK(std::fabs(mx->get1(0) - 3.f) < 1e-5f);
    CHECK(std::fabs(mx->get1(1) - 5.f) < 1e-5f);
    std::unique_ptr<Tensor> mn(tf->minAxis(x.get(), 1, 0));
    CHECK(std::fabs(mn->get1(0) - 1.f) < 1e-5f);

    std::unique_ptr<Tensor> am(tf->argmax(x.get(), 1, 0));
    CHECK_EQ(am->getDtype(), std::string("int32"));
    CHECK(std::fabs(am->get1(0) - 1.f) < 1e-5f);
    CHECK(std::fabs(am->get1(1) - 2.f) < 1e-5f);

    std::unique_ptr<Tensor> c(tf->cast(am.get(), "float32"));
    CHECK_EQ(c->getDtype(), std::string("float32"));
    std::unique_ptr<Tensor> c2(tf->cast(x.get(), "int32"));
    CHECK_EQ(c2->getDtype(), std::string("int32"));
    bool threw = false;
    try {
        tf->cast(x.get(), "uint8");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("tensor.sdpa") {
    auto                   *tf = TF::create();
    const int               B = 1, H = 1, T = 2, S = 3, D = 4;
    std::unique_ptr<Tensor> q(tf->zeros4(B, H, T, D));
    std::unique_ptr<Tensor> k(tf->zeros4(B, H, S, D));
    std::unique_ptr<Tensor> v(tf->zeros4(B, H, S, D));
    for (int t = 0; t < T; ++t)
        for (int d = 0; d < D; ++d) q->set4(0, 0, t, d, float((t + 1) * (d + 1)) * 0.25f);
    for (int s = 0; s < S; ++s)
        for (int d = 0; d < D; ++d) k->set4(0, 0, s, d, float((s + 1) * (d + 1)) * 0.2f);
    for (int s = 0; s < S; ++s)
        for (int d = 0; d < D; ++d) v->set4(0, 0, s, d, float(s * 10 + d));

    std::unique_ptr<Tensor> out(tf->sdpa(q.get(), k.get(), v.get(), 1.f));
    // reference: softmax(q@k^T) @ v
    auto ref = [&](int t) {
        float scores[3] = {};
        for (int s = 0; s < S; ++s) {
            double acc = 0.0;
            for (int d = 0; d < D; ++d) acc += double(q->get4(0, 0, t, d)) * k->get4(0, 0, s, d);
            scores[s] = float(acc);
        }
        float  mx  = std::max(scores[0], std::max(scores[1], scores[2]));
        double sum = 0.0;
        for (int s = 0; s < S; ++s) sum += std::exp(double(scores[s] - mx));
        for (int d = 0; d < D; ++d) {
            double acc = 0.0;
            for (int s = 0; s < S; ++s) acc += std::exp(double(scores[s] - mx)) * v->get4(0, 0, s, d);
            CHECK(std::fabs(out->get4(0, 0, t, d) - float(acc / sum)) < 1e-4f);
        }
    };
    ref(0);
    ref(1);

    // masked: -inf mask forces first key to zero weight
    std::unique_ptr<Tensor> mask(tf->zeros4(B, H, T, S));
    for (int t = 0; t < T; ++t) mask->set4(0, 0, t, 0, -1000.f);
    std::unique_ptr<Tensor> outm(tf->sdpaMasked(q.get(), k.get(), v.get(), mask.get(), 1.f));
    // masking the first key must change the output
    CHECK(std::fabs(outm->get4(0, 0, 0, 0) - out->get4(0, 0, 0, 0)) > 1e-3f);
}

TEST_CASE("tensor.resize2d") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros4(1, 1, 2, 2));
    x->set4(0, 0, 0, 0, 1.f);
    x->set4(0, 0, 0, 1, 2.f);
    x->set4(0, 0, 1, 0, 3.f);
    x->set4(0, 0, 1, 1, 4.f);
    std::unique_ptr<Tensor> n(tf->resize2d(x.get(), 4, 4, 0));
    REQUIRE_EQ(n->getDim2(), 4);
    CHECK(std::fabs(n->get4(0, 0, 0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(n->get4(0, 0, 3, 3) - 4.f) < 1e-5f);
    std::unique_ptr<Tensor> b(tf->resize2d(x.get(), 3, 3, 1));
    CHECK(std::fabs(b->get4(0, 0, 1, 1) - 2.5f) < 1e-4f);  // bilinear center
}

TEST_CASE("tensor.matmul.batched") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->zeros3(2, 2, 2));
    a->set3(0, 0, 0, 1.f);
    a->set3(0, 0, 1, 2.f);
    a->set3(0, 1, 0, 3.f);
    a->set3(0, 1, 1, 4.f);
    a->set3(1, 0, 0, 1.f);
    a->set3(1, 0, 1, 0.f);
    a->set3(1, 1, 0, 0.f);
    a->set3(1, 1, 1, 1.f);
    std::unique_ptr<Tensor> b(tf->zeros3(2, 2, 2));
    b->set3(0, 0, 0, 1.f);
    b->set3(0, 0, 1, 0.f);
    b->set3(0, 1, 0, 0.f);
    b->set3(0, 1, 1, 1.f);
    b->set3(1, 0, 0, 2.f);
    b->set3(1, 0, 1, 1.f);
    b->set3(1, 1, 0, 1.f);
    b->set3(1, 1, 1, 2.f);
    std::unique_ptr<Tensor> c(tf->matmul(a.get(), b.get()));
    REQUIRE_EQ(c->getDim0(), 2);
    CHECK(std::fabs(c->get3(0, 0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(c->get3(0, 0, 1) - 2.f) < 1e-5f);
    CHECK(std::fabs(c->get3(1, 1, 1) - 2.f) < 1e-5f);
}

TEST_CASE("tensor.func.gameAIModel") {
    // Small "game AI" network: x @ W1 -> +b1 -> relu -> x @ W2 -> softmax
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> w1(tf->zeros2(4, 3));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) w1->set2(i, j, float(i * 3 + j + 1) * 0.1f);
    std::unique_ptr<Tensor> b1(tf->fill1(3, 0.5f));
    std::unique_ptr<Tensor> w2(tf->zeros2(3, 2));
    w2->set2(0, 0, 1.f);
    w2->set2(0, 1, -1.f);
    w2->set2(1, 0, 0.5f);
    w2->set2(1, 1, 1.f);
    w2->set2(2, 0, -0.5f);
    w2->set2(2, 1, 2.f);

    std::unique_ptr<Tensor> xEager(tf->zeros1(4));
    xEager->set1(0, 1.f);
    xEager->set1(1, 2.f);
    xEager->set1(2, 3.f);
    xEager->set1(3, 4.f);

    // eager reference
    std::unique_ptr<Tensor> xr(tf->reshape2(xEager.get(), 1, 4));
    std::unique_ptr<Tensor> h1(tf->matmul(xr.get(), w1.get()));
    std::unique_ptr<Tensor> h2(tf->add(h1.get(), b1.get()));
    std::unique_ptr<Tensor> h3(tf->relu(h2.get()));
    std::unique_ptr<Tensor> h4(tf->matmul(h3.get(), w2.get()));
    std::unique_ptr<Tensor> ref(tf->softmax(h4.get(), 1));

    // compiled graph (captures w1/b1/w2 as constants)
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x  = fn->input1(4);
    Tensor               *x2 = tf->reshape2(x, 1, 4);
    Tensor               *y  = tf->matmul(x2, w1.get());
    y                        = tf->add(y, b1.get());
    y                        = tf->relu(y);
    y                        = tf->matmul(y, w2.get());
    y                        = tf->softmax(y, 1);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor>           out(compiled->run1(xEager.get()));
    REQUIRE_EQ(out->getDim0(), 1);
    REQUIRE_EQ(out->getDim1(), 2);
    for (int j = 0; j < 2; ++j) CHECK(std::fabs(out->get2(0, j) - ref->get2(0, j)) < 1e-3f);
    CHECK(std::fabs(out->reduceSum() - 1.f) < 1e-4f);
}

TEST_CASE("tensor.func.convSoftmaxGraph") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros4(1, 1, 3, 3));
    for (int i = 0; i < 9; ++i) x->set(i, float(i + 1));
    std::unique_ptr<Tensor> w(tf->fill4(1, 1, 2, 2, 1.f));
    std::unique_ptr<Tensor> eagerConv(tf->conv2d(x.get(), w.get(), 1, 0));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in = fn->input4(1, 1, 3, 3);
    Tensor               *y  = tf->conv2d(in, w.get(), 1, 0);
    y                        = tf->relu(y);
    y                        = tf->reshape2(y, 1, 4);
    y                        = tf->softmax(y, 1);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor>           out(compiled->run1(x.get()));
    std::unique_ptr<Tensor>           sm(tf->softmax(tf->reshape2(eagerConv.get(), 1, 4), 1));
    REQUIRE_EQ(out->getDim0(), 1);
    REQUIRE_EQ(out->getDim1(), 4);
    for (int j = 0; j < 4; ++j) CHECK(std::fabs(out->get2(0, j) - sm->get2(0, j)) < 1e-4f);
}

TEST_CASE("tensor.func.fiveSixInputs") {
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *a = fn->input1(2);
    Tensor               *b = fn->input1(2);
    Tensor               *c = fn->input1(2);
    Tensor               *d = fn->input1(2);
    Tensor               *e = fn->input1(2);
    Tensor               *f = fn->input1(2);
    Tensor               *y = tf->add(a, tf->add(b, tf->add(c, tf->add(d, tf->add(e, f)))));
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getPlaceholderCount(), 6);
    std::unique_ptr<Tensor> v1(tf->fill1(2, 1.f));
    std::unique_ptr<Tensor> v2(tf->fill1(2, 2.f));
    std::unique_ptr<Tensor> v3(tf->fill1(2, 3.f));
    std::unique_ptr<Tensor> v4(tf->fill1(2, 4.f));
    std::unique_ptr<Tensor> v5(tf->fill1(2, 5.f));
    std::unique_ptr<Tensor> v6(tf->fill1(2, 6.f));
    std::unique_ptr<Tensor> out(compiled->run6(v1.get(), v2.get(), v3.get(), v4.get(), v5.get(), v6.get()));
    CHECK(std::fabs(out->get1(0) - 21.f) < 1e-4f);
}

TEST_CASE("tensor.func.transformerBlockInference") {
    // A general neural-network inference graph: layernorm -> SDPA attention ->
    // residual -> MLP(gelu) -> residual -> classifier softmax. Verified against
    // the eager reference and (when a live Vulkan device exists) on the generated GPU kernels.
    auto       *tf = TF::create();
    const int   B = 1, T = 2, D = 4, C = 3;
    const float scale = 1.f / std::sqrt(float(D));
    const float eps   = 1e-5f;

    auto rand = [&](int d0, int d1, float s) {
        std::unique_ptr<Tensor> w(tf->randomNormal2(d0, d1));
        w.reset(w->mulScalar(s));
        return w;
    };
    std::unique_ptr<Tensor> Wq(rand(D, D, 0.2f));
    std::unique_ptr<Tensor> Wk(rand(D, D, 0.2f));
    std::unique_ptr<Tensor> Wv(rand(D, D, 0.2f));
    std::unique_ptr<Tensor> W1(rand(D, 2 * D, 0.2f));
    std::unique_ptr<Tensor> W2(rand(2 * D, D, 0.2f));
    std::unique_ptr<Tensor> Wc(rand(D, C, 0.2f));

    std::unique_ptr<Tensor> xEager(tf->randomNormal3(B, T, D));

    // eager reference
    std::unique_ptr<Tensor> ln1(tf->layernorm(xEager.get(), eps));
    std::unique_ptr<Tensor> ln1m(tf->reshape2(ln1.get(), B * T, D));
    std::unique_ptr<Tensor> q(tf->reshape4(tf->matmul(ln1m.get(), Wq.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> k(tf->reshape4(tf->matmul(ln1m.get(), Wk.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> v(tf->reshape4(tf->matmul(ln1m.get(), Wv.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> att(tf->reshape3(tf->sdpa(q.get(), k.get(), v.get(), scale), B, T, D));
    std::unique_ptr<Tensor> x1(tf->add(xEager.get(), att.get()));
    std::unique_ptr<Tensor> ln2(tf->layernorm(x1.get(), eps));
    std::unique_ptr<Tensor> ln2m(tf->reshape2(ln2.get(), B * T, D));
    std::unique_ptr<Tensor> h(tf->matmul(ln2m.get(), W1.get()));
    h.reset(tf->gelu(h.get()));
    h.reset(tf->matmul(h.get(), W2.get()));
    std::unique_ptr<Tensor> x2(tf->add(x1.get(), tf->reshape3(h.get(), B, T, D)));
    std::unique_ptr<Tensor> logits(tf->matmul(tf->reshape2(x2.get(), B * T, D), Wc.get()));
    std::unique_ptr<Tensor> ref(tf->softmax(logits.get(), 1));

    // compiled graph (weights captured as constants)
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x   = fn->input3(B, T, D);
    Tensor               *l1  = tf->layernorm(x, eps);
    Tensor               *l1m = tf->reshape2(l1, B * T, D);
    Tensor               *qq  = tf->reshape4(tf->matmul(l1m, Wq.get()), 1, 1, T, D);
    Tensor               *kk  = tf->reshape4(tf->matmul(l1m, Wk.get()), 1, 1, T, D);
    Tensor               *vv  = tf->reshape4(tf->matmul(l1m, Wv.get()), 1, 1, T, D);
    Tensor               *a1  = tf->reshape3(tf->sdpa(qq, kk, vv, scale), B, T, D);
    Tensor               *x1s = tf->add(x, a1);
    Tensor               *l2  = tf->layernorm(x1s, eps);
    Tensor               *l2m = tf->reshape2(l2, B * T, D);
    Tensor               *hh  = tf->matmul(l2m, W1.get());
    hh                        = tf->gelu(hh);
    hh                        = tf->matmul(hh, W2.get());
    Tensor *x2s               = tf->add(x1s, tf->reshape3(hh, B, T, D));
    Tensor *lg                = tf->matmul(tf->reshape2(x2s, B * T, D), Wc.get());
    fn->setOutput(tf->softmax(lg, 1));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    REQUIRE_EQ(out->getDim0(), B * T);
    REQUIRE_EQ(out->getDim1(), C);
    for (int i = 0; i < out->getSize(); ++i)
        // Compiled may run on GPU (the headless device persists across cases):
        // float32 layernorm/softmax/matmul accumulation order differs slightly.
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-2f);
    CHECK(std::fabs(out->reduceSum() - float(B * T)) < 1e-2f);

    // action selection (game-AI style) from the compiled logits
    std::unique_ptr<Tensor> action(tf->argmax(out.get(), 1, 0));
    REQUIRE_EQ(action->getSize(), B * T);
}

// --- new-op GPU paths (only run with a live Vulkan device; headless) -------

TEST_CASE("tensor.eager.binaryAndUnaryOps") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->fill2(2, 2, 4.f));
    std::unique_ptr<Tensor> b(tf->fill2(2, 2, 2.f));
    std::unique_ptr<Tensor> sub(tf->sub(a.get(), b.get()));
    CHECK(std::fabs(sub->get(0) - 2.f) < 1e-5f);
    std::unique_ptr<Tensor> div(tf->div(a.get(), b.get()));
    CHECK(std::fabs(div->get(0) - 2.f) < 1e-5f);
    std::unique_ptr<Tensor> neg(tf->neg(a.get()));
    CHECK(std::fabs(neg->get(0) + 4.f) < 1e-5f);
    std::unique_ptr<Tensor> negB(tf->fill1(3, -3.f));
    std::unique_ptr<Tensor> absv(tf->abs(negB.get()));
    CHECK(std::fabs(absv->get1(0) - 3.f) < 1e-5f);
    std::unique_ptr<Tensor> sq(tf->fill1(3, 9.f));
    std::unique_ptr<Tensor> sqrtv(tf->sqrt(sq.get()));
    CHECK(std::fabs(sqrtv->get1(0) - 3.f) < 1e-5f);
    std::unique_ptr<Tensor> zero(tf->zeros1(3));
    std::unique_ptr<Tensor> expv(tf->exp(zero.get()));
    CHECK(std::fabs(expv->get1(0) - 1.f) < 1e-5f);
    std::unique_ptr<Tensor> one(tf->fill1(3, std::exp(1.f)));
    std::unique_ptr<Tensor> logv(tf->log(one.get()));
    CHECK(std::fabs(logv->get1(0) - 1.f) < 1e-4f);
    std::unique_ptr<Tensor> sinv(tf->sin(zero.get()));
    CHECK(std::fabs(sinv->get1(0)) < 1e-6f);
    std::unique_ptr<Tensor> cosv(tf->cos(zero.get()));
    CHECK(std::fabs(cosv->get1(0) - 1.f) < 1e-6f);
    std::unique_ptr<Tensor> tanhv(tf->tanh(zero.get()));
    CHECK(std::fabs(tanhv->get1(0)) < 1e-6f);
    std::unique_ptr<Tensor> sig(tf->sigmoid(zero.get()));
    CHECK(std::fabs(sig->get1(0) - 0.5f) < 1e-6f);
    std::unique_ptr<Tensor> powv(tf->powScalar(b.get(), 3.f));
    CHECK(std::fabs(powv->get(0) - 8.f) < 1e-5f);
    std::unique_ptr<Tensor> cl(tf->fill1(3, 2.f));
    std::unique_ptr<Tensor> clamped(tf->clamp(cl.get(), 0.f, 1.f));
    CHECK(std::fabs(clamped->get1(0) - 1.f) < 1e-6f);
    std::unique_ptr<Tensor> mx(tf->maximumScalar(cl.get(), 3.f));
    CHECK(std::fabs(mx->get1(0) - 3.f) < 1e-6f);
    std::unique_ptr<Tensor> mn(tf->minimumScalar(cl.get(), 1.f));
    CHECK(std::fabs(mn->get1(0) - 1.f) < 1e-6f);
}

TEST_CASE("tensor.eager.inplaceAndDot") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> p(tf->fill2(2, 2, 3.f));
    std::unique_ptr<Tensor> q(tf->fill2(2, 2, 2.f));
    p->addInPlace(q.get());       // 5
    p->multiplyInPlace(q.get());  // 10
    p->addScalarInPlace(1.f);     // 11
    CHECK(std::fabs(p->get(0) - 11.f) < 1e-5f);
    p->mulScalarInPlace(0.5f);  // 5.5
    CHECK(std::fabs(p->get(3) - 5.5f) < 1e-5f);

    std::unique_ptr<Tensor> v1(tf->zeros1(3));
    v1->set1(0, 1.f);
    v1->set1(1, 2.f);
    v1->set1(2, 3.f);
    std::unique_ptr<Tensor> v2(tf->zeros1(3));
    v2->set1(0, 4.f);
    v2->set1(1, 5.f);
    v2->set1(2, 6.f);
    CHECK(std::fabs(v1->dot(v2.get()) - 32.f) < 1e-5f);
}

TEST_CASE("tensor.eager.factoriesAndAxisReduces") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> z5(tf->zeros5(1, 2, 3, 4, 5));
    CHECK_EQ(z5->getRank(), 5);
    CHECK_EQ(z5->getSize(), 120);
    std::unique_ptr<Tensor> z6(tf->zeros6(1, 1, 1, 1, 1, 2));
    CHECK_EQ(z6->getRank(), 6);
    CHECK_EQ(z6->getSize(), 2);
    std::unique_ptr<Tensor> o3(tf->ones3(2, 2, 2));
    CHECK(std::fabs(o3->reduceSum() - 8.f) < 1e-5f);
    std::unique_ptr<Tensor> o4(tf->ones4(1, 2, 1, 2));
    CHECK_EQ(o4->getSize(), 4);
    std::unique_ptr<Tensor> f3(tf->fill3(2, 1, 1, 5.f));
    CHECK(std::fabs(f3->reduceSum() - 10.f) < 1e-5f);
    std::unique_ptr<Tensor> f4(tf->fill4(1, 1, 2, 2, 7.f));
    CHECK(std::fabs(f4->reduceSum() - 28.f) < 1e-5f);
    std::unique_ptr<Tensor> eye(tf->eye(3));
    CHECK(std::fabs(eye->get2(0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(eye->get2(1, 1) - 1.f) < 1e-5f);
    CHECK(std::fabs(eye->get2(0, 1)) < 1e-6f);
    std::unique_ptr<Tensor> cs(tf->constantScalar(3.5f));
    CHECK_EQ(cs->getSize(), 1);
    CHECK(std::fabs(cs->get(0) - 3.5f) < 1e-5f);
    std::unique_ptr<Tensor> ru1(tf->randomUniform1(8));
    CHECK(ru1->reduceMin() >= 0.f);
    CHECK(ru1->reduceMax() <= 1.f);
    std::unique_ptr<Tensor> rn1(tf->randomNormal1(8));
    CHECK(std::isfinite(rn1->reduceSum()));
    std::unique_ptr<Tensor> ru4(tf->randomUniform4(1, 1, 2, 2));
    CHECK_EQ(ru4->getSize(), 4);

    std::unique_ptr<Tensor> x(tf->zeros2(2, 3));
    x->set2(0, 0, 1.f);
    x->set2(0, 1, 2.f);
    x->set2(0, 2, 3.f);
    x->set2(1, 0, 4.f);
    x->set2(1, 1, 5.f);
    x->set2(1, 2, 6.f);
    std::unique_ptr<Tensor> mean(tf->meanAxis(x.get(), 1, 0));
    CHECK_EQ(mean->getRank(), 1);
    CHECK(std::fabs(mean->get1(0) - 2.f) < 1e-5f);
    CHECK(std::fabs(mean->get1(1) - 5.f) < 1e-5f);
    std::unique_ptr<Tensor> mn(tf->minAxis(x.get(), 1, 0));
    CHECK(std::fabs(mn->get1(0) - 1.f) < 1e-5f);
    std::unique_ptr<Tensor> mx(tf->maxAxis(x.get(), 1, 1));
    CHECK_EQ(mx->getRank(), 2);
    CHECK(std::fabs(mx->get2(1, 0) - 6.f) < 1e-5f);
    std::unique_ptr<Tensor> keep(tf->sumAxis(x.get(), 1, 1));
    CHECK_EQ(keep->getDim1(), 1);
    CHECK(std::fabs(keep->get2(0, 0) - 6.f) < 1e-5f);
}

TEST_CASE("tensor.eager.castAndMaskedSdpa") {
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> c(tf->cast(tf->fill2(2, 2, 2.9f), "int32"));
    CHECK_EQ(c->getDtype(), std::string("int32"));
    CHECK(std::fabs(c->get(0) - 2.9f) < 1e-5f);  // int32 stored losslessly as float

    std::unique_ptr<Tensor> q(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> k(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> v(tf->randomNormal4(1, 1, 2, 4));
    std::unique_ptr<Tensor> mask(tf->ones4(1, 1, 2, 2));
    std::unique_ptr<Tensor> ref(tf->sdpa(q.get(), k.get(), v.get(), 0.5f));
    std::unique_ptr<Tensor> masked(tf->sdpaMasked(q.get(), k.get(), v.get(), mask.get(), 0.5f));
    REQUIRE_EQ(masked->getSize(), ref->getSize());
    for (int i = 0; i < masked->getSize(); ++i) CHECK(std::fabs(masked->get(i) - ref->get(i)) < 1e-4f);
}
}  // namespace test_tensor_unit
#endif
