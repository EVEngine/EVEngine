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

std::string assetPath(const std::string &name) {
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

bool loadFile(const std::string &path, std::vector<char> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    f.read(out.data(), n);
    return !f.fail() || n == 0;
}

bool loadModel(Model &m) {
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

std::vector<float> readCsvInts(const std::string &path) {
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

Tensor *toTensor(const std::vector<float> &data) {
    const int dims[1] = {static_cast<int>(data.size())};
    auto     *t       = new Tensor(dims, 1);
    for (size_t i = 0; i < data.size(); ++i) t->set(static_cast<int>(i), data[i]);
    return t;
}

Tensor *toTensor(const Weight &wt) {
    auto *t = new Tensor(wt.dims.data(), static_cast<int>(wt.dims.size()));
    for (size_t i = 0; i < wt.data.size(); ++i) t->set(static_cast<int>(i), wt.data[i]);
    return t;
}

/** Position ids [0..n) as a plain eager tensor (traceable as a constant). */
Tensor *arangeTensor(int n) {
    auto *t = new Tensor(n);
    for (int i = 0; i < n; ++i) t->set1(i, float(i));
    return t;
}

/** Causal mask [1, H, T, T]: 0 for s<=t, -1e4 for s>t. */
Tensor *causalMask(TF *tf, int T, int H) {
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

WeightMap buildWeightTensors(const Model &m) {
    WeightMap out;
    for (const auto &kv : m.weights) out[kv.first] = toTensor(kv.second);
    return out;
}

const char *quantTag(DType dt) {
    switch (dt) {
        case DType::Fp16: return "fp16";
        case DType::Fp8E4M3: return "fp8";
        case DType::Fp4E2M1: return "fp4";
        case DType::Int8: return "int8";
        case DType::Int4: return "int4";
        default: return "float32";
    }
}

WeightMap buildQuantWeightTensors(TF *tf, const Model &m, DType dt, int group) {
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

Tensor *buildHidden(TF *tf, Tensor *ids, const WeightMap &wm) {
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
Tensor *fullLogits(TF *tf, Tensor *hidden, const WeightMap &wm) {
    Tensor *head = wm.at("lm_head.weight");
    return tf->matmul(hidden, tf->transpose(head));
}

int greedyNext(TF *tf, const std::vector<float> &ids, const WeightMap &wm) {
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

void softmaxRows(float *data, int rows, int cols) {
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

std::string decode(const Model &m, const std::vector<float> &ids) {
    std::string s;
    for (float f : ids) {
        const int id = static_cast<int>(f);
        if (id >= 0 && id < static_cast<int>(m.tokens.size())) s += m.tokens[static_cast<size_t>(id)];
    }
    return s;
}

Model *model() {
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

bool ensureReference(Model &m) {
    if (m.reference.empty()) m.reference = readCsvInts(assetPath("reference_tokens.txt"));
    if (m.prompt.empty()) m.prompt = readCsvInts(assetPath("prompt.txt"));
    return !m.reference.empty() && !m.prompt.empty();
}

}  // namespace

TEST_CASE("tensor.llm.loadBundle") {
    Model *mp = model();
    if (!mp) return;
    const Model &m = *mp;
    CHECK_EQ(static_cast<int>(m.weights.size()), 101);
    const auto &wte = m.w("transformer.wte.weight");
    CHECK_EQ(wte.dims.size(), size_t(2));
    CHECK_EQ(wte.dims[0], kVocab);
    CHECK_EQ(wte.dims[1], kEmbDim);
    const auto &head = m.w("lm_head.weight");
    CHECK_EQ(head.dims[0], kVocab);
    const auto &ca = m.w("transformer.h.0.attn.c_attn.weight");
    CHECK_EQ(ca.dims[0], kEmbDim);
    CHECK_EQ(ca.dims[1], 3 * kEmbDim);
    CHECK_EQ(m.tokens.size(), size_t(kVocab));
}

TEST_CASE("tensor.llm.forwardMatchesNumpy") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    // Reference context: first 16 tokens of the numpy greedy generation.
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    std::vector<char> buf;
    REQUIRE(loadFile(assetPath("probs16.bin"), buf));
    REQUIRE_EQ(buf.size(), size_t(16 * kVocab * 4));

    auto     *tf     = TF::create();
    Tensor   *in     = toTensor(ctx);
    WeightMap wm     = buildWeightTensors(m);
    Tensor   *logits = fullLogits(tf, buildHidden(tf, in, wm), wm);
    REQUIRE_EQ(logits->getSize(), 16 * kVocab);
    std::vector<float> probs(logits->data(), logits->data() + logits->getSize());
    softmaxRows(probs.data(), 16, kVocab);

    const float *ref       = reinterpret_cast<const float *>(buf.data());
    float        maxDiff   = 0.f;
    int          top1Match = 0;
    for (int r = 0; r < 16; ++r) {
        int engineArgmax = 0, refArgmax = 0;
        for (int c = 0; c < kVocab; ++c) {
            const float e = probs[static_cast<size_t>(r) * kVocab + c];
            const float g = ref[static_cast<size_t>(r) * kVocab + c];
            maxDiff       = std::max(maxDiff, std::fabs(e - g));
            if (e > probs[static_cast<size_t>(r) * kVocab + engineArgmax]) engineArgmax = c;
            if (g > ref[static_cast<size_t>(r) * kVocab + refArgmax]) refArgmax = c;
        }
        if (engineArgmax == refArgmax) ++top1Match;
    }
    std::fprintf(stderr, "[llm] eager-vs-numpy max prob diff = %.3e, top-1 agreement %d/16\n", maxDiff, top1Match);
    CHECK(maxDiff < 1e-4f);
    CHECK_GE(top1Match, 14);

    delete logits;
    delete in;
}

TEST_CASE("tensor.llm.compiledMatchesEager") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    auto     *tf          = TF::create();
    Tensor   *in          = toTensor(ctx);
    WeightMap wm          = buildWeightTensors(m);
    Tensor   *eagerLogits = fullLogits(tf, buildHidden(tf, in, wm), wm);

    auto   *fn = tf->func();
    Tensor *ph = fn->input1(16);
    fn->setOutput(fullLogits(tf, buildHidden(tf, ph, wm), wm));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    Tensor                           *out = compiled->run1(in);

    std::fprintf(stderr, "[llm] compiled device = %s\n", compiled->getDevice().c_str());
    float maxDiff = 0.f;
    for (int i = 0; i < out->getSize(); ++i) maxDiff = std::max(maxDiff, std::fabs(out->get(i) - eagerLogits->get(i)));
    std::fprintf(stderr, "[llm] compiled-vs-eager max logit diff = %.3e\n", maxDiff);
    CHECK(maxDiff < 1e-4f);

    delete out;
    delete in;
    delete eagerLogits;
}

TEST_CASE("tensor.llm.generate") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), m.prompt.size() + size_t(kGenToks));
    REQUIRE_GT(m.prompt.size(), size_t(0));

    auto              *tf      = TF::create();
    WeightMap          wm      = buildWeightTensors(m);
    std::vector<float> gen     = m.prompt;
    const auto         t0      = std::chrono::steady_clock::now();
    int                matches = 0;
    const size_t       refOff  = m.prompt.size();  // reference includes the prompt prefix
    for (int i = 0; i < kGenToks; ++i) {
        const int tok = greedyNext(tf, gen, wm);
        gen.push_back(float(tok));
        if (tok == static_cast<int>(m.reference[refOff + static_cast<size_t>(i)])) ++matches;
        if (tok == kEos) break;
    }
    const double msPerTok = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
                            static_cast<double>(gen.size() - m.prompt.size());

    std::fprintf(stderr,
                 "[llm] generated %zu tokens (%.1f ms/token), greedy match vs numpy "
                 "%d/%d\n",
                 gen.size() - m.prompt.size(), msPerTok, matches, kGenToks);
    std::fprintf(stderr, "[llm] prompt:  %s\n", decode(m, m.prompt).c_str());
    std::fprintf(stderr, "[llm] engine:  %s\n", decode(m, gen).c_str());
    std::fprintf(stderr, "[llm] numpy:   %s\n",
                 decode(m, std::vector<float>(m.reference.begin() + ptrdiff_t(refOff),
                                              m.reference.begin() + ptrdiff_t(refOff) + kGenToks))
                     .c_str());
    CHECK_GE(matches, static_cast<int>(kGenToks * 0.8));
}

TEST_CASE("tensor.llm.quantizedDtypes") {
    Model *mp = model();
    if (!mp || !ensureReference(*mp)) return;
    Model &m = *mp;
    REQUIRE_GE(m.reference.size(), size_t(16));
    const std::vector<float> ctx(m.reference.begin(), m.reference.begin() + 16);

    auto              *tf   = TF::create();
    Tensor            *in   = toTensor(ctx);
    WeightMap          wm32 = buildWeightTensors(m);
    Tensor            *base = fullLogits(tf, buildHidden(tf, in, wm32), wm32);
    std::vector<float> baseProbs(base->data(), base->data() + base->getSize());
    softmaxRows(baseProbs.data(), 16, kVocab);

    const struct {
        DType dt;
        int   minTop1;
    } cases[] = {
        {DType::Fp16, 16}, {DType::Int8, 15},    {DType::Fp8E4M3, 13},
        {DType::Int4, 10}, {DType::Fp4E2M1, 14},  // block scale = maxAbs/6 uses the full e2m1 range
    };
    for (const auto &c : cases) {
        WeightMap          wq     = buildQuantWeightTensors(tf, m, c.dt, 64);
        Tensor            *logits = fullLogits(tf, buildHidden(tf, in, wq), wq);
        std::vector<float> probs(logits->data(), logits->data() + logits->getSize());
        softmaxRows(probs.data(), 16, kVocab);

        float maxDiff = 0.f;
        int   top1    = 0;
        for (int r = 0; r < 16; ++r) {
            int a = 0, b = 0;
            for (int c2 = 0; c2 < kVocab; ++c2) {
                const float e = probs[static_cast<size_t>(r) * kVocab + c2];
                const float g = baseProbs[static_cast<size_t>(r) * kVocab + c2];
                maxDiff       = std::max(maxDiff, std::fabs(e - g));
                if (e > probs[static_cast<size_t>(r) * kVocab + a]) a = c2;
                if (g > baseProbs[static_cast<size_t>(r) * kVocab + b]) b = c2;
            }
            if (a == b) ++top1;
        }
        long long bytes = 0;
        for (const auto &kv : m.weights) {
            const int          n      = int(kv.second.data.size());
            const std::string &nm     = kv.first;
            const bool         matrix = nm.find(".bias") == std::string::npos && nm.find("ln_") == std::string::npos;
            if (matrix) {
                bytes += q::quantByteSize(c.dt, n);
                if (c.dt == DType::Int8 || c.dt == DType::Int4)
                    bytes += int64_t((n + 63) / 64) * int64_t(sizeof(float));
            } else {
                bytes += int64_t(n) * int64_t(sizeof(float));
            }
        }
        std::fprintf(stderr,
                     "[llm] quant %s: max prob diff = %.4f, top-1 %d/16, weights %.2f MB "
                     "(fp32 27.6 MB)\n",
                     quantTag(c.dt), maxDiff, top1, double(bytes) / (1024.0 * 1024.0));
        CHECK_GE(top1, c.minTop1);
        delete logits;
        for (auto &kv : wq) delete kv.second;
    }
    delete base;
    delete in;
}

// tensor_onnx.cpp
namespace {
using Bytes = std::vector<uint8_t>;
void varint(Bytes &b, uint64_t n) {
    while (n >= 128) {
        b.push_back(static_cast<uint8_t>(n) | 128);
        n >>= 7;
    }
    b.push_back(static_cast<uint8_t>(n));
}
void number(Bytes &b, int id, uint64_t n) {
    varint(b, id * 8);
    varint(b, n);
}
void field(Bytes &b, int id, const Bytes &value) {
    varint(b, id * 8 + 2);
    varint(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
Bytes text(const char *s) { return Bytes(s, s + std::char_traits<char>::length(s)); }
Bytes unknownModel() {
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

TEST_CASE("tensor.onnx.import.transactionAndDiagnostics") {
    using eve::tensor::OnnxModel;
    auto bytes = unknownModel();
    auto good  = OnnxModel::load(bytes);
    REQUIRE(good.ok());
    CHECK(good.value()->info().unsupportedNodes.size() == 1);
    auto rejected = good.value()->run({});
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK(rejected.error()->code() == eve::DiagnosticCode::Unsupported);
    CHECK(rejected.error()->path() == "unsupported-node");
    bytes.pop_back();
    auto broken = OnnxModel::load(bytes);
    CHECK(!broken.ok());
    CHECK(good.value()->info().nodeCount == 1);
    auto empty = OnnxModel::load({});
    CHECK(!empty.ok());
    auto overflow = OnnxModel::load(Bytes(12, 255));
    CHECK(!overflow.ok());
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

TEST_CASE("tensor.onnx.gpu.failureAndScope") {
    class FailedGpu final : public eve::tensor::OnnxCompute {
    public:
        int                               begins = 0, ends = 0, dispatches = 0;
        eve::Result<std::vector<uint8_t>> dispatch(const eve::tensor::OnnxKernel &) override {
            ++dispatches;
            return eve::Result<std::vector<uint8_t>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected GPU failure"));
        }

    protected:
        void beginRun() noexcept override { ++begins; }
        void endRun() noexcept override { ++ends; }
    } gpu;

    auto unsupported = eve::tensor::OnnxModel::load(unknownModel());
    REQUIRE(unsupported.ok());

    auto rejected = unsupported.value()->runGpu({}, gpu);

    CHECK(!rejected.ok());
    CHECK(gpu.dispatches == 0);
    CHECK(gpu.begins == 1);
    CHECK(gpu.ends == 1);
    Bytes tensor, node, graph, output, opset, model;
    number(tensor, 2, 1);
    field(tensor, 8, text("x"));
    float value = -2;
    Bytes data(4);
    std::memcpy(data.data(), &value, 4);
    field(tensor, 9, data);
    field(node, 1, text("x"));
    field(node, 2, text("y"));
    field(node, 3, text("relu-test"));
    field(node, 4, text("Relu"));
    field(graph, 1, node);
    field(graph, 5, tensor);
    field(output, 1, text("y"));
    field(graph, 12, output);
    number(opset, 2, 14);
    number(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    auto imported = eve::tensor::OnnxModel::load(model);
    REQUIRE(imported.ok());

    auto failed = imported.value()->runGpu({}, gpu);
    REQUIRE(!failed.ok());
    CHECK(failed.error()->code() == eve::DiagnosticCode::Failed);
    CHECK(failed.error()->path() == "relu-test");
    CHECK(gpu.dispatches == 1);
    CHECK(gpu.begins == 2);
    CHECK(gpu.ends == 2);
    auto cpu = imported.value()->run({});
    REQUIRE(cpu.ok());
    float out = -1;
    std::memcpy(&out, cpu.value()[0].tensor.bytes.data(), 4);
    CHECK(out == 0);
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

std::vector<float> randData(int n, unsigned seed) {
    std::vector<float> v(static_cast<size_t>(n));
    unsigned           s = seed;
    for (int i = 0; i < n; ++i) {
        s                         = s * 1664525u + 1013904223u;
        v[static_cast<size_t>(i)] = float(int(s % 2001) - 1000) / 1000.f;  // [-1, 1]
    }
    return v;
}

Tensor *fromData(const std::vector<float> &d, int d0, int d1) {
    auto *t = new Tensor(d0, d1);
    for (size_t i = 0; i < d.size(); ++i) t->set(int(i), d[static_cast<size_t>(i)]);
    return t;
}

float maxAbsDiff(const Tensor *a, const Tensor *b) {
    float m = 0.f;
    for (int i = 0; i < a->getSize(); ++i) m = std::max(m, std::fabs(a->get(i) - b->get(i)));
    return m;
}

const char *dtypeTag(DType dt) {
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
