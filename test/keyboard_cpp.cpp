#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "keyboard/Keyboard.h"
#include "window/Window.h"

namespace {

eve::keyboard::Keyboard* keyboard() {
    return eve::keyboard::Keyboard::create();
}

eve::window::Window* ensureVideoWindow() {
    auto* win = eve::window::Window::create();
    if (win == nullptr) return nullptr;
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    if (!win->setWindowSettings(settings)) return nullptr;
    return win;
}

}  // namespace

TEST_CASE("keyboard.create") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    CHECK(kb->getName() == "Keyboard");
}

TEST_CASE("keyboard.keyRepeat") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    kb->setKeyRepeat(false);
    CHECK(!kb->hasKeyRepeat());
    kb->setKeyRepeat(true);
    CHECK(kb->hasKeyRepeat());
}

TEST_CASE("keyboard.textInput") {
    auto* win = ensureVideoWindow();
    if (win == nullptr) return;

    auto* kb = keyboard();
    REQUIRE(kb != nullptr);

    kb->setTextInput(false);
    CHECK(!kb->hasTextInput());
    kb->setTextInput(true);
    CHECK(kb->hasTextInput());
    kb->setTextInput(false);
    CHECK(!kb->hasTextInput());
}

TEST_CASE("keyboard.keyScancodeRoundTrip") {
    auto* win = ensureVideoWindow();
    if (win == nullptr) return;

    auto* kb = keyboard();
    REQUIRE(kb != nullptr);

    // SDL names; layout may remap but A should round-trip on QWERTY-like layouts.
    std::string scancode = kb->getScancodeFromKey("A");
    CHECK(!scancode.empty());
    std::string key = kb->getKeyFromScancode(scancode);
    CHECK(!key.empty());
}

TEST_CASE("keyboard.isDownUnknown") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    CHECK(!kb->isDown("NotARealKey"));
    CHECK(!kb->isScancodeDown("NotARealScancode"));
}
