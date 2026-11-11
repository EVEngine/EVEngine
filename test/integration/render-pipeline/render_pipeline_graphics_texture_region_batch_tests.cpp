// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"graphics","default":"render-offscreen"}
#include "GraphicsParitySupport.h"
#include "zeroerr/unittest.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <array>
#include <cstdint>
#include <memory>

using namespace eve::graphics;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.textureRegionBatch.strideOverlapAndReadback") {
    Graphics* gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    std::array<std::uint8_t, 4 * 4 * 4> blue{};
    for (std::size_t index = 0; index < blue.size(); index += 4) {
        blue[index + 2] = 255;
        blue[index + 3] = 255;
    }
    Texture* texture = gfx->newTexture(4, 4, blue.data());
    REQUIRE(texture != nullptr);

    // The first upload has a 16-byte source stride for 12 bytes of pixels.
    const std::array<std::uint8_t, 28> red = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 19, 19, 19, 19, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    const std::array<std::uint8_t, 16> green = {
        0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
    };
    const TextureRegionUpload batch[] = {
        {0, 0, 3, 2, red, 16},
        {1, 1, 2, 2, green, 0},
    };
    REQUIRE(gfx->updateTextureRegions(texture, batch).ok());

    Canvas* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->drawTexturedRect(texture, 0.f, 0.f, 64.f, 64.f, Color(1.f));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto* redPixel = pixel(*image, 8, 8);
    CHECK(redPixel[0] > 247);
    CHECK(redPixel[1] < 8);
    CHECK(redPixel[2] < 8);
    const auto* greenPixel = pixel(*image, 24, 24);
    CHECK(greenPixel[0] < 8);
    CHECK(greenPixel[1] > 247);
    CHECK(greenPixel[2] < 8);
    const auto* bluePixel = pixel(*image, 56, 56);
    CHECK(bluePixel[0] < 8);
    CHECK(bluePixel[1] < 8);
    CHECK(bluePixel[2] > 247);
}
