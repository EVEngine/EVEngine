#include "graphics/Volumetric.h"

#include "graphics/AtmosphereVolume.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Drawable.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/volumetric_post_frag_spv.inc"
#include "graphics/shaders/volumetric_raymarch_frag_spv.inc"
#include "graphics/shaders/volumetric_fog_frag_spv.inc"
#include "graphics/shaders/volumetric_froxel_frag_spv.inc"
#include "graphics/shaders/volumetric_cloud_frag_spv.inc"
#include "graphics/shaders/VolumetricWgsl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace eve::graphics {
namespace {

Shader *newVolShader(Graphics *gfx, const std::vector<uint32_t> &frag, const char *wgsl) {
    if (gfx->getBackendName() == "webgpu")
        return gfx->newShaderFromWgsl({}, std::string(shaders::kVolCommon) + wgsl);
    return gfx->newShaderFromSpv({}, frag);
}

Shader *createScreenspaceShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_post_frag_spv,
                               volumetric_post_frag_spv + volumetric_post_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = newVolShader(gfx, frag, shaders::kVolScreen);
    sh->declareFloat("lightX");
    sh->declareFloat("lightY");
    sh->declareFloat("exposure");
    sh->declareFloat("decay");
    sh->declareFloat("density");
    sh->declareFloat("weight");
    sh->declareFloat("sampleCount");
    sh->declareFloat("dustAmount");
    sh->declareFloat("fogAmount");
    sh->declareFloat("fogR");
    sh->declareFloat("fogG");
    sh->declareFloat("fogB");
    sh->declareFloat("shaftR");
    sh->declareFloat("shaftG");
    sh->declareFloat("shaftB");
    sh->declareFloat("time");
    sh->declareFloat("compositeMode");
    sh->declareFloat("intensity");

    sh->sendFloat("lightX", 0.5f);
    sh->sendFloat("lightY", 0.35f);
    sh->sendFloat("exposure", 0.35f);
    sh->sendFloat("decay", 0.96f);
    sh->sendFloat("density", 0.85f);
    sh->sendFloat("weight", 0.55f);
    sh->sendFloat("sampleCount", 48.f);
    sh->sendFloat("dustAmount", 0.25f);
    sh->sendFloat("fogAmount", 0.18f);
    sh->sendFloat("fogR", 0.55f);
    sh->sendFloat("fogG", 0.62f);
    sh->sendFloat("fogB", 0.75f);
    sh->sendFloat("shaftR", 1.f);
    sh->sendFloat("shaftG", 0.95f);
    sh->sendFloat("shaftB", 0.85f);
    sh->sendFloat("time", 0.f);
    sh->sendFloat("compositeMode", 0.f);
    sh->sendFloat("intensity", 1.f);
    return sh;
}

Shader *createRayMarchShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_raymarch_frag_spv,
                               volumetric_raymarch_frag_spv + volumetric_raymarch_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = newVolShader(gfx, frag, shaders::kVolRay);
    sh->declareMatrix("invViewProj");
    sh->declareFloat("lightDx");
    sh->declareFloat("lightDy");
    sh->declareFloat("lightDz");
    sh->declareFloat("density");
    sh->declareFloat("shaftR");
    sh->declareFloat("shaftG");
    sh->declareFloat("shaftB");
    sh->declareFloat("intensity");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("sampleCount");
    sh->declareFloat("dustAmount");
    sh->declareFloat("fogAmount");
    sh->declareFloat("shadowSteps");
    sh->declareFloat("lightU");
    sh->declareFloat("lightV");

    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("lightDx", 0.4f);
    sh->sendFloat("lightDy", 1.f);
    sh->sendFloat("lightDz", 0.3f);
    sh->sendFloat("density", 0.35f);
    sh->sendFloat("shaftR", 1.f);
    sh->sendFloat("shaftG", 0.95f);
    sh->sendFloat("shaftB", 0.85f);
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("sampleCount", 24.f);
    sh->sendFloat("dustAmount", 0.25f);
    sh->sendFloat("fogAmount", 0.2f);
    sh->sendFloat("shadowSteps", 8.f);
    sh->sendFloat("lightU", 0.7f);
    sh->sendFloat("lightV", 0.2f);
    return sh;
}

Shader *createFogShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_fog_frag_spv,
                               volumetric_fog_frag_spv + volumetric_fog_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = newVolShader(gfx, frag, shaders::kVolFog);
    sh->declareMatrix("invViewProj");
    sh->declareFloat("lightDx");
    sh->declareFloat("lightDy");
    sh->declareFloat("lightDz");
    sh->declareFloat("density");
    sh->declareFloat("fogR");
    sh->declareFloat("fogG");
    sh->declareFloat("fogB");
    sh->declareFloat("intensity");
    sh->declareFloat("nearZ");
    sh->declareFloat("farZ");
    sh->declareFloat("sampleCount");
    sh->declareFloat("fogHeight");
    sh->declareFloat("heightFalloff");
    sh->declareFloat("fogStart");
    sh->declareFloat("fogEnd");
    sh->declareFloat("noiseAmount");

    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("lightDx", 0.4f);
    sh->sendFloat("lightDy", 1.f);
    sh->sendFloat("lightDz", 0.3f);
    sh->sendFloat("density", 0.25f);
    sh->sendFloat("fogR", 0.55f);
    sh->sendFloat("fogG", 0.62f);
    sh->sendFloat("fogB", 0.75f);
    sh->sendFloat("intensity", 1.f);
    sh->sendFloat("nearZ", 0.1f);
    sh->sendFloat("farZ", 100.f);
    sh->sendFloat("sampleCount", 24.f);
    sh->sendFloat("fogHeight", 0.f);
    sh->sendFloat("heightFalloff", 0.15f);
    sh->sendFloat("fogStart", 2.f);
    sh->sendFloat("fogEnd", 40.f);
    sh->sendFloat("noiseAmount", 0.35f);
    return sh;
}

Shader *createFroxelShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_froxel_frag_spv,
                               volumetric_froxel_frag_spv + volumetric_froxel_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = newVolShader(gfx, frag, shaders::kVolFroxel);
    sh->declareFloat("atlasCols");
    sh->declareFloat("atlasRows");
    sh->declareFloat("sliceCount");
    sh->declareFloat("nearDistance");
    sh->declareFloat("farDistance");
    sh->sendFloat("atlasCols", 1.f);
    sh->sendFloat("atlasRows", 1.f);
    sh->sendFloat("sliceCount", 1.f);
    sh->sendFloat("nearDistance", 0.1f);
    sh->sendFloat("farDistance", 100.f);
    return sh;
}

Shader *createCloudShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    std::vector<uint32_t> frag(volumetric_cloud_frag_spv,
                               volumetric_cloud_frag_spv + volumetric_cloud_frag_spv_count);
    std::vector<uint32_t> vert;
    Shader *sh = newVolShader(gfx, frag, shaders::kVolCloud);
    sh->declareMatrix("invViewProj");
    sh->declareFloat("lightDx");
    sh->declareFloat("lightDy");
    sh->declareFloat("lightDz");
    sh->declareFloat("time");
    sh->declareFloat("cloudBottom");
    sh->declareFloat("cloudTop");
    sh->declareFloat("cloudCoverage");
    sh->declareFloat("cloudDensity");
    sh->declareFloat("cloudScale");
    sh->declareFloat("cloudWindX");
    sh->declareFloat("cloudWindZ");
    sh->declareFloat("sampleCount");
    sh->declareFloat("shadowSteps");
    sh->declareFloat("cloudLightR");
    sh->declareFloat("cloudLightG");
    sh->declareFloat("cloudLightB");
    sh->sendMatrix("invViewProj", glm::mat4(1.f));
    sh->sendFloat("lightDx", 0.4f);
    sh->sendFloat("lightDy", 1.f);
    sh->sendFloat("lightDz", 0.3f);
    sh->sendFloat("time", 0.f);
    sh->sendFloat("cloudBottom", 8.f);
    sh->sendFloat("cloudTop", 16.f);
    sh->sendFloat("cloudCoverage", 0.55f);
    sh->sendFloat("cloudDensity", 1.f);
    sh->sendFloat("cloudScale", 18.f);
    sh->sendFloat("cloudWindX", 1.5f);
    sh->sendFloat("cloudWindZ", 0.4f);
    sh->sendFloat("sampleCount", 32.f);
    sh->sendFloat("shadowSteps", 6.f);
    sh->sendFloat("cloudLightR", 1.f);
    sh->sendFloat("cloudLightG", 0.92f);
    sh->sendFloat("cloudLightB", 0.78f);
    return sh;
}

}  // namespace

Volumetric::Volumetric(Graphics *gfx) : gfx_(gfx) {
    shader_ = createScreenspaceShader(gfx);
    rayShader_ = createRayMarchShader(gfx);
    fogShader_ = createFogShader(gfx);
    cloudShader_ = createCloudShader(gfx);
    froxelShader_ = createFroxelShader(gfx);
    atmosphereVolume_ = std::make_unique<AtmosphereVolume>();
    applyQualityDefaults();
}

Volumetric::~Volumetric() = default;

void Volumetric::applyQualityDefaults() {
    if (quality_ == "low") {
        downscale_ = 4.f;
        if (mode_ == "raymarch") {
            rayShader_->sendFloat("sampleCount", 8.f);
            rayShader_->sendFloat("shadowSteps", 4.f);
            rayShader_->sendFloat("dustAmount", 0.12f);
            rayShader_->sendFloat("fogAmount", 0.12f);
        } else if (mode_ == "fog") {
            fogShader_->sendFloat("sampleCount", 8.f);
            fogShader_->sendFloat("noiseAmount", 0.15f);
            fogShader_->sendFloat("density", 0.18f);
        } else if (mode_ == "cloud") {
            cloudShader_->sendFloat("sampleCount", 16.f);
            cloudShader_->sendFloat("shadowSteps", 3.f);
        } else {
            setFloat("sampleCount", 16.f);
            setFloat("dustAmount", 0.12f);
            setFloat("fogAmount", 0.10f);
            setFloat("exposure", 0.40f);
        }
    } else if (quality_ == "high") {
        downscale_ = 1.f;
        if (mode_ == "raymarch") {
            rayShader_->sendFloat("sampleCount", 48.f);
            rayShader_->sendFloat("shadowSteps", 16.f);
            rayShader_->sendFloat("dustAmount", 0.35f);
            rayShader_->sendFloat("fogAmount", 0.25f);
        } else if (mode_ == "fog") {
            fogShader_->sendFloat("sampleCount", 48.f);
            fogShader_->sendFloat("noiseAmount", 0.45f);
            fogShader_->sendFloat("density", 0.32f);
        } else if (mode_ == "cloud") {
            cloudShader_->sendFloat("sampleCount", 64.f);
            cloudShader_->sendFloat("shadowSteps", 10.f);
        } else {
            setFloat("sampleCount", 96.f);
            setFloat("dustAmount", 0.35f);
            setFloat("fogAmount", 0.22f);
            setFloat("exposure", 0.32f);
        }
    } else {
        quality_ = "medium";
        downscale_ = 2.f;
        if (mode_ == "raymarch") {
            rayShader_->sendFloat("sampleCount", 24.f);
            rayShader_->sendFloat("shadowSteps", 8.f);
            rayShader_->sendFloat("dustAmount", 0.25f);
            rayShader_->sendFloat("fogAmount", 0.2f);
        } else if (mode_ == "fog") {
            fogShader_->sendFloat("sampleCount", 24.f);
            fogShader_->sendFloat("noiseAmount", 0.35f);
            fogShader_->sendFloat("density", 0.25f);
        } else if (mode_ == "cloud") {
            cloudShader_->sendFloat("sampleCount", 32.f);
            cloudShader_->sendFloat("shadowSteps", 6.f);
        } else {
            setFloat("sampleCount", 48.f);
            setFloat("dustAmount", 0.25f);
            setFloat("fogAmount", 0.18f);
            setFloat("exposure", 0.35f);
        }
    }
}

void Volumetric::setQuality(const std::string &quality) {
    quality_ = quality;
    applyQualityDefaults();
}

void Volumetric::setMode(const std::string &mode) {
    if (mode == "raymarch")
        mode_ = "raymarch";
    else if (mode == "fog")
        mode_ = "fog";
    else if (mode == "froxel")
        mode_ = "froxel";
    else if (mode == "cloud")
        mode_ = "cloud";
    else
        mode_ = "screenspace";
    applyQualityDefaults();
}

void Volumetric::setLightScreenUV(float u, float v) {
    setFloat("lightX", u);
    setFloat("lightY", v);
    rayShader_->sendFloat("lightU", u);
    rayShader_->sendFloat("lightV", v);
}

float Volumetric::getLightScreenU() const { return getFloat("lightX"); }
float Volumetric::getLightScreenV() const { return getFloat("lightY"); }

void Volumetric::setLightScreenPos(float x, float y, float width, float height) {
    if (width < 1.f) width = 1.f;
    if (height < 1.f) height = 1.f;
    setLightScreenUV(x / width, y / height);
}

void Volumetric::setLightDirection(float dx, float dy, float dz) {
    glm::vec3 d(dx, dy, dz);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    else d = glm::normalize(d);
    lightDir_ = d;
    rayShader_->sendFloat("lightDx", d.x);
    rayShader_->sendFloat("lightDy", d.y);
    rayShader_->sendFloat("lightDz", d.z);
    fogShader_->sendFloat("lightDx", d.x);
    fogShader_->sendFloat("lightDy", d.y);
    fogShader_->sendFloat("lightDz", d.z);
    cloudShader_->sendFloat("lightDx", d.x);
    cloudShader_->sendFloat("lightDy", d.y);
    cloudShader_->sendFloat("lightDz", d.z);
}

void Volumetric::setInvViewProj(const glm::mat4 &invViewProj) {
    invViewProj_ = invViewProj;
    rayShader_->sendMatrix("invViewProj", invViewProj_);
    fogShader_->sendMatrix("invViewProj", invViewProj_);
    cloudShader_->sendMatrix("invViewProj", invViewProj_);
}

void Volumetric::setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY,
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
    rayShader_->sendFloat("nearZ", nearZ_);
    rayShader_->sendFloat("farZ", farZ_);
    fogShader_->sendFloat("nearZ", nearZ_);
    fogShader_->sendFloat("farZ", farZ_);
}

void Volumetric::setShaftColor(float r, float g, float b) {
    setFloat("shaftR", r);
    setFloat("shaftG", g);
    setFloat("shaftB", b);
    rayShader_->sendFloat("shaftR", r);
    rayShader_->sendFloat("shaftG", g);
    rayShader_->sendFloat("shaftB", b);
}

void Volumetric::setFogColor(float r, float g, float b) {
    setFloat("fogR", r);
    setFloat("fogG", g);
    setFloat("fogB", b);
    fogShader_->sendFloat("fogR", r);
    fogShader_->sendFloat("fogG", g);
    fogShader_->sendFloat("fogB", b);
}

void Volumetric::setIntensity(float intensity) {
    setFloat("intensity", intensity);
    rayShader_->sendFloat("intensity", intensity);
    fogShader_->sendFloat("intensity", intensity);
}

void Volumetric::setTime(float seconds) { setFloat("time", seconds); }

void Volumetric::setDensity(float density) {
    setFloat("density", density);
    rayShader_->sendFloat("density", density);
    fogShader_->sendFloat("density", density);
}

void Volumetric::setFogHeight(float worldY) {
    fogHeight_ = worldY;
    fogShader_->sendFloat("fogHeight", fogHeight_);
}

void Volumetric::setFogHeightFalloff(float falloff) {
    fogHeightFalloff_ = falloff < 0.f ? 0.f : falloff;
    fogShader_->sendFloat("heightFalloff", fogHeightFalloff_);
}

void Volumetric::setFogStart(float startDistance) {
    fogStart_ = startDistance < 0.f ? 0.f : startDistance;
    fogShader_->sendFloat("fogStart", fogStart_);
}

void Volumetric::setFogEnd(float endDistance) {
    fogEnd_ = endDistance <= fogStart_ ? fogStart_ + 1.f : endDistance;
    fogShader_->sendFloat("fogEnd", fogEnd_);
}

void Volumetric::setFogNoise(float amount) {
    fogNoise_ = amount < 0.f ? 0.f : amount;
    fogShader_->sendFloat("noiseAmount", fogNoise_);
}

void Volumetric::setCloudLayer(float bottom, float top) {
    cloudBottom_ = bottom;
    cloudTop_ = top > bottom + 0.01f ? top : bottom + 0.01f;
    cloudShader_->sendFloat("cloudBottom", cloudBottom_);
    cloudShader_->sendFloat("cloudTop", cloudTop_);
}

void Volumetric::setCloudCoverage(float coverage) {
    cloudCoverage_ = std::clamp(coverage, 0.f, 1.f);
    cloudShader_->sendFloat("cloudCoverage", cloudCoverage_);
}

void Volumetric::setCloudDensity(float density) {
    cloudDensity_ = std::max(density, 0.f);
    cloudShader_->sendFloat("cloudDensity", cloudDensity_);
}

void Volumetric::setCloudScale(float worldScale) {
    cloudScale_ = std::max(worldScale, 0.01f);
    cloudShader_->sendFloat("cloudScale", cloudScale_);
}

void Volumetric::setCloudWind(float x, float z) {
    cloudWind_ = glm::vec3(x, 0.f, z);
    cloudShader_->sendFloat("cloudWindX", x);
    cloudShader_->sendFloat("cloudWindZ", z);
}

void Volumetric::setCloudLightColor(float r, float g, float b) {
    cloudLightColor_ = glm::vec3(std::max(r, 0.f), std::max(g, 0.f), std::max(b, 0.f));
    cloudShader_->sendFloat("cloudLightR", cloudLightColor_.x);
    cloudShader_->sendFloat("cloudLightG", cloudLightColor_.y);
    cloudShader_->sendFloat("cloudLightB", cloudLightColor_.z);
}

bool Volumetric::hasParam(const std::string &name) const {
    return (shader_ && shader_->hasUniform(name)) || (rayShader_ && rayShader_->hasUniform(name)) ||
           (fogShader_ && fogShader_->hasUniform(name)) ||
           (cloudShader_ && cloudShader_->hasUniform(name));
}

void Volumetric::setFloat(const std::string &name, float value) {
    if (!shader_) throw eve::Exception("Volumetric.setFloat: null shader");
    if (shader_->hasUniform(name)) shader_->sendFloat(name, value);
    if (rayShader_ && rayShader_->hasUniform(name)) rayShader_->sendFloat(name, value);
    if (fogShader_ && fogShader_->hasUniform(name)) fogShader_->sendFloat(name, value);
    if (cloudShader_ && cloudShader_->hasUniform(name)) cloudShader_->sendFloat(name, value);
}

float Volumetric::getFloat(const std::string &name) const {
    // Prefer the active mode's shader so quality/sample queries match setMode.
    if (mode_ == "raymarch" && rayShader_ && rayShader_->hasUniform(name)) {
        float v = 0.f;
        if (rayShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (mode_ == "fog" && fogShader_ && fogShader_->hasUniform(name)) {
        float v = 0.f;
        if (fogShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (mode_ == "cloud" && cloudShader_ && cloudShader_->hasUniform(name)) {
        float v = 0.f;
        if (cloudShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (shader_ && shader_->hasUniform(name)) {
        float v = 0.f;
        if (shader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (rayShader_ && rayShader_->hasUniform(name)) {
        float v = 0.f;
        if (rayShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (fogShader_ && fogShader_->hasUniform(name)) {
        float v = 0.f;
        if (fogShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    if (cloudShader_ && cloudShader_->hasUniform(name)) {
        float v = 0.f;
        if (cloudShader_->getFromVar(name, &v, sizeof(v)) == int(sizeof(v))) return v;
    }
    throw eve::Exception("Volumetric.getFloat: missing param '%s'", name.c_str());
}

int Volumetric::getSampleCount() const { return int(std::lround(getFloat("sampleCount"))); }

int Volumetric::resolutionFor(int fullSize) const {
    if (fullSize < 1) return 1;
    const float ds = std::max(downscale_, 1.f);
    return std::max(1, int(std::floor(float(fullSize) / ds)));
}

void Volumetric::uploadCommon(bool compositeFromScene) {
    setFloat("compositeMode", compositeFromScene ? 1.f : 0.f);
}

void Volumetric::uploadRayMarchCommon() {
    rayShader_->sendMatrix("invViewProj", invViewProj_);
    rayShader_->sendFloat("lightDx", lightDir_.x);
    rayShader_->sendFloat("lightDy", lightDir_.y);
    rayShader_->sendFloat("lightDz", lightDir_.z);
    rayShader_->sendFloat("nearZ", nearZ_);
    rayShader_->sendFloat("farZ", farZ_);
}

void Volumetric::uploadFogCommon() {
    fogShader_->sendMatrix("invViewProj", invViewProj_);
    fogShader_->sendFloat("lightDx", lightDir_.x);
    fogShader_->sendFloat("lightDy", lightDir_.y);
    fogShader_->sendFloat("lightDz", lightDir_.z);
    fogShader_->sendFloat("nearZ", nearZ_);
    fogShader_->sendFloat("farZ", farZ_);
    fogShader_->sendFloat("fogHeight", fogHeight_);
    fogShader_->sendFloat("heightFalloff", fogHeightFalloff_);
    fogShader_->sendFloat("fogStart", fogStart_);
    fogShader_->sendFloat("fogEnd", fogEnd_);
    fogShader_->sendFloat("noiseAmount", fogNoise_);
}

void Volumetric::uploadCloudCommon() {
    cloudShader_->sendMatrix("invViewProj", invViewProj_);
    cloudShader_->sendFloat("lightDx", lightDir_.x);
    cloudShader_->sendFloat("lightDy", lightDir_.y);
    cloudShader_->sendFloat("lightDz", lightDir_.z);
    cloudShader_->sendFloat("cloudBottom", cloudBottom_);
    cloudShader_->sendFloat("cloudTop", cloudTop_);
    cloudShader_->sendFloat("cloudCoverage", cloudCoverage_);
    cloudShader_->sendFloat("cloudDensity", cloudDensity_);
    cloudShader_->sendFloat("cloudScale", cloudScale_);
    cloudShader_->sendFloat("cloudWindX", cloudWind_.x);
    cloudShader_->sendFloat("cloudWindZ", cloudWind_.z);
    cloudShader_->sendFloat("cloudLightR", cloudLightColor_.x);
    cloudShader_->sendFloat("cloudLightG", cloudLightColor_.y);
    cloudShader_->sendFloat("cloudLightB", cloudLightColor_.z);
}

void Volumetric::drawFullscreen(Graphics *gfx, Texture *source, Shader *shader) {
    if (!gfx) throw eve::Exception("Volumetric: null graphics");
    if (!source) throw eve::Exception("Volumetric: null source texture");
    if (!shader) throw eve::Exception("Volumetric: null shader");

    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh =
        gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShader(source, shader, 0.f, 0.f, dw, dh, Color(1.f, 1.f, 1.f, 1.f));
}

void Volumetric::beginOcclusionMap(Graphics *gfx, float lightPixelX, float lightPixelY,
                                   float lightRadiusPixels) {
    if (!gfx) throw eve::Exception("Volumetric.beginOcclusionMap: null graphics");
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    const float r = std::max(lightRadiusPixels, 1.f);
    gfx->drawSolidRect(lightPixelX - r, lightPixelY - r, r * 2.f, r * 2.f,
                       Color(1.f, 1.f, 1.f, 1.f));
    const float w = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float h = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    setLightScreenPos(lightPixelX, lightPixelY, w, h);
}

void Volumetric::drawOccluder(Graphics *gfx, Drawable *drawable, const glm::mat4 &matrix) {
    if (!gfx || !drawable) return;
    gfx->drawOcclusion(drawable, matrix);
}

void Volumetric::drawOccluderSolid(Graphics *gfx, float x, float y, float w, float h) {
    if (!gfx) return;
    gfx->drawOcclusionSolid(x, y, w, h);
}

void Volumetric::drawOccluderTexture(Graphics *gfx, Texture *texture, float x, float y, float w,
                                     float h) {
    if (!gfx) return;
    gfx->drawOcclusionTexture(texture, x, y, w, h);
}

void Volumetric::drawOccluders2D(Graphics *gfx) {
    if (!gfx) return;
    if (ecs::current()->getManager<Renderable2D>() == nullptr) return;
    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (!sp->visible || !sp->castOcclusion) continue;
        const float w = sp->width * (xf->sx == 0.f ? 1.f : xf->sx);
        const float h = sp->height * (xf->sy == 0.f ? 1.f : xf->sy);
        if (sp->texture)
            gfx->drawOcclusionTexture(sp->texture, xf->x, xf->y, w, h);
        else
            gfx->drawOcclusionSolid(xf->x, xf->y, w, h);
    }
}

void Volumetric::scatter(Graphics *gfx, Texture *occlusion) {
    uploadCommon(false);
    drawFullscreen(gfx, occlusion, shader_);
}

void Volumetric::scatterTo(Graphics *gfx, Texture *occlusion, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.scatterTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.scatterTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    scatter(gfx, occlusion);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::applyFromScene(Graphics *gfx, Texture *scene) {
    uploadCommon(true);
    drawFullscreen(gfx, scene, shader_);
}

void Volumetric::applyFromSceneTo(Graphics *gfx, Texture *scene, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.applyFromSceneTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.applyFromSceneTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFromScene(gfx, scene);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::rayMarch(Graphics *gfx, Texture *linearDepth) {
    uploadRayMarchCommon();
    drawFullscreen(gfx, linearDepth, rayShader_);
}

void Volumetric::rayMarchTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.rayMarchTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.rayMarchTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    rayMarch(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::applyFog(Graphics *gfx, Texture *linearDepth) {
    uploadFogCommon();
    drawFullscreen(gfx, linearDepth, fogShader_);
}

void Volumetric::applyFogTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.applyFogTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.applyFogTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFog(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::renderClouds(Graphics *gfx, Texture *linearDepth) {
    uploadCloudCommon();
    drawFullscreen(gfx, linearDepth, cloudShader_);
}

void Volumetric::renderCloudsTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx) throw eve::Exception("Volumetric.renderCloudsTo: null graphics");
    if (!dest) throw eve::Exception("Volumetric.renderCloudsTo: null dest");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    renderClouds(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

void Volumetric::configureFroxelGrid(int width, int height, int depth, float nearDistance,
                                     float farDistance) {
    atmosphereVolume_->resize(width, height, depth);
    atmosphereVolume_->setDepthRange(nearDistance, farDistance);
    froxelAtlasCols_ = std::max(1, int(std::ceil(std::sqrt(float(atmosphereVolume_->getDepth())))));
    froxelAtlasRows_ = std::max(1, (atmosphereVolume_->getDepth() + froxelAtlasCols_ - 1) /
        froxelAtlasCols_);
}

void Volumetric::clearFroxelGrid() { atmosphereVolume_->clear(); }

void Volumetric::injectFroxelHeightFog(float extinction, float albedoR, float albedoG,
                                       float albedoB, float baseHeight, float heightFalloff,
                                       float minWorldY, float maxWorldY) {
    atmosphereVolume_->injectHeightFog(extinction, glm::vec3(albedoR, albedoG, albedoB),
                                       baseHeight, heightFalloff, minWorldY, maxWorldY);
}

void Volumetric::integrateFroxel(float lightR, float lightG, float lightB, float phaseScale) {
    atmosphereVolume_->integrate(glm::vec3(lightR, lightG, lightB), phaseScale);
}

void Volumetric::uploadFroxel(Graphics *gfx) {
    if (!gfx) throw eve::Exception("Volumetric.uploadFroxel: null graphics");
    const int w = atmosphereVolume_->getWidth();
    const int h = atmosphereVolume_->getHeight();
    const int d = atmosphereVolume_->getDepth();
    if (w <= 0 || h <= 0 || d <= 0)
        throw eve::Exception("Volumetric.uploadFroxel: grid is not configured");
    const int atlasW = w * froxelAtlasCols_;
    const int atlasH = h * froxelAtlasRows_;
    std::vector<uint8_t> rgba(std::size_t(atlasW) * std::size_t(atlasH) * 4u, 0u);
    for (int z = 0; z < d; ++z) {
        const int ox = (z % froxelAtlasCols_) * w;
        const int oy = (z / froxelAtlasCols_) * h;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const glm::vec4 value = atmosphereVolume_->integratedAt(x, y, z);
                const std::size_t offset = (std::size_t(oy + y) * std::size_t(atlasW) +
                                            std::size_t(ox + x)) * 4u;
                const glm::vec3 mapped = glm::vec3(value) / (glm::vec3(1.f) + glm::vec3(value));
                rgba[offset + 0] = uint8_t(std::lround(glm::clamp(mapped.r, 0.f, 1.f) * 255.f));
                rgba[offset + 1] = uint8_t(std::lround(glm::clamp(mapped.g, 0.f, 1.f) * 255.f));
                rgba[offset + 2] = uint8_t(std::lround(glm::clamp(mapped.b, 0.f, 1.f) * 255.f));
                rgba[offset + 3] = uint8_t(std::lround(glm::clamp(value.a, 0.f, 1.f) * 255.f));
            }
        }
    }
    if (!froxelAtlas_ || froxelAtlas_->getWidth() != atlasW || froxelAtlas_->getHeight() != atlasH)
        froxelAtlas_ = gfx->newTexture(atlasW, atlasH, rgba.data());
    else
        gfx->updateTexture(froxelAtlas_, atlasW, atlasH, rgba.data());
    froxelShader_->sendFloat("atlasCols", float(froxelAtlasCols_));
    froxelShader_->sendFloat("atlasRows", float(froxelAtlasRows_));
    froxelShader_->sendFloat("sliceCount", float(d));
    froxelShader_->sendFloat("nearDistance", atmosphereVolume_->getNearDistance());
    froxelShader_->sendFloat("farDistance", atmosphereVolume_->getFarDistance());
}

void Volumetric::applyFroxel(Graphics *gfx, Texture *linearDepth) {
    if (!gfx) throw eve::Exception("Volumetric.applyFroxel: null graphics");
    if (!linearDepth) throw eve::Exception("Volumetric.applyFroxel: null depth");
    if (!froxelAtlas_) throw eve::Exception("Volumetric.applyFroxel: uploadFroxel not called");
    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    gfx->drawTexturedRectShaderDepth(linearDepth, froxelAtlas_, froxelShader_, 0.f, 0.f, dw, dh,
                                     Color(1.f, 1.f, 1.f, 1.f));
}

void Volumetric::applyFroxelTo(Graphics *gfx, Texture *linearDepth, Canvas *dest) {
    if (!gfx || !dest) throw eve::Exception("Volumetric.applyFroxelTo: null argument");
    Canvas *prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    applyFroxel(gfx, linearDepth);
    gfx->setCanvas(prev == gfx ? nullptr : prev);
}

Texture *Volumetric::newLinearDepthTexture(Graphics *gfx, int width, int height,
                                           float (*depth01)(int x, int y, void *userdata),
                                           void *userdata) {
    if (!gfx) throw eve::Exception("Volumetric.newLinearDepthTexture: null graphics");
    if (width < 1 || height < 1)
        throw eve::Exception("Volumetric.newLinearDepthTexture: invalid size");
    if (!depth01) throw eve::Exception("Volumetric.newLinearDepthTexture: null depth fn");
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
