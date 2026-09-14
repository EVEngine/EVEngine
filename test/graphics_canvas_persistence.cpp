#include "zeroerr/unittest.h"
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include <cmath>
using namespace eve::graphics;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.canvas.separateSubmissionsPreserveAndClearRGBA8") {
    auto* gfx = headlessGraphics();
    auto* canvas = gfx->newCanvas(16, 16);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 8, 16, Color(1.f, 0.f, 0.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();
    gfx->setCanvas(canvas);
    gfx->drawSolidRect(0, 0, 4, 16, Color(0.f, 0.f, 1.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> snapshot(canvas->newImageData());
    auto overlap = snapshot->getPixel(2, 2);
    CHECK(std::abs(overlap.r - 0.25f) < 0.01f);
    CHECK(std::abs(overlap.b - 0.5f) < 0.01f);
    CHECK(std::abs(overlap.a - 0.75f) < 0.01f);
    CHECK(std::abs(snapshot->getPixel(6, 2).r - 0.5f) < 0.01f);
    CHECK(snapshot->getPixel(12, 2).a == 0.f);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(12, 0, 4, 16, Color(0.f, 1.f, 0.f, 1.f));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> cleared(canvas->newImageData());
    CHECK(cleared->getPixel(2, 2).a == 0.f);
    CHECK(cleared->getPixel(14, 2).g == 1.f);
    CHECK(std::abs(snapshot->getPixel(2, 2).a - 0.75f) < 0.01f);
}
