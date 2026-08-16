#include "graphics/ScreenSpaceReflection.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/ssr_frag_spv.inc"

#include <algorithm>
#include <cmath>

namespace eve::graphics {

namespace {
Shader *createSsrShader(Graphics *gfx) {
    std::vector<uint32_t> frag(ssr_frag_spv, ssr_frag_spv + ssr_frag_spv_count);
    Shader *sh = gfx->newShaderFromSpv({}, frag);
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
    return sh;
}
}  // namespace

ScreenSpaceReflection::ScreenSpaceReflection(Graphics *gfx) : gfx_(gfx) {
    ssr_ = createSsrShader(gfx);
}

ScreenSpaceReflection::~ScreenSpaceReflection() {
    // Shader is owned by Graphics.
    gfx_ = nullptr;
    ssr_ = nullptr;
}

void ScreenSpaceReflection::setInvViewProj(const glm::mat4 &invViewProj) {
    invViewProj_ = invViewProj;
    ssr_->sendMatrix("invViewProj", invViewProj_);
}

void ScreenSpaceReflection::setCamera(float eyeX, float eyeY, float eyeZ, float targetX,
                                       float targetY, float targetZ, float upX, float upY,
                                       float upZ, float fovYDeg, float aspect, float nearZ,
                                       float farZ) {
    nearZ_ = nearZ > 1e-4f ? nearZ : 0.1f;
    farZ_ = farZ > nearZ_ ? farZ : nearZ_ + 1.f;
    const float aspectSafe = aspect > 1e-4f ? aspect : 1.f;
    const float fovRad = fovYDeg * 0.017453292519943295f;
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(eyeX, eyeY, eyeZ), glm::vec3(targetX, targetY, targetZ),
                      glm::vec3(upX, upY, upZ));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(fovRad, aspectSafe, nearZ_, farZ_);
    setInvViewProj(glm::inverse(proj * view));
    ssr_->sendFloat("nearZ", nearZ_);
    ssr_->sendFloat("farZ", farZ_);
}

void ScreenSpaceReflection::setEnabled(bool enabled) {
    enabled_ = enabled;
    ssr_->sendFloat("enabled", enabled_ ? 1.f : 0.f);
}

void ScreenSpaceReflection::setMaxDistance(float v) { setFloat("maxDist", std::max(v, 0.1f)); }
void ScreenSpaceReflection::setStepLength(float v) { setFloat("stepLen", std::max(v, 0.01f)); }
void ScreenSpaceReflection::setMaxSteps(int v) { setFloat("maxSteps", float(std::clamp(v, 1, 512))); }
void ScreenSpaceReflection::setThickness(float v) { setFloat("thickness", std::max(v, 0.0f)); }
void ScreenSpaceReflection::setStrength(float v) { setFloat("strength", std::max(v, 0.f)); }

bool ScreenSpaceReflection::hasParam(const std::string &name) const {
    return ssr_ && ssr_->hasUniform(name);
}

void ScreenSpaceReflection::setFloat(const std::string &name, float value) {
    if (!ssr_ || !ssr_->hasUniform(name))
        throw eve::Exception("ScreenSpaceReflection.setFloat: missing param '%s'", name.c_str());
    ssr_->sendFloat(name, value);
    if (name == "maxDist") maxDist_ = value;
    if (name == "stepLen") stepLen_ = value;
    if (name == "maxSteps") maxSteps_ = value;
    if (name == "thickness") thickness_ = value;
    if (name == "strength") strength_ = value;
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
}

void ScreenSpaceReflection::applyFromSceneTo(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                                             Texture *worldNormal, Canvas *dest) {
    if (!gfx) throw eve::Exception("ScreenSpaceReflection: null graphics");
    if (!dest) throw eve::Exception("ScreenSpaceReflection.applyFromSceneTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromScene(gfx, sceneColor, hwDepth, worldNormal);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

Canvas *ScreenSpaceReflection::getReflectionCanvas() {
    if (!reflection_ && gfx_) {
        reflection_ = gfx_->newCanvas(std::max(1, gfx_->getWidth()), std::max(1, gfx_->getHeight()));
    }
    return reflection_;
}

Texture *ScreenSpaceReflection::getReflectionTexture() {
    Canvas *c = getReflectionCanvas();
    return c ? c->getTexture() : nullptr;
}

void ScreenSpaceReflection::applyFromScene(Graphics *gfx, Texture *sceneColor, Texture *hwDepth,
                                           Texture *worldNormal) {
    if (!gfx) throw eve::Exception("ScreenSpaceReflection: null graphics");
    if (!sceneColor) throw eve::Exception("ScreenSpaceReflection: null scene color");
    if (!hwDepth) throw eve::Exception("ScreenSpaceReflection: null depth");
    (void)worldNormal;  // normal is reconstructed from depth in the shader
    uploadUniforms(sceneColor->getWidth(), sceneColor->getHeight());
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShaderDepth(sceneColor, hwDepth, ssr_, 0.f, 0.f, dw, dh,
                                     Color(1.f, 1.f, 1.f, 1.f));
}

}  // namespace eve::graphics
