#include "graphics/Water.h"

#include "graphics/Graphics.h"
#include "graphics/GBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/shaders/WaterWgsl.h"
#include "graphics/shaders/water_frag_spv.inc"
#include "graphics/shaders/water_vert_spv.inc"

#include <cmath>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 waveSpeed, 2 waveAmp, 3 waveScale, 4 rippleAmp,
//   5 rippleCount, 6 rippleInterval, 7 foamWidth, 8 foamSoftness,
//   9 foamStrength, 10..12 deepColor, 13..15 shallowColor,
//   16 depthDistance, 17..19 foamColor, 20 reflectionIntensity,
//   21..23 reflectionTint, 24 sunIntensity, 25 fresnelPower,
//   26 causticsStrength, 27 signedSsrStrength, 28 refractionStrength,
//   29 signedOpacityAndDepthAvailability, 30 causticsScale, 31 waveSharpness.

const char *kUniformNames[] = {
    "time",       "waveSpeed", "waveAmp",   "waveScale",   "rippleAmp",
    "rippleCnt",  "rippleInt", "foamWidth", "foamSoftness", "foamStrength",
    "deepColor",  "shallowColor", "depthDistance", "foamColor", "reflInten",
    "reflTint",   "sunInten", "fresnelPower", "causticsStrength", "ssrStrength",
    "refractionStrength", "opacityAndDepth", "causticsScale", "waveSharpness",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

Shader *newWaterShader(Graphics *gfx) {
    // Use precompiled SPIR-V (not runtime glslc) so the water shader also works
    // on platforms without a runtime compiler (e.g. Windows).
    Shader *sh = nullptr;
    if (gfx->getBackendName() == "webgpu") {
        sh = gfx->newMeshShaderFromWgsl(shaders::kWaterVertWgsl, shaders::kWaterFragWgsl);
    } else {
        std::vector<uint32_t> vert(water_vert_spv, water_vert_spv + water_vert_spv_count);
        std::vector<uint32_t> frag(water_frag_spv, water_frag_spv + water_frag_spv_count);
        sh = gfx->newMeshShaderFromSpv(vert, frag);
    }
    gfx->configureMeshShaderSurface(*sh, BlendMode::Alpha, false, false)
        .expect("water requires alpha blending with depth writes disabled");
    for (int i = 0; i < kUniformCount; ++i) {
        const std::string name(kUniformNames[i]);
        if (name == "deepColor" || name == "shallowColor" || name == "foamColor" || name == "reflTint")
            sh->declareVec3(kUniformNames[i]);
        else
            sh->declareFloat(kUniformNames[i]);
    }
    return sh;
}

int Water::paramCount() { return kUniformCount; }

std::string Water::paramName(int index) {
    if (index < 0 || index >= kUniformCount) return {};
    return kUniformNames[index];
}

Water::Water(Graphics *gfx) : gfx_(gfx) {
    shader_ = newWaterShader(gfx);
    bindParams();
    captureDrawerToken_ = RenderSystem3D::addCaptureExtraDrawer(
        0xffffffffu,
        [this](Graphics &, const Camera3D::Data &, const glm::mat4 &, float, uint32_t mask) {
            if (!reflectionCaptureEnabled_ || (reflectionCaptureMask_ & mask) == 0u) return;
            drawReflectionCapture();
        });
}

Water::~Water() { RenderSystem3D::removeCaptureExtraDrawer(captureDrawerToken_); }

void Water::createPlane(float sizeX, float sizeZ, int segX, int segZ) {
    segX = std::max(1, segX);
    segZ = std::max(1, segZ);
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int z = 0; z <= segZ; ++z) {
        for (int x = 0; x <= segX; ++x) {
            pos.push_back(-sizeX * 0.5f + sizeX * float(x) / segX);
            pos.push_back(0.f);
            pos.push_back(-sizeZ * 0.5f + sizeZ * float(z) / segZ);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(float(x) / segX);
            uv.push_back(float(z) / segZ);
        }
    }
    for (int z = 0; z < segZ; ++z) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t a = uint32_t(z * (segX + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(segX + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(d);
        }
    }
    mesh_ = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                    idx.data(), int(idx.size()));
}

void Water::update(float dt) {
    time_ += dt;
    bindParams();
}

void Water::setTime(float seconds) {
    time_ = seconds;
    bindParams();
}

void Water::setWaveSpeed(float v) { config_.waveSpeed = std::clamp(v, -20.0F, 20.0F); }
void Water::setWaveAmplitude(float v) { config_.waveAmplitude = std::clamp(v, 0.0F, 4.0F); }
void Water::setRippleAmplitude(float v) { config_.rippleAmplitude = std::clamp(v, 0.0F, 4.0F); }
void Water::setEdgeFalloff(float v) { config_.foamWidth = std::clamp(v, 0.001F, 1000.0F); }
void Water::setRippleCount(int v) { config_.rippleCount = std::clamp(v, 0, 8); }
void Water::setRippleInterval(float v) { config_.rippleInterval = std::clamp(v, 0.05F, 60.0F); }
void Water::setWaveScale(float v) { config_.waveScale = std::clamp(v, 0.01F, 256.0F); }
void Water::setWaterColor(float r, float g, float b) {
    config_.deepColor = glm::max(glm::vec3(r, g, b), glm::vec3(0.0F));
}
void Water::setReflectionTint(float r, float g, float b) {
    config_.reflectionTint = glm::max(glm::vec3(r, g, b), glm::vec3(0.0F));
}
void Water::setReflectionIntensity(float v) { config_.reflectionIntensity = std::clamp(v, 0.0F, 4.0F); }
void Water::setSunIntensity(float v) { config_.sunIntensity = std::clamp(v, 0.0F, 8.0F); }
void Water::setScreenSpaceReflection(bool enabled, float strength) {
    config_.screenSpaceReflection = enabled;
    config_.screenSpaceReflectionStrength = std::clamp(strength, 0.0F, 4.0F);
}
void Water::setViewport(float w, float h) {
    viewportW_ = std::max(0.f, w);
    viewportH_ = std::max(0.f, h);
}

void Water::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("waveSpeed", config_.waveSpeed);
    shader_->sendFloat("waveAmp", config_.waveAmplitude);
    shader_->sendFloat("waveScale", config_.waveScale);
    shader_->sendFloat("rippleAmp", config_.rippleAmplitude);
    shader_->sendFloat("rippleCnt", float(config_.rippleCount));
    shader_->sendFloat("rippleInt", config_.rippleInterval);
    shader_->sendFloat("foamWidth", config_.foamWidth);
    shader_->sendFloat("foamSoftness", config_.foamSoftness);
    shader_->sendFloat("foamStrength", config_.foamStrength);
    shader_->sendVec3("deepColor", config_.deepColor.x, config_.deepColor.y, config_.deepColor.z);
    shader_->sendVec3("shallowColor", config_.shallowColor.x, config_.shallowColor.y, config_.shallowColor.z);
    shader_->sendFloat("depthDistance", config_.depthDistance);
    shader_->sendVec3("foamColor", config_.foamColor.x, config_.foamColor.y, config_.foamColor.z);
    shader_->sendFloat("reflInten", config_.reflectionIntensity);
    shader_->sendVec3("reflTint", config_.reflectionTint.x, config_.reflectionTint.y, config_.reflectionTint.z);
    shader_->sendFloat("sunInten", config_.sunIntensity);
    shader_->sendFloat("fresnelPower", config_.fresnelPower);
    shader_->sendFloat("causticsStrength", config_.causticsStrength);
    shader_->sendFloat("ssrStrength", config_.screenSpaceReflection ? config_.screenSpaceReflectionStrength : -1.0F);
    shader_->sendFloat("refractionStrength", config_.refractionStrength);
    shader_->sendFloat("opacityAndDepth", -(config_.opacity + 1.0F));
    shader_->sendFloat("causticsScale", config_.causticsScale);
    shader_->sendFloat("waveSharpness", config_.waveSharpness);
}

Result<void> Water::applyConfigJson(const std::string& json) {
    auto candidate = WaterStyleConfig::fromJson(json);
    if (!candidate) return Result<void>::failure(candidate.status());
    config_ = std::move(candidate).takeValue();
    bindParams();
    return Result<void>::success();
}

Result<std::string> Water::configJson() const { return config_.toJson(); }

void Water::draw() { drawWithReflection(nullptr, 0.0F); }

void Water::drawWithPlanarReflection(Texture* planarReflection, float strength) {
    drawWithReflection(planarReflection, std::clamp(strength, 0.0F, 4.0F));
}

void Water::drawWithReflection(Texture* requestedReflection, float requestedStrength) {
    if (!gfx_ || !mesh_ || !shader_) return;
    bindParams();
    Texture *reflection = requestedReflection;
    float reflectionStrength = requestedReflection ? requestedStrength : -1.0F;
    RenderControl *rc = gfx_->getRenderControl();
    if (!reflection && config_.screenSpaceReflection && rc && rc->isEnabled("ssr")) {
        ScreenSpaceReflection *ssr = gfx_->pipelineScreenSpaceReflection();
        if (ssr->hasValidHistory()) {
            reflection = ssr->getReflectionTexture();
            reflectionStrength = config_.screenSpaceReflectionStrength;
        }
    }
    shader_->sendFloat("ssrStrength", reflection ? reflectionStrength : -1.0F);
    Texture *depth = rc && rc->getGBuffer() ? rc->getGBuffer()->getDepthTexture() : nullptr;
    Texture *sceneColor = gfx_->getSceneColorTexture();
    shader_->sendFloat("opacityAndDepth", depth ? config_.opacity : -(config_.opacity + 1.0F));
    shader_->sendFloat("refractionStrength", sceneColor ? config_.refractionStrength : 0.0F);
    gfx_->setMesh3DHeightTexture(reflection);
    gfx_->setMesh3DSceneDepth(depth);
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), sceneColor, glm::vec4(1.f), shader_);
    gfx_->setMesh3DHeightTexture(nullptr);
    gfx_->setMesh3DSceneDepth(nullptr);
}

void Water::drawReflectionCapture() {
    if (!gfx_ || !mesh_ || !shader_) return;
    bindParams();
    // Probe captures must never sample the main-view SSR history: that creates
    // view-dependent feedback and recursively bakes an old reflection into the cube.
    shader_->sendFloat("ssrStrength", -1.0F);
    shader_->sendFloat("opacityAndDepth", -(config_.opacity + 1.0F));
    shader_->sendFloat("refractionStrength", 0.0F);
    gfx_->setMesh3DHeightTexture(nullptr);
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
    // Restore the authored state for a subsequent main-view draw in the same frame.
    shader_->sendFloat("ssrStrength", config_.screenSpaceReflection ? config_.screenSpaceReflectionStrength : -1.0F);
    shader_->sendFloat("refractionStrength", config_.refractionStrength);
}

}  // namespace eve::graphics
