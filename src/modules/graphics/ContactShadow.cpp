#include "graphics/ContactShadow.h"

#include "common/Exception.h"
#include "graphics/BlendMode.h"
#include "graphics/ClipSpace.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/ContactShadowWgsl.h"
#include "graphics/shaders/contact_shadow_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace eve::graphics {
namespace {

Shader* createContactShadowShader(Graphics* gfx) {
    if (!gfx) throw eve::Exception("ContactShadow: null graphics");
    Shader* sh = nullptr;
    if (gfx->getBackendName() == "webgpu") {
        sh = gfx->newShaderFromWgsl(
            {}, std::string(shaders::kContactShadowCommon) + shaders::kContactShadow);
    } else {
        std::vector<uint32_t> frag(contact_shadow_frag_spv,
                                   contact_shadow_frag_spv + contact_shadow_frag_spv_count);
        sh = gfx->newShaderFromSpv({}, frag);
    }
    if (!sh || !sh->gpuHandle) throw eve::Exception("ContactShadow: failed to create shader");
    sh->declareMatrix("invViewProj");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("length");
    sh->declareFloat("thickness");
    sh->declareFloat("intensity");
    sh->declareFloat("stepCount");
    sh->declareFloat("lightDirX");
    sh->declareFloat("lightDirY");
    sh->declareFloat("lightDirZ");
    sh->declareFloat("texelW");
    sh->declareFloat("texelH");
    sh->declareFloat("hasNormal");
    sh->declareFloat("startBias");
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("length", 0.35f);
    sh->sendFloat("thickness", 0.12f);
    sh->sendFloat("intensity", 0.65f);
    sh->sendFloat("stepCount", 12.f);
    sh->sendFloat("lightDirX", 0.48f);
    sh->sendFloat("lightDirY", 0.86f);
    sh->sendFloat("lightDirZ", 0.28f);
    sh->sendFloat("texelW", 1.f / 256.f);
    sh->sendFloat("texelH", 1.f / 256.f);
    sh->sendFloat("hasNormal", 0.f);
    sh->sendFloat("startBias", 0.01f);
    return sh;
}

}  // namespace

ContactShadow::ContactShadow(Graphics* gfx) : gfx_(gfx) {
    shader_ = createContactShadowShader(gfx);
    applyQualityDefaults();
}

ContactShadow::~ContactShadow() = default;

void ContactShadow::applyQualityDefaults() {
    if (quality_ == "low") {
        stepCount_ = 8;
        length_ = 0.22f;
        thickness_ = 0.10f;
        intensity_ = 0.55f;
    } else if (quality_ == "high") {
        stepCount_ = 20;
        length_ = 0.48f;
        thickness_ = 0.14f;
        intensity_ = 0.75f;
    } else {
        quality_ = "medium";
        stepCount_ = 12;
        length_ = 0.35f;
        thickness_ = 0.12f;
        intensity_ = 0.65f;
    }
}

void ContactShadow::setQuality(const std::string& quality) {
    quality_ = quality;
    applyQualityDefaults();
}

void ContactShadow::setInvViewProj(const glm::mat4& invViewProj) {
    invViewProj_ = invViewProj;
    if (shader_) shader_->sendMatrix("invViewProj", invViewProj_);
}

void ContactShadow::setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY,
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
    if (shader_) {
        shader_->sendFloat("nearZ", nearZ_);
        shader_->sendFloat("farZ", farZ_);
    }
}

void ContactShadow::setLightDirection(float x, float y, float z) {
    glm::vec3 d(x, y, z);
    const float len = glm::length(d);
    lightDir_ = len > 1e-6f ? d / len : glm::vec3(0.f, 1.f, 0.f);
}

void ContactShadow::setLength(float length) { length_ = length > 1e-4f ? length : 1e-4f; }

void ContactShadow::setThickness(float thickness) {
    thickness_ = thickness > 1e-4f ? thickness : 1e-4f;
}

void ContactShadow::setIntensity(float intensity) { intensity_ = intensity < 0.f ? 0.f : intensity; }

void ContactShadow::setStartBias(float bias) { startBias_ = bias < 0.f ? 0.f : bias; }

void ContactShadow::uploadCommon(int width, int height) {
    if (!shader_) throw eve::Exception("ContactShadow: null shader");
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    shader_->sendMatrix("invViewProj", invViewProj_);
    shader_->sendFloat("nearZ", nearZ_);
    shader_->sendFloat("farZ", farZ_);
    shader_->sendFloat("length", length_);
    shader_->sendFloat("thickness", thickness_);
    shader_->sendFloat("intensity", intensity_);
    shader_->sendFloat("stepCount", float(stepCount_));
    shader_->sendFloat("lightDirX", lightDir_.x);
    shader_->sendFloat("lightDirY", lightDir_.y);
    shader_->sendFloat("lightDirZ", lightDir_.z);
    shader_->sendFloat("texelW", 1.f / float(width));
    shader_->sendFloat("texelH", 1.f / float(height));
    shader_->sendFloat("startBias", startBias_);
}

void ContactShadow::applyFromGBuffer(Graphics* gfx, Texture* hwDepth, Texture* worldNormal) {
    if (!gfx) throw eve::Exception("ContactShadow.applyFromGBuffer: null graphics");
    if (!hwDepth) throw eve::Exception("ContactShadow.applyFromGBuffer: null hwDepth");
    if (!shader_) throw eve::Exception("ContactShadow.applyFromGBuffer: missing shader");
    shader_->sendFloat("hasNormal", worldNormal ? 1.f : 0.f);
    uploadCommon(hwDepth->getWidth(), hwDepth->getHeight());
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    if (worldNormal) {
        gfx->drawTexturedRectShaderDepth(hwDepth, worldNormal, shader_, 0.f, 0.f, dw, dh,
                                         Color(1.f, 1.f, 1.f, 1.f));
    } else {
        gfx->drawTexturedRectShaderUV(hwDepth, shader_, 0.f, 0.f, dw, dh, 0.f, 0.f, 1.f, 1.f,
                                      Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Alpha);
    }
}

}  // namespace eve::graphics
