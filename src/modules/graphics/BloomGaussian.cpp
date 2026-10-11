#include "graphics/Bloom.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/BloomGaussianWgsl.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "graphics/shaders/bloom_gaussian_frag_spv.inc"

namespace eve::graphics {
Result<void> validateBloomFilterSettings(const BloomFilterSettings& value) {
    if ((value.filter != BloomFilter::KarisTent && value.filter != BloomFilter::GaussianScatter &&
         value.filter != BloomFilter::GaussianPyramid) ||
        !std::isfinite(value.scatter) || value.scatter < 0.f || value.scatter > 1.f || value.maxIterations < 1 ||
        value.maxIterations > 16 || !std::isfinite(value.clamp) || value.clamp <= 0.f || value.clamp > 65504.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid bloom filter settings", "graphics.bloom"));
    return Result<void>::success();
}

Result<void> Bloom::configureFilter(const BloomFilterSettings& settings) {
    auto valid = validateBloomFilterSettings(settings);
    if (!valid) return Result<void>::failure(valid.status());
    filterSettings_ = settings;
    return Result<void>::success();
}

Texture* Bloom::buildGaussian(Texture* source, float threshold) {
    if (!gaussianShader_) {
        gaussianShader_ = gfx_->getBackendName() == "webgpu"
                              ? gfx_->newShaderFromWgsl({}, std::string(shaders::kPostCommon) + shaders::kBloomGaussian)
                              : gfx_->newShaderFromSpv(
                                    {}, std::vector<uint32_t>(bloom_gaussian_frag_spv,
                                                              bloom_gaussian_frag_spv + bloom_gaussian_frag_spv_count));
        if (!gaussianShader_ || !gaussianShader_->gpuHandle) throw Exception("Gaussian bloom shader creation failed");
        for (const char* name : {"texelW", "texelH", "phase", "threshold", "clamp", "scatter", "intensity", "radius",
                                 "tint", "hasLow", "prefilter"})
            gaussianShader_->declareFloat(name);
    }
    const bool      pyramid = filterSettings_.filter == BloomFilter::GaussianPyramid;
    constexpr float sizes[]{.3f, 1.f, 2.f, 10.f, 30.f, 64.f};
    const int width = std::max(source->getWidth(), 1), height = std::max(source->getHeight(), 1);
    const int       halfWidth  = std::max((width + (pyramid ? 1 : 0)) / 2, 1);
    const int       halfHeight = std::max((height + (pyramid ? 1 : 0)) / 2, 1);
    const int       count      = pyramid ? std::min(6, filterSettings_.maxIterations)
                                         : std::clamp(int(std::floor(std::log2(std::max(halfWidth, halfHeight)))) - 1, 1,
                                                      filterSettings_.maxIterations);
    // Check each target so switching filters with equal level counts rebuilds
    // the differently sized fast-blur intermediate without stale reuse.
    int w = halfWidth, h = halfHeight;
    for (int i = 0; i < count; ++i) {
        const bool fast              = pyramid && w * sizes[i] * .02f >= 7.f;
        const int  intermediateWidth = fast ? std::max((w + 1) / 2, 1) : w;
        if (!gaussianDown_[i] || gaussianDown_[i]->getWidth() != w || gaussianDown_[i]->getHeight() != h) {
            gaussianDown_[i] = gfx_->newHDRCanvas(w, h);
        }
        if (!gaussianUp_[i] || gaussianUp_[i]->getWidth() != intermediateWidth || gaussianUp_[i]->getHeight() != h) {
            gaussianUp_[i] = gfx_->newHDRCanvas(intermediateWidth, h);
        }
        if (!gaussianDown_[i] || !gaussianUp_[i]) throw Exception("Gaussian bloom target allocation failed");
        w = std::max((w + (pyramid ? 1 : 0)) / 2, 1);
        h = std::max((h + (pyramid ? 1 : 0)) / 2, 1);
    }
    struct RestoreCanvas {
        Graphics* gfx;
        Canvas*   previous;
        ~RestoreCanvas() { gfx->setCanvas(previous); }
    } restore{gfx_, gfx_->getCanvas()};
    float pyramidRadius = 0, pyramidTint = 0;
    auto draw = [&](Texture* high, Texture* low, Canvas* target, float phase) {
        target->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
        gfx_->setCanvas(target);
        gaussianShader_->sendFloat("texelW", 1.f / high->getWidth());
        gaussianShader_->sendFloat("texelH", 1.f / high->getHeight());
        gaussianShader_->sendFloat("phase", phase);
        gaussianShader_->sendFloat("radius", pyramidRadius);
        gaussianShader_->sendFloat("tint", pyramidTint);
        gaussianShader_->sendFloat("hasLow", low ? 1.f : 0.f);
        gaussianShader_->sendFloat("prefilter", phase == 5.f && high == source ? 1.f : 0.f);
        gaussianShader_->sendFloat("threshold", std::max(threshold, 0.f));
        gaussianShader_->sendFloat("clamp", filterSettings_.clamp);
        gaussianShader_->sendFloat("scatter", filterSettings_.scatter);
        gfx_->drawTexturedRectShaderDepth(high, low ? low : high, gaussianShader_, 0, 0, float(target->getWidth()),
                                          float(target->getHeight()), Color(1.f));
    };
    if (pyramid) {
        Texture* input = source;
        for (int i = 0; i < count; ++i) {
            draw(input, nullptr, gaussianDown_[i], 5.f);
            input = gaussianDown_[i]->getTexture();
        }
        constexpr float tints[]{.3465f, .138f, .1176f, .066f, .066f, .061f};
        Texture*        low = nullptr;
        for (int i = count - 1; i >= 0; --i) {
            const float radius = std::clamp(gaussianDown_[i]->getWidth() * sizes[i] * .02f, .0001f, 31.f);
            pyramidRadius      = radius;
            pyramidTint        = tints[i] / 6.f;
            draw(gaussianDown_[i]->getTexture(), nullptr, gaussianUp_[i], 6.f);
            draw(gaussianUp_[i]->getTexture(), low, gaussianDown_[i], 7.f);
            low = gaussianDown_[i]->getTexture();
        }
        return low;
    }
    draw(source, nullptr, gaussianDown_[0], 0.f);
    for (int i = 1; i < count; ++i) {
        draw(gaussianDown_[i - 1]->getTexture(), nullptr, gaussianUp_[i], 1.f);
        draw(gaussianUp_[i]->getTexture(), nullptr, gaussianDown_[i], 2.f);
    }
    Texture* low = gaussianDown_[count - 1]->getTexture();
    for (int i = count - 2; i >= 0; --i) {
        draw(gaussianDown_[i]->getTexture(), low, gaussianUp_[i], 3.f);
        low = gaussianUp_[i]->getTexture();
    }
    return low;
}

Texture* Bloom::compositeGaussian(Texture* source, Texture* bloom, float intensity) {
    struct RestoreCanvas {
        Graphics* gfx;
        Canvas*   previous;
        ~RestoreCanvas() { gfx->setCanvas(previous); }
    } restore{gfx_, gfx_->getCanvas()};
    composite_->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
    gfx_->setCanvas(composite_);
    gaussianShader_->sendFloat("texelW", 1.f / source->getWidth());
    gaussianShader_->sendFloat("texelH", 1.f / source->getHeight());
    gaussianShader_->sendFloat("phase", 4.f);
    gaussianShader_->sendFloat("intensity", intensity);
    gfx_->drawTexturedRectShaderDepth(source, bloom, gaussianShader_, 0, 0, float(composite_->getWidth()),
                                      float(composite_->getHeight()), Color(1.f));
    return composite_->getTexture();
}
}  // namespace eve::graphics
