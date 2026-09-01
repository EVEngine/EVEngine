#include "graphics/GlobalIllumination.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/gi_ssgi_frag_spv.inc"
#include "graphics/shaders/ssr_temporal_frag_spv.inc"
#include "graphics/shaders/ScreenEffectsWgsl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {
namespace {

Shader *createSsgiShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("GlobalIllumination: null graphics");
    std::vector<uint32_t> frag(gi_ssgi_frag_spv, gi_ssgi_frag_spv + gi_ssgi_frag_spv_count);
    Shader *sh = gfx->getBackendName() == "webgpu"
                     ? gfx->newShaderFromWgsl({}, std::string(shaders::kScreenEffectCommon) +
                                                     shaders::kSsgi)
                     : gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle) throw eve::Exception("GlobalIllumination: failed to create SSGI shader");
    sh->declareMatrix("invViewProj");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("radius");
    sh->declareFloat("intensity");
    sh->declareFloat("sampleCount");
    sh->declareFloat("lightDirX");
    sh->declareFloat("lightDirY");
    sh->declareFloat("lightDirZ");
    sh->declareFloat("lightR");
    sh->declareFloat("lightG");
    sh->declareFloat("lightB");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("useNdcDepth");
    sh->declareFloat("normalValid");
    sh->declareFloat("thickness");
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("radius", 1.25f);
    sh->sendFloat("intensity", 0.45f);
    sh->sendFloat("sampleCount", 12.f);
    sh->sendFloat("lightDirX", 0.4f);
    sh->sendFloat("lightDirY", 1.f);
    sh->sendFloat("lightDirZ", 0.3f);
    sh->sendFloat("lightR", 1.f);
    sh->sendFloat("lightG", 1.f);
    sh->sendFloat("lightB", 1.f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("useNdcDepth", 0.f);
    sh->sendFloat("normalValid", 0.f);
    sh->sendFloat("thickness", 0.2f);
    return sh;
}

Shader *createTemporalShader(Graphics *gfx) {
    std::vector<uint32_t> frag(ssr_temporal_frag_spv,
                               ssr_temporal_frag_spv + ssr_temporal_frag_spv_count);
    Shader *sh = gfx->getBackendName() == "webgpu"
                     ? gfx->newShaderFromWgsl({}, std::string(shaders::kScreenEffectCommon) +
                                                     shaders::kSsrTemporal)
                     : gfx->newShaderFromSpv({}, frag);
    for (int i = 0; i < 16; ++i) sh->declareFloat("reprojection" + std::to_string(i));
    sh->declareFloat("temporalTexelW");
    sh->declareFloat("temporalTexelH");
    sh->declareFloat("temporalHistoryValid");
    sh->declareFloat("temporalNearZ");
    sh->declareFloat("temporalFarZ");
    sh->declareFloat("temporalCurrentWeight");
    sh->declareFloat("spatialFilterStrength");
    sh->declareFloat("temporalEnergyEncoded");
    sh->declareFloat("spatialStep");
    return sh;
}

}  // namespace

GlobalIllumination::GlobalIllumination(Graphics *gfx) : gfx_(gfx) {
    ssgi_ = createSsgiShader(gfx);
    temporal_ = createTemporalShader(gfx);
    applyQualityDefaults();
}

GlobalIllumination::~GlobalIllumination() = default;

void GlobalIllumination::applyQualityDefaults() {
    if (quality_ == "low") {
        radius_ = 0.9f;
        thickness_ = 0.32f;
        temporalCurrentWeight_ = 0.3f;
        spatialFilterStrength_ = 0.8f;
        resolutionScale_ = 0.5f;
        setFloat("sampleCount", 8.f);
    } else if (quality_ == "high") {
        radius_ = 1.6f;
        thickness_ = 0.14f;
        temporalCurrentWeight_ = 0.12f;
        spatialFilterStrength_ = 0.4f;
        resolutionScale_ = 1.f;
        setFloat("sampleCount", 24.f);
    } else if (quality_ == "ultra") {
        radius_ = 1.8f;
        thickness_ = 0.1f;
        temporalCurrentWeight_ = 0.08f;
        spatialFilterStrength_ = 0.3f;
        resolutionScale_ = 1.f;
        setFloat("sampleCount", 32.f);
    } else {
        radius_ = 1.25f;
        thickness_ = 0.2f;
        temporalCurrentWeight_ = 0.16f;
        spatialFilterStrength_ = 0.5f;
        resolutionScale_ = 1.f;
        setFloat("sampleCount", 16.f);
    }
    setFloat("radius", radius_);
    setFloat("intensity", intensity_);
}

void GlobalIllumination::setQuality(const std::string &quality) {
    if (quality == "low" || quality == "medium" || quality == "high" || quality == "ultra")
        quality_ = quality;
    else
        quality_ = "medium";
    applyQualityDefaults();
    historyValid_ = false;
}

void GlobalIllumination::setInvViewProj(const glm::mat4 &invViewProj) {
    const glm::mat4 nextViewProj = glm::inverse(invViewProj);
    if (viewProjValid_) {
        float maxDelta = 0.f;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                maxDelta = std::max(maxDelta,
                                    std::abs(nextViewProj[c][r] - previousViewProj_[c][r]));
        if (maxDelta > 2.f) historyValid_ = false;
    }
    invViewProj_ = invViewProj;
    ssgi_->sendMatrix("invViewProj", invViewProj_);
}

void GlobalIllumination::setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY,
                                   float targetZ, float upX, float upY, float upZ, float fovYDeg,
                                   float aspect, float nearZ, float farZ) {
    nearZ_ = nearZ > 1e-4f ? nearZ : 0.1f;
    farZ_ = farZ > nearZ_ ? farZ : nearZ_ + 1.f;
    const float aspectSafe = aspect > 1e-4f ? aspect : 1.f;
    const float fovRad = fovYDeg * 0.017453292519943295f;
    const glm::vec3 eye(eyeX, eyeY, eyeZ);
    glm::vec3 forward = glm::vec3(targetX, targetY, targetZ) - eye;
    forward = glm::length(forward) > 1e-5f ? glm::normalize(forward)
                                            : glm::vec3(0.f, 0.f, -1.f);
    if (cameraValid_ &&
        (glm::distance(eye, previousEye_) > std::max(1.f, farZ_ * 0.1f) ||
         glm::dot(forward, previousForward_) < 0.65f ||
         std::abs(fovYDeg - previousFovY_) > 3.f ||
         std::abs(aspectSafe - previousAspect_) > std::max(0.05f, previousAspect_ * 0.1f)))
        historyValid_ = false;
    previousEye_ = eye;
    previousForward_ = forward;
    previousFovY_ = fovYDeg;
    previousAspect_ = aspectSafe;
    cameraValid_ = true;
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(targetX, targetY, targetZ),
                      glm::vec3(upX, upY, upZ));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(fovRad, aspectSafe, nearZ_, farZ_);
    setInvViewProj(glm::inverse(proj * view));
    ssgi_->sendFloat("nearZ", nearZ_);
    ssgi_->sendFloat("farZ", farZ_);
}

void GlobalIllumination::setRadius(float radius) {
    radius_ = radius > 1e-4f ? radius : 1e-4f;
    setFloat("radius", radius_);
}

void GlobalIllumination::setIntensity(float intensity) {
    intensity_ = std::max(intensity, 0.f);
    setFloat("intensity", intensity_);
}

void GlobalIllumination::setThickness(float thickness) {
    thickness_ = std::clamp(thickness, 0.01f, 0.99f);
    historyValid_ = false;
}

void GlobalIllumination::setResolutionScale(float scale) {
    const float next = std::clamp(scale, 0.25f, 1.f);
    if (std::abs(next - resolutionScale_) < 1e-4f) return;
    resolutionScale_ = next;
    giCanvas_ = nullptr;
    historyValid_ = false;
}

void GlobalIllumination::setLightDirection(float dx, float dy, float dz) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    else d = glm::normalize(d);
    lightDir_ = d;
    historyValid_ = false;
    ssgi_->sendFloat("lightDirX", lightDir_.x);
    ssgi_->sendFloat("lightDirY", lightDir_.y);
    ssgi_->sendFloat("lightDirZ", lightDir_.z);
}

void GlobalIllumination::setLightColor(float r, float g, float b) {
    lightColor_ = glm::vec3(std::max(r, 0.f), std::max(g, 0.f), std::max(b, 0.f));
    historyValid_ = false;
    ssgi_->sendFloat("lightR", lightColor_.x);
    ssgi_->sendFloat("lightG", lightColor_.y);
    ssgi_->sendFloat("lightB", lightColor_.z);
}

bool GlobalIllumination::hasParam(const std::string &name) const {
    return ssgi_ && ssgi_->hasUniform(name);
}

void GlobalIllumination::setFloat(const std::string &name, float value) {
    if (!ssgi_ || !ssgi_->hasUniform(name))
        throw eve::Exception("GlobalIllumination.setFloat: missing param '%s'", name.c_str());
    if (name == "thickness") {
        setThickness(value);
        ssgi_->sendFloat("thickness", float(samplingFrame_ & 7u) + thickness_);
        return;
    }
    ssgi_->sendFloat(name, value);
    if (name == "radius" || name == "intensity" || name == "sampleCount")
        historyValid_ = false;
    if (name == "radius") radius_ = value;
    if (name == "intensity") intensity_ = value;
}

float GlobalIllumination::getFloat(const std::string &name) const {
    if (!ssgi_ || !ssgi_->hasUniform(name))
        throw eve::Exception("GlobalIllumination.getFloat: missing param '%s'", name.c_str());
    if (name == "thickness") return thickness_;
    float v = 0.f;
    if (ssgi_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    throw eve::Exception("GlobalIllumination.getFloat: missing param '%s'", name.c_str());
}

int GlobalIllumination::getSampleCount() const {
    return int(std::lround(getFloat("sampleCount")));
}

Canvas *GlobalIllumination::getGiCanvas() {
    if (gfx_) {
        const int fullW = std::max(1, gfx_->getPixelWidth() > 0 ? gfx_->getPixelWidth() : gfx_->getWidth());
        const int fullH = std::max(1, gfx_->getPixelHeight() > 0 ? gfx_->getPixelHeight() : gfx_->getHeight());
        const int targetW = std::max(1, int(std::lround(float(fullW) * resolutionScale_)));
        const int targetH = std::max(1, int(std::lround(float(fullH) * resolutionScale_)));
        if (!giCanvas_ || giCanvas_->getWidth() != targetW || giCanvas_->getHeight() != targetH)
            giCanvas_ = gfx_->newHDRCanvas(targetW, targetH);
    }
    return giCanvas_;
}

void GlobalIllumination::uploadUniforms(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    ssgi_->sendMatrix("invViewProj", invViewProj_);
    ssgi_->sendFloat("nearZ", nearZ_);
    ssgi_->sendFloat("farZ", farZ_);
    ssgi_->sendFloat("radius", radius_);
    ssgi_->sendFloat("intensity", intensity_);
    ssgi_->sendFloat("lightDirX", lightDir_.x);
    ssgi_->sendFloat("lightDirY", lightDir_.y);
    ssgi_->sendFloat("lightDirZ", lightDir_.z);
    ssgi_->sendFloat("lightR", lightColor_.x);
    ssgi_->sendFloat("lightG", lightColor_.y);
    ssgi_->sendFloat("lightB", lightColor_.z);
    ssgi_->sendFloat("texelW", 1.f / float(width));
    ssgi_->sendFloat("texelH", 1.f / float(height));
    ssgi_->sendFloat("normalValid", worldNormal_ ? 1.f : 0.f);
    // Push constants are capped at 32 floats. Pack the 8-phase sampling index in
    // the integer part and world-space thickness in the fractional part.
    const uint32_t packedPhase = (samplingFrame_ & 7u) +
                                 uint32_t(std::max(depthPyramidLevels_, 0)) * 8u;
    ssgi_->sendFloat("thickness", float(packedPhase) +
                                      std::clamp(thickness_, 0.01f, 0.99f));
    ++samplingFrame_;
}

void GlobalIllumination::drawFullscreen(Graphics *gfx, Texture *source, Shader *shader,
                                        Texture *hwDepth) {
    if (!gfx) throw eve::Exception("GlobalIllumination: null graphics");
    if (!source) throw eve::Exception("GlobalIllumination: null source texture");
    if (!shader) throw eve::Exception("GlobalIllumination: null shader");
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    if (hwDepth)
        gfx->drawTexturedRectShader4(source, hwDepth, worldNormal_ ? worldNormal_ : source,
                                     albedo_ ? albedo_ : source, shader, 0.f, 0.f, dw, dh,
                                     Color(1.f, 1.f, 1.f, 1.f));
    else
        gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, Color(1.f, 1.f, 1.f, 1.f));
}

void GlobalIllumination::applyFromDepth(Graphics *gfx, Texture *packedAlbedo) {
    if (!packedAlbedo) throw eve::Exception("GlobalIllumination.applyFromDepth: null packedAlbedo");
    ssgi_->sendFloat("useNdcDepth", 0.f);
    uploadUniforms(packedAlbedo->getWidth(), packedAlbedo->getHeight());
    ssgi_->sendFloat("normalValid", 0.f);
    drawFullscreen(gfx, packedAlbedo, ssgi_, nullptr);
}

void GlobalIllumination::applyFromScene(Graphics *gfx, Texture *color, Texture *hwDepth) {
    if (!color) throw eve::Exception("GlobalIllumination.applyFromScene: null color");
    if (!hwDepth) throw eve::Exception("GlobalIllumination.applyFromScene: null hwDepth");
    ssgi_->sendFloat("useNdcDepth", 1.f);
    uploadUniforms(color->getWidth(), color->getHeight());
    ssgi_->sendFloat("normalValid", worldNormal_ ? 1.f : 0.f);
    Texture *traceDepth = depthPyramid_ && depthPyramidLevels_ > 0 ? depthPyramid_ : hwDepth;
    drawFullscreen(gfx, color, ssgi_, traceDepth);
}

void GlobalIllumination::applyFromDepthTo(Graphics *gfx, Texture *packedAlbedo, Canvas *dest) {
    if (!gfx) throw eve::Exception("GlobalIllumination.applyFromDepthTo: null graphics");
    if (!dest) throw eve::Exception("GlobalIllumination.applyFromDepthTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromDepth(gfx, packedAlbedo);
    gfx->setCanvas(prev);
}

Canvas *GlobalIllumination::getWorkingCanvas() {
    return getGiCanvas();
}

Texture *GlobalIllumination::getWorkingTexture() {
    getGiCanvas();
    return historyValid_ && historyRead_ ? historyRead_->getTexture()
                                         : (giCanvas_ ? giCanvas_->getTexture() : nullptr);
}

void GlobalIllumination::ensureHistory(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (!historyA_ || !historyB_ || historyWidth_ != width || historyHeight_ != height) {
        historyA_ = gfx_->newHDRCanvas(width, height);
        historyB_ = gfx_->newHDRCanvas(width, height);
        spatialA_ = gfx_->newHDRCanvas(width, height);
        spatialB_ = gfx_->newHDRCanvas(width, height);
        historyRead_ = historyA_;
        historyWrite_ = historyB_;
        historyWidth_ = width;
        historyHeight_ = height;
        historyValid_ = false;
    }
}

void GlobalIllumination::resolveTemporal(Graphics *gfx, Texture *current) {
    if (!gfx || !current || !temporalMotionDepth_ || !temporal_) {
        historyValid_ = false;
        return;
    }
    ensureHistory(current->getWidth(), current->getHeight());
    const glm::mat4 currentViewProj = glm::inverse(invViewProj_);
    const glm::mat4 reprojection = previousViewProj_ * invViewProj_;
    const float *matrix = &reprojection[0][0];
    for (int i = 0; i < 16; ++i)
        temporal_->sendFloat("reprojection" + std::to_string(i), matrix[i]);
    temporal_->sendFloat("temporalTexelW", 1.f / float(historyWidth_));
    temporal_->sendFloat("temporalTexelH", 1.f / float(historyHeight_));
    temporal_->sendFloat("temporalNearZ", nearZ_);
    temporal_->sendFloat("temporalFarZ", farZ_);
    temporal_->sendFloat("temporalCurrentWeight", temporalCurrentWeight_);
    temporal_->sendFloat("temporalEnergyEncoded", 1.f);

    const int spatialPasses = getSpatialPassCount();
    Texture *filtered = current;
    for (int pass = 0; pass < spatialPasses; ++pass) {
        Canvas *target = (pass & 1) == 0 ? spatialA_ : spatialB_;
        temporal_->sendFloat("temporalHistoryValid", 0.f);
        temporal_->sendFloat("spatialFilterStrength", spatialFilterStrength_);
        temporal_->sendFloat("spatialStep", float(1 << pass));
        gfx->setCanvas(target);
        gfx->drawTexturedRectShaderDepthMotion(
            filtered, filtered, temporalMotionDepth_, temporal_, 0.f, 0.f, float(historyWidth_),
            float(historyHeight_), Color(1.f, 1.f, 1.f, 1.f));
        filtered = target->getTexture();
    }

    temporal_->sendFloat("temporalHistoryValid", historyValid_ && viewProjValid_ ? 1.f : 0.f);
    temporal_->sendFloat("spatialFilterStrength", spatialPasses == 0 ? spatialFilterStrength_ : 0.f);
    temporal_->sendFloat("spatialStep", 1.f);
    gfx->setCanvas(historyWrite_);
    Texture *history = historyValid_ ? historyRead_->getTexture() : filtered;
    gfx->drawTexturedRectShaderDepthMotion(
        filtered, history, temporalMotionDepth_, temporal_, 0.f, 0.f, float(historyWidth_),
        float(historyHeight_), Color(1.f, 1.f, 1.f, 1.f));
    std::swap(historyRead_, historyWrite_);
    historyValid_ = true;
    viewProjValid_ = true;
    previousViewProj_ = currentViewProj;
}

void GlobalIllumination::applyFromSceneTo(Graphics *gfx, Texture *color, Texture *hwDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("GlobalIllumination.applyFromSceneTo: null graphics");
    if (!dest) throw eve::Exception("GlobalIllumination.applyFromSceneTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromScene(gfx, color, hwDepth);
    resolveTemporal(gfx, dest->getTexture());
    gfx->setCanvas(prev);
}

void GlobalIllumination::applyFromSceneTo(Graphics *gfx, Texture *color, Texture *hwDepth) {
    if (!color) throw eve::Exception("GlobalIllumination.applyFromSceneTo: null color");
    if (!hwDepth) throw eve::Exception("GlobalIllumination.applyFromSceneTo: null hwDepth");
    Canvas *canvas = getGiCanvas();
    if (!canvas) throw eve::Exception("GlobalIllumination.applyFromSceneTo: failed to create canvas");
    applyFromSceneTo(gfx, color, hwDepth, canvas);
}

}  // namespace eve::graphics
