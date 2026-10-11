#include "graphics/WaterPlanarReflection.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {
namespace {
bool finite3(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float resolutionScale(WaterReflectionResolution value) {
    switch (value) {
        case WaterReflectionResolution::Full: return 1.0F;
        case WaterReflectionResolution::Half: return 0.5F;
        case WaterReflectionResolution::Third: return 0.33F;
        case WaterReflectionResolution::Quarter: return 0.25F;
    }
    return 0.0F;
}
}  // namespace

Result<WaterPlanarReflectionPlan> buildWaterPlanarReflectionPlan(
    const WaterPlanarReflectionSettings& settings, const WaterPlanarReflectionInput& input) {
    const float multiplier = resolutionScale(settings.resolutionMultiplier);
    if (multiplier == 0.0F || settings.textureResolution <= 0 || settings.textureResolution > 16384 ||
        !std::isfinite(settings.clipPlaneOffset) || !std::isfinite(settings.customRenderDistance) ||
        settings.customRenderDistance < 0.0F || !std::isfinite(settings.lodBias) || settings.lodBias <= 0.0F || !finite3(input.cameraPosition) || !finite3(input.cameraForward) ||
        glm::dot(input.cameraForward, input.cameraForward) <= 0.0F || !std::isfinite(input.waterPlaneY) ||
        !std::isfinite(input.renderScale) || input.renderScale <= 0.0F) {
        return Result<WaterPlanarReflectionPlan>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.planarReflection: finite camera, plane, scale and valid settings required"));
    }
    for (float distance : settings.customRenderDistances) {
        if (!std::isfinite(distance) || distance < 0.0F)
            return Result<WaterPlanarReflectionPlan>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "water.planarReflection: layer distances must be finite and nonnegative"));
    }

    WaterPlanarReflectionPlan plan;
    const float planeY = input.waterPlaneY + settings.clipPlaneOffset;
    plan.reflectionMatrix = glm::mat4(1.0F);
    plan.reflectionMatrix[1][1] = -1.0F;
    plan.reflectionMatrix[3][1] = 2.0F * planeY;
    plan.cameraPosition = {input.cameraPosition.x, 2.0F * planeY - input.cameraPosition.y,
                           input.cameraPosition.z};
    plan.cameraForward = glm::normalize(glm::vec3(input.cameraForward.x, -input.cameraForward.y,
                                                   input.cameraForward.z));
    plan.worldClipPlane = {0.0F, 1.0F, 0.0F, -planeY};
    plan.cullingMask = settings.enabled ? (settings.reflectLayers & ~(1U << 4U)) : 0U;
    plan.renderShadows = settings.shadows;
    plan.shouldRender = settings.enabled && !settings.disableSkyboxReflections && !input.orthographic &&
                        !input.reflectionOrPreviewCamera;
    const double pixels = static_cast<double>(settings.textureResolution) * input.renderScale * multiplier;
    plan.textureWidth = plan.textureHeight = std::max(1, static_cast<int>(pixels));
    if (settings.enableRenderDistance) {
        if (settings.enablePerLayerDistances) {
            for (std::size_t i=0;i<plan.layerCullDistances.size();++i)
                plan.layerCullDistances[i]=settings.customRenderDistances[i]*settings.lodBias;
        } else plan.layerCullDistances.fill(settings.customRenderDistance*settings.lodBias);
    }
    return Result<WaterPlanarReflectionPlan>::success(plan);
}

}  // namespace eve::graphics
