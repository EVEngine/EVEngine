#include "graphics/Water.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/shaders/WaterWgsl.h"
#include "graphics/shaders/water_frag_spv.inc"

#include <cmath>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 waveSpeed, 2 waveAmp, 3 rippleAmp, 4 edgeFalloff,
//   5 reflIntensity, 6 rippleCount, 7 rippleInterval, 8 waveScale,
//   9..11 waterColor, 12..14 reflTint, 15 sunIntensity,
//   16 viewportW, 17 viewportH, 18 ssrEnabled, 19 ssrStrength.

const char *kUniformNames[] = {
    "time",      "waveSpeed", "waveAmp",  "rippleAmp",   "edgeFalloff",
    "reflInten", "rippleCnt", "rippleInt", "waveScale",   "waterCol",
    "reflTint",  "sunInten",  "viewportW", "viewportH",   "ssrEnabled",
    "ssrStrength",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

Shader *newWaterShader(Graphics *gfx) {
    // Use precompiled SPIR-V (not runtime glslc) so the water shader also works
    // on platforms without a runtime compiler (e.g. Windows).
    Shader *sh = nullptr;
    if (gfx->getBackendName() == "webgpu") {
        sh = gfx->newMeshShaderFromWgsl({}, shaders::kWaterFragWgsl);
    } else {
        std::vector<uint32_t> frag(water_frag_spv, water_frag_spv + water_frag_spv_count);
        sh = gfx->newMeshShaderFromSpv({}, frag);
    }
    for (int i = 0; i < kUniformCount; ++i) {
        if (std::string(kUniformNames[i]) == "waterCol")
            sh->declareVec3(kUniformNames[i]);
        else if (std::string(kUniformNames[i]) == "reflTint")
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
}

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

void Water::setWaveSpeed(float v) { waveSpeed_ = v; }
void Water::setWaveAmplitude(float v) { waveAmplitude_ = v; }
void Water::setRippleAmplitude(float v) { rippleAmplitude_ = v; }
void Water::setEdgeFalloff(float v) { edgeFalloff_ = std::max(0.001f, v); }
void Water::setRippleCount(int v) { rippleCount_ = std::max(0, v); }
void Water::setRippleInterval(float v) { rippleInterval_ = std::max(0.05f, v); }
void Water::setWaveScale(float v) { waveScale_ = std::max(0.1f, v); }
void Water::setWaterColor(float r, float g, float b) {
    waterColor_[0] = r;
    waterColor_[1] = g;
    waterColor_[2] = b;
}
void Water::setReflectionTint(float r, float g, float b) {
    reflectionTint_[0] = r;
    reflectionTint_[1] = g;
    reflectionTint_[2] = b;
}
void Water::setReflectionIntensity(float v) { reflectionIntensity_ = std::max(0.f, v); }
void Water::setSunIntensity(float v) { sunIntensity_ = std::max(0.f, v); }
void Water::setScreenSpaceReflection(bool enabled, float strength) {
    ssrEnabled_ = enabled;
    ssrStrength_ = std::max(0.f, strength);
}
void Water::setViewport(float w, float h) {
    viewportW_ = std::max(0.f, w);
    viewportH_ = std::max(0.f, h);
}

void Water::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("waveSpeed", waveSpeed_);
    shader_->sendFloat("waveAmp", waveAmplitude_);
    shader_->sendFloat("rippleAmp", rippleAmplitude_);
    shader_->sendFloat("edgeFalloff", edgeFalloff_);
    shader_->sendFloat("reflInten", reflectionIntensity_);
    shader_->sendFloat("rippleCnt", float(rippleCount_));
    shader_->sendFloat("rippleInt", rippleInterval_);
    shader_->sendFloat("waveScale", waveScale_);
    shader_->sendVec3("waterCol", waterColor_[0], waterColor_[1], waterColor_[2]);
    shader_->sendVec3("reflTint", reflectionTint_[0], reflectionTint_[1], reflectionTint_[2]);
    shader_->sendFloat("sunInten", sunIntensity_);
    shader_->sendFloat("viewportW", viewportW_);
    shader_->sendFloat("viewportH", viewportH_);
    shader_->sendFloat("ssrEnabled", ssrEnabled_ ? 1.f : 0.f);
    shader_->sendFloat("ssrStrength", ssrStrength_);
}

void Water::draw() {
    if (!gfx_ || !mesh_ || !shader_) return;
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
}

}  // namespace eve::graphics
