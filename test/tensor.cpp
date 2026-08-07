#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "tensor/Graph.h"

#include "graphics/Graphics.h"
#include "window/Window.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace eve::tensor;

namespace {

/** GPU tensor tests need a live Vulkan device, which only exists after a window is created. */
bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    win->setGraphics(gfx);
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
