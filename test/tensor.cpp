#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "tensor/Graph.h"
#include "tensor/Optimizer.h"
#include "tensor/KernelGen.h"

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
#include "window/Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

using namespace eve::tensor;

namespace {

/** GPU tensor tests need a live Vulkan device, which only exists after a window is created. */
bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

}  // namespace

TEST_CASE("tensor.create.zerosOnes") {
    auto *tf = TF::create();
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
    auto *tf = TF::create();
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
    auto *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->arange(5));
    CHECK(std::fabs(tf->reduceSum(a.get()) - 10.f) < 1e-5f);
    CHECK(std::fabs(tf->reduceMean(a.get()) - 2.f) < 1e-5f);
    a->mulScalarInPlace(2.f);
    CHECK(std::fabs(a->get1(4) - 8.f) < 1e-5f);
    a->reluInPlace();
}

TEST_CASE("tensor.reshape.transpose") {
    auto *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->arange(6));
    std::unique_ptr<Tensor> b(tf->reshape2(a.get(), 2, 3));
    CHECK_EQ(b->get2(1, 2), 5.f);
    std::unique_ptr<Tensor> c(tf->transpose(b.get()));
    CHECK_EQ(c->getDim0(), 3);
    CHECK_EQ(c->getDim1(), 2);
    CHECK_EQ(c->get2(2, 1), 5.f);
}

TEST_CASE("tensor.activations.where") {
    auto *tf = TF::create();
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
    Tensor *x = fn->input2(2, 3);
    Tensor *y = tf->matmul(x, w.get());  // captures w as Const
    y         = tf->relu(y);
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
    auto *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor *x = fn->input1(4);
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
    std::unique_ptr<Tensor> feed(tf->arange(4));
    std::unique_ptr<Tensor> out(compiled->run1(feed.get()));
    CHECK(std::fabs(out->get1(3) - 3.f) < 1e-5f);
}

// --- GPU (compute shader) path ---------------------------------------------
// These skip (return) when no Vulkan device / glslc is available (e.g. some CI
// images), matching test/gpgpu.cpp's tolerant pattern. When the environment
// does support it, CompiledFunction::getDevice() should report "gpu".

TEST_CASE("tensor.gpu.compiledFunctionElementwiseAndMatmul") {
    if (!tryInitGpuWindow()) return;

    auto *tf = TF::create();

    std::unique_ptr<Tensor> w(tf->zeros2(3, 2));
    w->set2(0, 0, 1.f);
    w->set2(1, 0, 0.f);
    w->set2(2, 0, 1.f);
    w->set2(0, 1, 0.f);
    w->set2(1, 1, 1.f);
    w->set2(2, 1, 0.f);

    // relu(x * 2 + 1) matmul w  — exercises binary(add), unary(mul/relu chain as
    // separate nodes) and matmul kernels together.
    std::unique_ptr<Func> fn(tf->func());
    Tensor *x = fn->input2(2, 3);
    Tensor *y = tf->mulScalar(x, 2.f);
    y         = tf->addScalar(y, 1.f);
    y         = tf->relu(y);
    y         = tf->matmul(y, w.get());
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> xd(tf->zeros2(2, 3));
    xd->set2(0, 0, -1.f);
    xd->set2(0, 1, 2.f);
    xd->set2(0, 2, -3.f);
    xd->set2(1, 0, 4.f);
    xd->set2(1, 1, -5.f);
    xd->set2(1, 2, 6.f);

    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));

    // CPU reference via eager ops.
    std::unique_ptr<Tensor> ref1(xd->mulScalar(2.f));
    std::unique_ptr<Tensor> ref2(ref1->addScalar(1.f));
    std::unique_ptr<Tensor> ref3(ref2->relu());
    std::unique_ptr<Tensor> ref(ref3->matmul(w.get()));

    REQUIRE_EQ(out->getDim0(), ref->getDim0());
    REQUIRE_EQ(out->getDim1(), ref->getDim1());
    for (int i = 0; i < out->getSize(); ++i) CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-3f);

    // Confirms the compute-shader path actually ran (not a silent CPU fallback).
    // Only meaningful when glslc + a Vulkan device are both available.
    CHECK_EQ(compiled->getDevice(), std::string("gpu"));
}

TEST_CASE("tensor.gpu.compiledFunctionWhereTranspose") {
    if (!tryInitGpuWindow()) return;

    auto *tf = TF::create();

    std::unique_ptr<Func> fn(tf->func());
    Tensor *cond = fn->input1(4);
    Tensor *a    = fn->input1(4);
    Tensor *b    = fn->input1(4);
    Tensor *sel  = tf->where(cond, a, b);
    Tensor *sel2 = tf->reshape2(sel, 2, 2);
    Tensor *out  = tf->transpose(sel2);
    fn->setOutput(out);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> c(tf->zeros1(4));
    c->set1(0, 1.f);
    c->set1(1, 0.f);
    c->set1(2, 1.f);
    c->set1(3, 0.f);
    std::unique_ptr<Tensor> av(tf->fill1(4, 10.f));
    std::unique_ptr<Tensor> bv(tf->fill1(4, 20.f));

    // select -> [10,20,10,20]; reshape(2,2) -> [[10,20],[10,20]]; transpose -> [[10,10],[20,20]]
    std::unique_ptr<Tensor> result(compiled->run3(c.get(), av.get(), bv.get()));
    REQUIRE_EQ(result->getDim0(), 2);
    REQUIRE_EQ(result->getDim1(), 2);
    CHECK(std::fabs(result->get2(0, 0) - 10.f) < 1e-4f);
    CHECK(std::fabs(result->get2(0, 1) - 10.f) < 1e-4f);
    CHECK(std::fabs(result->get2(1, 0) - 20.f) < 1e-4f);
    CHECK(std::fabs(result->get2(1, 1) - 20.f) < 1e-4f);
}

TEST_CASE("tensor.gpu.reduceLargeTensor") {
    if (!tryInitGpuWindow()) return;

    auto *tf = TF::create();
    const int n = 20000;  // above the GPU-reduce size threshold
    std::unique_ptr<Tensor> t(tf->zeros1(n));
    for (int i = 0; i < n; ++i) t->set1(i, float(i % 17) - 5.f);

    float gpuSum  = tf->reduceSum(t.get());
    float gpuMin  = tf->reduceMin(t.get());
    float gpuMax  = tf->reduceMax(t.get());
    float gpuMean = tf->reduceMean(t.get());

    double cpuSum = 0.0;
    float cpuMin  = t->get(0);
    float cpuMax  = t->get(0);
    for (int i = 0; i < n; ++i) {
        float v = t->get(i);
        cpuSum += v;
        cpuMin = std::min(cpuMin, v);
        cpuMax = std::max(cpuMax, v);
    }
    CHECK(std::fabs(gpuSum - float(cpuSum)) < std::max(1.f, std::fabs(float(cpuSum))) * 1e-3f);
    CHECK(std::fabs(gpuMin - cpuMin) < 1e-4f);
    CHECK(std::fabs(gpuMax - cpuMax) < 1e-4f);
    CHECK(std::fabs(gpuMean - float(cpuSum / n)) < 1.f);
}

// --- AITemplate-style refactor: new op coverage ---------------------------

TEST_CASE("tensor.broadcast.binary") {
    auto *tf = TF::create();
    // [2,3] + [3] -> [2,3]; [2,3] * [2,1] -> [2,3]
    std::unique_ptr<Tensor> a(tf->zeros2(2, 3));
    a->set2(0, 0, 1.f); a->set2(0, 1, 2.f); a->set2(0, 2, 3.f);
    a->set2(1, 0, 4.f); a->set2(1, 1, 5.f); a->set2(1, 2, 6.f);
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
    bool threw = false;
    std::unique_ptr<Tensor> bad(tf->fill2(3, 3, 1.f));
    try {
        tf->add(a.get(), bad.get());
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("tensor.softmax.logsoftmax") {
    auto *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 3));
    x->set2(0, 0, 1.f); x->set2(0, 1, 2.f); x->set2(0, 2, 3.f);
    x->set2(1, 0, 0.f); x->set2(1, 1, 0.f); x->set2(1, 2, 0.f);

    std::unique_ptr<Tensor> s(tf->softmax(x.get(), -1));
    const float s00 = std::exp(1.f) / (std::exp(1.f) + std::exp(2.f) + std::exp(3.f));
    CHECK(std::fabs(s->get2(0, 0) - s00) < 1e-5f);
    CHECK(std::fabs(s->reduceSum() - 2.f) < 1e-4f);

    std::unique_ptr<Tensor> ls(tf->logSoftmax(x.get(), 1));
    CHECK(std::fabs(ls->get2(0, 0) - std::log(s00)) < 1e-4f);
    CHECK(std::fabs(ls->get2(1, 0) - std::log(1.f / 3.f)) < 1e-4f);

    // axis-0 softmax
    std::unique_ptr<Tensor> s0(tf->softmax(x.get(), 0));
    const float e1 = std::exp(1.f), e0 = std::exp(0.f);
    CHECK(std::fabs(s0->get2(0, 0) - e1 / (e1 + e0)) < 1e-5f);
    CHECK(std::fabs(s0->get2(1, 0) - e0 / (e1 + e0)) < 1e-5f);
}

TEST_CASE("tensor.layernorm.rmsnorm") {
    auto *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 4));
    x->set2(0, 0, 1.f); x->set2(0, 1, 2.f); x->set2(0, 2, 3.f); x->set2(0, 3, 4.f);
    x->set2(1, 0, -1.f); x->set2(1, 1, 0.f); x->set2(1, 2, 1.f); x->set2(1, 3, 2.f);

    std::unique_ptr<Tensor> ln(tf->layernorm(x.get(), 1e-5f));
    const float mean0 = 2.5f, inv0 = 1.f / std::sqrt(1.25f + 1e-5f);
    CHECK(std::fabs(ln->get2(0, 0) - (1.f - mean0) * inv0) < 1e-4f);
    CHECK(std::fabs(ln->get2(0, 3) - (4.f - mean0) * inv0) < 1e-4f);
    // zero-mean row -> output is a centered unit vector
    CHECK(std::fabs(ln->get2(1, 0) - (-1.5f) * inv0) < 1e-4f);

    std::unique_ptr<Tensor> scale(tf->fill1(4, 2.f));
    std::unique_ptr<Tensor> bias(tf->fill1(4, 1.f));
    std::unique_ptr<Tensor> lnwb(tf->layernormWB(x.get(), scale.get(), bias.get(), 1e-5f));
    CHECK(std::fabs(lnwb->get2(0, 0) - (ln->get2(0, 0) * 2.f + 1.f)) < 1e-4f);

    std::unique_ptr<Tensor> rn(tf->rmsnorm(x.get(), 1e-5f));
    const double rms0 = std::sqrt((1.0 + 4.0 + 9.0 + 16.0) / 4.0 + 1e-5);
    CHECK(std::fabs(rn->get2(0, 0) - float(1.0 / rms0)) < 1e-4f);
    std::unique_ptr<Tensor> rnw(tf->rmsnormW(x.get(), scale.get(), 1e-5f));
    CHECK(std::fabs(rnw->get2(0, 0) - rn->get2(0, 0) * 2.f) < 1e-4f);
}

TEST_CASE("tensor.gelu.silu") {
    auto *tf = TF::create();
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

float conv2dRef(const float *x, int N, int C, int H, int W, const float *w, int F, int KH,
                int KW, int stride, int pad, int oh, int ow) {
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
    x1->set3(0, 0, 0, 1.f); x1->set3(0, 0, 1, 2.f); x1->set3(0, 0, 2, 3.f); x1->set3(0, 0, 3, 4.f);
    std::unique_ptr<Tensor> w1(tf->zeros3(1, 1, 3));
    w1->set3(0, 0, 0, 1.f); w1->set3(0, 0, 1, 2.f); w1->set3(0, 0, 2, 1.f);
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
    table->set2(0, 0, 10.f); table->set2(0, 1, 11.f);
    table->set2(1, 0, 20.f); table->set2(1, 1, 21.f);
    table->set2(2, 0, 30.f); table->set2(2, 1, 31.f);
    table->set2(3, 0, 40.f); table->set2(3, 1, 41.f);
    std::unique_ptr<Tensor> idx(tf->zeros1(3));
    idx->set1(0, 0.f); idx->set1(1, 2.f); idx->set1(2, 3.f);
    std::unique_ptr<Tensor> emb(tf->embedding(table.get(), idx.get()));
    REQUIRE_EQ(emb->getRank(), 2);
    REQUIRE_EQ(emb->getDim1(), 2);
    CHECK(std::fabs(emb->get2(0, 0) - 10.f) < 1e-5f);
    CHECK(std::fabs(emb->get2(1, 1) - 31.f) < 1e-5f);
    CHECK(std::fabs(emb->get2(2, 0) - 40.f) < 1e-5f);

    // concat along last axis
    std::unique_ptr<Tensor> a(tf->zeros2(2, 2));
    a->set2(0, 0, 1.f); a->set2(0, 1, 2.f); a->set2(1, 0, 3.f); a->set2(1, 1, 4.f);
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
    auto *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros2(2, 3));
    x->set2(0, 0, 1.f); x->set2(0, 1, 3.f); x->set2(0, 2, 2.f);
    x->set2(1, 0, 4.f); x->set2(1, 1, 1.f); x->set2(1, 2, 5.f);

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
    auto *tf = TF::create();
    const int B = 1, H = 1, T = 2, S = 3, D = 4;
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
        float mx = std::max(scores[0], std::max(scores[1], scores[2]));
        double sum = 0.0;
        for (int s = 0; s < S; ++s) sum += std::exp(double(scores[s] - mx));
        for (int d = 0; d < D; ++d) {
            double acc = 0.0;
            for (int s = 0; s < S; ++s)
                acc += std::exp(double(scores[s] - mx)) * v->get4(0, 0, s, d);
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
    auto *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros4(1, 1, 2, 2));
    x->set4(0, 0, 0, 0, 1.f); x->set4(0, 0, 0, 1, 2.f);
    x->set4(0, 0, 1, 0, 3.f); x->set4(0, 0, 1, 1, 4.f);
    std::unique_ptr<Tensor> n(tf->resize2d(x.get(), 4, 4, 0));
    REQUIRE_EQ(n->getDim2(), 4);
    CHECK(std::fabs(n->get4(0, 0, 0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(n->get4(0, 0, 3, 3) - 4.f) < 1e-5f);
    std::unique_ptr<Tensor> b(tf->resize2d(x.get(), 3, 3, 1));
    CHECK(std::fabs(b->get4(0, 0, 1, 1) - 2.5f) < 1e-4f);  // bilinear center
}

TEST_CASE("tensor.matmul.batched") {
    auto *tf = TF::create();
    std::unique_ptr<Tensor> a(tf->zeros3(2, 2, 2));
    a->set3(0, 0, 0, 1.f); a->set3(0, 0, 1, 2.f); a->set3(0, 1, 0, 3.f); a->set3(0, 1, 1, 4.f);
    a->set3(1, 0, 0, 1.f); a->set3(1, 0, 1, 0.f); a->set3(1, 1, 0, 0.f); a->set3(1, 1, 1, 1.f);
    std::unique_ptr<Tensor> b(tf->zeros3(2, 2, 2));
    b->set3(0, 0, 0, 1.f); b->set3(0, 0, 1, 0.f); b->set3(0, 1, 0, 0.f); b->set3(0, 1, 1, 1.f);
    b->set3(1, 0, 0, 2.f); b->set3(1, 0, 1, 1.f); b->set3(1, 1, 0, 1.f); b->set3(1, 1, 1, 2.f);
    std::unique_ptr<Tensor> c(tf->matmul(a.get(), b.get()));
    REQUIRE_EQ(c->getDim0(), 2);
    CHECK(std::fabs(c->get3(0, 0, 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(c->get3(0, 0, 1) - 2.f) < 1e-5f);
    CHECK(std::fabs(c->get3(1, 1, 1) - 2.f) < 1e-5f);
}

TEST_CASE("tensor.func.gameAIModel") {
    // Small "game AI" network: x @ W1 -> +b1 -> relu -> x @ W2 -> softmax
    auto *tf = TF::create();
    std::unique_ptr<Tensor> w1(tf->zeros2(4, 3));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) w1->set2(i, j, float(i * 3 + j + 1) * 0.1f);
    std::unique_ptr<Tensor> b1(tf->fill1(3, 0.5f));
    std::unique_ptr<Tensor> w2(tf->zeros2(3, 2));
    w2->set2(0, 0, 1.f); w2->set2(0, 1, -1.f);
    w2->set2(1, 0, 0.5f); w2->set2(1, 1, 1.f);
    w2->set2(2, 0, -0.5f); w2->set2(2, 1, 2.f);

    std::unique_ptr<Tensor> xEager(tf->zeros1(4));
    xEager->set1(0, 1.f); xEager->set1(1, 2.f); xEager->set1(2, 3.f); xEager->set1(3, 4.f);

    // eager reference
    std::unique_ptr<Tensor> xr(tf->reshape2(xEager.get(), 1, 4));
    std::unique_ptr<Tensor> h1(tf->matmul(xr.get(), w1.get()));
    std::unique_ptr<Tensor> h2(tf->add(h1.get(), b1.get()));
    std::unique_ptr<Tensor> h3(tf->relu(h2.get()));
    std::unique_ptr<Tensor> h4(tf->matmul(h3.get(), w2.get()));
    std::unique_ptr<Tensor> ref(tf->softmax(h4.get(), 1));

    // compiled graph (captures w1/b1/w2 as constants)
    std::unique_ptr<Func> fn(tf->func());
    Tensor *x = fn->input1(4);
    Tensor *x2 = tf->reshape2(x, 1, 4);
    Tensor *y = tf->matmul(x2, w1.get());
    y = tf->add(y, b1.get());
    y = tf->relu(y);
    y = tf->matmul(y, w2.get());
    y = tf->softmax(y, 1);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    REQUIRE_EQ(out->getDim0(), 1);
    REQUIRE_EQ(out->getDim1(), 2);
    for (int j = 0; j < 2; ++j)
        CHECK(std::fabs(out->get2(0, j) - ref->get2(0, j)) < 1e-3f);
    CHECK(std::fabs(out->reduceSum() - 1.f) < 1e-4f);
}

TEST_CASE("tensor.func.convSoftmaxGraph") {
    auto *tf = TF::create();
    std::unique_ptr<Tensor> x(tf->zeros4(1, 1, 3, 3));
    for (int i = 0; i < 9; ++i) x->set(i, float(i + 1));
    std::unique_ptr<Tensor> w(tf->fill4(1, 1, 2, 2, 1.f));
    std::unique_ptr<Tensor> eagerConv(tf->conv2d(x.get(), w.get(), 1, 0));

    std::unique_ptr<Func> fn(tf->func());
    Tensor *in = fn->input4(1, 1, 3, 3);
    Tensor *y = tf->conv2d(in, w.get(), 1, 0);
    y = tf->relu(y);
    y = tf->reshape2(y, 1, 4);
    y = tf->softmax(y, 1);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor> out(compiled->run1(x.get()));
    std::unique_ptr<Tensor> sm(tf->softmax(tf->reshape2(eagerConv.get(), 1, 4), 1));
    REQUIRE_EQ(out->getDim0(), 1);
    REQUIRE_EQ(out->getDim1(), 4);
    for (int j = 0; j < 4; ++j)
        CHECK(std::fabs(out->get2(0, j) - sm->get2(0, j)) < 1e-4f);
}

TEST_CASE("tensor.func.fiveSixInputs") {
    auto *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor *a = fn->input1(2);
    Tensor *b = fn->input1(2);
    Tensor *c = fn->input1(2);
    Tensor *d = fn->input1(2);
    Tensor *e = fn->input1(2);
    Tensor *f = fn->input1(2);
    Tensor *y = tf->add(a, tf->add(b, tf->add(c, tf->add(d, tf->add(e, f)))));
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getPlaceholderCount(), 6);
    std::unique_ptr<Tensor> v1(tf->fill1(2, 1.f));
    std::unique_ptr<Tensor> v2(tf->fill1(2, 2.f));
    std::unique_ptr<Tensor> v3(tf->fill1(2, 3.f));
    std::unique_ptr<Tensor> v4(tf->fill1(2, 4.f));
    std::unique_ptr<Tensor> v5(tf->fill1(2, 5.f));
    std::unique_ptr<Tensor> v6(tf->fill1(2, 6.f));
    std::unique_ptr<Tensor> out(compiled->run6(v1.get(), v2.get(), v3.get(), v4.get(), v5.get(),
                                               v6.get()));
    CHECK(std::fabs(out->get1(0) - 21.f) < 1e-4f);
}

TEST_CASE("tensor.func.transformerBlockInference") {
    // A general neural-network inference graph: layernorm -> SDPA attention ->
    // residual -> MLP(gelu) -> residual -> classifier softmax. Verified against
    // the eager reference and (when a window exists) on the generated GPU kernels.
    auto *tf = TF::create();
    const int B = 1, T = 2, D = 4, C = 3;
    const float scale = 1.f / std::sqrt(float(D));
    const float eps = 1e-5f;

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
    Tensor *x = fn->input3(B, T, D);
    Tensor *l1 = tf->layernorm(x, eps);
    Tensor *l1m = tf->reshape2(l1, B * T, D);
    Tensor *qq = tf->reshape4(tf->matmul(l1m, Wq.get()), 1, 1, T, D);
    Tensor *kk = tf->reshape4(tf->matmul(l1m, Wk.get()), 1, 1, T, D);
    Tensor *vv = tf->reshape4(tf->matmul(l1m, Wv.get()), 1, 1, T, D);
    Tensor *a1 = tf->reshape3(tf->sdpa(qq, kk, vv, scale), B, T, D);
    Tensor *x1s = tf->add(x, a1);
    Tensor *l2 = tf->layernorm(x1s, eps);
    Tensor *l2m = tf->reshape2(l2, B * T, D);
    Tensor *hh = tf->matmul(l2m, W1.get());
    hh = tf->gelu(hh);
    hh = tf->matmul(hh, W2.get());
    Tensor *x2s = tf->add(x1s, tf->reshape3(hh, B, T, D));
    Tensor *lg = tf->matmul(tf->reshape2(x2s, B * T, D), Wc.get());
    fn->setOutput(tf->softmax(lg, 1));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    REQUIRE_EQ(out->getDim0(), B * T);
    REQUIRE_EQ(out->getDim1(), C);
    for (int i = 0; i < out->getSize(); ++i)
        // Compiled may run on GPU (an earlier test's window may still be alive):
        // float32 layernorm/softmax/matmul accumulation order differs slightly.
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-2f);
    CHECK(std::fabs(out->reduceSum() - float(B * T)) < 1e-2f);

    // action selection (game-AI style) from the compiled logits
    std::unique_ptr<Tensor> action(tf->argmax(out.get(), 1, 0));
    REQUIRE_EQ(action->getSize(), B * T);
}

// --- new-op GPU paths (only run with a live Vulkan window) -----------------

TEST_CASE("tensor.gpu.fusedElementwiseChain") {
    if (!tryInitGpuWindow()) return;
    auto *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor *x = fn->input2(4, 4);
    Tensor *y = tf->mulScalar(x, 2.f);
    y = tf->addScalar(y, 1.f);
    y = tf->relu(y);
    y = tf->mulScalar(y, 0.5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    CHECK_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->randomNormal2(4, 4));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));
    std::unique_ptr<Tensor> ref(xd->mulScalar(2.f));
    ref.reset(ref->addScalar(1.f));
    ref.reset(ref->relu());
    ref.reset(ref->mulScalar(0.5f));
    for (int i = 0; i < out->getSize(); ++i)
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

TEST_CASE("tensor.gpu.softmaxLayernormConv") {
    if (!tryInitGpuWindow()) return;
    auto *tf = TF::create();
    std::unique_ptr<Tensor> w(tf->fill4(2, 1, 3, 3, 0.1f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor *in = fn->input4(1, 1, 6, 6);
    Tensor *y = tf->conv2d(in, w.get(), 1, 0);
    y = tf->relu(y);
    y = tf->flatten(y);
    y = tf->layernorm(y, 1e-5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    CHECK_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->zeros4(1, 1, 6, 6));
    for (int i = 0; i < xd->getSize(); ++i) xd->set(i, float((i % 7) - 3));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));

    std::unique_ptr<Tensor> ref(tf->conv2d(xd.get(), w.get(), 1, 0));
    ref.reset(ref->relu());
    ref.reset(ref->flatten());
    ref.reset(tf->layernorm(ref.get(), 1e-5f));
    REQUIRE_EQ(out->getSize(), ref->getSize());
    for (int i = 0; i < out->getSize(); ++i)
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-3f);
}

TEST_CASE("tensor.gpu.transformerBlockInference") {
    if (!tryInitGpuWindow()) return;
    auto *tf = TF::create();
    const int B = 1, T = 4, D = 8, C = 3;
    const float scale = 1.f / std::sqrt(float(D));
    const float eps = 1e-5f;

    auto rand = [&](int d0, int d1, float s) {
        std::unique_ptr<Tensor> w(tf->randomNormal2(d0, d1));
        w.reset(w->mulScalar(s));
        return w;
    };
    std::unique_ptr<Tensor> Wq(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> Wk(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> Wv(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> W1(rand(D, 2 * D, 0.15f));
    std::unique_ptr<Tensor> W2(rand(2 * D, D, 0.15f));
    std::unique_ptr<Tensor> Wc(rand(D, C, 0.15f));

    std::unique_ptr<Tensor> xEager(tf->randomNormal3(B, T, D));

    std::unique_ptr<Func> fn(tf->func());
    Tensor *x = fn->input3(B, T, D);
    Tensor *l1 = tf->layernorm(x, eps);
    Tensor *l1m = tf->reshape2(l1, B * T, D);
    Tensor *qq = tf->reshape4(tf->matmul(l1m, Wq.get()), 1, 1, T, D);
    Tensor *kk = tf->reshape4(tf->matmul(l1m, Wk.get()), 1, 1, T, D);
    Tensor *vv = tf->reshape4(tf->matmul(l1m, Wv.get()), 1, 1, T, D);
    Tensor *a1 = tf->reshape3(tf->sdpa(qq, kk, vv, scale), B, T, D);
    Tensor *x1s = tf->add(x, a1);
    Tensor *l2 = tf->layernorm(x1s, eps);
    Tensor *l2m = tf->reshape2(l2, B * T, D);
    Tensor *hh = tf->matmul(l2m, W1.get());
    hh = tf->gelu(hh);
    hh = tf->matmul(hh, W2.get());
    Tensor *x2s = tf->add(x1s, tf->reshape3(hh, B, T, D));
    Tensor *lg = tf->matmul(tf->reshape2(x2s, B * T, D), Wc.get());
    fn->setOutput(tf->softmax(lg, 1));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    CHECK_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    CHECK(std::fabs(out->reduceSum() - float(B * T)) < 1e-3f);
    std::unique_ptr<Tensor> action(tf->argmax(out.get(), 1, 0));
    REQUIRE_EQ(action->getSize(), B * T);

    // cross-check the generated GPU kernels against the eager CPU reference
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
    for (int i = 0; i < out->getSize(); ++i)
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-2f);
}

TEST_CASE("tensor.gpu.sdpaMatchesCpu") {
    if (!tryInitGpuWindow()) return;
    auto *tf = TF::create();
    const int B = 1, H = 1, T = 4, S = 4, D = 8;
    std::unique_ptr<Tensor> q(tf->randomNormal4(B, H, T, D));
    std::unique_ptr<Tensor> k(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> v(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> ref(tf->sdpa(q.get(), k.get(), v.get(), 0.353553f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor *qq = fn->input4(B, H, T, D);
    Tensor *kk = fn->input4(B, H, S, D);
    Tensor *vv = fn->input4(B, H, S, D);
    fn->setOutput(tf->sdpa(qq, kk, vv, 0.353553f));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    CHECK_EQ(compiled->getDevice(), std::string("gpu"));
    std::unique_ptr<Tensor> out(compiled->run3(q.get(), k.get(), v.get()));
    for (int i = 0; i < out->getSize(); ++i)
        CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

namespace {

void checkGpuGraphMatchesEager(TF *tf, const std::function<Tensor *(Tensor *)> &build,
                               Tensor *feed, const char *name) {
    std::unique_ptr<Func> fn(tf->func());
    Tensor *in = fn->input3(1, 4, 8);
    Tensor *outSym = build(in);
    fn->setOutput(outSym);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    if (compiled->getDevice() != std::string("gpu")) {
        // e.g. no window in this environment — CPU reference covers correctness
        std::unique_ptr<Tensor> out(compiled->run1(feed));
        (void)out;
        return;
    }
    std::unique_ptr<Tensor> out(compiled->run1(feed));
    std::unique_ptr<Tensor> ref(build(feed));
    REQUIRE_EQ(out->getRank(), ref->getRank());
    REQUIRE_EQ(out->getSize(), ref->getSize());
    float worst = 0.f;
    for (int i = 0; i < out->getSize(); ++i)
        worst = std::max(worst, std::fabs(out->get(i) - ref->get(i)));
    if (worst >= 1e-3f) fprintf(stderr, "stage %s worst=%.6g\n", name, double(worst));
    CHECK_LT(worst, 1e-3f);
}

}  // namespace

TEST_CASE("tensor.gpu.stageBisect") {
    if (!tryInitGpuWindow()) return;
    auto *tf = TF::create();
    const int T = 4, D = 8;
    std::unique_ptr<Tensor> x(tf->randomNormal3(1, T, D));
    std::unique_ptr<Tensor> W(tf->randomNormal2(D, D));

    auto stage1 = [&](Tensor *in) {
        Tensor *l = tf->layernorm(in, 1e-5f);
        Tensor *m = tf->reshape2(l, T, D);
        return tf->reshape4(tf->matmul(m, W.get()), 1, 1, T, D);
    };
    auto stage2 = [&](Tensor *in) {
        Tensor *q = stage1(in);
        return tf->sdpa(q, q, q, 0.353553f);
    };
    auto stage3 = [&](Tensor *in) {
        Tensor *a = stage2(in);
        return tf->add(in, tf->reshape3(a, 1, T, D));
    };
    auto stage4 = [&](Tensor *in) {
        Tensor *r = stage3(in);
        return tf->layernorm(r, 1e-5f);
    };

    checkGpuGraphMatchesEager(tf, stage1, x.get(), "stage1");
    checkGpuGraphMatchesEager(tf, stage2, x.get(), "stage2");
    checkGpuGraphMatchesEager(tf, stage3, x.get(), "stage3");
    checkGpuGraphMatchesEager(tf, stage4, x.get(), "stage4");
}

TEST_CASE("tensor.debug.dumpStage3") {
    auto *tf = TF::create();
    const int T = 4, D = 8;
    std::unique_ptr<Tensor> W(tf->randomNormal2(D, D));
    std::unique_ptr<Func> fn(tf->func());
    Tensor *in = fn->input3(1, T, D);
    Tensor *l = tf->layernorm(in, 1e-5f);
    Tensor *m = tf->reshape2(l, T, D);
    Tensor *mm = tf->matmul(m, W.get());
    Tensor *q = tf->reshape4(mm, 1, 1, T, D);
    Tensor *a = tf->sdpa(q, q, q, 0.353553f);
    Tensor *a3 = tf->reshape3(a, 1, T, D);
    fn->setOutput(tf->add(in, a3));

    OptimizedGraph opt = optimizeGraph(fn->graph(), fn->outputNode());
    fprintf(stderr, "[dump] groups=%zu order=%zu\n", opt.groups.size(), opt.order.size());
    for (size_t gi = 0; gi < opt.groups.size(); ++gi) {
        const auto &g = opt.groups[gi];
        fprintf(stderr, "[dump] group %zu kind=%d out=%d inputs=[", gi, int(g.kind),
                g.outputNode);
        for (size_t k = 0; k < g.inputs.size(); ++k)
            fprintf(stderr, "%s%d", k ? "," : "", g.inputs[k]);
        fprintf(stderr, "] nodes=[");
        for (size_t k = 0; k < g.nodes.size(); ++k)
            fprintf(stderr, "%s%d", k ? "," : "", g.nodes[k]);
        fprintf(stderr, "]\n");
    }
    fprintf(stderr, "[dump] slots: ");
    for (int id = 0; id < fn->graph().nodeCount(); ++id)
        fprintf(stderr, "n%d->%d ", id, opt.nodeSlot[static_cast<size_t>(id)]);
    fprintf(stderr, "\n");
    for (const auto &g : opt.groups) {
        KernelSpec spec;
        if (g.kind == GroupKind::Elementwise) {
            std::fprintf(stderr, "[dump] generating group out=%d\n", g.outputNode);
            std::fflush(stderr);
            generateKernel(fn->graph(), g, spec);
            std::fprintf(stderr, "[dump] generated %zu bytes\n", spec.pass2.size());
            std::fflush(stderr);
            fprintf(stderr, "[dump] elementwise GLSL:\n%s\n", spec.pass2.c_str());
            std::fflush(stderr);
        }
    }
}
