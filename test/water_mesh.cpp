#include <cmath>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "graphics/WaterStyleConfig.h"
#include "graphics/WaterSystem.h"
#include "graphics/WaterUnderwaterEffects.h"
#include "image/ImageData.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// water_mesh.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterMesh.pcgPlaneAndCircleCounts") {
    WaterMeshSettings plane;
    plane.sizeX         = 8.0F;
    plane.sizeZ         = 4.0F;
    plane.densityX      = 40.0F;
    plane.densityY      = 40.0F;
    auto planeTriangles = calculateWaterMeshTriangles(plane);
    REQUIRE(static_cast<bool>(planeTriangles));
    CHECK(planeTriangles.value() == 64);

    WaterMeshSettings circle;
    circle.type          = WaterMeshType::Circle;
    circle.sizeX         = 8.0F;
    circle.sizeZ         = 8.0F;
    circle.densityX      = 40.0F;
    circle.densityY      = 40.0F;
    auto circleTriangles = calculateWaterMeshTriangles(circle);
    REQUIRE(static_cast<bool>(circleTriangles));
    CHECK(circleTriangles.value() == 96);
}

TEST_CASE("graphics.WaterMesh.rejectsInvalidAndExcessiveSettings") {
    WaterMeshSettings settings;
    settings.sizeX = 0.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 100000.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 8.0F;
    settings.type  = static_cast<WaterMeshType>(9);
    CHECK(!calculateWaterMeshTriangles(settings));
}

TEST_CASE("graphics.WaterGradient.bakesAndPreservesTextureOnFailure") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings windowSettings;
    windowSettings.width  = 64;
    windowSettings.height = 64;
    REQUIRE(window->setWindowSettings(windowSettings));
    Water* water = graphics->newWater();
    REQUIRE(water != nullptr);
    WaterDepthGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addColorStop(0.0F, 0.05F, 0.5F, 0.7F)));
    REQUIRE(static_cast<bool>(gradient.addColorStop(1.0F, 0.0F, 0.04F, 0.12F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(0.0F, 0.35F)));
    REQUIRE(static_cast<bool>(gradient.addAlphaStop(1.0F, 1.0F)));
    auto baked = water->setDepthGradient(gradient, 8);
    REQUIRE(static_cast<bool>(baked));
    CHECK(baked.value() == 64);
    auto* published = water->getDepthGradientTexture();
    REQUIRE(published != nullptr);
    CHECK(published->getWidth() == 8);
    CHECK(published->getHeight() == 8);
    auto failed = water->setDepthGradient(gradient, 1);
    CHECK(!failed);
    CHECK(water->getDepthGradientTexture() == published);
    delete water;
    window->close();
}

// water_planar_reflection.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterPlanarReflection.pcgMirrorResolutionMaskAndDistances") {
    WaterPlanarReflectionSettings settings;
    settings.textureResolution    = 600;
    settings.resolutionMultiplier = WaterReflectionResolution::Third;
    settings.clipPlaneOffset      = 0.2F;
    settings.reflectLayers        = 0xffffffffU;
    settings.shadows              = true;
    settings.enableRenderDistance = true;
    settings.customRenderDistance = 275.0F;

    WaterPlanarReflectionInput input;
    input.cameraPosition = {3.0F, 10.0F, -4.0F};
    input.cameraForward  = {0.0F, -0.6F, -0.8F};
    input.waterPlaneY    = 2.0F;
    input.renderScale    = 0.8F;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    const auto& plan = result.value();
    CHECK(plan.shouldRender);
    CHECK(plan.renderShadows);
    CHECK(plan.textureWidth == 158);  // Pcg truncates 600 * 0.8 * 0.33.
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
    settings.enableRenderDistance     = true;
    settings.enablePerLayerDistances  = true;
    settings.customRenderDistances[7] = 91.0F;
    WaterPlanarReflectionInput input;
    input.orthographic = true;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    CHECK(!result.value().shouldRender);
    CHECK(result.value().layerCullDistances[7] == 91.0F);
    CHECK(result.value().layerCullDistances[6] == 0.0F);

    settings.customRenderDistances[4] = std::numeric_limits<float>::quiet_NaN();
    auto invalid                      = buildWaterPlanarReflectionPlan(settings, input);
    CHECK(!invalid);
}

// water_reflection_masker.cpp
using namespace eve::graphics;

TEST_CASE("graphics.waterReflectionMasker.samplesPcgRoundedCoordinatesAndChannels") {
    eve::image::ImageData mask(2, 2, "RGBA8");
    mask.setPixel(0, 0, {.1f, .2f, .3f, .4f});
    mask.setPixel(1, 0, {.6f, .1f, .1f, .1f});
    mask.setPixel(0, 1, {.1f, .7f, .1f, .1f});
    mask.setPixel(1, 1, {.1f, .1f, .8f, .1f});
    WaterReflectionMasker         masker;
    WaterPlanarReflectionSettings settings;
    settings.enabled = false;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R, .5f, .7f, false).ok());
    REQUIRE(masker.setMask(mask).ok());
    auto enabled = masker.evaluate(25, 10, 0, 0, 100, 100, settings);
    REQUIRE(enabled.ok());
    CHECK_EQ(enabled.value(), WaterReflectionMaskTransition::Enabled);
    CHECK(settings.enabled);
    CHECK_EQ(masker.getSampleX(), 1);
    CHECK_EQ(masker.getSampleY(), 0);
    auto disabled = masker.evaluate(75, 75, 0, 0, 100, 100, settings);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value(), WaterReflectionMaskTransition::Disabled);
    CHECK(!settings.enabled);
    REQUIRE(masker.configure(WaterReflectionMaskChannel::RGBA, .75f, .85f, false).ok());
    REQUIRE(masker.evaluate(75, 75, 0, 0, 100, 100, settings).ok());
    CHECK(settings.enabled);
}

TEST_CASE("graphics.waterReflectionMasker.handlesMissingEdgesAndFailureAtomically") {
    eve::image::ImageData mask(1, 1, "RGBA8");
    mask.setPixel(0, 0, {1, 0, 0, 1});
    WaterReflectionMasker         masker;
    WaterPlanarReflectionSettings settings;
    settings.enabled = true;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R, .9f, 1.f, true).ok());
    REQUIRE(masker.setMask(mask).ok());
    auto unchanged = masker.evaluate(0, 0, 0, 0, 100, 100, settings);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), WaterReflectionMaskTransition::Unchanged);
    auto edge = masker.evaluate(100, 100, 0, 0, 100, 100, settings);
    REQUIRE(edge.ok());
    CHECK_EQ(edge.value(), WaterReflectionMaskTransition::Disabled);
    CHECK_EQ(masker.getSampleX(), 1);
    CHECK(!masker.evaluate(0, 0, 0, 0, -1, 1, settings).ok());
    CHECK(!settings.enabled);
    CHECK(!masker.configure(WaterReflectionMaskChannel::R, 2, 1, false).ok());
}

TEST_CASE("graphics.waterReflectionMasker.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeWaterPlanarReflectionBindings(table);
    exposeWaterReflectionMaskerBindings(table);
    vm.run(vm.compileSource(R"(
      local m=eve.WaterReflectionMasker();local s=eve.WaterPlanarReflectionSettings();s.enabled=true;
      assert(m.configure(0,0.35,1.0,true).ok);m.clearMask();
      local r=m.evaluate(0.0,0.0,0.0,0.0,100.0,100.0,s);assert(r.ok && r.value==2);
      assert(!m.getEnabled() && !m.getHeightFeaturesEnabled() && !s.enabled);
    )"));
}

// water_style_config.cpp
using eve::DiagnosticCode;
using eve::graphics::WaterStyleConfig;

TEST_CASE("graphics.waterStyleConfig.roundTrip") {
    WaterStyleConfig authored;
    authored.deepColor                     = {0.01F, 0.08F, 0.18F};
    authored.shallowColor                  = {0.18F, 0.72F, 0.64F};
    authored.waveSharpness                 = 2.25F;
    authored.depthDistance                 = 7.5F;
    authored.foamStrength                  = 1.2F;
    authored.screenSpaceReflection         = true;
    authored.screenSpaceReflectionStrength = 0.7F;
    authored.refractionStrength            = 0.04F;
    authored.causticsStrength              = 0.65F;
    authored.causticsScale                 = 4.5F;

    auto json = authored.toJson();
    REQUIRE(static_cast<bool>(json));
    auto decoded = WaterStyleConfig::fromJson(json.value());
    REQUIRE(static_cast<bool>(decoded));

    CHECK(std::abs(decoded.value().deepColor.z - 0.18F) < 0.0001F);
    CHECK(std::abs(decoded.value().shallowColor.y - 0.72F) < 0.0001F);
    CHECK(std::abs(decoded.value().waveSharpness - 2.25F) < 0.0001F);
    CHECK(std::abs(decoded.value().depthDistance - 7.5F) < 0.0001F);
    CHECK(std::abs(decoded.value().foamStrength - 1.2F) < 0.0001F);
    CHECK(decoded.value().screenSpaceReflection);
    CHECK(std::abs(decoded.value().refractionStrength - 0.04F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsStrength - 0.65F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsScale - 4.5F) < 0.0001F);
}

TEST_CASE("graphics.waterStyleConfig.rejectsUnknownFields") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root   = encoded.value().getIf<eve::Value::Object>();
    auto* colors = root->at("colors").getIf<eve::Value::Object>();
    colors->emplace("surprise", 1.0);
    auto decoded = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!decoded);
    REQUIRE(decoded.error() != nullptr);
    CHECK(decoded.error()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("graphics.waterStyleConfig.rejectsFutureVersionsAndInvalidRanges") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root                = encoded.value().getIf<eve::Value::Object>();
    root->at("schemaVersion") = static_cast<std::int64_t>(WaterStyleConfig::SchemaVersion + 1);
    auto future               = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!future);
    REQUIRE(future.error() != nullptr);
    CHECK(future.error()->code() == DiagnosticCode::UnknownVersion);

    config.depthDistance = 0.0F;
    auto invalid         = config.validate();
    CHECK(!invalid);
}

// water_system.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterSystem.infiniteIntervalAndSeaLevel") {
    WaterSystemState     state;
    WaterSystemSettings  settings;
    WaterSceneConditions scene;
    auto                 initialized = initializeWaterSystem(state, settings, scene, 7);
    REQUIRE(static_cast<bool>(initialized));

    auto first = advanceWaterSystem(state, settings, scene, 4.0F, -3.0F, 0.25F, 9);
    REQUIRE(static_cast<bool>(first));
    CHECK(!first.value());
    CHECK(state.positionX == 4.0F);
    CHECK(state.positionY == 25.0F);
    CHECK(state.positionZ == -3.0F);

    auto second = advanceWaterSystem(state, settings, scene, 8.0F, 2.0F, 0.3F, 9);
    REQUIRE(static_cast<bool>(second));
    CHECK(second.value());
    CHECK(state.refreshRevision == 1);

    auto level = updateWaterSeaLevel(state, 31.5F, true);
    REQUIRE(static_cast<bool>(level));
    CHECK(level.value());
    CHECK(state.positionY == 31.5F);
    CHECK(state.refreshRevision == 2);
}

TEST_CASE("graphics.WaterSystem.sceneConditionsAndAtomicFailure") {
    WaterSystemState    state;
    WaterSystemSettings settings;
    settings.autoUpdateMode        = WaterAutoUpdateMode::SceneConditions;
    settings.ignoreSceneConditions = false;
    WaterSceneConditions scene;
    auto                 initialized = initializeWaterSystem(state, settings, scene, 0);
    REQUIRE(static_cast<bool>(initialized));

    WaterSceneConditions changedScene = scene;
    changedScene.sunIntensity         = 2.0F;
    auto refresh                      = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, 0.0F, 8);
    REQUIRE(static_cast<bool>(refresh));
    CHECK(refresh.value());
    CHECK(state.sceneCheckFrames == 7);

    const WaterSystemState before  = state;
    auto                   invalid = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, -1.0F, 8);
    CHECK(!invalid);
    CHECK(state.refreshRevision == before.refreshRevision);
    CHECK(state.positionX == before.positionX);
    CHECK(state.sceneCheckFrames == before.sceneCheckFrames);
}

// water_underwater_effects.cpp
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
    constexpr int                          zoneTag   = 8101;
    constexpr int                          playerTag = 8102;
    auto*                                  physics   = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.0F, 0.0F, 0.0F, false));
    auto*                                  zone   = world->newBody("static", 0.0F, 0.0F, 0.0F);
    auto*                                  sensor = zone->newBoxShape(4.0F, 4.0F, 4.0F);
    sensor->setTag(zoneTag);
    sensor->setSensor(true);
    auto* player = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    player->newSphereShape(0.5F)->setTag(playerTag);

    WaterUnderwaterDisableTriggerState state;
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    WaterUnderwaterTriggerEvent enter{world->getBeginTriggerSensorShapeTag(0), world->getBeginTriggerVisitorShapeTag(0),
                                      true, false};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, enter, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    const auto                  before = state;
    WaterUnderwaterTriggerEvent malformed{zoneTag, playerTag, true, true};
    CHECK(!advanceWaterUnderwaterDisableTrigger(state, malformed, zoneTag, playerTag));
    CHECK(state.effectsEnabled == before.effectsEnabled);
    WaterUnderwaterTriggerEvent unrelated{zoneTag, 9999, false, true};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, unrelated, zoneTag, playerTag)));
    CHECK(!state.effectsEnabled);

    player->setPosition(10.0F, 0.0F, 0.0F);
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    WaterUnderwaterTriggerEvent exit{world->getEndTriggerSensorShapeTag(0), world->getEndTriggerVisitorShapeTag(0),
                                     false, true};
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterDisableTrigger(state, exit, zoneTag, playerTag)));
    CHECK(state.effectsEnabled);
}

TEST_CASE("graphics.WaterUnderwater.transitionsFogCausticsAndSurfaceRestore") {
    WaterUnderwaterSettings settings;
    settings.causticTextureCount = 16;
    settings.fogColorMultiplier  = {0.1F, -1.0F, 0.0F};
    WaterUnderwaterInput input;
    input.seaLevel       = 50.0F;
    input.cameraY        = 60.0F;
    input.mainLightColor = {0.7F, 0.8F, 0.9F};
    WaterUnderwaterState  state;
    WaterUnderwaterOutput output;
    const auto            depth     = depthGradient();
    const auto            time      = timeGradient();
    const auto            exposure  = exposureCurve();
    const auto            postColor = timeGradient();

    WaterUnderwaterState  transitionState;
    WaterUnderwaterOutput transitionOutput;
    WaterUnderwaterInput  transitionInput = input;
    transitionInput.cameraY               = 50.04F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(transitionState, transitionOutput, settings,
                                                            transitionInput, depth, time, exposure, postColor)));
    transitionInput.cameraY = 49.96F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(transitionState, transitionOutput, settings,
                                                            transitionInput, depth, time, exposure, postColor)));
    CHECK(transitionOutput.entered);
    CHECK(std::abs(transitionOutput.transitionWeight - 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignette - 0.1F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignetteSmoothness - 0.44F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensDistortion - 0.1008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensScale - 1.008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLift.r - settings.transitionLift.r * 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionInverseGamma.r -
                   (1.0F + (settings.transitionInverseGamma.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionGain.r - (1.0F + (settings.transitionGain.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionColorFilter.r - 0.6F) < 0.0002F);

    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(!output.isUnderwater);
    CHECK(output.surfaceVfx);

    input.cameraY      = 25.0F;
    input.causticTicks = 18;
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
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

    input.cameraY      = 51.0F;
    input.causticTicks = 0;
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.exited);
    CHECK(output.playSubmergeUp);
    CHECK(!output.loopAudio);
    CHECK(output.surfaceVfx);
    CHECK(state.causticFrame == 0);
}

TEST_CASE("graphics.WaterUnderwater.hdrpOverrideAndAtomicFailure") {
    WaterUnderwaterSettings settings;
    settings.hdrp                  = true;
    settings.overrideFogColor      = true;
    settings.timeDrivenPostFx      = true;
    settings.overrideFogMultiplier = {0.5F, 0.8F, 1.0F};
    settings.overrideFogCurve      = 0.75F;
    WaterUnderwaterInput input;
    input.seaLevel     = 100.0F;
    input.cameraY      = 0.0F;
    input.timeOfDay    = 0.5F;
    input.hasMainLight = false;
    WaterUnderwaterState  state;
    WaterUnderwaterOutput output;
    const auto            depth     = depthGradient();
    const auto            time      = timeGradient();
    const auto            exposure  = exposureCurve();
    const auto            postColor = timeGradient();

    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
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

    const WaterUnderwaterState  beforeState  = state;
    const WaterUnderwaterOutput beforeOutput = output;
    input.cameraY                            = std::numeric_limits<float>::quiet_NaN();
    auto invalid = advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor);
    CHECK(!invalid);
    CHECK(state.isUnderwater == beforeState.isUnderwater);
    CHECK(output.hdrpBaseHeight == beforeOutput.hdrpBaseHeight);
}

TEST_CASE("graphics.WaterUnderwaterFog.appliesAndRestoresVolumetricProvider") {
    eve::window::Window* window   = nullptr;
    Graphics*            graphics = nullptr;
    openGfxWindow(window, graphics);
    auto* volumetric = graphics->newVolumetric();
    REQUIRE(volumetric != nullptr);

    WaterUnderwaterOutput output;
    output.isUnderwater = true;
    output.fogColor     = {0.12F, 0.32F, 0.48F};
    output.fogDensity   = 0.045F;
    output.fogStart     = -4.0F;
    output.fogEnd       = 45.0F;
    output.fogHeight    = 12.0F;
    WaterSurfaceFogSnapshot surface;
    surface.color   = {0.5F, 0.6F, 0.7F};
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

    auto* camera                        = Camera3D::createCamera();
    output.isUnderwater                 = true;
    output.postFx                       = true;
    output.postExposure                 = -1.25F;
    output.postColor                    = {0.2F, 0.6F, 0.8F};
    output.transitionFx                 = true;
    output.transitionVignette           = 0.25F;
    output.transitionVignetteSmoothness = 0.8F;
    output.transitionLensDistortion     = 0.252F;
    output.transitionLensScale          = 1.02F;
    output.transitionLift               = {-0.02F, -0.01F, 0.005F};
    output.transitionInverseGamma       = {1.35F, 1.36F, 1.27F};
    output.transitionGain               = {0.67F, 0.69F, 0.72F};
    output.transitionColorFilter        = {0.6F, 0.6F, 0.6F};
    WaterSurfacePostFxSnapshot surfacePostFx;
    surfacePostFx.exposureEv = 0.4F;
    surfacePostFx.color      = {0.9F, 0.8F, 0.7F};
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

    auto* horizon  = Renderable3D::create();
    output.horizon = true;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(horizon->getVisible());
    output.horizon = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterHorizon(horizon, output)));
    CHECK(!horizon->getVisible());

    WaterSurfaceMaterialSnapshot surfaceMaterial;
    surfaceMaterial.color          = {0.12F, 0.34F, 0.56F};
    surfaceMaterial.alpha          = 0.8F;
    output.isUnderwater            = true;
    output.underwaterMaterialColor = {0.7F, 0.8F, 0.9F};
    REQUIRE(static_cast<bool>(applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.7F);
    CHECK(horizon->getTintG() == 0.8F);
    output.isUnderwater = false;
    REQUIRE(static_cast<bool>(applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial)));
    CHECK(horizon->getTintR() == 0.12F);
    CHECK(horizon->getTintB() == 0.56F);
    surfaceMaterial.alpha = 2.0F;
    CHECK(!applyWaterUnderwaterMaterial(horizon, output, surfaceMaterial));
    CHECK(horizon->getTintR() == 0.12F);

    auto* cookie = graphics->newCanvas(8, 8);
    REQUIRE(cookie != nullptr);
    CHECK(static_cast<bool>(
        volumetric->projectDirectionalCookie(graphics, cookie->getTexture(), cookie->getTexture(), 15.0F, 0.5F)));
    CHECK(!volumetric->projectDirectionalCookie(graphics, cookie->getTexture(), cookie->getTexture(), 0.0F, 0.5F));

    surface.end = surface.start;
    CHECK(!applyWaterUnderwaterFog(volumetric, output, surface));
    CHECK(volumetric->getFloat("density") == 0.006F);
    delete volumetric;
    window->close();
}
