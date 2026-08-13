#include "graphics/AmbientOcclusion.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/ao_ssao_frag_spv.inc"
#include "graphics/shaders/ao_hbao_frag_spv.inc"
#include "graphics/shaders/ao_gtao_frag_spv.inc"
#include "graphics/shaders/ao_blur_frag_spv.inc"
#include "graphics/shaders/ao_overlay_frag_spv.inc"
#include "graphics/shaders/ao_from_depth_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {
namespace {

void declareComputeCommon(Shader *sh, bool horizonStyle) {
    sh->declareMatrix("invViewProj");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("radius");
    sh->declareFloat("bias");
    sh->declareFloat("intensity");
    sh->declareFloat("power");
    if (horizonStyle) {
        sh->declareFloat("dirCount");
        sh->declareFloat("stepCount");
    } else {
        sh->declareFloat("sampleCount");
    }
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
}

Shader *createSsaoShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_ssao_frag_spv, ao_ssao_frag_spv + ao_ssao_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    declareComputeCommon(sh, false);
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("radius", 0.75f);
    sh->sendFloat("bias", 0.025f);
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("power", 1.5f);
    sh->sendFloat("sampleCount", 16.f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    return sh;
}

Shader *createHbaoShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_hbao_frag_spv, ao_hbao_frag_spv + ao_hbao_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    declareComputeCommon(sh, true);
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("radius", 0.75f);
    sh->sendFloat("bias", 0.025f);
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("power", 1.5f);
    sh->sendFloat("dirCount", 6.f);
    sh->sendFloat("stepCount", 6.f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    return sh;
}

Shader *createGtaoShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_gtao_frag_spv, ao_gtao_frag_spv + ao_gtao_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    declareComputeCommon(sh, true);
    sh->declareFloat("thickness");
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("radius", 0.75f);
    sh->sendFloat("bias", 0.025f);
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("power", 1.5f);
    sh->sendFloat("dirCount", 6.f);
    sh->sendFloat("stepCount", 6.f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("thickness", 0.5f);
    return sh;
}

Shader *createBlurShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_blur_frag_spv, ao_blur_frag_spv + ao_blur_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("depthSigma");
    sh->declareFloat("spatialKernel");
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("depthSigma", 0.05f);
    sh->sendFloat("spatialKernel", 2.f);
    return sh;
}

Shader *createOverlayShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_overlay_frag_spv, ao_overlay_frag_spv + ao_overlay_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = gfx->newShaderFromSpv(vert, frag);
    sh->declareFloat("intensity");
    sh->declareFloat("power");
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("power", 1.f);
    return sh;
}

Shader *createFromDepthShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    std::vector<uint32_t> frag(ao_from_depth_frag_spv,
                               ao_from_depth_frag_spv + ao_from_depth_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("AmbientOcclusion: failed to create from-depth shader");
    declareComputeCommon(sh, false);
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("radius", 0.75f);
    sh->sendFloat("bias", 0.025f);
    sh->sendFloat("intensity", 0.55f);
    sh->sendFloat("power", 1.35f);
    sh->sendFloat("sampleCount", 16.f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->declareFloat("hasNormal");
    sh->sendFloat("hasNormal", 0.f);
    return sh;
}

}  // namespace

AmbientOcclusion::AmbientOcclusion(Graphics *gfx) : gfx_(gfx) {
    ssaoShader_ = createSsaoShader(gfx);
    hbaoShader_ = createHbaoShader(gfx);
    gtaoShader_ = createGtaoShader(gfx);
    blurShader_ = createBlurShader(gfx);
    overlayShader_ = createOverlayShader(gfx);
    fromDepthShader_ = createFromDepthShader(gfx);
    applyQualityDefaults();
}

AmbientOcclusion::~AmbientOcclusion() = default;

Shader *AmbientOcclusion::activeComputeShader() const {
    if (mode_ == "hbao") return hbaoShader_;
    if (mode_ == "gtao") return gtaoShader_;
    return ssaoShader_;
}

Shader *AmbientOcclusion::getShader() const { return activeComputeShader(); }

void AmbientOcclusion::applyQualityDefaults() {
    if (quality_ == "low") {
        downscale_ = 4.f;
        radius_ = 0.55f;
        if (mode_ == "hbao" || mode_ == "gtao") {
            setFloat("dirCount", 4.f);
            setFloat("stepCount", 4.f);
        } else {
            setFloat("sampleCount", 8.f);
        }
    } else if (quality_ == "high") {
        downscale_ = 1.f;
        radius_ = 1.f;
        if (mode_ == "hbao" || mode_ == "gtao") {
            setFloat("dirCount", 8.f);
            setFloat("stepCount", 8.f);
        } else {
            setFloat("sampleCount", 24.f);
        }
    } else {
        quality_ = "medium";
        downscale_ = 2.f;
        radius_ = 0.75f;
        if (mode_ == "hbao" || mode_ == "gtao") {
            setFloat("dirCount", 6.f);
            setFloat("stepCount", 6.f);
        } else {
            setFloat("sampleCount", 16.f);
        }
    }
    setRadius(radius_);
    setBias(bias_);
    setIntensity(intensity_);
    setPower(power_);
    setThickness(thickness_);
}

void AmbientOcclusion::setQuality(const std::string &quality) {
    quality_ = quality;
    applyQualityDefaults();
}

void AmbientOcclusion::setMode(const std::string &mode) {
    if (mode == "hbao")
        mode_ = "hbao";
    else if (mode == "gtao")
        mode_ = "gtao";
    else
        mode_ = "ssao";
    applyQualityDefaults();
}

void AmbientOcclusion::setInvViewProj(const glm::mat4 &invViewProj) {
    invViewProj_ = invViewProj;
    ssaoShader_->sendMatrix("invViewProj", invViewProj_);
    hbaoShader_->sendMatrix("invViewProj", invViewProj_);
    gtaoShader_->sendMatrix("invViewProj", invViewProj_);
    if (fromDepthShader_) fromDepthShader_->sendMatrix("invViewProj", invViewProj_);
}

void AmbientOcclusion::setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY,
                                 float targetZ, float upX, float upY, float upZ, float fovYDeg,
                                 float aspect, float nearZ, float farZ) {
    nearZ_ = nearZ > 1e-4f ? nearZ : 0.1f;
    farZ_ = farZ > nearZ_ ? farZ : nearZ_ + 1.f;
    const float aspectSafe = aspect > 1e-4f ? aspect : 1.f;
    const float fovRad = fovYDeg * 0.017453292519943295f;
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(eyeX, eyeY, eyeZ), glm::vec3(targetX, targetY, targetZ),
                      glm::vec3(upX, upY, upZ));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(fovRad, aspectSafe, nearZ_, farZ_);
    setInvViewProj(glm::inverse(proj * view));
    ssaoShader_->sendFloat("nearZ", nearZ_);
    ssaoShader_->sendFloat("farZ", farZ_);
    hbaoShader_->sendFloat("nearZ", nearZ_);
    hbaoShader_->sendFloat("farZ", farZ_);
    gtaoShader_->sendFloat("nearZ", nearZ_);
    gtaoShader_->sendFloat("farZ", farZ_);
    if (fromDepthShader_) {
        fromDepthShader_->sendFloat("nearZ", nearZ_);
        fromDepthShader_->sendFloat("farZ", farZ_);
    }
}

void AmbientOcclusion::setRadius(float radius) {
    radius_ = radius > 1e-4f ? radius : 1e-4f;
    setFloat("radius", radius_);
}

void AmbientOcclusion::setBias(float bias) {
    bias_ = bias < 0.f ? 0.f : bias;
    setFloat("bias", bias_);
}

void AmbientOcclusion::setIntensity(float intensity) {
    intensity_ = intensity < 0.f ? 0.f : intensity;
    setFloat("intensity", intensity_);
    if (overlayShader_) overlayShader_->sendFloat("intensity", intensity_);
}

void AmbientOcclusion::setPower(float power) {
    power_ = power < 0.01f ? 0.01f : power;
    setFloat("power", power_);
}

void AmbientOcclusion::setThickness(float thickness) {
    thickness_ = thickness < 0.f ? 0.f : thickness;
    if (gtaoShader_ && gtaoShader_->hasUniform("thickness"))
        gtaoShader_->sendFloat("thickness", thickness_);
}

float AmbientOcclusion::getRadius() const { return radius_; }
float AmbientOcclusion::getBias() const { return bias_; }
float AmbientOcclusion::getIntensity() const { return intensity_; }
float AmbientOcclusion::getPower() const { return power_; }

bool AmbientOcclusion::hasParam(const std::string &name) const {
    return (ssaoShader_ && ssaoShader_->hasUniform(name)) ||
           (hbaoShader_ && hbaoShader_->hasUniform(name)) ||
           (gtaoShader_ && gtaoShader_->hasUniform(name)) ||
           (blurShader_ && blurShader_->hasUniform(name)) ||
           (overlayShader_ && overlayShader_->hasUniform(name));
}

void AmbientOcclusion::setFloat(const std::string &name, float value) {
    if (ssaoShader_ && ssaoShader_->hasUniform(name)) ssaoShader_->sendFloat(name, value);
    if (hbaoShader_ && hbaoShader_->hasUniform(name)) hbaoShader_->sendFloat(name, value);
    if (gtaoShader_ && gtaoShader_->hasUniform(name)) gtaoShader_->sendFloat(name, value);
    if (blurShader_ && blurShader_->hasUniform(name)) blurShader_->sendFloat(name, value);
    if (overlayShader_ && overlayShader_->hasUniform(name)) overlayShader_->sendFloat(name, value);
    if (fromDepthShader_ && fromDepthShader_->hasUniform(name)) fromDepthShader_->sendFloat(name, value);
}

float AmbientOcclusion::getFloat(const std::string &name) const {
    Shader *active = activeComputeShader();
    if (active && active->hasUniform(name)) {
        float v = 0.f;
        if (active->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (blurShader_ && blurShader_->hasUniform(name)) {
        float v = 0.f;
        if (blurShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (overlayShader_ && overlayShader_->hasUniform(name)) {
        float v = 0.f;
        if (overlayShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    // Fall back across compute shaders.
    for (Shader *sh : {ssaoShader_, hbaoShader_, gtaoShader_}) {
        if (sh && sh->hasUniform(name)) {
            float v = 0.f;
            if (sh->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
        }
    }
    throw eve::Exception("AmbientOcclusion.getFloat: missing param '%s'", name.c_str());
}

int AmbientOcclusion::getSampleCount() const {
    if (mode_ == "hbao" || mode_ == "gtao") {
        const int dirs = int(std::lround(getFloat("dirCount")));
        const int steps = int(std::lround(getFloat("stepCount")));
        return dirs * steps;
    }
    return int(std::lround(getFloat("sampleCount")));
}

int AmbientOcclusion::resolutionFor(int fullSize) const {
    if (fullSize < 1) return 1;
    const float ds = std::max(downscale_, 1.f);
    return std::max(1, int(std::floor(float(fullSize) / ds)));
}

void AmbientOcclusion::uploadComputeCommon(Shader *shader, int width, int height) {
    if (!shader) throw eve::Exception("AmbientOcclusion: null compute shader");
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    shader->sendMatrix("invViewProj", invViewProj_);
    shader->sendFloat("nearZ", nearZ_);
    shader->sendFloat("farZ", farZ_);
    shader->sendFloat("radius", radius_);
    shader->sendFloat("bias", bias_);
    shader->sendFloat("intensity", intensity_);
    shader->sendFloat("power", power_);
    shader->sendFloat("texelW", 1.f / float(width));
    shader->sendFloat("texelH", 1.f / float(height));
    if (shader->hasUniform("thickness")) shader->sendFloat("thickness", thickness_);
}

void AmbientOcclusion::drawFullscreen(Graphics *gfx, Texture *source, Shader *shader) {
    if (!gfx) throw eve::Exception("AmbientOcclusion: null graphics");
    if (!source) throw eve::Exception("AmbientOcclusion: null source texture");
    if (!shader) throw eve::Exception("AmbientOcclusion: null shader");
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, Color(1.f, 1.f, 1.f, 1.f));
}

void AmbientOcclusion::compute(Graphics *gfx, Texture *linearDepth) {
    if (!linearDepth) throw eve::Exception("AmbientOcclusion.compute: null depth");
    Shader *sh = activeComputeShader();
    uploadComputeCommon(sh, linearDepth->getWidth(), linearDepth->getHeight());
    drawFullscreen(gfx, linearDepth, sh);
}

void AmbientOcclusion::computeTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.computeTo: null graphics");
    if (!dest) throw eve::Exception("AmbientOcclusion.computeTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    compute(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void AmbientOcclusion::blur(Graphics *gfx, Texture *aoMap) {
    if (!aoMap) throw eve::Exception("AmbientOcclusion.blur: null aoMap");
    blurShader_->sendFloat("texelW", 1.f / float(std::max(aoMap->getWidth(), 1)));
    blurShader_->sendFloat("texelH", 1.f / float(std::max(aoMap->getHeight(), 1)));
    drawFullscreen(gfx, aoMap, blurShader_);
}

void AmbientOcclusion::blurTo(Graphics *gfx, Texture *aoMap, Canvas *dest) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.blurTo: null graphics");
    if (!dest) throw eve::Exception("AmbientOcclusion.blurTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    blur(gfx, aoMap);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void AmbientOcclusion::applyOverlay(Graphics *gfx, Texture *aoMap) {
    if (!aoMap) throw eve::Exception("AmbientOcclusion.applyOverlay: null aoMap");
    overlayShader_->sendFloat("intensity", intensity_);
    // Overlay uses power=1 by default so compute-pass power is not double-applied.
    if (!overlayShader_->hasUniform("power")) {
        // unreachable — declared at create
    }
    overlayShader_->sendFloat("power", 1.f);
    drawFullscreen(gfx, aoMap, overlayShader_);
}

void AmbientOcclusion::applyOverlayTo(Graphics *gfx, Texture *aoMap, Canvas *dest) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.applyOverlayTo: null graphics");
    if (!dest) throw eve::Exception("AmbientOcclusion.applyOverlayTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyOverlay(gfx, aoMap);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void AmbientOcclusion::applyFromDepth(Graphics *gfx, Texture *hwDepth) {
    applyFromGBuffer(gfx, hwDepth, nullptr);
}

void AmbientOcclusion::applyFromGBuffer(Graphics *gfx, Texture *hwDepth, Texture *worldNormal) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.applyFromGBuffer: null graphics");
    if (!hwDepth) throw eve::Exception("AmbientOcclusion.applyFromGBuffer: null hwDepth");
    if (!fromDepthShader_) throw eve::Exception("AmbientOcclusion.applyFromGBuffer: missing shader");
    fromDepthShader_->sendFloat("hasNormal", worldNormal ? 1.f : 0.f);
    uploadComputeCommon(fromDepthShader_, hwDepth->getWidth(), hwDepth->getHeight());
    if (worldNormal) {
        const float dw =
            gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
        const float dh =
            gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
        gfx->drawTexturedRectShaderDepth(hwDepth, worldNormal, fromDepthShader_, 0.f, 0.f, dw, dh,
                                         Color(1.f, 1.f, 1.f, 1.f));
    } else {
        drawFullscreen(gfx, hwDepth, fromDepthShader_);
    }
}

void AmbientOcclusion::applyFromDepthTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.applyFromDepthTo: null graphics");
    if (!dest) throw eve::Exception("AmbientOcclusion.applyFromDepthTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromDepth(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

Texture *AmbientOcclusion::newLinearDepthTexture(Graphics *gfx, int width, int height,
                                                 float (*depth01)(int x, int y, void *userdata),
                                                 void *userdata) {
    if (!gfx) throw eve::Exception("AmbientOcclusion.newLinearDepthTexture: null graphics");
    if (width < 1 || height < 1)
        throw eve::Exception("AmbientOcclusion.newLinearDepthTexture: invalid size");
    if (!depth01) throw eve::Exception("AmbientOcclusion.newLinearDepthTexture: null depth fn");
    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float d = depth01(x, y, userdata);
            if (d < 0.f) d = 0.f;
            if (d > 1.f) d = 1.f;
            const uint8_t b = uint8_t(std::lround(d * 255.f));
            const size_t i = size_t(y * width + x) * 4;
            rgba[i + 0] = b;
            rgba[i + 1] = b;
            rgba[i + 2] = b;
            rgba[i + 3] = 255;
        }
    }
    return gfx->newTexture(width, height, rgba.data());
}

}  // namespace eve::graphics
