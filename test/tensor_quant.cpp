#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "tensor/Graph.h"
#include "tensor/Quant.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace eve::tensor;

namespace {

std::vector<float> randData(int n, unsigned seed) {
    std::vector<float> v(static_cast<size_t>(n));
    unsigned s = seed;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
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
    auto *tf = TF::create();
    const std::vector<float> src = randData(32 * 48, 7);
    Tensor *ref = fromData(src, 32, 48);

    const struct {
        DType dt;
        float bound;
    } cases[] = {
        {DType::Fp16, 1e-3f},
        {DType::Int8, 0.03f},
        {DType::Fp8E4M3, 0.08f},
        {DType::Int4, 0.3f},
        {DType::Fp4E2M1, 0.35f},
    };
    for (const auto &c : cases) {
        Tensor *q = tf->quantizeWeight(ref, dtypeTag(c.dt), 16);
        REQUIRE(q != nullptr);
        CHECK(q->isQuantized());
        CHECK_EQ(std::string(q->getDtype()), std::string(dtypeTag(c.dt)));
        float maxErr = 0.f;
        for (int i = 0; i < q->getSize(); ++i)
            maxErr = std::max(maxErr, std::fabs(q->get(i) - src[static_cast<size_t>(i)]));
        std::fprintf(stderr, "[quant] %s round-trip max err = %.4f (bound %.3f)\n",
                     dtypeTag(c.dt), maxErr, c.bound);
        CHECK(maxErr < c.bound);
        delete q;
    }
    delete ref;
}

TEST_CASE("tensor.quant.eagerMatmul") {
    auto *tf = TF::create();
    const std::vector<float> ad = randData(4 * 8, 11);
    const std::vector<float> bd = randData(8 * 16, 23);
    Tensor *a = fromData(ad, 4, 8);
    Tensor *b = fromData(bd, 8, 16);
    Tensor *baseline = a->matmul(b);

    const struct {
        DType dt;
        float bound;
    } cases[] = {
        {DType::Fp16, 0.01f},
        {DType::Int8, 0.15f},
        {DType::Fp8E4M3, 0.35f},
        {DType::Int4, 1.2f},
        {DType::Fp4E2M1, 1.4f},
    };
    for (const auto &c : cases) {
        Tensor *qb = tf->quantizeWeight(b, dtypeTag(c.dt), 8);
        Tensor *out = a->matmul(qb);
        const float diff = maxAbsDiff(out, baseline);
        std::fprintf(stderr, "[quant] eager %s matmul max diff = %.4f (bound %.2f)\n",
                     dtypeTag(c.dt), diff, c.bound);
        CHECK(diff < c.bound);
        delete qb;
        delete out;
    }
    delete a;
    delete b;
    delete baseline;
}

TEST_CASE("tensor.quant.compiledMatmul") {
    auto *tf = TF::create();
    const std::vector<float> ad = randData(4 * 8, 31);
    const std::vector<float> bd = randData(8 * 16, 43);
    Tensor *a = fromData(ad, 4, 8);
    Tensor *b = fromData(bd, 8, 16);

    for (const char *tag : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        Tensor *qb = tf->quantizeWeight(b, tag, 8);
        Tensor *eager = a->matmul(qb);

        auto *fn = tf->func();
        Tensor *ph = fn->input2(4, 8);
        fn->setOutput(tf->matmul(ph, qb));  // quantized const captured as a graph const
        std::unique_ptr<CompiledFunction> cf(fn->compile());
        Tensor *out = cf->run1(a);
        const float diff = maxAbsDiff(out, eager);
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
    auto *tf = TF::create();
    const std::vector<float> td = randData(10 * 6, 51);
    Tensor *table = fromData(td, 10, 6);
    const float idxv[4] = {0.f, 3.f, 9.f, 5.f};
    Tensor *idx = new Tensor(4);
    for (int i = 0; i < 4; ++i) idx->set1(i, idxv[i]);
    Tensor *baseline = tf->embedding(table, idx);

    for (const char *tag : {"fp16", "int8", "fp8", "int4", "fp4"}) {
        Tensor *qt = tf->quantizeWeight(table, tag, 6);
        Tensor *out = tf->embedding(qt, idx);
        const float diff = maxAbsDiff(out, baseline);
        std::fprintf(stderr, "[quant] eager %s embedding max diff = %.4f\n", tag, diff);
        CHECK(diff < 0.4f);

        auto *fn = tf->func();
        Tensor *ph = fn->input1(4);
        fn->setOutput(tf->embedding(qt, ph));
        std::unique_ptr<CompiledFunction> cf(fn->compile());
        Tensor *cout = cf->run1(idx);
        const float cdiff = maxAbsDiff(cout, out);
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
    auto *tf = TF::create();
    Tensor *t = tf->fill2(2, 2, 1.f);
    Tensor *q = tf->quantizeWeight(t, "int8", 2);
    bool threw = false;
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
