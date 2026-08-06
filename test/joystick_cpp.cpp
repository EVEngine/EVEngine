#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "joystick/Joystick.h"
#include "joystick/Pad.h"
#include "common/Exception.h"

namespace {

eve::joystick::Joystick* joystick() {
    return eve::joystick::Joystick::create();
}

}  // namespace

TEST_CASE("joystick.create") {
    auto* joy = joystick();
    REQUIRE(joy != nullptr);
    CHECK(joy->getName() == "Joystick");
    CHECK(joy->getJoystickCount() >= 0);
}

TEST_CASE("joystick.getJoystickOutOfRange") {
    auto* joy = joystick();
    REQUIRE(joy != nullptr);
    CHECK(joy->getJoystick(-1) == nullptr);
    CHECK(joy->getJoystick(joy->getJoystickCount()) == nullptr);
    CHECK(joy->getJoystickFromID(-99999) == nullptr);
}

TEST_CASE("joystick.mappingRoundTrip") {
    auto* joy = joystick();
    REQUIRE(joy != nullptr);

    // Empty is allowed; invalid non-empty should throw.
    joy->loadGamepadMappings("");
    std::string saved = joy->saveGamepadMappings();
    // May be empty if no gamepads have been seen.
    CHECK(saved.find("invalid") == std::string::npos);

    bool threw = false;
    try {
        joy->loadGamepadMappings("not-a-valid-mapping-line");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("joystick.connectedPadsQueryable") {
    auto* joy = joystick();
    REQUIRE(joy != nullptr);

    int n = joy->getJoystickCount();
    for (int i = 0; i < n; i++) {
        auto* pad = joy->getJoystick(i);
        REQUIRE(pad != nullptr);
        CHECK(joy->getIndex(pad) == i);
        CHECK(pad->getID() >= 0);
        // Connected pads expose non-negative counts.
        if (pad->isConnected()) {
            CHECK(pad->getAxisCount() >= 0);
            CHECK(pad->getButtonCount() >= 0);
            CHECK(pad->getHatCount() >= 0);
            CHECK(!pad->getGUID().empty());
            (void)pad->isDown(0);
            (void)pad->isGamepadDown("a");
            (void)pad->getGamepadAxis("leftx");
            (void)pad->getHat(0);
        }
    }
}
