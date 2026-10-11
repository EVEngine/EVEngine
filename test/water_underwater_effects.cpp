#include "zeroerr/unittest.h"

#include "graphics/WaterUnderwaterEffects.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Volumetric.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "Fixtures.h"

#include <cmath>
#include <limits>
#include <memory>

using namespace eve::graphics;

namespace {
UnderwaterColorGradient depthGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.4F, 0.7F, 1.0F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.03F, 0.08F, 0.18F)));
    return gradient;
}
UnderwaterColorGradient timeGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.1F, 0.2F, 0.4F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.8F, 0.5F, 0.2F)));
    return gradient;
}
UnderwaterScalarCurve exposureCurve() {
    UnderwaterScalarCurve curve;
    REQUIRE(static_cast<bool>(curve.addKey(0.0F, -0.5F)));
    REQUIRE(static_cast<bool>(curve.addKey(1.0F, 0.5F)));
    return curve;
}
}  // namespace

TEST_CASE("graphics.WaterUnderwaterDisableTrigger.consumesRealBox3DEvents") {
    constexpr int zoneTag = 8101;
    constexpr int playerTag = 8102;
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(
        physics->newWorld3D(0.0F, 0.0F, 0.0F, false));
    auto* zone = world->newBody("static", 0.0F, 0.0F, 0.0F);
    auto* sensor = zone->newBoxShape(4.0F, 4.0F, 4.0F);
    sensor->setTag(zoneTag);
    sensor->setSensor(true);
    auto* player = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    player->newSphereShape(0.5F)->setTag(playerTag);

    WaterUnderwaterDisableTriggerState state;
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    WaterUnderwaterTriggerEvent enter{world->getBeginTriggerSensorShapeTag(0),
                                      world->getBeginTriggerVisitorShapeTag(0), true, false};
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterDisableTrigger(state, enter, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    const auto before = state;
    WaterUnderwaterTriggerEvent malformed{zoneTag, playerTag, true, true};
    CHECK(!advanceWaterUnderwaterDisableTrigger(state, malformed, zoneTag, playerTag));
    CHECK(state.effectsEnabled == before.effectsEnabled);
    WaterUnderwaterTriggerEvent unrelated{zoneTag, 9999, false, true};
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterDisableTrigger(state, unrelated, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    player->setPosition(10.0F, 0.0F, 0.0F);
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    WaterUnderwaterTriggerEvent exit{world->getEndTriggerSensorShapeTag(0),
                                     world->getEndTriggerVisitorShapeTag(0), false, true};
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterDisableTrigger(state, exit, zoneTag, playerTag)));
    CHECK(state.effectsEnabled);
}

TEST_CASE("graphics.WaterUnderwater.transitionsFogCausticsAndSurfaceRestore") {
    WaterUnderwaterSettings settings;
    settings.causticTextureCount = 16;
    settings.fogColorMultiplier = {0.1F, -1.0F, 0.0F};
    WaterUnderwaterInput input;
    input.seaLevel = 50.0F;
    input.cameraY = 60.0F;
    input.mainLightColor = {0.7F, 0.8F, 0.9F};
    WaterUnderwaterState state;
    WaterUnderwaterOutput output;
    const auto depth = depthGradient();
    const auto time = timeGradient();
    const auto exposure = exposureCurve();
    const auto postColor = timeGradient();

    WaterUnderwaterState transitionState;
    WaterUnderwaterOutput transitionOutput;
    WaterUnderwaterInput transitionInput = input;
    transitionInput.cameraY = 50.04F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(
        transitionState, transitionOutput, settings, transitionInput, depth, time, exposure,
        postColor)));
    transitionInput.cameraY = 49.96F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(
        transitionState, transitionOutput, settings, transitionInput, depth, time, exposure,
        postColor)));
    CHECK(transitionOutput.entered);
    CHECK(std::abs(transitionOutput.transitionWeight - 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignette - 0.1F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignetteSmoothness - 0.44F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensDistortion - 0.1008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensScale - 1.008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLift.r - settings.transitionLift.r * 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionInverseGamma.r -
                   (1.0F + (settings.transitionInverseGamma.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionGain.r -
                   (1.0F + (settings.transitionGain.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionColorFilter.r - 0.6F) < 0.0002F);

    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(!output.isUnderwater);
    CHECK(output.surfaceVfx);

    input.cameraY = 25.0F;
    input.causticTicks = 18;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.entered);
    CHECK(output.playSubmergeDown);
    CHECK(output.loopAudio);
    CHECK(output.particles);
    CHECK(output.horizon);
    CHECK(output.causticFrame == 2);
    CHECK(std::abs(output.depth01 - 0.25F) < 0.0001F);
    CHECK(std::abs(output.fogColor.r - 0.4075F) < 0.0001F);
    CHECK(std::abs(output.fogColor.g - 0.545F) < 0.0001F);
    CHECK(output.underwaterMaterialColor == input.mainLightColor);

    input.cameraY = 51.0F;
    input.causticTicks = 0;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.exited);
    CHECK(output.playSubmergeUp);
    CHECK(!output.loopAudio);
    CHECK(output.surfaceVfx);
    CHECK(state.causticFrame == 0);
}

TEST_CASE("graphics.WaterUnderwater.hdrpOverrideAndAtomicFailure") {
    WaterUnderwaterSettings settings;
    settings.hdrp = true;
    settings.overrideFogColor = true;
    settings.timeDrivenPostFx = true;
    settings.overrideFogMultiplier = {0.5F, 0.8F, 1.0F};
    settings.overrideFogCurve = 0.75F;
    WaterUnderwaterInput input;
    input.seaLevel = 100.0F;
    input.cameraY = 0.0F;
    input.timeOfDay = 0.5F;
    input.hasMainLight = false;
    WaterUnderwaterState state;
    WaterUnderwaterOutput output;
    const auto depth = depthGradient();
    const auto time = timeGradient();
    const auto exposure = exposureCurve();
    const auto postColor = timeGradient();

    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.isUnderwater);
    CHECK(!output.horizon);
    CHECK(output.hdrpBaseHeight == 6000.0F);
    CHECK(output.hdrpMeanFreePath == 1.0F);
    CHECK(output.hdrpProbeDimmer == 0.325F);
    CHECK(output.hdrpDepthExtent == 300.0F);
    CHECK(std::abs(output.fogColor.r - 0.028125F) < 0.0001F);
    CHECK(std::abs(output.postExposure) < 0.0001F);
    CHECK(std::abs(output.postColor.r - 0.45F) < 0.0001F);
    CHECK(std::abs(output.underwaterMaterialColor.r - 207.0F / 255.0F) < 0.0001F);

    const WaterUnderwaterState beforeState = state;
    const WaterUnderwaterOutput beforeOutput = output;
    input.cameraY = std::numeric_limits<float>::quiet_NaN();
    auto invalid = advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor);
    CHECK(!invalid);
    CHECK(state.isUnderwater == beforeState.isUnderwater);
    CHECK(output.hdrpBaseHeight == beforeOutput.hdrpBaseHeight);
}

TEST_CASE("graphics.WaterUnderwaterFog.appliesAndRestoresVolumetricProvider") {
    eve::window::Window* window = nullptr;
    Graphics* graphics = nullptr;
    openGfxWindow(window, graphics);
    auto* volumetric = graphics->newVolumetric();
    REQUIRE(volumetric != nullptr);

    WaterUnderwaterOutput output;
    output.isUnderwater = true;
    output.fogColor = {0.12F, 0.32F, 0.48F};
    output.fogDensity = 0.045F;
    output.fogStart = -4.0F;
    output.fogEnd = 45.0F;
    output.fogHeight = 12.0F;
    WaterSurfaceFogSnapshot surface;
    surface.color = {0.5F, 0.6F, 0.7F};
    surface.density = 0.006F;

    REQUIRE(static_cast<bool>(applyWaterUnderwaterFog(volumetric, output, surface)));
    CHECK(volumetric->getMode() == "fog");
    CHECK(volumetric->getFloat("fogR") == 0.12F);
    CHECK(volumetric->getFloat("fogG") == 0.32F);
    CHECK(volumetric->getFloat("density") == 0.045F);

    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterFog(volumetric, output, surface)));
    CHECK(volumetric->getFloat("fogR") == 0.5F);
    CHECK(volumetric->getFloat("fogB") == 0.7F);
    CHECK(volumetric->getFloat("density") == 0.006F);

    auto* camera = Camera3D::createCamera();
    output.isUnderwater = true;
    output.postFx = true;
    output.postExposure = -1.25F;
    output.postColor = {0.2F, 0.6F, 0.8F};
    output.transitionFx = true;
    output.transitionVignette = 0.25F;
    output.transitionVignetteSmoothness = 0.8F;
    output.transitionLensDistortion = 0.252F;
    output.transitionLensScale = 1.02F;
    output.transitionLift = {-0.02F, -0.01F, 0.005F};
    output.transitionInverseGamma = {1.35F, 1.36F, 1.27F};
    output.transitionGain = {0.67F, 0.69F, 0.72F};
    output.transitionColorFilter = {0.6F, 0.6F, 0.6F};
    WaterSurfacePostFxSnapshot surfacePostFx;
    surfacePostFx.exposureEv = 0.4F;
    surfacePostFx.color = {0.9F, 0.8F, 0.7F};
    REQUIRE(static_cast<bool>(applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx)));
    CHECK(camera->getExposure() == -1.25F);
    CHECK(graphics->getSceneColorFilter() == glm::vec3(0.12F, 0.36F, 0.48F));
    CHECK(graphics->getSceneTransitionVignette() == 0.25F);
    CHECK(graphics->getSceneTransitionVignetteSmoothness() == 0.8F);
    CHECK(graphics->getSceneTransitionLensDistortion() == 0.252F);
    CHECK(graphics->getSceneTransitionLensScale() == 1.02F);
    CHECK(graphics->getSceneLift() == output.transitionLift);
    CHECK(graphics->getSceneInverseGamma() == output.transitionInverseGamma);
    CHECK(graphics->getSceneGain() == output.transitionGain);
    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx)));
    CHECK(camera->getExposure() == 0.4F);
    CHECK(graphics->getSceneColorFilter() == surfacePostFx.color);
    CHECK(graphics->getSceneTransitionVignette() == 0.0F);
    CHECK(graphics->getSceneTransitionVignetteSmoothness() == 0.2F);
    CHECK(graphics->getSceneTransitionLensDistortion() == 0.0F);
    CHECK(graphics->getSceneTransitionLensScale() == 1.0F);
    CHECK(graphics->getSceneLift() == glm::vec3(0.0F));
    CHECK(graphics->getSceneInverseGamma() == glm::vec3(1.0F));
    CHECK(graphics->getSceneGain() == glm::vec3(1.0F));

    surfacePostFx.color.r = -1.0F;
    CHECK(!applyWaterUnderwaterPostFx(graphics, camera, output, surfacePostFx));
    CHECK(camera->getExposure() == 0.4F);
    CHECK(graphics->getSceneColorFilter() == glm::vec3(0.9F, 0.8F, 0.7F));

    auto* horizon = Renderable3D::create();
    output.horizon = true;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(horizon->getVisible());
    output.horizon = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(!horizon->getVisible());

    WaterSurfaceMaterialSnapshot surfaceMaterial;
    surfaceMaterial.color = {0.12F, 0.34F, 0.56F};
    surfaceMaterial.alpha = 0.8F;
    output.isUnderwater = true;
    output.underwaterMaterialColor = {0.7F, 0.8F, 0.9F};
    REQUIRE(static_cast<bool>(
        applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.7F);
    CHECK(horizon->getTintG() == 0.8F);
    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(
        applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.12F);
    CHECK(horizon->getTintB() == 0.56F);
    surfaceMaterial.alpha = 2.0F;
    CHECK(!applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial));
    CHECK(horizon->getTintR() == 0.12F);

    auto* cookie = graphics->newCanvas(8, 8);
    REQUIRE(cookie != nullptr);
    CHECK(static_cast<bool>(volumetric->projectDirectionalCookie(
        graphics, cookie->getTexture(), cookie->getTexture(), 15.0F, 0.5F)));
    CHECK(!volumetric->projectDirectionalCookie(graphics, cookie->getTexture(),
                                                 cookie->getTexture(), 0.0F, 0.5F));

    surface.end = surface.start;
    CHECK(!applyWaterUnderwaterFog(volumetric, output, surface));
    CHECK(volumetric->getFloat("density") == 0.006F);
    delete volumetric;
    window->close();
}
