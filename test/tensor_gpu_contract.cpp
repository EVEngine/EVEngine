#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "tensor/GpuBackend.h"
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
using Operation = std::function<Tensor *(Tensor *)>;
void compareKernel(const char *name, Tensor *input, const Operation &operation,
                   zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    std::printf("GPU kernel contract: %s\n", name);
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *symbolic = nullptr;
    switch (input->getRank()) {
        case 1: symbolic = fn->input1(input->getDim0()); break;
        case 2: symbolic = fn->input2(input->getDim0(), input->getDim1()); break;
        case 3: symbolic = fn->input3(input->getDim0(), input->getDim1(), input->getDim2()); break;
        case 4: symbolic = fn->input4(input->getDim0(), input->getDim1(), input->getDim2(), input->getDim3()); break;
        default: REQUIRE(false); return;
    }
    std::unique_ptr<Tensor> traced(operation(symbolic));
    fn->setOutput(traced.get());
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    // Changing the feed detects stale uploads and arena contents on a reused program.
    for (int repeat = 0; repeat < 2; ++repeat) {
        std::unique_ptr<Tensor> feed(input->mulScalar(repeat == 0 ? 1.f : 0.5f));
        std::unique_ptr<Tensor> expected(operation(feed.get()));
        std::unique_ptr<Tensor> actual(compiled->run1(feed.get()));
        REQUIRE_EQ(actual->getSize(), expected->getSize());
        for (int i = 0; i < expected->getSize(); ++i) {
            REQUIRE(std::isfinite(actual->get(i)));
            REQUIRE(std::fabs(actual->get(i) - expected->get(i)) < 1e-3f * (1.f + std::fabs(expected->get(i))));
        }
    }
}
void fillPattern(Tensor *tensor) {
    for (int i = 0; i < tensor->getSize(); ++i) tensor->set(i, float((i * 7) % 19 - 9) / 10.f);
}
}  // namespace

TEST_CASE("tensor.gpu.kernelFamiliesMatchEager") {
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> matrix(tf->zeros2(4, 3));
    fillPattern(matrix.get());
    const auto check = [&](const char *name, const Operation &op) {
        compareKernel(name, matrix.get(), op, _ZEROERR_TEST_CONTEXT);
    };
    check("sum axis 0", [&](Tensor *x) { return tf->sumAxis(x, 0, 0); });
    check("mean axis 1", [&](Tensor *x) { return tf->meanAxis(x, 1, 0); });
    check("min axis 0", [&](Tensor *x) { return tf->minAxis(x, 0, 0); });
    check("max axis 1", [&](Tensor *x) { return tf->maxAxis(x, 1, 0); });
    check("argmax", [&](Tensor *x) { return tf->argmax(x, 1, 0); });
    check("softmax noncontiguous axis", [&](Tensor *x) { return tf->softmax(x, 0); });
    check("log softmax", [&](Tensor *x) { return tf->logSoftmax(x, 1); });
    check("rms norm", [&](Tensor *x) { return tf->rmsnorm(x, 1e-5f); });
    check("permute", [&](Tensor *x) { return tf->permute2(x, 1, 0); });
    check("slice", [&](Tensor *x) { return tf->slice(x, 1, 1, 3); });
    std::unique_ptr<Tensor> scale(tf->fill1(3, 1.2f)), bias(tf->fill1(3, -0.2f));
    check("scaled rms norm", [&](Tensor *x) { return tf->rmsnormW(x, scale.get(), 1e-5f); });
    check("affine layer norm", [&](Tensor *x) { return tf->layernormWB(x, scale.get(), bias.get(), 1e-5f); });
    std::unique_ptr<Tensor> other(tf->fill2(1, 3, 0.4f));
    check("concat", [&](Tensor *x) { return tf->concat2(x, other.get(), 0); });
    check("broadcast addition", [&](Tensor *x) { return tf->add(x, scale.get()); });
    std::unique_ptr<Tensor> selected(tf->fill2(4, 3, 1.2f)), rejected(tf->fill2(4, 3, -0.2f));
    check("where", [&](Tensor *x) { return tf->where(x, selected.get(), rejected.get()); });
    std::unique_ptr<Tensor> indices(tf->zeros1(3));
    indices->set(0, -1.f);
    indices->set(1, 2.f);
    indices->set(2, 5.f);
    check("embedding clamps indices", [&](Tensor *x) { return tf->embedding(x, indices.get()); });
    std::unique_ptr<Tensor> projection(tf->zeros2(3, 5)), projectionBias(tf->fill1(5, 0.15f));
    fillPattern(projection.get());
    check("matrix bias activation with edge tiles", [&](Tensor *x) {
        std::unique_ptr<Tensor> product(tf->matmul(x, projection.get()));
        std::unique_ptr<Tensor> biased(tf->add(product.get(), projectionBias.get()));
        return tf->relu(biased.get());
    });
    std::unique_ptr<Tensor> attention(tf->zeros4(1, 1, 3, 5)), mask(tf->zeros4(1, 1, 3, 3));
    fillPattern(attention.get());
    mask->set(1, -1000.f);
    mask->set(2, -1000.f);
    mask->set(5, -1000.f);
    compareKernel(
        "masked attention with aliased inputs", attention.get(),
        [&](Tensor *x) { return tf->sdpaMasked(x, x, x, mask.get(), 0.4472136f); }, _ZEROERR_TEST_CONTEXT);


    std::unique_ptr<Tensor> image(tf->zeros4(1, 1, 5, 6));
    fillPattern(image.get());
    compareKernel(
        "max pool with padding", image.get(), [&](Tensor *x) { return tf->maxpool2d(x, 2, 1, 1); },
        _ZEROERR_TEST_CONTEXT);
    compareKernel(
        "average pool with padding", image.get(), [&](Tensor *x) { return tf->avgpool2d(x, 2, 1, 1); },
        _ZEROERR_TEST_CONTEXT);
    for (int mode : {0, 1})
        compareKernel(
            mode == 0 ? "nearest resize" : "bilinear resize", image.get(),
            [&](Tensor *x) { return tf->resize2d(x, 7, 3, mode); }, _ZEROERR_TEST_CONTEXT);
    std::unique_ptr<Tensor> signal(tf->zeros3(1, 1, 7)), weights(tf->fill3(2, 1, 3, 0.2f)),
        convBias(tf->fill1(2, -0.1f));
    convBias->set(1, 0.4f);
    fillPattern(signal.get());
    compareKernel(
        "conv1d bias and padding", signal.get(),
        [&](Tensor *x) { return tf->conv1dBias(x, weights.get(), convBias.get(), 2, 1); }, _ZEROERR_TEST_CONTEXT);
    std::unique_ptr<Tensor> spatialWeights(tf->fill4(2, 1, 3, 3, 0.2f)), columnBias(tf->fill1(6, 0.3f));
    fillPattern(columnBias.get());
    compareKernel(
        "conv2d channel bias and fused column bias", image.get(),
        [&](Tensor *x) {
            std::unique_ptr<Tensor> convolution(tf->conv2dBias(x, spatialWeights.get(), convBias.get(), 1, 1));
            std::unique_ptr<Tensor> biased(tf->add(convolution.get(), columnBias.get()));
            return tf->relu(biased.get());
        },
        _ZEROERR_TEST_CONTEXT);
    std::vector<float> values(513);
    float              sum = 0.f;
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = float(int(i % 7) - 3);
        sum += values[i];
    }
    for (int op : {0, 1, 2}) {
        float      actual   = 0.f;
        const bool executed = gpuReduce(values.data(), int(values.size()), op, actual);
        REQUIRE(executed);
        const float expected = op == 0 ? sum : (op == 1 ? -3.f : 3.f);
        REQUIRE(std::fabs(actual - expected) < 1e-4f);
    }
}

TEST_CASE("tensor.gpu.quantizedWeightsMatchEager") {
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> input(tf->zeros2(3, 5)), weights(tf->zeros2(5, 7)), indices(tf->zeros1(3));
    fillPattern(input.get());
    fillPattern(weights.get());
    indices->set(0, 0.f);
    indices->set(1, 3.f);
    indices->set(2, 4.f);
    for (const char *dtype : {"fp16", "fp8", "fp4", "int8", "int4"}) {
        std::unique_ptr<Tensor> packed(tf->quantizeWeight(weights.get(), dtype, 8));
        compareKernel(
            dtype, input.get(), [&](Tensor *x) { return tf->matmul(x, packed.get()); }, _ZEROERR_TEST_CONTEXT);
        compareKernel(
            dtype, indices.get(), [&](Tensor *x) { return tf->embedding(packed.get(), x); }, _ZEROERR_TEST_CONTEXT);
    }
}
