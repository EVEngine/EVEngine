#include "GraphicsParitySupport.h"
#include "zeroerr/unittest.h"

#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/MapFog.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <cstdint>
#include <memory>
#include <vector>

using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::MapFog;
using eve::graphics::Texture;
using eve::graphics::parity_test::headlessGraphics;

namespace {

const uint8_t *pixel(eve::image::ImageData &img, int x, int y) {
    return static_cast<const uint8_t *>(img.getData()) + size_t((y * img.getWidth() + x) * 4);
}

}  // namespace

TEST_CASE("graphics.mapFog.unlockedCellsStayClear") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    // 2x1 mask: left fogged (R=0), right unlocked (R=1).
    const std::vector<uint8_t> maskRgba = {
        0,   0, 0, 255,  // fogged
        255, 0, 0, 255,  // unlocked
    };
    Texture *mask = gfx->newTexture(2, 1, maskRgba.data());
    REQUIRE(mask != nullptr);

    std::unique_ptr<MapFog> fog(gfx->newMapFog());
    REQUIRE(fog.get() != nullptr);
    fog->setMaskTexture(mask);
    fog->setCloudTexture(fog->makeCloudTexture(64));
    fog->setTime(0.25f);
    fog->setShadowEnabled(false);
    fog->setFogAlpha(1.f);
    fog->setFogColor(1.f, 1.f, 1.f);
    fog->setDistort(0.f);
    fog->setEdgeSoftness(0.05f);

    auto *canvas = gfx->newCanvas(64, 32);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    fog->draw(0.f, 0.f, 64.f, 32.f);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);

    const uint8_t *left  = pixel(*image, 8, 16);
    const uint8_t *right = pixel(*image, 56, 16);

    // Fogged side should no longer be pure background blue.
    CHECK(int(left[2]) < 200);
    // Unlocked side should remain close to the clear blue background.
    CHECK(int(right[2]) > 200);
    CHECK(int(right[0]) < 40);
}

TEST_CASE("graphics.mapFog.makeCloudTextureIsRepeatable") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    std::unique_ptr<MapFog> fog(gfx->newMapFog());
    REQUIRE(fog.get() != nullptr);
    Texture *cloud = fog->makeCloudTexture(32);
    REQUIRE(cloud != nullptr);
    CHECK(cloud->getWidth() == 32);
    CHECK(cloud->getHeight() == 32);
    fog->setCloudTiling(2.f, 3.f);
    fog->setCloudSpeed(0.01f, 0.02f);
    CHECK(fog->getCloudTileA() == 2.f);
    CHECK(fog->getCloudTileB() == 3.f);
}
