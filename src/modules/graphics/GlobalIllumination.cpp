#include "graphics/GlobalIllumination.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/gi_ssgi_frag_spv.inc"

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
    Shader *sh = gfx->newShaderFromSpv({}, frag);
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
    return sh;
}

}  // namespace

GlobalIllumination::GlobalIllumination(Graphics *gfx) : gfx_(gfx) {
    ssgi_ = createSsgiShader(gfx);
    applyQualityDefaults();
}

GlobalIllumination::~GlobalIllumination() = default;

void GlobalIllumination::applyQualityDefaults() {
    if (quality_ == "low") {
        radius_ = 0.9f;
        setFloat("sampleCount", 8.f);
    } else if (quality_ == "high") {
        radius_ = 1.6f;
        setFloat("sampleCount", 24.f);
    } else {
        radius_ = 1.25f;
        setFloat("sampleCount", 16.f);
    }
    setFloat("radius", radius_);
    setFloat("intensity", intensity_);
}

void GlobalIllumination::setQuality(const std::string &quality) {
    if (quality == "low" || quality == "medium" || quality == "high")
        quality_ = quality;
    else
        quality_ = "medium";
    applyQualityDefaults();
}

void GlobalIllumination::setInvViewProj(const glm::mat4 &invViewProj) {
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
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(eyeX, eyeY, eyeZ), glm::vec3(targetX, targetY, targetZ),
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

void GlobalIllumination::setLightDirection(float dx, float dy, float dz) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    else d = glm::normalize(d);
    lightDir_ = d;
    ssgi_->sendFloat("lightDirX", lightDir_.x);
    ssgi_->sendFloat("lightDirY", lightDir_.y);
    ssgi_->sendFloat("lightDirZ", lightDir_.z);
}

void GlobalIllumination::setLightColor(float r, float g, float b) {
    lightColor_ = glm::vec3(std::max(r, 0.f), std::max(g, 0.f), std::max(b, 0.f));
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
    ssgi_->sendFloat(name, value);
    if (name == "radius") radius_ = value;
    if (name == "intensity") intensity_ = value;
}

float GlobalIllumination::getFloat(const std::string &name) const {
    if (!ssgi_ || !ssgi_->hasUniform(name))
        throw eve::Exception("GlobalIllumination.getFloat: missing param '%s'", name.c_str());
    float v = 0.f;
    if (ssgi_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    throw eve::Exception("GlobalIllumination.getFloat: missing param '%s'", name.c_str());
}

int GlobalIllumination::getSampleCount() const {
    return int(std::lround(getFloat("sampleCount")));
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
        gfx->drawTexturedRectShaderDepth(source, hwDepth, shader, 0.f, 0.f, dw, dh,
                                         Color(1.f, 1.f, 1.f, 1.f));
    else
        gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, Color(1.f, 1.f, 1.f, 1.f));
}

void GlobalIllumination::applyFromDepth(Graphics *gfx, Texture *packedAlbedo) {
    if (!packedAlbedo) throw eve::Exception("GlobalIllumination.applyFromDepth: null packedAlbedo");
    ssgi_->sendFloat("useNdcDepth", 0.f);
    uploadUniforms(packedAlbedo->getWidth(), packedAlbedo->getHeight());
    drawFullscreen(gfx, packedAlbedo, ssgi_, nullptr);
}

void GlobalIllumination::applyFromScene(Graphics *gfx, Texture *color, Texture *hwDepth) {
    if (!color) throw eve::Exception("GlobalIllumination.applyFromScene: null color");
    if (!hwDepth) throw eve::Exception("GlobalIllumination.applyFromScene: null hwDepth");
    ssgi_->sendFloat("useNdcDepth", 1.f);
    uploadUniforms(color->getWidth(), color->getHeight());
    drawFullscreen(gfx, color, ssgi_, hwDepth);
}

void GlobalIllumination::applyFromDepthTo(Graphics *gfx, Texture *packedAlbedo, Canvas *dest) {
    if (!gfx) throw eve::Exception("GlobalIllumination.applyFromDepthTo: null graphics");
    if (!dest) throw eve::Exception("GlobalIllumination.applyFromDepthTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromDepth(gfx, packedAlbedo);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

}  // namespace eve::graphics
