#include "graphics/Outline.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/outline_post_frag_spv.inc"
#include "graphics/shaders/PostProcessWgsl.h"

#include <algorithm>

namespace eve::graphics {
namespace {

Shader *createOutlineShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Outline: null graphics");
    std::vector<uint32_t> frag(outline_post_frag_spv, outline_post_frag_spv + outline_post_frag_spv_count);
    Shader *sh = gfx->getBackendName() == "webgpu"
                     ? gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) +
                                                     shaders::kOutline)
                     : gfx->newShaderFromSpv({}, frag);
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("Outline: failed to create outline shader");
    sh->declareFloat("width");
    sh->declareFloat("depthThreshold");
    sh->declareFloat("depthSensitivity");
    sh->declareFloat("normalThreshold");
    sh->declareFloat("softness");
    sh->declareFloat("colorR");
    sh->declareFloat("colorG");
    sh->declareFloat("colorB");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->sendFloat("width", 1.f);
    sh->sendFloat("depthThreshold", 0.3f);
    sh->sendFloat("depthSensitivity", 0.f);
    sh->sendFloat("normalThreshold", 0.35f);
    sh->sendFloat("softness", 0.15f);
    sh->sendFloat("colorR", 0.05f);
    sh->sendFloat("colorG", 0.04f);
    sh->sendFloat("colorB", 0.07f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    return sh;
}

}  // namespace

Outline::Outline(Graphics *gfx) : gfx_(gfx) {
    shader_ = createOutlineShader(gfx);
}

Outline::~Outline() = default;

void Outline::setColor(float r, float g, float b) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    if (shader_) {
        shader_->sendFloat("colorR", r);
        shader_->sendFloat("colorG", g);
        shader_->sendFloat("colorB", b);
    }
}

float Outline::getColorR() const { return colorR_; }
float Outline::getColorG() const { return colorG_; }
float Outline::getColorB() const { return colorB_; }

void Outline::setWidth(float width) {
    width_ = std::max(width, 0.5f);
    if (shader_) shader_->sendFloat("width", width_);
}

float Outline::getWidth() const { return width_; }

void Outline::setDepthThreshold(float threshold) {
    depthThreshold_ = std::max(threshold, 0.f);
    if (shader_) shader_->sendFloat("depthThreshold", depthThreshold_);
}

float Outline::getDepthThreshold() const { return depthThreshold_; }

void Outline::setDepthSensitivity(float sensitivity) {
    depthSensitivity_ = std::max(sensitivity, 0.f);
    if (shader_) shader_->sendFloat("depthSensitivity", depthSensitivity_);
}

float Outline::getDepthSensitivity() const { return depthSensitivity_; }

void Outline::setNormalThreshold(float threshold) {
    normalThreshold_ = std::clamp(threshold, 0.f, 2.f);
    if (shader_) shader_->sendFloat("normalThreshold", normalThreshold_);
}

float Outline::getNormalThreshold() const { return normalThreshold_; }

void Outline::setSoftness(float softness) {
    softness_ = std::clamp(softness, 0.f, 1.f);
    if (shader_) shader_->sendFloat("softness", softness_);
}

float Outline::getSoftness() const { return softness_; }

void Outline::setClip(float nearZ, float farZ) {
    nearZ_ = nearZ > 1e-4f ? nearZ : 0.1f;
    farZ_ = farZ > nearZ_ ? farZ : nearZ_ + 1.f;
    if (shader_) {
        shader_->sendFloat("nearZ", nearZ_);
        shader_->sendFloat("farZ", farZ_);
    }
}

bool Outline::hasParam(const std::string &name) const {
    return shader_ && shader_->hasUniform(name);
}

void Outline::setFloat(const std::string &name, float value) {
    if (!shader_) throw eve::Exception("Outline.setFloat: null shader");
    shader_->sendFloat(name, value);
}

float Outline::getFloat(const std::string &name) const {
    if (!shader_) throw eve::Exception("Outline.getFloat: null shader");
    float v = 0.f;
    if (shader_->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("Outline.getFloat: missing param '%s'", name.c_str());
    return v;
}

void Outline::uploadCommon(Graphics *gfx, Texture *hwDepth) {
    const int w = hwDepth->getWidth();
    const int h = hwDepth->getHeight();
    if (shader_) {
        shader_->sendFloat("texelW", 1.f / float(std::max(w, 1)));
        shader_->sendFloat("texelH", 1.f / float(std::max(h, 1)));
        shader_->sendFloat("nearZ", nearZ_);
        shader_->sendFloat("farZ", farZ_);
    }
}

bool Outline::apply(Graphics *gfx, Texture *hwDepth, Texture *worldNormal) {
    if (!gfx) throw eve::Exception("Outline.apply: null graphics");
    if (!shader_) throw eve::Exception("Outline.apply: null shader");
    if (!hwDepth) return false;
    if (!worldNormal) return false;

    uploadCommon(gfx, hwDepth);

    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShaderDepth(hwDepth, worldNormal, shader_, 0.f, 0.f, dw, dh,
                                     Color(1.f, 1.f, 1.f, 1.f));
    return true;
}

bool Outline::applyTo(Graphics *gfx, Texture *hwDepth, Texture *worldNormal, Canvas *dest) {
    if (!gfx) throw eve::Exception("Outline.applyTo: null graphics");
    if (!dest) throw eve::Exception("Outline.applyTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    const bool ok = apply(gfx, hwDepth, worldNormal);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
    return ok;
}

}  // namespace eve::graphics
