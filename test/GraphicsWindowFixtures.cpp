#include "Fixtures.h"

void openGfxWindow(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w, int h) {
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
