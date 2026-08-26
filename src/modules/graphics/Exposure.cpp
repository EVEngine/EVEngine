#include "graphics/Exposure.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "common/Exception.h"
#include "graphics/BlendMode.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "graphics/shaders/exposure_adapt_frag_spv.inc"
#include "graphics/shaders/exposure_apply_frag_spv.inc"
#include "graphics/shaders/exposure_meter_frag_spv.inc"

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

Shader *newPostShader(Graphics *gfx, const uint32_t *spv, size_t count, const char *wgsl) {
    Shader *shader = gfx->getBackendName() == "webgpu"
                         ? gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) + wgsl)
                         : gfx->newShaderFromSpv({}, copySpv(spv, count));
    if (!shader || !shader->gpuHandle) throw eve::Exception("Exposure: failed to create shader");
    return shader;
}

}  // namespace

Exposure::Exposure(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("Exposure: null graphics");
    meterShader_ = newPostShader(gfx_, exposure_meter_frag_spv, exposure_meter_frag_spv_count,
                                 shaders::kExposureMeter);
    meterShader_->declareFloat("minEV");
    meterShader_->declareFloat("maxEV");

    adaptShader_ = newPostShader(gfx_, exposure_adapt_frag_spv, exposure_adapt_frag_spv_count,
                                 shaders::kExposureAdapt);
    adaptShader_->declareFloat("deltaSeconds");
    adaptShader_->declareFloat("darkSpeed");
    adaptShader_->declareFloat("brightSpeed");
    adaptShader_->declareFloat("resetHistory");

    applyShader_ = newPostShader(gfx_, exposure_apply_frag_spv, exposure_apply_frag_spv_count,
                                 shaders::kExposureApply);
    applyShader_->declareFloat("manualExposure");
    applyShader_->declareFloat("automatic");
}

void Exposure::ensureTargets(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (!meter_) {
        meter_ = gfx_->newHDRCanvas(1, 1);
        historyA_ = gfx_->newHDRCanvas(1, 1);
        historyB_ = gfx_->newHDRCanvas(1, 1);
        historyRead_ = historyA_;
        historyWrite_ = historyB_;
    }
    if (!output_ || width_ != width || height_ != height) {
        output_ = gfx_->newHDRCanvas(width, height);
        width_ = width;
        height_ = height;
        historyValid_ = false;
    }
    if (!meter_ || !historyRead_ || !historyWrite_ || !output_)
        throw eve::Exception("Exposure: failed to allocate HDR targets");
}

Texture *Exposure::apply(Texture *source, float manualExposure, bool automatic, float minEV,
                         float maxEV, Texture *meterSource, float deltaSeconds) {
    if (!source) throw eve::Exception("Exposure.apply: null source");
    ensureTargets(source->getWidth(), source->getHeight());
    Canvas *previousCanvas = gfx_->getCanvas();
    Texture *exposureTexture = source;
    if (!meterSource) meterSource = source;

    if (automatic) {
        meterShader_->sendFloat("minEV", std::min(minEV, maxEV));
        meterShader_->sendFloat("maxEV", std::max(minEV, maxEV));
        meter_->clear(Color(0.f, 0.f, 0.f, 1.f), {}, {});
        gfx_->setCanvas(meter_);
        gfx_->drawTexturedRectShaderUV(meterSource, meterShader_, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f,
                                       1.f, 1.f, Color(1.f, 1.f, 1.f, 1.f), false,
                                       BlendMode::Opaque);

        adaptShader_->sendFloat("deltaSeconds", std::clamp(deltaSeconds, 0.f, 0.25f));
        adaptShader_->sendFloat("darkSpeed", 1.5f);
        adaptShader_->sendFloat("brightSpeed", 3.f);
        adaptShader_->sendFloat("resetHistory", historyValid_ ? 0.f : 1.f);
        historyWrite_->clear(Color(1.f, 0.f, 0.f, 1.f), {}, {});
        gfx_->setCanvas(historyWrite_);
        Texture *meterTexture = meter_->getTexture();
        Texture *previousExposure = historyValid_ ? historyRead_->getTexture() : meterTexture;
        gfx_->drawTexturedRectShaderDepthMotion(meterTexture, previousExposure, meterTexture,
                                                adaptShader_, 0.f, 0.f, 1.f, 1.f,
                                                Color(1.f, 1.f, 1.f, 1.f));
        exposureTexture = historyWrite_->getTexture();
        historyValid_ = true;
        std::swap(historyRead_, historyWrite_);
    }

    applyShader_->sendFloat("manualExposure", std::max(manualExposure, 0.f));
    applyShader_->sendFloat("automatic", automatic ? 1.f : 0.f);
    output_->clear(Color(0.f, 0.f, 0.f, 0.f), {}, {});
    gfx_->setCanvas(output_);
    gfx_->drawTexturedRectShaderDepthMotion(source, exposureTexture, source, applyShader_, 0.f, 0.f,
                                            float(width_), float(height_),
                                            Color(1.f, 1.f, 1.f, 1.f));
    gfx_->setCanvas(previousCanvas);
    Texture *result = output_->getTexture();
    if (!result) throw eve::Exception("Exposure: output texture missing");
    return result;
}

}  // namespace eve::graphics
