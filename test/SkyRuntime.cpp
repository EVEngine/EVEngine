#include "daynight/sky/SkyRuntime.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include "Fixtures.h"
#include "SkyPreparationSupport.h"
#include "common/Value.h"
#include "daynight/sky/SkyProfile.h"
#include "filesystem/Filesystem.h"
#include "graphics/Canvas.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#endif

TEST_CASE("daynight.sky runtime advances one clock and publishes solar lighting to capture") {
    using namespace eve::daynight;
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture    fixture(32, 32, true);
    std::ifstream profileFile(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(profileFile.good());
    const std::string profileText((std::istreambuf_iterator<char>(profileFile)), std::istreambuf_iterator<char>());
    auto              document = eve::Value::fromJson(profileText);
    REQUIRE(document.ok());
    auto profile = SkyProfile::decode(document.value());
    REQUIRE(profile.ok());
    auto settings            = profile.value().settings();
    settings.clock           = {5.5, 1};
    auto invalid             = settings;
    invalid.sunIrradiance[0] = std::numeric_limits<float>::infinity();
    REQUIRE(!SkyRuntime::prepare(*fixture.gfx, invalid, {}).ok());
    auto prepared = SkyRuntime::prepare(*fixture.gfx, settings, profile.value().atmosphere());
#if defined(EVENGINE_WEBGPU) || !defined(EVE_TEST_RESOURCE_VALIDATION)
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->code() == eve::DiagnosticCode::Unsupported);
#else
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    // Actual UDS DirectionalLightComponent intensity at 05:30 in atmosphere mode.
    REQUIRE(std::abs(sky->frame().sunIrradiance[0] - 4.99556827545166) < 1e-5);
    SkyWeather weather;
    weather.cloudCoverage = 10;
    weather.windXZ        = {4, 0};
    REQUIRE(sky->transitionTo(weather, eve::Duration::fromNanoseconds(4000000000)).ok());
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(6500000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->frame().simulation.weather.cloudCoverage == 10);
    REQUIRE(sky->frame().simulation.cloudTravelXZ[0] == 18);
    REQUIRE(sky->frame().sunIrradiance[0] == 5);
    REQUIRE(std::abs(sky->frame().sunDirection[0] + .5) < 1e-6);
    REQUIRE(!sky->advance(eve::Duration::fromNanoseconds(-1)).ok());
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->frame().sunIrradiance[0] == 5);
    auto* camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(0, 2.3f, -1);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    auto* canvas = fixture.gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    fixture.gfx->setBackgroundColor(Color(0, 0, 0, 1));
    REQUIRE(sky->attach().ok());
    REQUIRE(sky->attach().ok());
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> noon(canvas->newHDRImageData());
    REQUIRE(noon->getPixel(16, 16).b > .0001f);
    // Render consumers must not advance simulation independently.
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    REQUIRE(sky->frame().simulation.hour == 12);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(12000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 0);
    REQUIRE(sky->frame().sunIrradiance[0] == 0);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> midnight(canvas->newHDRImageData());
    REQUIRE(midnight->getPixel(16, 16).b == 0);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(12000000000)).ok());
    sky.reset();
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> removed(canvas->newHDRImageData());
    REQUIRE(removed->getPixel(16, 16).b == 0);
#endif
}

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("daynight.sky runtime rejects updates after provider retirement without advancing state") {
    auto graphics = std::make_unique<eve::graphics::vulkan::Graphics>();
    graphics->initHeadless(16, 16);
    auto prepared = eve::daynight::SkyRuntime::prepare(*graphics, {}, {});
    REQUIRE(prepared.ok());
    auto sky = std::move(prepared).takeValue();
    REQUIRE(sky->attach().ok());
    graphics.reset();
    auto result = sky->advance(eve::Duration::fromNanoseconds(1000000000));
    REQUIRE(!result.ok());
    REQUIRE(result.error()->code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(sky->frame().simulation.elapsedSeconds == 0);
    REQUIRE(!sky->attach().ok());
    REQUIRE(!sky->transitionTo({}, eve::Duration{}).ok());
    sky.reset();
}
#endif

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("daynight.sky injected cloud time changes authored wisps and closes the morph cycle") {
    using namespace eve::daynight;
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    const std::string source = std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky";
    REQUIRE(fs->mountRealDirectory(source, "sky-motion", false));
    auto asset = SkyWispsAsset::generate(2026);
    REQUIRE(asset.ok());
    std::ifstream input(source + "/sky-animated.json");
    REQUIRE(input.good());
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    auto              document = eve::Value::fromJson(text);
    REQUIRE(document.ok());
    auto profile = SkyProfile::decode(document.value());
    REQUIRE(profile.ok());
    auto settings = profile.value().settings();
    REQUIRE(settings.cloudMotion.speed == .35);
    settings.cloudMotion.speed = .5;
    GfxFixture fixture(160, 90, true);
    auto runtime = SkyRuntime::prepare(*fixture.gfx, settings, profile.value().atmosphere(), &asset.value().layer());
    if (!skyPreparationAvailable(runtime)) return;
    auto sky = std::move(runtime).takeValue();
    REQUIRE(sky->attach().ok());
    auto* camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(.965925826f, 2.258819045f, 0);
    camera->setFov(46.6921257f);
    camera->setClipPlanes(.1f, 10000);
    auto* canvas = fixture.gfx->newHDRCanvas(160, 90);
    REQUIRE(canvas != nullptr);
    const auto capture = [&]() {
        RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
        return std::unique_ptr<eve::image::ImageData>(canvas->newHDRImageData());
    };
    auto first = capture();
    REQUIRE(first != nullptr);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    REQUIRE(sky->frame().cloudTime == 5);
    REQUIRE(sky->frame().wispsMorphPhase == .5f);
    auto moved = capture();
    REQUIRE(moved != nullptr);
    int changed = 0;
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            const auto a = first->getPixel(x, y), b = moved->getPixel(x, y);
            if (std::abs(a.b - b.b) > .0001f) ++changed;
        }
    REQUIRE(changed > 100);
    REQUIRE(sky->frame().simulation.elapsedSeconds == 10);
    REQUIRE(!sky->advance(eve::Duration::fromNanoseconds(-1)).ok());
    REQUIRE(sky->frame().cloudTime == 5);
    REQUIRE(sky->frame().wispsMorphPhase == .5f);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    REQUIRE(sky->frame().cloudTime == 10);
    REQUIRE(sky->frame().wispsMorphPhase == 0);
    auto cycle = capture();
    REQUIRE(cycle != nullptr);
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            const auto a = first->getPixel(x, y), b = cycle->getPixel(x, y);
            REQUIRE(std::abs(a.r - b.r) < .00001f);
            REQUIRE(std::abs(a.g - b.g) < .00001f);
            REQUIRE(std::abs(a.b - b.b) < .00001f);
        }
    sky.reset();
    REQUIRE(fs->unmountRealDirectory(source));
}
#endif

TEST_CASE("daynight.sky optical cycle survives midnight and restores the same rendered snapshot") {
#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
    using namespace eve::graphics;
    using namespace eve::daynight;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture        fixture(32, 32, true);
    auto*             fs     = eve::filesystem::Filesystem::create();
    const std::string source = std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky";
    REQUIRE(fs->mountRealDirectory(source, "optical-sky", false));
    auto asset = SkyWispsAsset::generate(2026);
    REQUIRE(asset.ok());
    SkyRuntimeSettings settings;
    settings.clock             = {23, 1};
    settings.cloudMotion.speed = 0;
    auto result                = SkyRuntime::prepare(*fixture.gfx, settings, {}, &asset.value().layer());
    if (!skyPreparationAvailable(result)) return;
    auto sky = std::move(result).takeValue();
    REQUIRE(sky->attach().ok());
    auto* camera = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(.965925826f, 2.258819045f, 0);
    camera->setFov(46.6921257f);
    camera->setClipPlanes(.1f, 10000);
    auto* canvas = fixture.gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> before(canvas->newHDRImageData());
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(2000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 1);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> after(canvas->newHDRImageData());
    bool                                   changed = false;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto a = before->getPixel(x, y), b = after->getPixel(x, y);
            REQUIRE(std::isfinite(b.r));
            REQUIRE(std::isfinite(b.g));
            REQUIRE(std::isfinite(b.b));
            changed = changed || a.r != b.r || a.g != b.g || a.b != b.b;
        }
    REQUIRE(changed);
    REQUIRE(sky->advance(eve::Duration::fromNanoseconds(22000000000)).ok());
    REQUIRE(sky->frame().simulation.hour == 23);
    RenderSystem3D::renderToCanvas(*fixture.gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> restored(canvas->newHDRImageData());
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto a = before->getPixel(x, y), b = restored->getPixel(x, y);
            REQUIRE(a.r == b.r);
            REQUIRE(a.g == b.g);
            REQUIRE(a.b == b.b);
        }
    sky->detach();
    REQUIRE(fs->unmountRealDirectory(source));
#endif
}
