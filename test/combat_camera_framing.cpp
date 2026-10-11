#include "combat/CombatCamera.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

TEST_CASE("combatCameraFraming.looksAtMidpointFromBehindPlayer") {
    eve::combat::CombatCameraFramingRequest request;
    request.player       = {0.0, 0.0, 0.0};
    request.playerFacing = {1.0, 0.0, 0.0};
    request.lockTarget   = eve::combat::CombatVector3{4.0, 0.0, 0.0};
    request.distance     = 6.0;
    request.height       = 2.0;
    request.lookHeight   = 1.2;
    auto view            = eve::combat::CombatCameraFraming::solve(request);
    REQUIRE(view.ok());
    CHECK(std::abs(view.value().lookAt.x - 2.0) < 1e-9);
    CHECK(std::abs(view.value().lookAt.y - 1.2) < 1e-9);
    CHECK(std::abs(view.value().eye.x + 6.0) < 1e-9);
    CHECK(std::abs(view.value().eye.y - 2.0) < 1e-9);
    CHECK(std::abs(view.value().eye.z) < 1e-9);
}

TEST_CASE("combatCameraFraming.followsFacingWithoutLock") {
    eve::combat::CombatCameraFramingRequest request;
    request.player       = {0.0, 0.0, 0.0};
    request.playerFacing = {0.0, 0.0, 1.0};
    request.distance     = 6.0;
    request.height       = 2.0;
    request.lookHeight   = 1.2;
    auto view            = eve::combat::CombatCameraFraming::solve(request);
    REQUIRE(view.ok());
    CHECK(std::abs(view.value().lookAt.z) < 1e-9);
    CHECK(std::abs(view.value().eye.z + 6.0) < 1e-9);
    CHECK(std::abs(view.value().eye.y - 2.0) < 1e-9);
}

TEST_CASE("combatCameraFraming.rejectsInvalidDistance") {
    eve::combat::CombatCameraFramingRequest request;
    request.player   = {0.0, 0.0, 0.0};
    request.distance = 0.0;
    CHECK(!eve::combat::CombatCameraFraming::solve(request).ok());
}
