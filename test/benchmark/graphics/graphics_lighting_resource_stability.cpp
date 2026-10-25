// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "render-window",
  "module": "graphics"
}
*/
// clang-format on

// Opt-in long-duration GPU resource stability workloads. Short integration
// frame-slot tests do not reproduce reference-count overflow or pool exhaustion.
#include <glm/glm.hpp>
#include "graphics/ClusteredLight.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_graphics_lighting_stability {
TEST_CASE("graphics.deferredLighting.reusesCompletedFrameResources") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    struct CloseWindow {
        eve::window::Window* window;
        ~CloseWindow() { window->close(); }
    } cleanup{window};
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 48;
    settings.vsync  = 0;
    REQUIRE(window->setWindowSettings(settings));
    if (!graphics->supportsDeferredLighting()) return;

    // Exceeds VMA's mapping reference limit even with three frame slots.
    // Validation also checks that each descriptor is updated only after its
    // previous submission has completed. No readback/idle wait masks overlap.
    for (int frame = 0; frame < 800; ++frame) {
        graphics->beginGBufferPass(64, 48);
        graphics->endGBufferPass();
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(glm::mat4(1));
        graphics->setMesh3DView(glm::mat4(1));
        graphics->setMesh3DClip(0.1f, 100.f);
        graphics->drawDeferredLighting();
        graphics->present();
    }
}

TEST_CASE("graphics.clusteredLighting.reusesUnchangedStorageBindings") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    struct CloseWindow {
        eve::window::Window* window;
        ~CloseWindow() { window->close(); }
    } cleanup{window};
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 48;
    settings.vsync  = 0;
    REQUIRE(window->setWindowSettings(settings));
    auto* mesh = graphics->newMeshSphere(8, 4);
    REQUIRE(mesh != nullptr);
    const auto lighting =
        eve::graphics::buildClusteredLighting({}, {}, glm::mat4(1), 0.1f, 100.f, 64, 48, 1.f, glm::vec4(0.2f));
    // Re-uploading stable lighting must not allocate a fresh descriptor set
    // each frame and exhaust the shared descriptor pool.
    for (int frame = 0; frame < 1000; ++frame) {
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(glm::mat4(1));
        graphics->setMesh3DClusteredLighting(lighting);
        graphics->drawMesh(mesh, glm::mat4(1), nullptr, eve::graphics::Color(1.f, 1.f, 1.f, 1.f));
        graphics->present();
    }
}
}  // namespace test_graphics_lighting_stability
