#include <cmath>
#include <memory>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"
#ifndef EVENGINE_WEBGPU
#include "SkyWispsSpv.h"

TEST_CASE("graphics.sky lunar surface preserves phase earthlight occultation and source orientation") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(16, 16, true);
    auto*      gfx    = fixture.gfx;
    auto*      input  = gfx->newHDRCanvas(16, 16);
    auto*      output = gfx->newHDRCanvas(16, 16);
    auto*      shader = gfx->newShaderFromSpv({}, sky_wisps_probe_frag);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(shader != nullptr);
    // The source normal swizzle RBG maps this sample to (0,-1,0).
    gfx->setCanvas(input);
    gfx->clear(Color(.5f, .5f, 0, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 16, 16, Color(.5f, .5f, 0, 1));
    gfx->setCanvas();
    const auto sample = [&](float mode, float phase, float offset) {
        gfx->setCanvas(output);
        gfx->clear(Color(0, 0, 0, 0), std::nullopt, std::nullopt);
        gfx->drawTexturedRectShader(input->getTexture(), shader, 0, 0, 16, 16, Color(mode, phase, offset, 1));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(output->newHDRImageData());
        REQUIRE(pixels != nullptr);
        return pixels->getPixel(8, 8);
    };
    const auto full = sample(12, 0, 0), quarter = sample(12, 7.3825f, 0), dark = sample(12, 14.765f, 0);
    REQUIRE(std::abs(full.r - .5f) < .001f);
    REQUIRE(std::abs(quarter.r - .0025f) < .0001f);
    REQUIRE(std::abs(dark.r - .0025f) < .0001f);
    REQUIRE(full.a == 1);
    REQUIRE(dark.a == 1);  // Unlit lunar surface still blocks background stars.
    const auto opposite = sample(13, 0, 0), outside = sample(12, 0, .03f);
    REQUIRE(opposite.r == 0);
    REQUIRE(opposite.a == 0);
    REQUIRE(outside.r == 0);
    REQUIRE(outside.a == 0);
    const auto midnight = sample(14, 0, 0), dawn = sample(14, 6, 0), noon = sample(14, 12, 0);
    REQUIRE(std::abs(midnight.g + 1) < .001f);
    REQUIRE(std::abs(dawn.r - .573269f) < .001f);
    REQUIRE(std::abs(dawn.g + .032749f) < .001f);
    REQUIRE(std::abs(dawn.b + .818713f) < .001f);
    REQUIRE(std::abs(noon.g - 1) < .001f);
}
#endif
