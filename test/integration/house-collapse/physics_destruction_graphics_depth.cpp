// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "physics_destruction_graphics",
  "origins": [
    "physics_destruction_graphics_depth.cpp"
  ]
}
*/
// clang-format on

#include <cmath>
#include <memory>
#include "graphics/GBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "physics/World3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/CollapsePlayback.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/cook/AssemblyFractureSettings.h"
#include "physics/destruction/cook/VoronoiCook.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("physics_destruction_graphics.depthTracksCurrentSubmissionAndExpiresWithRenderer") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width    = 128;
    settings.height   = 128;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    auto* control = graphics->getRenderControl();
    control->enable("forward");
    control->enable("gbuffer");
    control->disable("taa");
    control->enable("shadow");
    auto* sun = eve::graphics::Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.5f);
    auto* camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(-0.55f, 1.f, 6.f);
    camera->setTarget(-0.55f, 1.f, 0.f);
    camera->setFov(45.f);
    camera->setClipPlanes(0.2f, 30.f);
    using namespace eve::physics;
    World3D world(0, 0, 0, true);
    auto    asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f).expect("asset");
    destruction_cook::AssemblyFractureSettings recipe;
    REQUIRE(recipe.setGroupPolicy(0, {1, 20.f, 20.f}).ok());
    auto cooked   = destruction_cook::cookVoronoiAssembly(asset, recipe).expect("cook");
    auto instance = GeometryCollectionInstance::createFractured(world, cooked.collection, cooked.geometry, 0, 0, 0)
                        .expect("instance");
    CollapseAnimation clip;
    REQUIRE(clip.capture(*instance, 0).ok());
    auto playback = CollapsePlayback::prepare(std::move(clip)).expect("freeze playback");
    instance.reset();
    world.destroy();
    graphics->setScreenReadbackEnabled(true);
    auto depth = [&](GeometryCollectionRenderer* renderer, float z) {
        graphics->clearScreen();
        if (renderer) {
            REQUIRE(playback.setPlacement({0, 0, z}).ok());
            REQUIRE(renderer->submitPlayback(playback).ok());
        }
        graphics->render3D();
        auto* texture = control->getGBuffer()->getDepthTexture();
        REQUIRE(texture != nullptr);
        graphics->drawTexturedRect(texture, 0.f, 0.f, 128.f, 128.f, 1.f, 1.f, 1.f, 1.f);
        graphics->present();
        return graphics->getPixel(64, 64).r;
    };
    float first = 0.f;
    {
        GeometryCollectionRenderer renderer(nullptr);
        REQUIRE(renderer.preparePlayback(playback, graphics).ok());
        first               = depth(&renderer, 0.f);
        const float farther = depth(&renderer, -4.f);
        REQUIRE(first > 0.05f);
        REQUIRE(first < 0.3f);
        REQUIRE(farther > first + 0.08f);
    }
    REQUIRE(depth(nullptr, 0.f) > 0.95f);
    {
        GeometryCollectionRenderer replacement(nullptr);
        REQUIRE(replacement.preparePlayback(playback, graphics).ok());
        REQUIRE(std::abs(depth(&replacement, 0.f) - first) < 0.02f);
    }
    REQUIRE(depth(nullptr, 0.f) > 0.95f);
    window->close();
}
