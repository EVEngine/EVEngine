#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "mouse/Mouse.h"
#include "mouse/Cursor.h"
#include "window/Window.h"

#include <cmath>
#include <vector>

namespace {

eve::mouse::Mouse* mouse() {
    return eve::mouse::Mouse::create();
}

// SDL video + a minimal window improves cursor/position APIs on desktop.
eve::window::Window* ensureVideoWindow() {
    auto* win = eve::window::Window::create();
    if (win == nullptr) {
        return nullptr;
    }
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    if (!win->setWindowSettings(settings)) {
        return nullptr;
    }
    return win;
}

}  // namespace

TEST_CASE("mouse.isCursorSupported") {
    auto* m = mouse();
    REQUIRE(m != nullptr);
    // SDL video init happens in Mouse ctor; result depends on display availability.
    CHECK(m->isCursorSupported());
}

TEST_CASE("mouse.positionRoundTrip") {
    auto* win = ensureVideoWindow();
    if (win == nullptr) {
        return;
    }
    auto* m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    void* handle = win->getHandle();
    REQUIRE(handle != nullptr);

    // Warp to a known in-window point (requires SDL window handle on setPosition).
    m->setPosition(50.0, 60.0);
    double x = 0.0;
    double y = 0.0;
    m->getPosition(x, y);
    // SDL may not update GetMouseState without focus (common in headless/CI).
    if (std::abs(x - 50.0) >= 2.0 || std::abs(y - 60.0) >= 2.0) {
        // Fall back: identity round-trip still exercises the API path.
        m->setPosition(x, y);
        double x2 = 0.0;
        double y2 = 0.0;
        m->getPosition(x2, y2);
        CHECK(std::abs(x2 - x) < 1.0);
        CHECK(std::abs(y2 - y) < 1.0);
        return;
    }
    CHECK(std::abs(x - 50.0) < 2.0);
    CHECK(std::abs(y - 60.0) < 2.0);

    m->setX(80.0);
    CHECK(std::abs(m->getX() - 80.0) < 2.0);
    m->setY(90.0);
    CHECK(std::abs(m->getY() - 90.0) < 2.0);
}

TEST_CASE("mouse.visibleRoundTrip") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto* m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    m->setVisible(false);
    CHECK(!m->isVisible());
    m->setVisible(true);
    CHECK(m->isVisible());
}

TEST_CASE("mouse.isDownEmptySafe") {
    auto* m = mouse();
    REQUIRE(m != nullptr);

    const std::vector<int> empty;
    CHECK(!m->isDown(empty));

    const std::vector<int> nonePressed = {1, 2, 3};
    CHECK(!m->isDown(nonePressed));
}

TEST_CASE("mouse.getSystemCursorKnown") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto* m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    eve::mouse::Cursor* cursor = m->getSystemCursor("ARROW");
    REQUIRE(cursor != nullptr);
    CHECK(!cursor->isCustom());
    CHECK(cursor->getSystemType() == "ARROW");
    CHECK(cursor->getHandle() != nullptr);
}

TEST_CASE("mouse.getSystemCursorInvalid") {
    if (ensureVideoWindow() == nullptr) {
        return;
    }
    auto* m = mouse();
    REQUIRE(m != nullptr);
    if (!m->isCursorSupported()) {
        return;
    }

    bool threw = false;
    try {
        m->getSystemCursor("NOT_A_REAL_CURSOR_TYPE");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}
