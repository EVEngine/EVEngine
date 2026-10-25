// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "helper",
  "default": "render-window",
  "module": "common"
}
*/
// clang-format on

#include "Fixtures.h"

#include <cstdio>

namespace {
void rejectRenderInUnitLane() {
    if (std::getenv("EVENGINE_TEST_NO_RENDER") != nullptr) {
        // Helpers do not receive zeroerr's TestContext. A helper REQUIRE can
        // print an assertion yet leave the caller's result green; this boundary
        // must fail the isolated process before touching SDL or the device.
        std::fprintf(stderr, "EVENGINE_TEST_NO_RENDER: unit test attempted rendering startup\n");
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
}
}  // namespace

void openGfxWindow(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w, int h) {
    rejectRenderInUnitLane();
    const char *viewW = std::getenv("EVENGINE_TEST_VIEW_W");
    const char *viewH = std::getenv("EVENGINE_TEST_VIEW_H");
    if (viewW && viewW[0]) w = std::atoi(viewW);
    if (viewH && viewH[0]) h = std::atoi(viewH);
    if (w <= 0 || h <= 0) {
        // Invalid EVENGINE_TEST_VIEW_W/H; fail the test that requested a window.
        REQUIRE(false);
        return;
    }

    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = static_cast<uint16_t>(w);
    s.height   = static_cast<uint16_t>(h);
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

void openHeadlessGfx(eve::graphics::Graphics *&gfx, int w, int h) {
    rejectRenderInUnitLane();
    const char *viewW = std::getenv("EVENGINE_TEST_VIEW_W");
    const char *viewH = std::getenv("EVENGINE_TEST_VIEW_H");
    if (viewW && viewW[0]) w = std::atoi(viewW);
    if (viewH && viewH[0]) h = std::atoi(viewH);
    if (w <= 0 || h <= 0) {
        // Invalid EVENGINE_TEST_VIEW_W/H; fail the test that requested graphics.
        REQUIRE(false);
        return;
    }

    gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(w, h);
}
