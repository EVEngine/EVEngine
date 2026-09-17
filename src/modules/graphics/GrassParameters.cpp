#include <array>
#include <cmath>
#include "common/Exception.h"
#include "graphics/Grass.h"
namespace eve::graphics::grass {
namespace {
const std::array<const char *, 32> kParamSlots = {
    "time",           "frameDuration",  "grassWidth",     "grassHeight",       "alphaCutoff",      "alwaysDark",
    "lightGreenX",    "lightGreenY",    "lightGreenZ",    "darkGreenX",        "darkGreenY",       "darkGreenZ",
    "frameCount",     "atlasCols",      "atlasRows",      "grassVariantCount", "leafVariantCount", "leafRowOffset",
    "windDirectionX", "windDirectionY", "windDirectionZ", "windStrength",      "windPhase",        "windDistance",
    "windFlexX",      "windFlexY",      "windFlexZ",      "windFrequencyX",    "windFrequencyY",   "windFrequencyZ",
    "windSinQuarter", "windSinFull"};
}
int paramCount() { return int(kParamSlots.size()); }

std::string paramName(int index) {
    if (index < 0 || index >= int(kParamSlots.size())) return {};
    return kParamSlots[size_t(index)];
}

void bindDefaults(Shader *shader) {
    if (!shader) throw eve::Exception("grass::bindDefaults: null shader");
    shader->declareFloat("time");
    shader->declareFloat("frameDuration");
    shader->declareFloat("grassWidth");
    shader->declareFloat("grassHeight");
    shader->declareFloat("alphaCutoff");
    shader->declareFloat("alwaysDark");
    shader->declareVec3("lightGreen");
    shader->declareVec3("darkGreen");
    shader->declareFloat("frameCount");
    shader->declareFloat("atlasCols");
    shader->declareFloat("atlasRows");
    shader->declareFloat("grassVariantCount");
    shader->declareFloat("leafVariantCount");
    shader->declareFloat("leafRowOffset");
    shader->declareVec4("windGlobals");
    shader->declareVec2("windPhaseDistance");
    shader->declareVec3("windFlex");
    shader->declareVec3("windFrequency");
    shader->declareVec2("windSineTime");
    shader->sendVec4("windGlobals", 0, 0, 0, 0);
    shader->sendVec2("windPhaseDistance", 0, 0);
    shader->sendVec3("windFlex", 0.8f, 1.15f, 0.1f);
    shader->sendVec3("windFrequency", 0.25f, 0.5f, 1.3f);
    shader->sendVec2("windSineTime", 0, 0);
    shader->sendFloat("time", 0.f);
    shader->sendFloat("frameDuration", 0.12f);
    shader->sendFloat("grassWidth", 0.62f);
    shader->sendFloat("grassHeight", 0.95f);
    shader->sendFloat("alphaCutoff", 0.35f);
    shader->sendFloat("alwaysDark", 0.f);
    shader->sendVec3("lightGreen", 0.58f, 0.84f, 0.26f);
    shader->sendVec3("darkGreen", 0.10f, 0.28f, 0.12f);
    shader->sendFloat("frameCount", 4.f);
    shader->sendFloat("atlasCols", 2.f);
    shader->sendFloat("atlasRows", 2.f);
    shader->sendFloat("grassVariantCount", 1.f);
    shader->sendFloat("leafVariantCount", 1.f);
    shader->sendFloat("leafRowOffset", 0.f);
}

void bindFoliage(Shader *shader, const GrassFoliageSettings &s) {
    bindDefaults(shader);
    shader->sendFloat("time", s.snowMinimumHeight);
    shader->sendFloat("frameDuration", s.normalStrength);
    shader->sendFloat("alphaCutoff", s.alphaCutoff);
    shader->sendVec3("lightGreen", s.baseR, s.baseG, s.baseB);
    shader->sendVec3("darkGreen", s.snowR, s.snowG, s.snowB);
    shader->sendFloat("frameCount", -1.f);
    shader->sendFloat("atlasCols", s.renderDistance);
    shader->sendFloat("atlasRows", s.fadeRange);
    shader->sendFloat("grassVariantCount", s.snowFadeDistance);
    shader->sendFloat("leafVariantCount", s.snowProgress);
    shader->sendFloat("leafRowOffset", std::floor(s.hardRenderDistance) + s.density * 0.5f);
}

TerrainDetailQuality terrainDetailQuality(int resolutionPerPatch) noexcept {
    switch (resolutionPerPatch) {
        case 2: return TerrainDetailQuality::Ultra2;
        case 4: return TerrainDetailQuality::VeryHigh4;
        case 8: return TerrainDetailQuality::High8;
        case 16: return TerrainDetailQuality::Medium16;
        case 32: return TerrainDetailQuality::Low32;
        default: return TerrainDetailQuality::VeryLow64;
    }
}

Result<int> applyTerrainDetailOverwrite(GrassFoliageSettings &foliage,
                                        const TerrainDetailOverwriteSettings &settings) {
    if (!std::isfinite(settings.pcgDetailDistance) || !std::isfinite(settings.pcgFadeoutDistance) ||
        settings.unityDetailDistance < 0 || settings.unityDetailDistance > 8388607 ||
        !std::isfinite(settings.unityDetailDensity) ||
        settings.unityDetailDensity < 0 || settings.unityDetailDensity > 1)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "grass.detailOverwrite: valid distance and density required"));
    auto candidate = foliage;
    candidate.renderDistance = std::max(settings.pcgDetailDistance, 0.f);
    candidate.fadeRange = std::max(settings.pcgFadeoutDistance, 0.f);
    candidate.hardRenderDistance = float(settings.unityDetailDistance);
    candidate.density = settings.unityDetailDensity;
    foliage = candidate;
    return Result<int>::success(static_cast<int>(terrainDetailQuality(settings.detailResolutionPerPatch)));
}

}  // namespace eve::graphics::grass
