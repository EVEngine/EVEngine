#include "zeroerr/unittest.h"

#include "graphics/WaterPlanarReflection.h"

#include <cmath>
#include <limits>

using namespace eve::graphics;

TEST_CASE("graphics.WaterPlanarReflection.pcgMirrorResolutionMaskAndDistances") {
    WaterPlanarReflectionSettings settings;
    settings.textureResolution = 600;
    settings.resolutionMultiplier = WaterReflectionResolution::Third;
    settings.clipPlaneOffset = 0.2F;
    settings.reflectLayers = 0xffffffffU;
    settings.shadows = true;
    settings.enableRenderDistance = true;
    settings.customRenderDistance = 275.0F;

    WaterPlanarReflectionInput input;
    input.cameraPosition = {3.0F, 10.0F, -4.0F};
    input.cameraForward = {0.0F, -0.6F, -0.8F};
    input.waterPlaneY = 2.0F;
    input.renderScale = 0.8F;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    const auto& plan = result.value();
    CHECK(plan.shouldRender);
    CHECK(plan.renderShadows);
    CHECK(plan.textureWidth == 158); // Pcg truncates 600 * 0.8 * 0.33.
    CHECK(plan.textureHeight == 158);
    CHECK(std::abs(plan.cameraPosition.y - (-5.6F)) < 0.0001F);
    CHECK(std::abs(plan.cameraForward.y - 0.6F) < 0.0001F);
    CHECK(plan.reflectionMatrix[1][1] == -1.0F);
    CHECK(std::abs(plan.reflectionMatrix[3][1] - 4.4F) < 0.0001F);
    CHECK((plan.cullingMask & (1U << 4U)) == 0U);
    CHECK(plan.layerCullDistances[0] == 275.0F);
    CHECK(plan.layerCullDistances[31] == 275.0F);
}

TEST_CASE("graphics.WaterPlanarReflection.skipRulesPerLayerAndInvalidInput") {
    WaterPlanarReflectionSettings settings;
    settings.enableRenderDistance = true;
    settings.enablePerLayerDistances = true;
    settings.customRenderDistances[7] = 91.0F;
    WaterPlanarReflectionInput input;
    input.orthographic = true;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    CHECK(!result.value().shouldRender);
    CHECK(result.value().layerCullDistances[7] == 91.0F);
    CHECK(result.value().layerCullDistances[6] == 0.0F);

    settings.customRenderDistances[4] = std::numeric_limits<float>::quiet_NaN();
    auto invalid = buildWaterPlanarReflectionPlan(settings, input);
    CHECK(!invalid);
}
