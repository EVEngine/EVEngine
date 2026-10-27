// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"render-offscreen","module":"graphics"}
*/
// clang-format on

// Full scale/resource-pressure workloads; short integration contracts are not equivalent.
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/graphics/PixelWorldGraphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_graphics_backend_resource_scale {
using namespace eve::graphics;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.backendParity.textureUpdateAndAlphaBlend") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    REQUIRE(backend == expectedBackendName());

    const uint8_t red[4]   = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    Texture      *texture  = gfx->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    Texture *stable = texture;
    REQUIRE(gfx->updateTexture(texture, 1, 1, green));
    REQUIRE(texture == stable);
    // Same-size streaming updates must retain the Vulkan image and descriptor.
    // The former replacement path exhausted the shared descriptor pool after
    // a few hundred frames in the volume-fluid presentation loop.
    for (int frame = 0; frame < 400; ++frame) REQUIRE(gfx->updateTexture(texture, 1, 1, (frame & 1) ? red : green));
    REQUIRE(!gfx->updateTexture(texture, 2, 1, green));
    REQUIRE(gfx->updateTextureRegion(texture, 0, 0, 1, 1, red).ok());
    CHECK(!gfx->updateTextureRegion(texture, 1, 0, 1, 1, red).ok());
    CHECK(!gfx->updateTextureRegion(texture, 0, 0, 1, 1, std::span<const uint8_t>(red, 3)).ok());
    const TextureRegionUpload rejectedBatch[] = {
        {0, 0, 1, 1, green, 0},
        {1, 0, 1, 1, green, 0},
    };
    CHECK(!gfx->updateTextureRegions(texture, rejectedBatch).ok());

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawTexturedRect(texture, 8.f, 8.f, 24.f, 24.f, Color(1.f));
    gfx->drawSolidRect(32.f, 8.f, 24.f, 24.f, Color(1.f, 0.f, 0.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);
    const uint8_t *background = pixel(*image, 2, 2);
    REQUIRE(background[0] < 8);
    REQUIRE(background[1] < 8);
    REQUIRE(background[2] > 247);

    const uint8_t *updated = pixel(*image, 16, 16);
    REQUIRE(updated[0] > 247);
    REQUIRE(updated[1] < 8);
    REQUIRE(updated[2] < 8);

    const uint8_t *blended    = pixel(*image, 40, 16);
    const bool     redBlended = blended[0] >= 126 && blended[0] <= 129;
    REQUIRE(redBlended);
    REQUIRE(blended[1] < 8);
    const bool blueBlended = blended[2] >= 126 && blended[2] <= 129;
    REQUIRE(blueBlended);
    writeParityArtifact(*image, "texture_update_alpha_blend", backend);
}


TEST_CASE("graphics.backendParity.pixelWorldMillionPixelAtlas") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    eve::pixelworld::PixelWorld world(4242);
    // Radius 724 covers every cell in [0,1024)^2, creating a genuine one-million
    // pixel viewport while also proving off-viewport chunks are clipped by the adapter.
    REQUIRE(world.paintCircleChecked(512, 512, 724, "stone").ok());
    eve::pixelworld_graphics::PixelWorldAtlasRenderer renderer(0, 0, 1024, 1024);
    REQUIRE(renderer.sync(&world, gfx) == 256);
    REQUIRE(renderer.texture() != nullptr);
    REQUIRE(renderer.texture()->width == 1024);
    REQUIRE(renderer.texture()->height == 1024);
    REQUIRE(renderer.uploadCount() == 1);
    REQUIRE(renderer.totalUploadedChunks() == 256);
    REQUIRE(renderer.renderedRevision() == world.revision());

    world.setMaterial(1, 1, "sand");
    world.setMaterial(1022, 1022, "water");
    REQUIRE(renderer.sync(&world, gfx) == 2);
    REQUIRE(renderer.uploadCount() == 2);
    REQUIRE(renderer.totalUploadedChunks() == 258);
    REQUIRE(renderer.renderedRevision() == world.revision());
}

}  // namespace test_graphics_backend_resource_scale
