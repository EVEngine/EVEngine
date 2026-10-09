#include "graphics/sky/SkyAtmospherePass.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ViewPreparation.h"
#include "graphics/sky/SkyAtmosphereLuts.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/shaders/scene_tonemap_frag_spv.inc"
#include "graphics/vulkan/Graphics.h"
#endif

TEST_CASE("graphics.sky atmosphere capture uses independent HDR lighting and detaches cleanly") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    const char*             mode      = std::getenv("EVENGINE_SKY_BENCHMARK");
    const bool              benchmark = mode && std::string_view(mode) == "1";
    const int               width = benchmark ? 1280 : 160, height = benchmark ? 720 : 90;
    GfxFixture              fixture(width, height, true);
    auto*                   gfx = fixture.gfx;
    SkyAtmosphereParameters invalid;
    invalid.groundRadiusKm = -1;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, invalid).ok());
    [[maybe_unused]] const auto prepareStarted = std::chrono::steady_clock::now();
    auto                        prepared       = SkyAtmospherePass::prepare(*gfx, {});
#if defined(EVENGINE_WEBGPU) || !defined(EVE_TEST_RESOURCE_VALIDATION)
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    REQUIRE(prepared.ok());
    const double prepareMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStarted).count();
    auto  sky    = std::move(prepared).takeValue();
    auto* camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 2.3f, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    SkyCelestialLight sourceLight;
    if (benchmark) {
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(float(2 * std::atan(std::tan(75.0 * 3.141592653589793 / 360.0) * height / width) * 180 /
                             3.141592653589793));
        sourceLight.direction = {-.5f, .8660254f, 0};
    }
    REQUIRE(sky->setLight(sourceLight).ok());
    SkyAtmosphereFrame invalidFrame;
    invalidFrame.dayPhase = 1;
    REQUIRE(!sky->setFrame(invalidFrame).ok());
    invalidFrame.dayPhase = 0;

    invalidFrame.sun.irradiance  = {0, 0, 0};
    invalidFrame.wispsMorphPhase = 1;
    REQUIRE(!sky->setFrame(invalidFrame).ok());
    auto* canvas = gfx->newHDRCanvas(width, height);
    REQUIRE(canvas != nullptr);
    gfx->setBackgroundColor(Color(0, 0, 0, 1));
    REQUIRE(sky->attach().ok());
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::array<float, 5> gpuMs{}, viewPreparationMs{}, changingViewWallMs{};
    if (benchmark)
        for (auto& ms : gpuMs) {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            ms = gfx->getLastOffscreen3DGpuDurationMs();
            REQUIRE(ms > 0);
        }
    if (benchmark) {
        size_t sampleIndex = 0;
        auto   observer    = addViewPreparation([&](Graphics& provider, const glm::mat4&, const glm::vec3&) {
            viewPreparationMs[sampleIndex] = provider.getLastOffscreen3DGpuDurationMs();
            return eve::Result<void>::success();
        });
        REQUIRE(observer.ok());
        for (; sampleIndex < viewPreparationMs.size(); ++sampleIndex) {
            auto changing = sourceLight;
            changing.irradiance *= 1.01f + float(sampleIndex) * .01f;
            REQUIRE(sky->setLight(changing).ok());
            const auto started = std::chrono::steady_clock::now();
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            changingViewWallMs[sampleIndex] =
                float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        }
        removeViewPreparation(observer.value());
        REQUIRE(sky->setLight(sourceLight).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    }
    std::unique_ptr<eve::image::ImageData> first(canvas->newHDRImageData());
    const auto                             sample = first->getPixel(width / 2, height / 2);
    REQUIRE(std::isfinite(sample.r));
    REQUIRE(std::isfinite(sample.g));
    REQUIRE(std::isfinite(sample.b));
    REQUIRE(sample.b > sample.r);
    REQUIRE(sample.b > .0001f);
    // Only the optical sky camera is lifted to the reference's minimum height.
    // Looking from below the virtual ground must not make the whole sky black.
    camera->setEye(0, -10, 0);
    camera->setTarget(0, -9.7f, -1);
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> underground(canvas->newHDRImageData());
    const auto                             undergroundSample = underground->getPixel(width / 2, height / 2);
    REQUIRE(std::isfinite(undergroundSample.b));
    REQUIRE(undergroundSample.b > .0001f);
    camera->setEye(0, 2, 0);
    if (benchmark)
        camera->setTarget(.965925826f, 2.258819045f, 0);
    else
        camera->setTarget(0, 2.3f, -1);
    sky->detach();
    SkyAtmosphereParameters singleParameters;
    singleParameters.multiScatteringFactor = 0;
    auto singlePrepared                    = SkyAtmospherePass::prepare(*gfx, singleParameters);
    REQUIRE(singlePrepared.ok());
    auto single = std::move(singlePrepared).takeValue();
    REQUIRE(single->setLight(sourceLight).ok());
    REQUIRE(single->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> singleImage(canvas->newHDRImageData());
    const auto                             singleSample = singleImage->getPixel(width / 2, height / 2);
    REQUIRE(sample.r > singleSample.r);
    REQUIRE(sample.g > singleSample.g);
    REQUIRE(sample.b > singleSample.b);
    single.reset();
    REQUIRE(sky->attach().ok());
    if (const char* path = std::getenv("EVENGINE_SKY_ARTIFACT")) {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.good());
        output << "PF\n" << width << " " << height << "\n-1.0\n";
        for (int y = height - 1; y >= 0; --y)
            for (int x = 0; x < width; ++x) {
                const auto  pixel = first->getPixel(x, y);
                const float rgb[]{pixel.r, pixel.g, pixel.b};
                output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
            }
        REQUIRE(output.good());
        if (benchmark) {
            std::ofstream timing(std::string(path) + ".timing.json");
            REQUIRE(timing.good());
            timing << "{\"schema\":\"eve.sky-pass-timing/1\",\"width\":" << width << ",\"height\":" << height
                   << ",\"prepareMs\":" << prepareMs << ",\"gpuOffscreenPassMs\":[";
            for (size_t i = 0; i < gpuMs.size(); ++i) timing << (i ? "," : "") << gpuMs[i];
            timing << "],\"viewPreparationGpuMs\":[";
            for (size_t i = 0; i < viewPreparationMs.size(); ++i) timing << (i ? "," : "") << viewPreparationMs[i];
            timing << "],\"changingViewWallMs\":[";
            for (size_t i = 0; i < changingViewWallMs.size(); ++i) timing << (i ? "," : "") << changingViewWallMs[i];
            timing << "]}\n";
            REQUIRE(timing.good());
        }
    }
    SkyCelestialLight doubled = sourceLight;
    doubled.irradiance        = glm::vec3(10);
    REQUIRE(sky->setLight(doubled).ok());
    auto rejected         = doubled;
    rejected.irradiance.x = -1;
    REQUIRE(!sky->setLight(rejected).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> second(canvas->newHDRImageData());
    const auto                             twice = second->getPixel(width / 2, height / 2);
    REQUIRE(std::abs(twice.b - 2 * sample.b) < std::max(.0001f, sample.b * .003f));
    sky.reset();
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> removed(canvas->newHDRImageData());
    REQUIRE(removed->getPixel(width / 2, height / 2).b == 0);
#endif
}

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("graphics.sky filmic display preserves middle grey and keeps prior modes explicit") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto*      gfx = fixture.gfx;
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Aces);
    REQUIRE(!gfx->setSceneToneMapping(static_cast<Graphics::SceneToneMapping>(99)).ok());
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Aces);
    REQUIRE(gfx->setSceneToneMapping(Graphics::SceneToneMapping::Filmic).ok());
    REQUIRE(gfx->getSceneToneMapping() == Graphics::SceneToneMapping::Filmic);
    auto* input  = gfx->newHDRCanvas(16, 16);
    auto* output = gfx->newHDRCanvas(16, 16);
    auto* shader =
        gfx->newShaderFromSpv({}, {scene_tonemap_frag_spv, scene_tonemap_frag_spv + scene_tonemap_frag_spv_count});
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    const auto sample = [&](float linear, float mode, float displayMode = 0) {
        gfx->setCanvas(input);
        gfx->clear(Color(linear, linear, linear, 1), std::nullopt, std::nullopt);
        gfx->drawSolidRect(0, 0, 16, 16, Color(linear, linear, linear, 1));
        gfx->setCanvas();
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16,
                                    Color(mode, displayMode, display::packNits(200, 1000), 0));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    const auto film = sample(.18f, 2), old = sample(.18f, 1), linear = sample(.18f, 0);
    REQUIRE(std::abs(film.r - .18f) < .002f);
    REQUIRE(std::abs(film.r - film.g) < .001f);
    REQUIRE(std::abs(film.g - film.b) < .001f);
    REQUIRE(old.r > film.r + .05f);
    REQUIRE(std::abs(linear.r - .18f) < .001f);
    const auto black = sample(0, 2), bright = sample(100, 2);
    REQUIRE(black.r == 0);
    REQUIRE(black.g == 0);
    REQUIRE(black.b == 0);
    REQUIRE(std::isfinite(bright.r));
    REQUIRE(bright.r > .99f);
    REQUIRE(bright.r <= 1);
    // Filmic participates in the dev HDR output contract without replacing its encoding.
    const auto compose = sample(.9f, 2, 3), scRgb = sample(.9f, 2, 1);
    REQUIRE(std::abs(compose.r - film.r * 5.f) < .005f);
    REQUIRE(std::abs(scRgb.r - compose.r * 2.5f) < .01f);
    const auto pq = sample(.9f, 2, 2), pqBright = sample(9.f, 2, 2);
    REQUIRE(std::isfinite(pq.r));
    REQUIRE(pq.r > 0);
    REQUIRE(pqBright.r > pq.r);
    REQUIRE(pqBright.r <= 1);
    REQUIRE(gfx->getScenePhotographicVignette() == 0);
    REQUIRE(gfx->setScenePhotographicVignette(.4f).ok());
    REQUIRE(!gfx->setScenePhotographicVignette(-1).ok());
    REQUIRE(!gfx->setScenePhotographicVignette(2).ok());
    REQUIRE(gfx->getScenePhotographicVignette() == .4f);
    (void)sample(1, 0);
    gfx->setCanvas(output);
    gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(0, .1f, 1, 0));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> vignette(output->newHDRImageData());
    REQUIRE(vignette != nullptr);
    // Pixel centers at (+/- .9375,+/- .9375) in square vignette space.
    const float edgeExpected = 1.f / std::pow(1.f + 2.f * .375f * .375f, 2.f);
    REQUIRE(std::abs(vignette->getPixel(0, 0).r - edgeExpected) < .001f);
    REQUIRE(std::abs(vignette->getPixel(15, 15).r - edgeExpected) < .001f);
    REQUIRE(vignette->getPixel(8, 8).r > .997f);
    REQUIRE(gfx->setScenePhotographicVignette(0).ok());
    REQUIRE(gfx->setSceneToneMapping(Graphics::SceneToneMapping::Aces).ok());
}

TEST_CASE("graphics.sky height fog fills the ground with independently integrated ambient light") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    const char*             timingPath = std::getenv("EVENGINE_SKY_FOG_TIMING");
    const int               width = timingPath ? 1280 : 64, height = timingPath ? 720 : 64;
    GfxFixture              fixture(width, height, true);
    auto*                   gfx = fixture.gfx;
    SkyAtmosphereParameters parameters;
    parameters.heightFog.densityPerMetre         = .000551f;
    parameters.heightFog.directionalInscattering = {0, 0, 0};
    auto tables                                  = detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(tables.ok());
    auto ambient = detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0});
    REQUIRE(ambient.ok());
    auto prepared = SkyAtmospherePass::prepare(*gfx, parameters);
    REQUIRE(prepared.ok());
    auto              sky = std::move(prepared).takeValue();
    SkyCelestialLight light;
    light.direction  = {0, 1, 0};
    light.irradiance = {5, 5, 5};
    REQUIRE(sky->setLight(light).ok());
    auto* camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 1, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(30);
    auto* canvas = gfx->newHDRCanvas(width, height);
    REQUIRE(canvas != nullptr);
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> image(canvas->newHDRImageData());
    const auto                             pixel = image->getPixel(width / 2, height / 2);
    const float                            actual[]{pixel.r, pixel.g, pixel.b};
    for (size_t axis = 0; axis < 3; ++axis) {
        const float expected = ambient.value()[axis] * 5 * parameters.heightFog.atmosphereContribution;
        REQUIRE(expected > 0);
        REQUIRE(std::isfinite(actual[axis]));
        REQUIRE(std::abs(actual[axis] - expected) < expected * .02f);
    }
    light.irradiance = {0, 0, 0};
    REQUIRE(sky->setLight(light).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> night(canvas->newHDRImageData());
    REQUIRE(night->getPixel(width / 2, height / 2).r == 0);
    REQUIRE(night->getPixel(width / 2, height / 2).g == 0);
    REQUIRE(night->getPixel(width / 2, height / 2).b == 0);
    if (timingPath) {
        light.direction  = {-.5f, .8660254f, 0};
        light.irradiance = {5, 5, 5};
        REQUIRE(sky->setLight(light).ok());
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(46.6921257f);
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::ofstream output(timingPath);
        REQUIRE(output.good());
        output << "{\"schema\":\"eve.sky-fog-pass-timing/1\",\"width\":1280,\"height\":720,\"gpuOffscreenPassMs\":[";
        for (int i = 0; i < 5; ++i) {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
            const float duration = gfx->getLastOffscreen3DGpuDurationMs();
            REQUIRE(duration > 0);
            output << (i ? "," : "") << duration;
        }
        output << "]}\n";
        REQUIRE(output.good());
    }
}

TEST_CASE("graphics.sky atmosphere survives graphics provider retirement") {
    auto graphics = std::make_unique<eve::graphics::vulkan::Graphics>();
    graphics->initHeadless(16, 16);
    auto prepared = eve::graphics::SkyAtmospherePass::prepare(*graphics, {});
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    REQUIRE(sky->attach().ok());
    graphics.reset();
    auto drawn = sky->draw(glm::mat4(1), glm::vec3(0, 2, 0));
    REQUIRE(!drawn.ok());
    REQUIRE(drawn.error()->code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(!sky->attach().ok());
    sky.reset();
}
#endif
