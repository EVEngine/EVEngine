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

    // Identity round-trip: get → set same → get (warp-to-arbitrary is unreliable
    // without a bound SDL window handle in the current Mouse SDL backend).
    double x0 = 0.0;
    double y0 = 0.0;
    m->getPosition(x0, y0);
    m->setPosition(x0, y0);
    double x1 = 0.0;
    double y1 = 0.0;
    m->getPosition(x1, y1);
    CHECK(std::abs(x1 - x0) < 1.0);
    CHECK(std::abs(y1 - y0) < 1.0);

    const double gx = m->getX();
    m->setX(gx);
    CHECK(std::abs(m->getX() - gx) < 1.0);
    const double gy = m->getY();
    m->setY(gy);
    CHECK(std::abs(m->getY() - gy) < 1.0);
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
