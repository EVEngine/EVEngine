#include "graphics/Waterfall.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/shaders/waterfall_mesh_frag_spv.inc"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 flowSpeed, 2 turbulence, 3 streakCount, 4 streakScale,
//   5 topFoam, 6 bottomFoam, 7 foamAmt, 8 reflIntensity,
//   9 sunIntensity, 10..12 waterColor.
// Fragment shader source lives in graphics/shaders/waterfall_mesh.frag; it is
// precompiled to SPIR-V (waterfall_mesh_frag_spv.inc) so it works on every
// backend without a runtime glslc.
const char *kUniformNames[] = {
    "time", "flowSpeed", "turbulence", "streakCnt", "streakScale",
    "topFoam", "bottomFoam", "foamAmt", "reflInten", "sunInten", "waterCol",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

Shader *newWaterfallShader(Graphics *gfx) {
    std::vector<uint32_t> frag(waterfall_mesh_frag_spv,
                               waterfall_mesh_frag_spv + waterfall_mesh_frag_spv_count);
    Shader *sh = gfx->newMeshShaderFromSpv({}, frag);
    for (int i = 0; i < kUniformCount; ++i) {
        if (std::string(kUniformNames[i]) == "waterCol")
            sh->declareVec3(kUniformNames[i]);
        else
            sh->declareFloat(kUniformNames[i]);
    }
    return sh;
}

int Waterfall::paramCount() { return kUniformCount; }

std::string Waterfall::paramName(int index) {
    if (index < 0 || index >= kUniformCount) return {};
    return kUniformNames[index];
}

Waterfall::Waterfall(Graphics *gfx) : gfx_(gfx) {
    shader_ = newWaterfallShader(gfx);
    bindParams();
}

void Waterfall::createSheet(float width, float height, int segX, int segY) {
    createCurvedSheet(width, height, segX, segY, 0.f, 0.f);
}

void Waterfall::createCurvedSheet(float width, float height, int segX, int segY,
                                  float curveDepth, float lipOverhang) {
    segX = std::max(1, segX);
    segY = std::max(1, segY);
    curveDepth = std::max(0.f, curveDepth);
    lipOverhang = std::max(0.f, lipOverhang);
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= segY; ++y) {
        const float v = float(y) / float(segY);
        const float lowerFan = 1.f + 0.12f * (1.f - v) * (1.f - v);
        for (int x = 0; x <= segX; ++x) {
            const float u = float(x) / float(segX);
            const float across = u * 2.f - 1.f;
            const float crown = std::max(0.f, 1.f - across * across);
            const float bow = curveDepth * crown * (0.62f + 0.38f * v);
            const float lip = lipOverhang * v * v;
            pos.push_back(across * width * 0.5f * lowerFan);
            pos.push_back(-height * 0.5f + height * v);
            pos.push_back(bow + lip);
            nrm.push_back(0.f);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            uv.push_back(u);
            uv.push_back(v);
        }
    }
    for (int y = 0; y < segY; ++y) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t a = uint32_t(y * (segX + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(segX + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(d);
            idx.push_back(c);
        }
    }
    mesh_ = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                    idx.data(), int(idx.size()));
}

void Waterfall::update(float dt) {
    time_ += dt;
    bindParams();
}

void Waterfall::setTime(float seconds) {
    time_ = seconds;
    bindParams();
}

void Waterfall::setFlowSpeed(float v) { flowSpeed_ = v; }
void Waterfall::setTurbulence(float v) { turbulence_ = std::max(0.f, v); }
void Waterfall::setStreakCount(int v) { streakCount_ = std::max(0, v); }
void Waterfall::setStreakScale(float v) { streakScale_ = std::max(0.1f, v); }
void Waterfall::setTopFoam(float v) { topFoam_ = std::clamp(v, 0.001f, 0.5f); }
void Waterfall::setBottomFoam(float v) { bottomFoam_ = std::clamp(v, 0.001f, 0.5f); }
void Waterfall::setFoamAmount(float v) { foamAmount_ = std::max(0.f, v); }
void Waterfall::setWaterColor(float r, float g, float b) {
    waterColor_[0] = r;
    waterColor_[1] = g;
    waterColor_[2] = b;
}
void Waterfall::setReflectionIntensity(float v) { reflectionIntensity_ = std::max(0.f, v); }
void Waterfall::setSunIntensity(float v) { sunIntensity_ = std::max(0.f, v); }

void Waterfall::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("flowSpeed", flowSpeed_);
    shader_->sendFloat("turbulence", turbulence_);
    shader_->sendFloat("streakCnt", float(streakCount_));
    shader_->sendFloat("streakScale", streakScale_);
    shader_->sendFloat("topFoam", topFoam_);
    shader_->sendFloat("bottomFoam", bottomFoam_);
    shader_->sendFloat("foamAmt", foamAmount_);
    shader_->sendFloat("reflInten", reflectionIntensity_);
    shader_->sendFloat("sunInten", sunIntensity_);
    shader_->sendVec3("waterCol", waterColor_[0], waterColor_[1], waterColor_[2]);
}

void Waterfall::draw() {
    if (!gfx_ || !mesh_ || !shader_) return;
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
}

}  // namespace eve::graphics
