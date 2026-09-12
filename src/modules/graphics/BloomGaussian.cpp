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
    if ((value.filter != BloomFilter::KarisTent && value.filter != BloomFilter::GaussianScatter) ||
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
        for (const char* name : {"texelW", "texelH", "phase", "threshold", "clamp", "scatter", "intensity"})
            gaussianShader_->declareFloat(name);
    }
    const int width = std::max(source->getWidth(), 1), height = std::max(source->getHeight(), 1);
    const int halfWidth = std::max(width / 2, 1), halfHeight = std::max(height / 2, 1);
    const int count =
        std::clamp(int(std::floor(std::log2(std::max(halfWidth, halfHeight)))) - 1, 1, filterSettings_.maxIterations);
    if (width != gaussianWidth_ || height != gaussianHeight_ || count != gaussianLevels_) {
        int w = halfWidth, h = halfHeight;
        for (int i = 0; i < count; ++i) {
            if (!gaussianDown_[i] || gaussianDown_[i]->getWidth() != w || gaussianDown_[i]->getHeight() != h) {
                gaussianDown_[i] = gfx_->newHDRCanvas(w, h);
                gaussianUp_[i]   = gfx_->newHDRCanvas(w, h);
                if (!gaussianDown_[i] || !gaussianUp_[i]) throw Exception("Gaussian bloom target allocation failed");
            }
            w = std::max(w / 2, 1);
            h = std::max(h / 2, 1);
        }
        gaussianWidth_  = width;
        gaussianHeight_ = height;
        gaussianLevels_ = count;
    }
    struct RestoreCanvas {
        Graphics* gfx;
        Canvas*   previous;
        ~RestoreCanvas() { gfx->setCanvas(previous); }
    } restore{gfx_, gfx_->getCanvas()};
    auto draw = [&](Texture* high, Texture* low, Canvas* target, float phase) {
        target->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
        gfx_->setCanvas(target);
        gaussianShader_->sendFloat("texelW", 1.f / high->getWidth());
        gaussianShader_->sendFloat("texelH", 1.f / high->getHeight());
        gaussianShader_->sendFloat("phase", phase);
        gaussianShader_->sendFloat("threshold", std::max(threshold, 0.f));
        gaussianShader_->sendFloat("clamp", filterSettings_.clamp);
        gaussianShader_->sendFloat("scatter", filterSettings_.scatter);
        gfx_->drawTexturedRectShaderDepth(high, low ? low : high, gaussianShader_, 0, 0, float(target->getWidth()),
                                          float(target->getHeight()), Color(1.f));
    };
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
