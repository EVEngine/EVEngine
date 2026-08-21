#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <functional>
#include <memory>

using namespace eve::graphics;

namespace {

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

}  // namespace

// One case so the shared Graphics singleton is initialized exactly once even in
// bundle mode (bundle/graphics_headless). Error contracts run before init.
TEST_CASE("graphics.headless.initCanvasDrawAndErrors") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);

    // Invalid sizes and double-init must throw.
    CHECK(expectException([&] { gfx->initHeadless(0, 240); }));
    CHECK(expectException([&] { gfx->initHeadless(320, -1); }));

    gfx->initHeadless(320, 240);
    CHECK(gfx->isHeadless());
    CHECK(gfx->getBackendName() == "vulkan");
    CHECK(gfx->getWidth() == 320);
    CHECK(gfx->getHeight() == 240);
    CHECK(expectException([&] { gfx->initHeadless(320, 240); }));

    // Render a solid rect into an offscreen canvas and read it back.
    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    gfx->setCanvas(rt);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(8.f, 8.f, 32.f, 32.f, Color(1.f, 0.f, 0.f, 1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const int w = img->getWidth();
    const int h = img->getHeight();
    REQUIRE(w == 64);
    REQUIRE(h == 64);
    const auto *px = static_cast<const unsigned char *>(img->getData());

    int lit = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] > 128 && p[1] < 64 && p[2] < 64) ++lit;
        }
    }
    CHECK(lit > 0);  // the red rectangle was rendered into the canvas

    // Background stays the clear color outside the rect.
    const unsigned char *corner = px + (size_t(1) * w + 1) * 4;
    CHECK(corner[0] < 32);
    CHECK(corner[1] < 32);
    CHECK(corner[2] < 32);

    // present() must be a no-op (no swapchain), not a crash; drawing after
    // present also stays valid.
    gfx->present();
    const uint8_t white[4] = {255, 255, 255, 255};
    Texture *tex = gfx->newTexture(1, 1, white);
    REQUIRE(tex != nullptr);
    gfx->drawTexturedRect(tex, 0.f, 0.f, 16.f, 16.f, Color(1.f, 1.f, 1.f, 1.f));
    gfx->present();
    gfx->clearScreen();
}
