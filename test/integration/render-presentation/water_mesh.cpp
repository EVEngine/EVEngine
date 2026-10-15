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


// water_reflection_masker.cpp
using namespace eve::graphics;


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


// water_system.cpp
using namespace eve::graphics;


// water_underwater_effects.cpp
using namespace eve::graphics;

namespace {
[[maybe_unused]] UnderwaterColorGradient depthGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.4F, 0.7F, 1.0F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.03F, 0.08F, 0.18F)));
    return gradient;
}
[[maybe_unused]] UnderwaterColorGradient timeGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.1F, 0.2F, 0.4F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.8F, 0.5F, 0.2F)));
    return gradient;
}
[[maybe_unused]] UnderwaterScalarCurve exposureCurve() {
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
