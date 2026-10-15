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

/** GPU tensor tests need a live Vulkan device; headless init is enough. */
[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace


// --- GPU (compute shader) path ---------------------------------------------
// These skip (return) when no Vulkan device / glslc is available (e.g. some CI
// images), matching test/gpgpu.cpp's tolerant pattern. When the environment
// does support it, CompiledFunction::getDevice() should report "gpu".

TEST_CASE("tensor.gpu.compiledFunctionElementwiseAndMatmul") {
    if (!tryInitHeadlessGfx()) return;

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
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
}

TEST_CASE("tensor.gpu.compiledFunctionWhereTranspose") {
    if (!tryInitHeadlessGfx()) return;

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
    if (!tryInitHeadlessGfx()) return;

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


namespace {

[[maybe_unused]] float conv2dRef(const float *x, int N, int C, int H, int W, const float *w, int F, int KH,
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


// --- new-op GPU paths (only run with a live Vulkan device; headless) -------
