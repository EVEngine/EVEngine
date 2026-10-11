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

TEST_CASE("weather.presetRoundTrip") {
    Weather w;
    w.setPreset("clear");
    CHECK(w.getPreset() == "clear");
    w.setPreset("drizzle");
    CHECK(w.getPreset() == "drizzle");
    w.setPreset("rain");
    CHECK(w.getPreset() == "rain");
    w.setPreset("storm");
    CHECK(w.getPreset() == "storm");
    w.setPreset("snow");
    CHECK(w.getPreset() == "snow");
    w.setPreset("fog");
    CHECK(w.getPreset() == "fog");
    w.setPreset("wind");
    CHECK(w.getPreset() == "wind");
    w.setPreset("blizzard");
    CHECK(w.getPreset() == "blizzard");
}

TEST_CASE("weather.unknownPresetIgnored") {
    Weather w;
    w.setPreset("rain");
    w.setPreset("tsunami");  // not a known preset; keep previous
    CHECK(w.getPreset() == "rain");
}

TEST_CASE("weather.intensityClamped") {
    Weather w;
    w.setIntensity(-1.0f);
    CHECK(w.getIntensity() == 0.0f);
    w.setIntensity(2.0f);
    CHECK(w.getIntensity() == 1.0f);
    w.setIntensity(0.6f);
    CHECK(std::fabs(w.getIntensity() - 0.6f) < 1e-6f);
}

TEST_CASE("weather.windClamped") {
    Weather w;
    w.setWindSpeed(12.0f);
    CHECK(w.getWindSpeed() == 12.0f);
    w.setWindSpeed(-4.0f);  // non-negative
    CHECK(w.getWindSpeed() == 0.0f);
    w.setWindDirection(90.0f);
    CHECK(w.getWindDirection() == 90.0f);
}

TEST_CASE("weather.lightningToggle") {
    Weather w;
    CHECK(w.isLightningEnabled());
    w.setLightningEnabled(false);
    CHECK(!w.isLightningEnabled());
    w.setLightningEnabled(true);
    CHECK(w.isLightningEnabled());
}

TEST_CASE("weather.moodParams") {
    Weather w;
    w.setSkyColor(0.2f, 0.3f, 0.4f);
    CHECK(w.getSkyColorR() == 0.2f);
    CHECK(w.getSkyColorG() == 0.3f);
    CHECK(w.getSkyColorB() == 0.4f);

    w.setFogColor(0.5f, 0.55f, 0.6f);
    CHECK(w.getFogColorR() == 0.5f);
    CHECK(w.getFogColorG() == 0.55f);
    CHECK(w.getFogColorB() == 0.6f);

    w.setFogDensity(0.03f);
    CHECK(w.getFogDensity() == 0.03f);
    w.setFogDensity(-1.0f);
    CHECK(w.getFogDensity() == 0.0f);

    w.setSunIntensity(0.4f);
    CHECK(w.getSunIntensity() == 0.4f);
    w.setSunIntensity(3.0f);  // clamped to [0,1]
    CHECK(w.getSunIntensity() == 1.0f);
}

TEST_CASE("weather.ambientInRange") {
    // intensityCur starts at 0 (clear); ambient is bright and bounded.
    Weather w;
    float amb = w.getAmbientBrightness();
    CHECK(amb >= 0.34f);
    CHECK(amb <= 0.91f);
}

TEST_CASE("weather.pcgSnowWindThreeAxisAndAtomicValidation") {
    Weather w;
    REQUIRE(w.setSnowWind(2.5f, -3.25f, 1.75f).ok());
    CHECK(w.hasSnowWind());
    CHECK(w.getSnowWindX() == 2.5f);
    CHECK(w.getSnowWindY() == -3.25f);
    CHECK(w.getSnowWindZ() == 1.75f);
    CHECK(!w.setSnowWind(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    CHECK(w.getSnowWindX() == 2.5f);
    w.clearSnowWind();
    CHECK(!w.hasSnowWind());
}

TEST_CASE("weather.pcgInteriorVolumeShapesValidateAtomically") {
    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureBox(2.f, 3.f, 4.f, 10.f, 8.f, 6.f, 1, 7, 3).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    CHECK(!volume.contains(7.01f, 7.f, 7.f));
    CHECK(!volume.configureSphere(0.f, 0.f, 0.f, -1.f, 0, 2, 4).ok());
    CHECK(volume.contains(7.f, 7.f, 7.f));
    REQUIRE(volume.configureSphere(0.f, 0.f, 0.f, 5.f, 0, 11, 13).ok());
    CHECK(volume.contains(3.f, 4.f, 0.f));
    CHECK(!volume.contains(3.01f, 4.f, 0.f));
    CHECK(!volume.contains(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f));
}

TEST_CASE("weather.pcgInteriorVolumeTransitionsAndReverb") {
    PcgInteriorWeatherVolume volume;
    REQUIRE(volume.configureSphere(0.f, 0.f, 0.f, 4.f, 0, 21, 8).ok());
    Weather weather;
    auto outside = weather.applyInteriorVolume(volume, 8.f, 0.f, 0.f);
    REQUIRE(outside.ok());
    CHECK(outside.value() == PcgInteriorWeatherTransition::Unchanged);
    CHECK(weather.getCurrentWeatherReverbPreset() == 8);
    auto entered = weather.applyInteriorVolume(volume, 0.f, 0.f, 0.f);
    REQUIRE(entered.ok());
    CHECK(entered.value() == PcgInteriorWeatherTransition::Entered);
    CHECK(weather.isInsideInteriorWeather());
    CHECK(weather.isInteriorWeatherCollisionRequested());
    CHECK(weather.getCurrentWeatherReverbPreset() == 21);
    auto unchanged = weather.setInteriorVolumeState(volume, true);
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value() == PcgInteriorWeatherTransition::Unchanged);
    auto exited = weather.applyInteriorVolume(volume, 5.f, 0.f, 0.f);
    REQUIRE(exited.ok());
    CHECK(exited.value() == PcgInteriorWeatherTransition::Exited);
    CHECK(!weather.isInsideInteriorWeather());
    CHECK(weather.getCurrentWeatherReverbPreset() == 8);
    CHECK(!weather.applyInteriorVolume(volume,
        std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
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
