#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "weather/Weather.h"
#include "weather/PcgInteriorWeatherVolume.h"

#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "window/Window.h"

#include <cmath>
#include <limits>

using namespace eve::weather;

TEST_CASE("weather.backendShaderInitialization") {
    auto *window = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 128;
    settings.height = 128;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    Weather weather;
    weather.init(graphics);
    // The renderer reads the material's albedo when a material is present.
    // Attaching the texture only to Renderable3D silently samples white.
    int  texturedFields = 0;
    auto fields         = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto [renderer] = *it;
        if (!renderer->material) continue;
        auto *texture = renderer->material->getAlbedoTexture();
        if (!texture) continue;
        CHECK(texture->getPixelWidth() > 1);
        CHECK(texture->getPixelHeight() > 1);
        CHECK(!renderer->material->getCastShadow());
        ++texturedFields;
    }
    CHECK(texturedFields == 2);
    weather.setPreset("storm");
    weather.strike();
    weather.update(1.f / 60.f, graphics);
    CHECK(weather.getIntensity() >= 0.f);
    CHECK(weather.getFlash() >= 0.f);
    window->close();
}


TEST_CASE("weather.pcgInteriorVolumeControlsRealPrecipitationRenderables") {
    auto *window = eve::window::Window::create();
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 96;
    settings.height = 96;
    REQUIRE(window->setWindowSettings(settings));

    Weather weather;
    weather.setPreset("rain");
    weather.setIntensity(1.f);
    weather.update(1.f, graphics);
    auto visibleWeatherFields = [] {
        int count = 0;
        auto fields = ecs::View<eve::graphics::Renderable3D,
                                eve::graphics::Renderable3D::MeshRenderer>();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            auto [renderer] = *it;
            if (renderer->visible) ++count;
        }
        return count;
    };
    CHECK(visibleWeatherFields() == 1);

    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureBox(0.f, 0.f, 0.f, 20.f, 20.f, 20.f, 1, 2, 0).ok());
    REQUIRE(weather.applyInteriorVolume(volume, 0.f, 0.f, 0.f).ok());
    weather.update(1.f / 60.f, graphics);
    CHECK(visibleWeatherFields() == 0);
    REQUIRE(weather.applyInteriorVolume(volume, 20.f, 0.f, 0.f).ok());
    weather.update(1.f / 60.f, graphics);
    CHECK(visibleWeatherFields() == 1);
    window->close();
}
