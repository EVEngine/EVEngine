#include "graphics/WaterPlanarReflection.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {

void exposeWaterPlanarReflectionBindings(ssq::Table& table) {
    auto settings = table.addClass("WaterPlanarReflectionSettings",
                                   ssq::Class::Ctor<WaterPlanarReflectionSettings()>());
    settings.addVar("enabled", &WaterPlanarReflectionSettings::enabled);
    settings.addVar("disableSkyboxReflections", &WaterPlanarReflectionSettings::disableSkyboxReflections);
    settings.addVar("clipPlaneOffset", &WaterPlanarReflectionSettings::clipPlaneOffset);
    settings.addVar("reflectLayers", &WaterPlanarReflectionSettings::reflectLayers);
    settings.addVar("shadows", &WaterPlanarReflectionSettings::shadows);
    settings.addVar("enableRenderDistance", &WaterPlanarReflectionSettings::enableRenderDistance);
    settings.addVar("enablePerLayerDistances", &WaterPlanarReflectionSettings::enablePerLayerDistances);
    settings.addVar("customRenderDistance", &WaterPlanarReflectionSettings::customRenderDistance);
    settings.addVar("lodBias", &WaterPlanarReflectionSettings::lodBias);
    settings.addVar("textureResolution", &WaterPlanarReflectionSettings::textureResolution);
    settings.addFunc("setResolutionMultiplier", [](WaterPlanarReflectionSettings* value, int multiplier) {
        value->resolutionMultiplier = static_cast<WaterReflectionResolution>(multiplier);
    });
    settings.addFunc("setLayerDistance", [](WaterPlanarReflectionSettings* value, int layer, float distance) {
        if (value && layer >= 0 && layer < 32) value->customRenderDistances[static_cast<std::size_t>(layer)] = distance;
    });

    auto input = table.addClass("WaterPlanarReflectionInput", ssq::Class::Ctor<WaterPlanarReflectionInput()>());
    input.addVar("waterPlaneY", &WaterPlanarReflectionInput::waterPlaneY);
    input.addVar("renderScale", &WaterPlanarReflectionInput::renderScale);
    input.addVar("orthographic", &WaterPlanarReflectionInput::orthographic);
    input.addVar("reflectionOrPreviewCamera", &WaterPlanarReflectionInput::reflectionOrPreviewCamera);
    input.addFunc("setCameraPosition", [](WaterPlanarReflectionInput* value, float x, float y, float z) {
        value->cameraPosition = {x, y, z};
    });
    input.addFunc("setCameraForward", [](WaterPlanarReflectionInput* value, float x, float y, float z) {
        value->cameraForward = {x, y, z};
    });

    auto plan = table.addClass("WaterPlanarReflectionPlan", ssq::Class::Ctor<WaterPlanarReflectionPlan()>());
    plan.addVar("textureWidth", &WaterPlanarReflectionPlan::textureWidth);
    plan.addVar("textureHeight", &WaterPlanarReflectionPlan::textureHeight);
    plan.addVar("cullingMask", &WaterPlanarReflectionPlan::cullingMask);
    plan.addVar("renderShadows", &WaterPlanarReflectionPlan::renderShadows);
    plan.addVar("shouldRender", &WaterPlanarReflectionPlan::shouldRender);
    plan.addFunc("getCameraX", [](const WaterPlanarReflectionPlan* value) { return value->cameraPosition.x; });
    plan.addFunc("getCameraY", [](const WaterPlanarReflectionPlan* value) { return value->cameraPosition.y; });
    plan.addFunc("getCameraZ", [](const WaterPlanarReflectionPlan* value) { return value->cameraPosition.z; });
    plan.addFunc("getForwardY", [](const WaterPlanarReflectionPlan* value) { return value->cameraForward.y; });
    plan.addFunc("getLayerDistance", [](const WaterPlanarReflectionPlan* value, int layer) {
        return value && layer >= 0 && layer < 32 ? value->layerCullDistances[static_cast<std::size_t>(layer)] : -1.0F;
    });
    plan.addFunc("getReflectionMatrix", [](const WaterPlanarReflectionPlan* value, int column, int row) {
        return value && column >= 0 && column < 4 && row >= 0 && row < 4
                   ? value->reflectionMatrix[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)]
                   : 0.0F;
    });

    table.addFunc("buildWaterPlanarReflectionPlan",
                  [vm = table.getHandle()](WaterPlanarReflectionPlan* output,
                                           const WaterPlanarReflectionSettings* settings,
                                           const WaterPlanarReflectionInput* input) {
                      if (!output || !settings || !input)
                          return eve::script::projectResult(
                              vm, Result<void>::failure(Diagnostic::error(
                                      DiagnosticCode::InvalidArgument,
                                      "water.planarReflection: non-null output, settings and input required")));
                      auto result = buildWaterPlanarReflectionPlan(*settings, *input);
                      if (!result) return eve::script::projectResult(vm, Result<void>::failure(result.status()));
                      *output = std::move(result).takeValue();
                      return eve::script::projectResult(vm, Result<void>::success());
                  });
}

}  // namespace eve::graphics
