#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"

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
bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace

TEST_CASE("tensor.gpu.fusedElementwiseChain") {
    if (!tryInitHeadlessGfx()) return;
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x = fn->input2(4, 4);
    Tensor               *y = tf->mulScalar(x, 2.f);
    y                       = tf->addScalar(y, 1.f);
    y                       = tf->relu(y);
    y                       = tf->mulScalar(y, 0.5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->randomNormal2(4, 4));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));
    std::unique_ptr<Tensor> ref(xd->mulScalar(2.f));
    ref.reset(ref->addScalar(1.f));
    ref.reset(ref->relu());
    ref.reset(ref->mulScalar(0.5f));
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

TEST_CASE("tensor.gpu.softmaxLayernormConv") {
    if (!tryInitHeadlessGfx()) return;
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> w(tf->fill4(2, 1, 3, 3, 0.1f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in = fn->input4(1, 1, 6, 6);
    Tensor               *y  = tf->conv2d(in, w.get(), 1, 0);
    y                        = tf->relu(y);
    y                        = tf->flatten(y);
    y                        = tf->layernorm(y, 1e-5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->zeros4(1, 1, 6, 6));
    for (int i = 0; i < xd->getSize(); ++i) xd->set(i, float((i % 7) - 3));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));

    std::unique_ptr<Tensor> ref(tf->conv2d(xd.get(), w.get(), 1, 0));
    ref.reset(ref->relu());
    ref.reset(ref->flatten());
    ref.reset(tf->layernorm(ref.get(), 1e-5f));
    REQUIRE_EQ(out->getSize(), ref->getSize());
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-3f);
}

TEST_CASE("tensor.gpu.transformerBlockInference") {
    if (!tryInitHeadlessGfx()) return;
    auto       *tf = TF::create();
    const int   B = 1, T = 4, D = 8, C = 3;
    const float scale = 1.f / std::sqrt(float(D));
    const float eps   = 1e-5f;

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
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    REQUIRE(std::fabs(out->reduceSum() - float(B * T)) < 1e-3f);
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
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-2f);
}

TEST_CASE("tensor.gpu.sdpaMatchesCpu") {
    if (!tryInitHeadlessGfx()) return;
    auto                   *tf = TF::create();
    const int               B = 1, H = 1, T = 4, S = 4, D = 8;
    std::unique_ptr<Tensor> q(tf->randomNormal4(B, H, T, D));
    std::unique_ptr<Tensor> k(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> v(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> ref(tf->sdpa(q.get(), k.get(), v.get(), 0.353553f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *qq = fn->input4(B, H, T, D);
    Tensor               *kk = fn->input4(B, H, S, D);
    Tensor               *vv = fn->input4(B, H, S, D);
    fn->setOutput(tf->sdpa(qq, kk, vv, 0.353553f));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    std::unique_ptr<Tensor> out(compiled->run3(q.get(), k.get(), v.get()));
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

namespace {

void checkGpuGraphMatchesEager(TF *tf, const std::function<Tensor *(Tensor *)> &build, Tensor *feed, const char *name) {
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in     = fn->input3(1, 4, 8);
    Tensor               *outSym = build(in);
    fn->setOutput(outSym);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    std::unique_ptr<Tensor> out(compiled->run1(feed));
    std::unique_ptr<Tensor> ref(build(feed));
    REQUIRE_EQ(out->getRank(), ref->getRank());
    REQUIRE_EQ(out->getSize(), ref->getSize());
    float worst = 0.f;
    for (int i = 0; i < out->getSize(); ++i) {
        const float error = std::fabs(out->get(i) - ref->get(i));
        REQUIRE(std::isfinite(error));
        worst = std::max(worst, error);
    }
    if (worst >= 1e-3f) fprintf(stderr, "stage %s worst=%.6g\n", name, double(worst));
    REQUIRE_LT(worst, 1e-3f);
}

}  // namespace

TEST_CASE("tensor.gpu.stageBisect") {
    if (!tryInitHeadlessGfx()) return;
    auto                   *tf = TF::create();
    const int               T = 4, D = 8;
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
