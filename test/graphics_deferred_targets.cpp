#include <memory>
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.deferredTargets.lateGbufferAndResize") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    // A loading screen builds the shadow-only graph before any G-buffer exists.
    graphics->begin3DFrame();
    graphics->present();
    const float    positions[] = {-1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    auto*          mesh        = graphics->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t red[]   = {255, 0, 0, 255};
    auto*         texture = graphics->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    for (int size : {64, 96}) {
        graphics->beginGBufferPass(size, size);
        graphics->drawMeshGBuffer(mesh, glm::mat4(1), glm::mat4(1), 0.1f, 100.f, texture);
        graphics->endGBufferPass();
        std::unique_ptr<eve::image::ImageData> image(graphics->readGBufferToImageData("albedo"));
        REQUIRE(image != nullptr);
        REQUIRE_EQ(image->getWidth(), size);
        const auto*  pixels = static_cast<const uint8_t*>(image->getData());
        const size_t center = (size_t(size / 2) * size + size / 2) * 4;
        REQUIRE(pixels[center] > 247);
        REQUIRE(pixels[center + 1] < 8);
        REQUIRE(pixels[center + 2] < 8);
        graphics->begin3DFrame();
        graphics->present();
        // Removing all geometry must still execute the clear-only pass.
        graphics->beginGBufferPass(size, size);
        graphics->endGBufferPass();
        std::unique_ptr<eve::image::ImageData> empty(graphics->readGBufferToImageData("depth"));
        REQUIRE(empty != nullptr);
        const auto* emptyPixels = static_cast<const uint8_t*>(empty->getData());
        REQUIRE_EQ(emptyPixels[center], uint8_t(255));
        graphics->begin3DFrame();
        graphics->present();
    }
}
