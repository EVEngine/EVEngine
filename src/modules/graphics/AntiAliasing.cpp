#include "graphics/AntiAliasing.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Exposure.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/aa_fxaa_frag_spv.inc"
#include "graphics/shaders/aa_nfaa_frag_spv.inc"
#include "graphics/shaders/aa_smaa_frag_spv.inc"
#include "graphics/shaders/aa_ssaa_frag_spv.inc"
#include "graphics/shaders/aa_taa_frag_spv.inc"
#include "graphics/shaders/PostProcessWgsl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/vec4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

Shader *newPostShader(Graphics *gfx, const std::vector<uint32_t> &frag, const char *wgsl) {
    if (gfx->getBackendName() == "webgpu")
        return gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) + wgsl);
    return gfx->newShaderFromSpv({}, frag);
}

Shader *createFxaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_fxaa_frag_spv, aa_fxaa_frag_spv_count);
    Shader *sh = newPostShader(gfx, frag, shaders::kFxaa);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create FXAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("edgeThreshold");
    sh->declareFloat("edgeThresholdMin");
    sh->declareFloat("subpix");
    sh->declareFloat("quality");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("edgeThreshold", 0.166f);
    sh->sendFloat("edgeThresholdMin", 0.0833f);
    sh->sendFloat("subpix", 0.75f);
    sh->sendFloat("quality", 1.f);
    return sh;
}

Shader *createSmaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_smaa_frag_spv, aa_smaa_frag_spv_count);
    Shader *sh = newPostShader(gfx, frag, shaders::kSmaa);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create SMAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("threshold");
    sh->declareFloat("localContrast");
    sh->declareFloat("maxSearch");
    sh->declareFloat("cornerRounding");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("threshold", 0.1f);
    sh->sendFloat("localContrast", 0.5f);
    sh->sendFloat("maxSearch", 8.f);
    sh->sendFloat("cornerRounding", 0.25f);
    return sh;
}

Shader *createSsaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_ssaa_frag_spv, aa_ssaa_frag_spv_count);
    Shader *sh = newPostShader(gfx, frag, shaders::kSsaa);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create SSAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("scale");
    sh->declareFloat("kernel");
    sh->declareFloat("sampleRadius");
    sh->sendFloat("texelW", 1.f / 512.f);
    sh->sendFloat("texelH", 1.f / 512.f);
    sh->sendFloat("scale", 2.f);
    sh->sendFloat("kernel", 1.f);  // tent
    sh->sendFloat("sampleRadius", 1.f);
    return sh;
}

Shader *createNfaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_nfaa_frag_spv, aa_nfaa_frag_spv_count);
    Shader *sh = newPostShader(gfx, frag, shaders::kNfaa);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create NFAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("strength");
    sh->declareFloat("power");
    sh->declareFloat("blurScale");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("strength", 1.f);
    sh->sendFloat("power", 1.f);
    sh->sendFloat("blurScale", 1.f);
    return sh;
}

Shader *createTaaShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    auto frag = copySpv(aa_taa_frag_spv, aa_taa_frag_spv_count);
    Shader *sh = newPostShader(gfx, frag, shaders::kTaa);
    if (!sh || !sh->gpuHandle) throw eve::Exception("AntiAliasing: failed to create TAA shader");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("blendCurrent");
    sh->declareFloat("clampAmount");
    sh->declareFloat("historyValid");
    sh->declareFloat("jitterDeltaX");
    sh->declareFloat("jitterDeltaY");
    for (int i = 0; i < 16; ++i) sh->declareFloat("reprojection" + std::to_string(i));
    sh->declareFloat("temporalNearZ");
    sh->declareFloat("temporalFarZ");
    sh->declareFloat("reprojectionValid");
    sh->declareFloat("motionValid");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("blendCurrent", 0.92f);
    sh->sendFloat("clampAmount", 0.85f);
    sh->sendFloat("historyValid", 0.f);
    sh->sendFloat("jitterDeltaX", 0.f);
    sh->sendFloat("jitterDeltaY", 0.f);
    for (int i = 0; i < 16; ++i)
        sh->sendFloat("reprojection" + std::to_string(i), i % 5 == 0 ? 1.f : 0.f);
    sh->sendFloat("temporalNearZ", 0.1f);
    sh->sendFloat("temporalFarZ", 100.f);
    sh->sendFloat("reprojectionValid", 0.f);
    sh->sendFloat("motionValid", 0.f);
    return sh;
}

}  // namespace

AntiAliasing::AntiAliasing(Graphics *gfx) : gfx_(gfx) {
    fxaa_ = createFxaaShader(gfx);
    smaa_ = createSmaaShader(gfx);
    ssaa_ = createSsaaShader(gfx);
    nfaa_ = createNfaaShader(gfx);
    taa_ = createTaaShader(gfx);
    applyQualityDefaults();
}

AntiAliasing::~AntiAliasing() = default;

void AntiAliasing::applyQualityDefaults() {
    if (quality_ == "low") {
        fxaa_->sendFloat("edgeThreshold", 0.25f);
        fxaa_->sendFloat("edgeThresholdMin", 0.0833f);
        fxaa_->sendFloat("subpix", 0.5f);
        fxaa_->sendFloat("quality", 0.f);
        smaa_->sendFloat("threshold", 0.15f);
        smaa_->sendFloat("localContrast", 0.55f);
        smaa_->sendFloat("maxSearch", 4.f);
        smaa_->sendFloat("cornerRounding", 0.1f);
        ssaa_->sendFloat("scale", 2.f);
        ssaa_->sendFloat("kernel", 0.f);  // box
        ssaa_->sendFloat("sampleRadius", 0.75f);
        nfaa_->sendFloat("strength", 0.7f);
        nfaa_->sendFloat("power", 1.2f);
        nfaa_->sendFloat("blurScale", 0.85f);
        taa_->sendFloat("blendCurrent", 0.20f);
        taa_->sendFloat("clampAmount", 0.7f);
        return;
    }
    if (quality_ == "high") {
        fxaa_->sendFloat("edgeThreshold", 0.125f);
        fxaa_->sendFloat("edgeThresholdMin", 0.0312f);
        fxaa_->sendFloat("subpix", 0.85f);
        fxaa_->sendFloat("quality", 2.f);
        smaa_->sendFloat("threshold", 0.05f);
        smaa_->sendFloat("localContrast", 0.4f);
        smaa_->sendFloat("maxSearch", 16.f);
        smaa_->sendFloat("cornerRounding", 0.4f);
        ssaa_->sendFloat("scale", 4.f);
        ssaa_->sendFloat("kernel", 2.f);  // gaussian
        ssaa_->sendFloat("sampleRadius", 2.f);
        nfaa_->sendFloat("strength", 1.25f);
        nfaa_->sendFloat("power", 0.85f);
        nfaa_->sendFloat("blurScale", 1.35f);
        taa_->sendFloat("blendCurrent", 0.08f);
        taa_->sendFloat("clampAmount", 0.95f);
        return;
    }
    // medium (default / unknown)
    quality_ = "medium";
    fxaa_->sendFloat("edgeThreshold", 0.166f);
    fxaa_->sendFloat("edgeThresholdMin", 0.0833f);
    fxaa_->sendFloat("subpix", 0.75f);
    fxaa_->sendFloat("quality", 1.f);
    smaa_->sendFloat("threshold", 0.1f);
    smaa_->sendFloat("localContrast", 0.5f);
    smaa_->sendFloat("maxSearch", 8.f);
    smaa_->sendFloat("cornerRounding", 0.25f);
    ssaa_->sendFloat("scale", 2.f);
    ssaa_->sendFloat("kernel", 1.f);  // tent
    ssaa_->sendFloat("sampleRadius", 1.25f);
    nfaa_->sendFloat("strength", 1.f);
    nfaa_->sendFloat("power", 1.f);
    nfaa_->sendFloat("blurScale", 1.f);
    taa_->sendFloat("blendCurrent", 0.12f);
    taa_->sendFloat("clampAmount", 0.8f);
}

void AntiAliasing::setQuality(const std::string &quality) {
    if (quality == "low" || quality == "medium" || quality == "high")
        quality_ = quality;
    else
        quality_ = "medium";
    applyQualityDefaults();
}

void AntiAliasing::setMode(const std::string &mode) {
    const std::string next =
        (mode == "fxaa" || mode == "smaa" || mode == "ssaa" || mode == "nfaa" || mode == "taa")
            ? mode
            : "fxaa";
    if (next == "taa" && mode_ != "taa") resetHistory();
    mode_ = next;
}

void AntiAliasing::resetHistory() {
    taaHistoryValid_ = false;
}

void AntiAliasing::ensureHistoryCanvases(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (!taaHistoryA_ || !taaHistoryB_ || historyW_ != width || historyH_ != height) {
        taaHistoryA_ = gfx_ ? gfx_->newHDRCanvas(width, height) : nullptr;
        taaHistoryB_ = gfx_ ? gfx_->newHDRCanvas(width, height) : nullptr;
        if (!taaHistoryA_ || !taaHistoryB_)
            throw eve::Exception("AntiAliasing: failed to allocate temporal history canvases");
        historyRead_ = taaHistoryA_;
        historyWrite_ = taaHistoryB_;
        resetHistory();
    }
    historyW_ = width;
    historyH_ = height;
    if (!historyRead_) historyRead_ = taaHistoryA_;
    if (!historyWrite_) historyWrite_ = taaHistoryB_;
}

void AntiAliasing::swapHistoryBuffers() {
    std::swap(historyRead_, historyWrite_);
}

Shader *AntiAliasing::shaderForMode() const {
    if (mode_ == "taa") return taa_;
    if (mode_ == "smaa") return smaa_;
    if (mode_ == "ssaa") return ssaa_;
    if (mode_ == "nfaa") return nfaa_;
    return fxaa_;
}

Shader *AntiAliasing::getShader() const { return shaderForMode(); }

bool AntiAliasing::hasParam(const std::string &name) const {
    Shader *sh = shaderForMode();
    return sh && sh->hasUniform(name);
}

void AntiAliasing::setFloat(const std::string &name, float value) {
    Shader *sh = shaderForMode();
    if (!sh) throw eve::Exception("AntiAliasing.setFloat: null shader");
    sh->sendFloat(name, value);
}

float AntiAliasing::getFloat(const std::string &name) const {
    Shader *sh = shaderForMode();
    if (!sh) throw eve::Exception("AntiAliasing.getFloat: null shader");
    float v = 0.f;
    if (sh->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("AntiAliasing.getFloat: missing param '%s'", name.c_str());
    return v;
}

float AntiAliasing::suggestScale() const {
    if (mode_ != "ssaa") return 1.f;
    if (quality_ == "high") return 4.f;
    return 2.f;
}

int AntiAliasing::resolutionFor(int destSize) const {
    if (destSize < 1) destSize = 1;
    const float s = suggestScale();
    return std::max(1, int(std::floor(float(destSize) * s)));
}

void AntiAliasing::uploadScreenUniforms(Texture *source) {
    if (!source) throw eve::Exception("AntiAliasing: null source texture");
    const int w = std::max(1, source->getWidth());
    const int h = std::max(1, source->getHeight());
    const float tw = 1.f / float(w);
    const float th = 1.f / float(h);
    Shader *sh = shaderForMode();
    if (sh->hasUniform("texelW")) sh->sendFloat("texelW", tw);
    if (sh->hasUniform("texelH")) sh->sendFloat("texelH", th);
    if (mode_ == "ssaa" && sh->hasUniform("scale")) {
        // Keep user override if they set scale explicitly after setQuality;
        // still ensure scale reflects suggestScale when it matches the quality default.
        // Prefer the quality-driven suggestScale unless caller already changed it away
        // from {2,4}. Here we always sync scale to suggestScale for predictability.
        sh->sendFloat("scale", suggestScale());
    }
}

void AntiAliasing::drawFullscreen(Graphics *gfx, Texture *source, Shader *shader) {
    if (!gfx) throw eve::Exception("AntiAliasing: null graphics");
    if (!source) throw eve::Exception("AntiAliasing: null source texture");
    if (!shader) throw eve::Exception("AntiAliasing: null shader");
    uploadScreenUniforms(source);
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, glm::vec4(1.f, 1.f, 1.f, 1.f));
}

void AntiAliasing::prepareSource(Texture *source) { uploadScreenUniforms(source); }

Canvas *AntiAliasing::beginTemporalFrame(Texture *source, Texture *motion) {
    if (!source) throw eve::Exception("AntiAliasing.beginTemporalFrame: null source");
    ensureHistoryCanvases(std::max(1, source->getWidth()), std::max(1, source->getHeight()));
    if (!historyRead_ || !historyWrite_)
        throw eve::Exception("AntiAliasing.beginTemporalFrame: history canvases missing");
    uploadScreenUniforms(source);
    taa_->sendFloat("historyValid", taaHistoryValid_ ? 1.f : 0.f);
    const glm::vec2 jitterDelta = (previousJitterNdc_ - temporalJitterNdc_) * 0.5f;
    taa_->sendFloat("jitterDeltaX", jitterDelta.x);
    taa_->sendFloat("jitterDeltaY", jitterDelta.y);
    const glm::mat4 reprojection = previousViewProj_ * glm::inverse(temporalViewProj_);
    const float *matrixData = glm::value_ptr(reprojection);
    for (int i = 0; i < 16; ++i)
        taa_->sendFloat("reprojection" + std::to_string(i), matrixData[i]);
    taa_->sendFloat("temporalNearZ", temporalNearZ_);
    taa_->sendFloat("temporalFarZ", temporalFarZ_);
    taa_->sendFloat("reprojectionValid",
                    taaHistoryValid_ && temporalViewValid_ ? 1.f : 0.f);
    taa_->sendFloat("motionValid", motion ? 1.f : 0.f);
    return historyWrite_;
}

Texture *AntiAliasing::getTemporalReadTexture() const {
    return taaHistoryValid_ && historyRead_ ? historyRead_->getTexture() : nullptr;
}

void AntiAliasing::endTemporalFrame() {
    taaHistoryValid_ = true;
    previousJitterNdc_ = temporalJitterNdc_;
    previousViewProj_ = temporalViewProj_;
    temporalViewValid_ = true;
    temporalObjectHistory_ = std::move(temporalObjectPending_);
    temporalObjectPending_.clear();
    ++temporalFrameIndex_;
    swapHistoryBuffers();
}

glm::vec2 AntiAliasing::prepareTemporalJitter(int width, int height) {
    auto halton = [](uint32_t index, uint32_t base) {
        float result = 0.f;
        float fraction = 1.f / float(base);
        while (index > 0) {
            result += fraction * float(index % base);
            index /= base;
            fraction /= float(base);
        }
        return result;
    };
    const uint32_t sample = temporalFrameIndex_ % 8u + 1u;
    temporalJitterNdc_.x = 2.f * (halton(sample, 2u) - 0.5f) / float(std::max(1, width));
    temporalJitterNdc_.y = 2.f * (halton(sample, 3u) - 0.5f) / float(std::max(1, height));
    return temporalJitterNdc_;
}

void AntiAliasing::setTemporalCamera(const glm::vec3 &eye, const glm::vec3 &target,
                                     float fovYDeg) {
    glm::vec3 forward = target - eye;
    const float forwardLength = glm::length(forward);
    if (forwardLength > 1e-5f) forward /= forwardLength;
    if (temporalCameraValid_) {
        const float positionCut = std::max(1.f, forwardLength * 0.25f);
        const bool cut = glm::length(eye - temporalEye_) > positionCut ||
                         glm::dot(forward, temporalForward_) < 0.94f ||
                         std::fabs(fovYDeg - temporalFovY_) > 5.f;
        if (cut) {
            resetHistory();
            if (gfx_) gfx_->pipelineExposure()->invalidateHistory();
        }
    }
    temporalEye_ = eye;
    temporalForward_ = forward;
    temporalFovY_ = fovYDeg;
    temporalCameraValid_ = true;
}

void AntiAliasing::invalidateTemporalHistory() {
    resetHistory();
    if (gfx_) gfx_->pipelineExposure()->invalidateHistory();
}

void AntiAliasing::setTemporalViewProjection(const glm::mat4 &viewProjection, float nearZ,
                                             float farZ) {
    temporalViewProj_ = viewProjection;
    temporalNearZ_ = std::max(nearZ, 1e-4f);
    temporalFarZ_ = std::max(farZ, temporalNearZ_ + 1e-3f);
}

glm::vec2 AntiAliasing::prepareTemporalObjectMotion(const void *objectKey,
                                                    const glm::mat4 &model) {
    if (!objectKey) return glm::vec2(0.f);
    temporalObjectPending_[objectKey] = model;
    const auto previous = temporalObjectHistory_.find(objectKey);
    if (!taaHistoryValid_ || !temporalViewValid_ || previous == temporalObjectHistory_.end())
        return glm::vec2(0.f);
    const glm::vec4 origin(0.f, 0.f, 0.f, 1.f);
    const glm::vec4 staticPrevious = previousViewProj_ * model * origin;
    const glm::vec4 actualPrevious = previousViewProj_ * previous->second * origin;
    if (std::fabs(staticPrevious.w) < 1e-6f || std::fabs(actualPrevious.w) < 1e-6f)
        return glm::vec2(0.f);
    const glm::vec2 staticUv = glm::vec2(staticPrevious) / staticPrevious.w * 0.5f + 0.5f;
    const glm::vec2 actualUv = glm::vec2(actualPrevious) / actualPrevious.w * 0.5f + 0.5f;
    return glm::clamp(actualUv - staticUv, glm::vec2(-1.f), glm::vec2(1.f));
}

void AntiAliasing::applyTemporal(Graphics *gfx, Texture *source, Texture *motion) {
    if (!gfx) throw eve::Exception("AntiAliasing.applyTemporal: null graphics");
    if (!source) throw eve::Exception("AntiAliasing.applyTemporal: null source");

    Canvas *finalCanvas = gfx->getCanvas();
    const float dw = finalCanvas ? float(finalCanvas->getWidth()) : float(gfx->getWidth());
    const float dh = finalCanvas ? float(finalCanvas->getHeight()) : float(gfx->getHeight());
    Canvas *writeTarget = beginTemporalFrame(source, motion);
    Texture *prevTex = getTemporalReadTexture();
    Shader *sh = getTaaShader();
    if (!sh) throw eve::Exception("AntiAliasing.applyTemporal: missing taa shader");

    gfx->setCanvas(writeTarget);
    gfx->drawTexturedRectShaderDepthMotion(
        source, prevTex ? prevTex : source, motion ? motion : source, sh, 0.f, 0.f,
        float(writeTarget->getWidth()), float(writeTarget->getHeight()),
        glm::vec4(1.f, 1.f, 1.f, 1.f));

    Texture *resolved = writeTarget->getTexture();
    if (!resolved) throw eve::Exception("AntiAliasing.applyTemporal: resolved texture missing");
    gfx->setCanvas(finalCanvas);
    gfx->drawTexturedRectShaderUV(resolved, nullptr, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                  glm::vec4(1.f, 1.f, 1.f, 1.f), false,
                                  BlendMode::Opaque);
    endTemporalFrame();
}

Texture *AntiAliasing::resolveTemporal(Graphics *gfx, Texture *source, Texture *motion) {
    if (!gfx) throw eve::Exception("AntiAliasing.resolveTemporal: null graphics");
    if (!source) throw eve::Exception("AntiAliasing.resolveTemporal: null source");
    Canvas *finalCanvas = gfx->getCanvas();
    Canvas *writeTarget = beginTemporalFrame(source, motion);
    Texture *previous = getTemporalReadTexture();
    Shader *shader = getTaaShader();
    if (!shader) throw eve::Exception("AntiAliasing.resolveTemporal: missing taa shader");
    gfx->setCanvas(writeTarget);
    gfx->drawTexturedRectShaderDepthMotion(
        source, previous ? previous : source, motion ? motion : source, shader, 0.f, 0.f,
        float(writeTarget->getWidth()), float(writeTarget->getHeight()),
        glm::vec4(1.f, 1.f, 1.f, 1.f));
    Texture *resolved = writeTarget->getTexture();
    if (!resolved) throw eve::Exception("AntiAliasing.resolveTemporal: result missing");
    gfx->setCanvas(finalCanvas);
    endTemporalFrame();
    return resolved;
}

void AntiAliasing::applyTemporalRect(Graphics *gfx, Texture *source, float x, float y, float width,
                                     float height, float r, float g, float b, float a) {
    if (!gfx) throw eve::Exception("AntiAliasing.applyTemporalRect: null graphics");
    if (!source) throw eve::Exception("AntiAliasing.applyTemporalRect: null source");

    Canvas *finalCanvas = gfx->getCanvas();
    Canvas *writeTarget = beginTemporalFrame(source);
    Texture *prevTex = getTemporalReadTexture();
    Shader *sh = getTaaShader();
    if (!sh) throw eve::Exception("AntiAliasing.applyTemporal: missing taa shader");

    // Pass 1: merge current + previous to history-write target.
    gfx->setCanvas(writeTarget);
    gfx->drawTexturedRectShaderDepthMotion(
        source, prevTex ? prevTex : source, source, sh, 0.f, 0.f,
        float(writeTarget->getWidth()), float(writeTarget->getHeight()),
        glm::vec4(1.f, 1.f, 1.f, 1.f));

    // Pass 2: composite resolved temporal result to the requested final target.
    Texture *resolved = writeTarget->getTexture();
    if (!resolved) throw eve::Exception("AntiAliasing.applyTemporal: resolved texture missing");
    gfx->setCanvas(finalCanvas);
    gfx->drawTexturedRectShaderUV(resolved, nullptr, x, y, width, height, 0.f, 0.f, 1.f, 1.f,
                                  glm::vec4(r, g, b, a), false, BlendMode::Opaque);

    endTemporalFrame();
}

void AntiAliasing::apply(Graphics *gfx, Texture *source) {
    if (mode_ == "taa")
        applyTemporal(gfx, source);
    else
        drawFullscreen(gfx, source, shaderForMode());
}

void AntiAliasing::applyTo(Graphics *gfx, Texture *source, Canvas *dest) {
    if (!gfx) throw eve::Exception("AntiAliasing.applyTo: null graphics");
    if (!dest) throw eve::Exception("AntiAliasing.applyTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    apply(gfx, source);
    gfx->setCanvas(prev);
}

void AntiAliasing::applyCanvas(Graphics *gfx, Canvas *source) {
    if (!source) throw eve::Exception("AntiAliasing.applyCanvas: null source");
    Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("AntiAliasing.applyCanvas: source has no sampleable texture");
    apply(gfx, tex);
}

void AntiAliasing::applyCanvasTo(Graphics *gfx, Canvas *source, Canvas *dest) {
    if (!source) throw eve::Exception("AntiAliasing.applyCanvasTo: null source");
    Texture *tex = source->getTexture();
    if (!tex) throw eve::Exception("AntiAliasing.applyCanvasTo: source has no sampleable texture");
    applyTo(gfx, tex, dest);
}

}  // namespace eve::graphics
