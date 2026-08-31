#include "graphics/Bloom.h"

#include <algorithm>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "graphics/BlendMode.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "graphics/shaders/bloom_downsample_frag_spv.inc"
#include "graphics/shaders/bloom_upsample_frag_spv.inc"

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

Shader *newPostShader(Graphics *gfx, const uint32_t *spv, size_t count, const char *wgsl) {
    Shader *shader = gfx->getBackendName() == "webgpu"
                         ? gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) + wgsl)
                         : gfx->newShaderFromSpv({}, copySpv(spv, count));
    if (!shader || !shader->gpuHandle) throw eve::Exception("Bloom: failed to create shader");
    return shader;
}

}  // namespace

Bloom::Bloom(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("Bloom: null graphics");
    downsample_ = newPostShader(gfx_, bloom_downsample_frag_spv,
                                bloom_downsample_frag_spv_count, shaders::kBloomDownsample);
    downsample_->declareFloat("texelW");
    downsample_->declareFloat("texelH");
    downsample_->declareFloat("firstPass");
    downsample_->declareFloat("threshold");
    downsample_->declareFloat("knee");

    upsample_ = newPostShader(gfx_, bloom_upsample_frag_spv, bloom_upsample_frag_spv_count,
                              shaders::kBloomUpsample);
    upsample_->declareFloat("texelW");
    upsample_->declareFloat("texelH");
    upsample_->declareFloat("scatter");
}

void Bloom::ensureTargets(int sourceWidth, int sourceHeight) {
    sourceWidth = std::max(sourceWidth, 1);
    sourceHeight = std::max(sourceHeight, 1);
    if (levels_[0] && sourceWidth_ == sourceWidth && sourceHeight_ == sourceHeight) return;

    sourceWidth_ = sourceWidth;
    sourceHeight_ = sourceHeight;
    composite_ = gfx_->newHDRCanvas(sourceWidth, sourceHeight);
    if (!composite_) throw eve::Exception("Bloom: failed to allocate HDR composite target");
    int width = sourceWidth;
    int height = sourceHeight;
    for (Canvas *&level : levels_) {
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
        level = gfx_->newHDRCanvas(width, height);
        if (!level) throw eve::Exception("Bloom: failed to allocate HDR pyramid target");
    }
}

void Bloom::configureDownsample(Texture *source, bool firstPass, float threshold) {
    downsample_->sendFloat("texelW", 1.f / float(std::max(source->getWidth(), 1)));
    downsample_->sendFloat("texelH", 1.f / float(std::max(source->getHeight(), 1)));
    downsample_->sendFloat("firstPass", firstPass ? 1.f : 0.f);
    downsample_->sendFloat("threshold", std::max(threshold, 0.f));
    downsample_->sendFloat("knee", std::max(threshold * 0.5f, 1e-4f));
}

void Bloom::configureUpsample(Texture *source, float scatter) {
    upsample_->sendFloat("texelW", 1.f / float(std::max(source->getWidth(), 1)));
    upsample_->sendFloat("texelH", 1.f / float(std::max(source->getHeight(), 1)));
    upsample_->sendFloat("scatter", std::clamp(scatter, 0.5f, 2.f));
}

Texture *Bloom::build(Texture *source, float threshold, float scatter) {
    if (!source) throw eve::Exception("Bloom.build: null source");
    ensureTargets(source->getWidth(), source->getHeight());
    Canvas *previous = gfx_->getCanvas();

    Texture *input = source;
    for (size_t i = 0; i < levels_.size(); ++i) {
        Canvas *target = levels_[i];
        target->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
        gfx_->setCanvas(target);
        configureDownsample(input, i == 0, threshold);
        gfx_->drawTexturedRectShaderUV(input, downsample_, 0.f, 0.f, float(target->getWidth()),
                                       float(target->getHeight()), 0.f, 0.f, 1.f, 1.f,
                                       Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Opaque);
        input = target->getTexture();
        if (!input) throw eve::Exception("Bloom: pyramid texture missing");
    }

    for (size_t i = levels_.size() - 1; i > 0; --i) {
        Texture *low = levels_[i]->getTexture();
        Canvas *high = levels_[i - 1];
        configureUpsample(low, scatter);
        gfx_->setCanvas(high);
        gfx_->drawTexturedRectShaderUV(low, upsample_, 0.f, 0.f, float(high->getWidth()),
                                       float(high->getHeight()), 0.f, 0.f, 1.f, 1.f,
                                       Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Additive);
    }

    gfx_->setCanvas(previous);
    return levels_[0]->getTexture();
}

Texture *Bloom::apply(Texture *source, float intensity, float threshold, float scatter) {
    if (!source) throw eve::Exception("Bloom.apply: null source");
    if (intensity <= 0.f) return source;
    Texture *bloom = build(source, threshold, scatter);
    Canvas *previous = gfx_->getCanvas();
    composite_->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
    gfx_->setCanvas(composite_);
    gfx_->drawTexturedRectShaderUV(source, nullptr, 0.f, 0.f, float(sourceWidth_),
                                   float(sourceHeight_), 0.f, 0.f, 1.f, 1.f,
                                   Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Opaque);
    gfx_->drawTexturedRectShaderUV(bloom, nullptr, 0.f, 0.f, float(sourceWidth_),
                                   float(sourceHeight_), 0.f, 0.f, 1.f, 1.f,
                                   Color(intensity, intensity, intensity, 1.f), false,
                                   BlendMode::Additive);
    gfx_->setCanvas(previous);
    Texture *result = composite_->getTexture();
    if (!result) throw eve::Exception("Bloom: composite texture missing");
    return result;
}

}  // namespace eve::graphics
