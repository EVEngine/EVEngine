#include <cmath>
#include <memory>
#include "Fixtures.h"
#include "common/config.h"
#include "graphics/Bloom.h"
#include "graphics/Canvas.h"
#include "graphics/shaders/PostProcessWgsl.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/shaders/textured_frag_spv.inc"
#endif
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.bloom photographic pyramid preserves weighted DC and threshold suppression") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    GfxFixture fixture(64, 32, true);
    auto*      gfx   = fixture.gfx;
    auto*      input = gfx->newHDRCanvas(1281, 721);
    REQUIRE(input != nullptr);
    gfx->setCanvas(input);
    gfx->clear(Color(1, .5f, .25f, 1), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 1281, 721, Color(1, .5f, .25f, 1));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> sourcePixels(input->newHDRImageData());
    REQUIRE(sourcePixels != nullptr);
    REQUIRE(sourcePixels->getPixel(32, 16).r == 1);
#ifdef EVENGINE_WEBGPU
    auto* copyShader =
        gfx->newShaderFromWgsl({}, std::string(shaders::kPostCommon) +
                                       "@fragment fn fs_main(i:FSIn)->@location(0) vec4f{return tex(i.uv)*i.color;}");
#else
    auto* copyShader = gfx->newShaderFromSpv({}, {textured_frag_spv, textured_frag_spv + textured_frag_spv_count});
#endif
    REQUIRE(copyShader != nullptr);
    Bloom               bloom(gfx);
    BloomFilterSettings settings;
    settings.filter = BloomFilter::GaussianPyramid;
    REQUIRE(bloom.configureFilter(settings).ok());
    const auto sample = [&](float threshold) {
        auto* result = bloom.apply(input->getTexture(), .675f, threshold);
        REQUIRE(result != nullptr);
        auto* output = gfx->newHDRCanvas(64, 32);
        REQUIRE(output != nullptr);
        gfx->setCanvas(output);
        gfx->drawTexturedRectShader(result, copyShader, 0, 0, 64, 32, Color(1));
        gfx->setCanvas();
        return std::unique_ptr<eve::image::ImageData>(output->newHDRImageData());
    };
    auto bright = sample(0);
    REQUIRE(bright != nullptr);
    constexpr float energy = 1 + .675f * (.3465f + .138f + .1176f + .066f + .066f + .061f) / 6;
    const auto      center = bright->getPixel(32, 16);
    REQUIRE(std::abs(center.r - energy) < .004f);
    for (const auto& point : {std::pair{0, 0}, std::pair{63, 31}}) {
        const auto pixel = bright->getPixel(point.first, point.second);
        // The reference blur uses a black border, not edge replication.
        REQUIRE(pixel.r > 1);
        REQUIRE(pixel.r < center.r);
        REQUIRE(std::abs(pixel.g - pixel.r * .5f) < .003f);
        REQUIRE(std::abs(pixel.b - pixel.r * .25f) < .002f);
    }
    auto suppressed = sample(10);
    REQUIRE(suppressed != nullptr);
    REQUIRE(std::abs(suppressed->getPixel(32, 16).r - 1) < .002f);
    // Equal level counts must not reuse fast-blur intermediates with wrong dimensions.
    settings.filter = BloomFilter::GaussianScatter;
    REQUIRE(bloom.configureFilter(settings).ok());
    auto scattered = sample(0);
    REQUIRE(scattered != nullptr);
    REQUIRE(std::abs(scattered->getPixel(32, 16).r - 1.675f) < .008f);
    settings.filter = BloomFilter::GaussianPyramid;
    REQUIRE(bloom.configureFilter(settings).ok());
    auto restored = sample(0);
    REQUIRE(restored != nullptr);
    REQUIRE(std::abs(restored->getPixel(32, 16).r - energy) < .004f);
}
