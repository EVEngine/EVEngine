#include "graphics/ScreenSpaceReflection.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/ssr_frag_spv.inc"
#include "graphics/shaders/ssr_temporal_frag_spv.inc"
#include "graphics/shaders/ScreenEffectsWgsl.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {

namespace {
Shader *createSsrShader(Graphics *gfx) {
    std::vector<uint32_t> frag(ssr_frag_spv, ssr_frag_spv + ssr_frag_spv_count);
    Shader *sh = gfx->getBackendName() == "webgpu"
                     ? gfx->newShaderFromWgsl({}, std::string(shaders::kScreenEffectCommon) +
                                                     shaders::kSsr)
                     : gfx->newShaderFromSpv({}, frag);
    sh->declareMatrix("invViewProj");
    sh->declareVec3("cameraPos");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("maxDist");
    sh->declareFloat("stepLen");
    sh->declareFloat("maxSteps");
    sh->declareFloat("thickness");
    sh->declareFloat("strength");
    sh->declareFloat("enabled");
    sh->declareFloat("bias");
    sh->declareFloat("maxRoughness");
    sh->declareFloat("depthPyramidLevels");
    return sh;
}

Shader *createSsrTemporalShader(Graphics *gfx) {
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

ScreenSpaceReflection::ScreenSpaceReflection(Graphics *gfx) : gfx_(gfx) {
    ssr_ = createSsrShader(gfx);
    temporal_ = createSsrTemporalShader(gfx);
}

ScreenSpaceReflection::~ScreenSpaceReflection() {
    // Shader is owned by Graphics.
    gfx_ = nullptr;
    ssr_ = nullptr;
    temporal_ = nullptr;
}

void ScreenSpaceReflection::setInvViewProj(const glm::mat4 &invViewProj) {
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
    ssr_->sendMatrix("invViewProj", invViewProj_);
}

void ScreenSpaceReflection::ensureHistory(int width, int height) {
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

void ScreenSpaceReflection::resolveTemporal(Graphics *gfx, Texture *current,
                                            Texture *motionDepth) {
    if (!current || !motionDepth || !temporal_) return;
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
    temporal_->sendFloat("temporalEnergyEncoded", 0.f);

    const int spatialPasses = getSpatialPassCount();
    Texture *filtered = current;
    for (int pass = 0; pass < spatialPasses; ++pass) {
        Canvas *target = (pass & 1) == 0 ? spatialA_ : spatialB_;
        temporal_->sendFloat("temporalHistoryValid", 0.f);
        temporal_->sendFloat("spatialFilterStrength", spatialFilterStrength_);
        temporal_->sendFloat("spatialStep", float(1 << pass));
        gfx->setCanvas(target);
        gfx->drawTexturedRectShaderDepthMotion(
            filtered, filtered, motionDepth, temporal_, 0.f, 0.f, float(historyWidth_),
            float(historyHeight_), Color(1.f, 1.f, 1.f, 1.f));
        filtered = target->getTexture();
    }

    temporal_->sendFloat("temporalHistoryValid", historyValid_ && viewProjValid_ ? 1.f : 0.f);
    temporal_->sendFloat("spatialFilterStrength", spatialPasses == 0 ? spatialFilterStrength_ : 0.f);
    temporal_->sendFloat("spatialStep", 1.f);
    gfx->setCanvas(historyWrite_);
    Texture *history = historyValid_ ? historyRead_->getTexture() : filtered;
    gfx->drawTexturedRectShaderDepthMotion(
        filtered, history, motionDepth, temporal_, 0.f, 0.f, float(historyWidth_),
        float(historyHeight_), Color(1.f, 1.f, 1.f, 1.f));
    std::swap(historyRead_, historyWrite_);
    historyValid_ = true;
    viewProjValid_ = true;
    previousViewProj_ = currentViewProj;
}

void ScreenSpaceReflection::setCamera(float eyeX, float eyeY, float eyeZ, float targetX,
                                       float targetY, float targetZ, float upX, float upY,
                                       float upZ, float fovYDeg, float aspect, float nearZ,
                                       float farZ) {
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
    ssr_->sendFloat("nearZ", nearZ_);
    ssr_->sendFloat("farZ", farZ_);
}

void ScreenSpaceReflection::setEnabled(bool enabled) {
    if (enabled_ != enabled) historyValid_ = false;
    enabled_ = enabled;
    ssr_->sendFloat("enabled", enabled_ ? 1.f : 0.f);
}

void ScreenSpaceReflection::setQuality(const std::string &quality) {
    quality_ = quality == "low" || quality == "medium" || quality == "high" ||
                       quality == "ultra"
                   ? quality
                   : "medium";
    historyValid_ = false;
    if (quality_ == "low") {
        setMaxSteps(48);
        setStepLength(0.7f);
        setThickness(0.75f);
        setMaxRoughness(0.65f);
        setResolutionScale(0.5f);
        temporalCurrentWeight_ = 0.3f;
        spatialFilterStrength_ = 0.8f;
    } else if (quality_ == "high") {
        setMaxSteps(160);
        setStepLength(0.25f);
        setThickness(0.35f);
        setMaxRoughness(0.9f);
        setResolutionScale(1.f);
        temporalCurrentWeight_ = 0.12f;
        spatialFilterStrength_ = 0.4f;
    } else if (quality_ == "ultra") {
        setMaxSteps(256);
        setStepLength(0.15f);
        setThickness(0.22f);
        setMaxRoughness(1.f);
        setResolutionScale(1.f);
        temporalCurrentWeight_ = 0.08f;
        spatialFilterStrength_ = 0.3f;
    } else {
        setMaxSteps(96);
        setStepLength(0.4f);
        setThickness(0.5f);
        setMaxRoughness(0.8f);
        setResolutionScale(1.f);
        temporalCurrentWeight_ = 0.16f;
        spatialFilterStrength_ = 0.5f;
    }
}

void ScreenSpaceReflection::setMaxDistance(float v) { setFloat("maxDist", std::max(v, 0.1f)); }
void ScreenSpaceReflection::setStepLength(float v) { setFloat("stepLen", std::max(v, 0.01f)); }
void ScreenSpaceReflection::setMaxSteps(int v) { setFloat("maxSteps", float(std::clamp(v, 1, 512))); }
void ScreenSpaceReflection::setThickness(float v) { setFloat("thickness", std::max(v, 0.0f)); }
void ScreenSpaceReflection::setStrength(float v) { setFloat("strength", std::max(v, 0.f)); }
void ScreenSpaceReflection::setMaxRoughness(float v) {
    setFloat("maxRoughness", std::clamp(v, 0.f, 1.f));
}
void ScreenSpaceReflection::setResolutionScale(float scale) {
    const float next = std::clamp(scale, 0.25f, 1.f);
    if (std::abs(next - resolutionScale_) < 1e-4f) return;
    resolutionScale_ = next;
    reflection_ = nullptr;
    historyValid_ = false;
}

bool ScreenSpaceReflection::hasParam(const std::string &name) const {
    return ssr_ && ssr_->hasUniform(name);
}

void ScreenSpaceReflection::setFloat(const std::string &name, float value) {
    if (!ssr_ || !ssr_->hasUniform(name))
        throw eve::Exception("ScreenSpaceReflection.setFloat: missing param '%s'", name.c_str());
    ssr_->sendFloat(name, value);
    if (name == "maxDist" || name == "stepLen" || name == "maxSteps" ||
        name == "thickness" || name == "strength" || name == "bias" ||
        name == "maxRoughness")
        historyValid_ = false;
    if (name == "maxDist") maxDist_ = value;
    if (name == "stepLen") stepLen_ = value;
    if (name == "maxSteps") maxSteps_ = value;
    if (name == "thickness") thickness_ = value;
    if (name == "strength") strength_ = value;
    if (name == "maxRoughness") maxRoughness_ = value;
}

float ScreenSpaceReflection::getFloat(const std::string &name) const {
    if (!ssr_ || !ssr_->hasUniform(name))
        throw eve::Exception("ScreenSpaceReflection.getFloat: missing param '%s'", name.c_str());
    float v = 0.f;
    if (ssr_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    throw eve::Exception("ScreenSpaceReflection.getFloat: missing param '%s'", name.c_str());
}

void ScreenSpaceReflection::uploadUniforms(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    ssr_->sendMatrix("invViewProj", invViewProj_);
    ssr_->sendFloat("nearZ", nearZ_);
    ssr_->sendFloat("farZ", farZ_);
    ssr_->sendFloat("texelW", 1.f / float(width));
    ssr_->sendFloat("texelH", 1.f / float(height));
    ssr_->sendFloat("maxDist", maxDist_);
    ssr_->sendFloat("stepLen", stepLen_);
    ssr_->sendFloat("maxSteps", maxSteps_);
    ssr_->sendFloat("thickness", thickness_);
    ssr_->sendFloat("strength", strength_);
    ssr_->sendFloat("enabled", enabled_ ? 1.f : 0.f);
    ssr_->sendFloat("bias", bias_);
    ssr_->sendFloat("maxRoughness", maxRoughness_);
    ssr_->sendFloat("depthPyramidLevels", float(std::max(depthPyramidLevels_, 0)));
}

void ScreenSpaceReflection::applyFromSceneTo(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                                             Texture *worldNormal, Texture *albedo, Canvas *dest) {
    if (!gfx) throw eve::Exception("ScreenSpaceReflection: null graphics");
    if (!dest) throw eve::Exception("ScreenSpaceReflection.applyFromSceneTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromScene(gfx, sceneColor, hwDepth, worldNormal, albedo);
    if (temporalMotionDepth_)
        resolveTemporal(gfx, dest->getTexture(), temporalMotionDepth_);
    else
        historyValid_ = false;
    gfx->setCanvas(prev);
}

Canvas *ScreenSpaceReflection::getReflectionCanvas() {
    if (gfx_) {
        const int fullW =
            std::max(1, gfx_->getPixelWidth() > 0 ? gfx_->getPixelWidth() : gfx_->getWidth());
        const int fullH =
            std::max(1, gfx_->getPixelHeight() > 0 ? gfx_->getPixelHeight() : gfx_->getHeight());
        const int targetW = std::max(1, int(std::lround(float(fullW) * resolutionScale_)));
        const int targetH = std::max(1, int(std::lround(float(fullH) * resolutionScale_)));
        if (!reflection_ || reflection_->getWidth() != targetW ||
            reflection_->getHeight() != targetH)
            reflection_ = gfx_->newHDRCanvas(targetW, targetH);
    }
    return reflection_;
}

Texture *ScreenSpaceReflection::getReflectionTexture() {
    getReflectionCanvas();
    return historyValid_ && historyRead_ ? historyRead_->getTexture()
                                         : (reflection_ ? reflection_->getTexture() : nullptr);
}

void ScreenSpaceReflection::applyFromScene(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                                           Texture *worldNormal, Texture *albedo) {
    if (!gfx) throw eve::Exception("ScreenSpaceReflection: null graphics");
    if (!sceneColor) throw eve::Exception("ScreenSpaceReflection: null scene color");
    if (!hwDepth) throw eve::Exception("ScreenSpaceReflection: null depth");
    uploadUniforms(sceneColor->getWidth(), sceneColor->getHeight());
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    Texture *traceDepth = depthPyramid_ && depthPyramidLevels_ > 0 ? depthPyramid_ : hwDepth;
    gfx->drawTexturedRectShader4(sceneColor, traceDepth,
                                 worldNormal ? worldNormal : sceneColor,
                                 albedo ? albedo : sceneColor, ssr_, 0.f, 0.f, dw, dh,
                                 Color(1.f, 1.f, 1.f, 1.f));
}

}  // namespace eve::graphics
