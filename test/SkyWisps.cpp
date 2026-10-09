#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyWispsLayer.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"
#ifndef EVENGINE_WEBGPU
#include "SkyWispsSpv.h"
#include "graphics/shaders/scene_tonemap_frag_spv.inc"
#endif

#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
TEST_CASE("graphics.sky authored wisps composite into the shared capture path") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture                  fixture(32, 32, true);
    auto*                       gfx = fixture.gfx;
    const std::array<float, 27> corners{10000, -30000, -30000, 0, 0,     1, 1,     1,   1, 10000, 30000, -30000, 1, 0,
                                        1,     1,      1,      1, 10000, 0, 30000, .5f, 1, 1,     1,     1,      1};
    const std::array<std::byte, 1> white{std::byte{255}};
    ShaderImageInput               texture;
    texture.format = ShaderImageFormat::R8;
    texture.width = texture.height = 1;
    texture.bytes                  = white;
    texture.sampler                = TextureSampler::linear();
    SkyWispsLayer layer;
    layer.corners        = corners;
    layer.densityTexture = &texture;
    layer.domeScale      = 1;
    layer.color          = {1, 1, 1};
    layer.gradient       = {0, 0, 0, 0};
    layer.opacity        = 1;
    layer.litIntensity   = 1;
    auto* camera         = Camera3D::createCamera();
    camera->setEye(0, 2, 0);
    camera->setTarget(1, 2, 0);
    camera->setClipPlanes(.1f, 10000);
    camera->setFov(50);
    auto* canvas = gfx->newHDRCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    const auto render = [&](const SkyWispsLayer* input, bool mainView = false) {
        auto prepared = SkyAtmospherePass::prepare(*gfx, {}, input);
        REQUIRE(prepared.ok());
        auto pass = std::move(prepared).takeValue();
        REQUIRE(pass->setLight({{1, 0, 0}, {5, 5, 5}}).ok());
        REQUIRE(pass->attach().ok());
        if (mainView) {
            const auto vp = perspectiveVulkanRH_ZO(glm::radians(50.f), 1.f, .1f, 10000.f) *
                            glm::lookAtRH(glm::vec3(0, 2, 0), glm::vec3(1, 2, 0), glm::vec3(0, 1, 0));
            REQUIRE(pass->prepareView(vp, {0, 2, 0}).ok());
            gfx->begin3DFrameToCanvas(canvas);
            REQUIRE(pass->draw(vp, {0, 2, 0}).ok());
            gfx->end3DFrameToCanvas();
        } else {
            RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        }
        std::unique_ptr<eve::image::ImageData> image(canvas->newHDRImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(16, 16);
    };
    const auto base = render(nullptr), cloud = render(&layer);
    REQUIRE(cloud.r > 2.2f);
    REQUIRE(cloud.r < 5.f);
    REQUIRE(std::abs(cloud.r - cloud.g) < .01f);
    REQUIRE(std::abs(cloud.r - cloud.b) < .01f);
    REQUIRE(std::abs(cloud.r - base.r) > .1f);
    layer.opacity          = 0;
    const auto transparent = render(&layer);
    REQUIRE(std::abs(transparent.r - base.r) < .001f);
    REQUIRE(std::abs(transparent.g - base.g) < .001f);
    REQUIRE(std::abs(transparent.b - base.b) < .001f);
    layer.sunDiskColor   = {100, 50, 25};
    layer.sunDiskShape   = {.3f, 3, 0};
    const auto hiddenSun = render(&layer);
    REQUIRE(std::abs(hiddenSun.r - transparent.r) < .001f);
    const auto visibleSun = render(&layer, true);
    REQUIRE(visibleSun.r > hiddenSun.r + 50);
    layer.sunDiskShape[2]     = 1;
    layer.sunDiskCurveEnabled = true;
    for (size_t channel = 0; channel < 3; ++channel) {
        layer.sunDiskCurve[channel * 4]     = {0, 0, 0, 0};
        layer.sunDiskCurve[channel * 4 + 1] = {.25f, 0, 0, 0};
        layer.sunDiskCurve[channel * 4 + 2] = {.75f, .5f, 0, 0};
        layer.sunDiskCurve[channel * 4 + 3] = {1, .5f, 0, 0};
    }
    const auto curveSun = render(&layer, true);
    REQUIRE(std::abs((curveSun.r - hiddenSun.r) * 2 - (visibleSun.r - hiddenSun.r)) < .15f);
    layer.sunDiskCurve[1][0] = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    layer.sunDiskCurveEnabled = false;
    const auto reflectedSun   = render(&layer);
    REQUIRE(std::abs(reflectedSun.r - visibleSun.r) < .1f);
    REQUIRE(reflectedSun.r > hiddenSun.r + 50);
    REQUIRE(reflectedSun.g > hiddenSun.g + 25);
    layer.opacity          = 1;
    const auto obscuredSun = render(&layer);
    // HDR readback is FP16: allow one quantization step near the 2.3 cloud value.
    REQUIRE(std::abs(obscuredSun.r - cloud.r) < .003f);
    layer.sunDiskShape[0] = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    layer.sunDiskShape[0] = .3f;
    layer.morphPeriod     = 0;
    REQUIRE(!SkyAtmospherePass::prepare(*gfx, {}, &layer).ok());
    if (const char* artifactDirectory = std::getenv("EVENGINE_SKY_WISPS_ARTIFACT")) {
        const std::filesystem::path root(artifactDirectory);
        const auto                  read = [&](const char* name) {
            std::ifstream input(root / name, std::ios::binary | std::ios::ate);
            REQUIRE(input.good());
            const auto size = input.tellg();
            REQUIRE(size > 0);
            REQUIRE(size < 64 * 1024 * 1024);
            std::vector<char> bytes(static_cast<size_t>(size));
            input.seekg(0);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(input.good());
            return bytes;
        };
        const auto meshBytes = read("corners.f32"), densityBytes = read("wisps.r8");
        REQUIRE(meshBytes.size() % (27 * sizeof(float)) == 0);
        REQUIRE(densityBytes.size() == 2048 * 2048);
        std::vector<float> authored(meshBytes.size() / sizeof(float));
        std::memcpy(authored.data(), meshBytes.data(), meshBytes.size());
        std::vector<std::byte> rgba(densityBytes.size() * 4);
        for (size_t i = 0; i < densityBytes.size(); ++i) {
            const auto value = std::byte(static_cast<unsigned char>(densityBytes[i]));
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = value;
            rgba[i * 4 + 3]                                 = std::byte{255};
        }
        ShaderImageInput density;
        density.format = ShaderImageFormat::RGBA8Srgb;
        density.width = density.height = 2048;
        density.bytes                  = rgba;
        density.sampler                = TextureSampler::linear();
        density.sampler.repeatU = density.sampler.repeatV = true;
        SkyWispsLayer source;
        source.corners        = authored;
        source.densityTexture = &density;
        SkyAtmosphereParameters atmosphere;
        atmosphere.heightFog.densityPerMetre = .000551f;
        auto prepared                        = SkyAtmospherePass::prepare(*gfx, atmosphere, &source);
        REQUIRE(prepared.ok());
        auto pass = std::move(prepared).takeValue();
        REQUIRE(pass->attach().ok());
        camera->setTarget(.965925826f, 2.258819045f, 0);
        camera->setFov(46.6921257f);
        auto* hdr     = gfx->newHDRCanvas(1280, 720);
        auto* display = gfx->newCanvas(1280, 720);
        REQUIRE(hdr != nullptr);
        REQUIRE(display != nullptr);
        RenderSystem3D::renderToCanvas(*gfx, hdr, camera);
        const float gpuMs = gfx->getLastOffscreen3DGpuDurationMs();
        auto*       film =
            gfx->newShaderFromSpv({}, {scene_tonemap_frag_spv, scene_tonemap_frag_spv + scene_tonemap_frag_spv_count});
        REQUIRE(film != nullptr);
        gfx->setCanvas(display);
        gfx->drawTexturedRectShader(hdr->getTexture(), film, 0, 0, 1280, 720, Color(2, 1, 1, 65536));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(display->newImageData());
        REQUIRE(pixels != nullptr);
        std::ofstream image(root / "wisps.ppm", std::ios::binary);
        image << "P6\n1280 720\n255\n";
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x) {
                const auto pixel = pixels->getPixel(x, y);
                for (const float value : {pixel.r, pixel.g, pixel.b}) {
                    REQUIRE(std::isfinite(value));
                    image.put(static_cast<char>(std::lround(std::clamp(value, 0.f, 1.f) * 255)));
                }
            }
        REQUIRE(image.good());
        std::ofstream timing(root / "timing.json");
        timing << "{\"gpuOffscreenPassMs\":" << gpuMs << "}\n";
        REQUIRE(timing.good());
    }
}
#endif

#ifndef EVENGINE_WEBGPU
TEST_CASE("graphics.sky wisps preserve phase blending and celestial vertex lighting") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto*      gfx    = fixture.gfx;
    auto*      input  = gfx->newCanvas(16, 16);
    auto*      output = gfx->newHDRCanvas(16, 16);
    auto*      shader = gfx->newShaderFromSpv({}, sky_wisps_probe_frag);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    gfx->setCanvas(input);
    gfx->clear(Color(1, 1, 1, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 8, 16, Color(1, 1, 1, 1));
    gfx->drawSolidRect(8, 0, 8, 16, Color(0, 0, 0, 1));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> sourcePixels(input->newImageData());
    REQUIRE(sourcePixels != nullptr);
    REQUIRE(sourcePixels->getPixel(4, 8).r > .99f);
    REQUIRE(sourcePixels->getPixel(12, 8).r < .01f);
    const auto sample = [&](float mode, float time) {
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(mode, time, 0, 1));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    REQUIRE(std::abs(sample(0, 0).r - .2f) < .001f);
    REQUIRE(std::abs(sample(0, 2.5f).r - .1f) < .001f);
    REQUIRE(std::abs(sample(0, 5).r) < .001f);
    REQUIRE(std::abs(sample(0, 10).r - .2f) < .001f);
    const auto day = sample(1, 0), night = sample(2, 0), highlight = sample(3, 0);
    REQUIRE(std::abs(day.r - 1) < .001f);
    REQUIRE(std::abs(day.g) < .001f);
    REQUIRE(std::abs(day.b - 3.28f) < .005f);
    REQUIRE(std::abs(night.r - 1) < .001f);
    REQUIRE(std::abs(night.g) < .001f);
    REQUIRE(std::abs(night.b) < .001f);
    REQUIRE(std::abs(highlight.r - 3.5f) < .005f);
    REQUIRE(std::abs(highlight.g - 2.81245f) < .005f);
    REQUIRE(std::abs(highlight.b - 2.12f) < .005f);
    // A nonlinear parent transform must follow composition; transforming
    // the two inputs before mixing would produce .48525 rather than .462.
    const auto composed = sample(4, 0);
    REQUIRE(std::abs(composed.r - .462f) < .001f);
    REQUIRE(std::abs(composed.g - .2295f) < .001f);
    REQUIRE(std::abs(composed.b - .9f) < .001f);
    REQUIRE(std::abs(sample(5, -1).r) < .001f);
    REQUIRE(std::abs(sample(5, .25f).r - .5f) < .001f);
    REQUIRE(std::abs(sample(5, .5f).r - .8125f) < .001f);
    REQUIRE(std::abs(sample(5, .75f).r - .75f) < .001f);
    REQUIRE(std::abs(sample(5, 2).r - 1.f) < .001f);
    REQUIRE(sample(6, -.1f).r == 0);
    REQUIRE(sample(6, 0).r == 0);
    REQUIRE(std::abs(sample(6, .1f).r - .5f) < .001f);
    REQUIRE(sample(6, .3f).r == 1);
    REQUIRE(sample(6, 0).g == 1);
    // Captured UDS 5.8 material values, not values computed by this shader.
    const std::array<float, 4> hours{0, 6, 12, 18};
    const std::array<Color, 4> clouds{Color(.003047f, .005015f, .009361f, 1), Color(.020577f, .008931f, .006047f, 1),
                                      Color(.355900f, .399502f, .469850f, 1), Color(.020577f, .008931f, .006047f, 1)};
    const std::array<float, 4> midpoints{.020513f, .021585f, .852876f, .021585f};
    const std::array<float, 4> intensities{.45f, .689543f, .375f, .689543f};
    for (size_t i = 0; i < hours.size(); ++i) {
        const auto cloud = sample(7, hours[i]);
        REQUIRE(std::abs(cloud.r - clouds[i].r) < .0003f);
        REQUIRE(std::abs(cloud.g - clouds[i].g) < .0003f);
        REQUIRE(std::abs(cloud.b - clouds[i].b) < .0003f);
        const auto controls = sample(8, hours[i]);
        REQUIRE(std::abs(controls.r - midpoints[i]) < .0006f);
        REQUIRE(std::abs(controls.g - intensities[i]) < .0006f);
    }
    const auto midnightMoon = sample(9, 0);
    REQUIRE(std::abs(midnightMoon.r - .555242f) < .0006f);
    REQUIRE(std::abs(midnightMoon.b + .831689f) < .0006f);
    const auto dawnGlow = sample(10, 6);
    REQUIRE(std::abs(dawnGlow.r - .000453f) < .000002f);
    REQUIRE(std::abs(dawnGlow.b - .001242f) < .000002f);
    const auto dawnFog = sample(11, 6);
    REQUIRE(std::abs(dawnFog.r - .000308f) < .000002f);
    REQUIRE(std::abs(dawnFog.b - .000845f) < .000002f);
}
#endif
