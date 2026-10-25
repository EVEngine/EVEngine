// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "weather"
}
*/
// clang-format on

#include <cmath>
#include <limits>
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "weather/Lightning.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::weather;
TEST_CASE("weather.lightning weapon endpoints pulse and stale entity") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings ws;
    ws.width    = 128;
    ws.height   = 128;
    ws.centered = true;
    REQUIRE(window->setWindowSettings(ws));
    LightningSettings s;
    s.variants    = 2;
    auto prepared = Lightning::prepare(*graphics, s);
    REQUIRE(prepared.ok());
    auto fx = std::move(prepared).takeValue();
    REQUIRE(fx->setEndpoints(2, 3, 4, 8, 3, 4).ok());
    REQUIRE(fx->setColor(.2f, .8f, 1).ok());
    REQUIRE(fx->trigger().ok());
    CHECK(fx->amplitude() == 1);
    CHECK(!fx->update(-1).ok());
    CHECK(fx->amplitude() == 1);
    CHECK(!fx->setEndpoints(0, 0, 0, 0, 0, 0).ok());
    REQUIRE(fx->update(.5f).ok());
    CHECK(fx->amplitude() == 0);
    REQUIRE(fx->trigger().ok());
    fx->stop();
    CHECK(fx->amplitude() == 0);
    // Simulate an ECS world reset before the owning visual is released.
    auto view = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
    std::vector<ecs::EntityHandle> handles;
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [r] = *it;
        (void)r;
        handles.push_back(ecs::handle_of(it.entity()));
    }
    for (auto handle : handles)
        if (auto* e = ecs::try_get(handle)) ecs::DestroyEntity(e);
    CHECK(!fx->trigger().ok());
    CHECK(fx->amplitude() == 0);
    fx.reset();
    window->close();
}
