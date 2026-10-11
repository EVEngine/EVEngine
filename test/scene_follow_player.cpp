#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "scene/FollowPlayer.h"

#include <limits>

using namespace eve::scene;

TEST_CASE("scene.followPlayer.matchesPcgOffsetsAndWaterThreshold") {
    FollowPlayerSettings settings;
    FollowPlayerInput input;
    FollowPlayerOutput output;
    input.hasPlayer = true;
    input.playerX = 10.f;
    input.playerY = 0.5f;
    input.playerZ = -20.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(output.applyPosition);
    CHECK_EQ(output.x, 10.f);
    CHECK_EQ(output.y, 0.5f);
    CHECK_EQ(output.z, -20.f);

    settings.useOffset = true;
    settings.offsetX = 1250.f;
    settings.offsetY = 200.f;
    settings.offsetZ = 300.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.x, 1260.f);
    CHECK_EQ(output.y, -199.5f);
    CHECK_EQ(output.z, -320.f);

    settings.waterObject = true;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -129.5f);
    input.playerY = 1.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -189.f);
}

TEST_CASE("scene.followPlayer.scaleNoPlayerAndAtomicValidation") {
    FollowPlayerSettings settings;
    settings.followPlayer = false;
    settings.useScale = true;
    settings.scaleX = 2.f;
    settings.scaleY = 3.f;
    settings.scaleZ = 4.f;
    FollowPlayerInput input;
    FollowPlayerOutput output;
    output.x = 77.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(!output.applyPosition);
    CHECK(output.applyScale);
    CHECK_EQ(output.scaleX, 2.f);
    CHECK_EQ(output.scaleY, 3.f);
    CHECK_EQ(output.scaleZ, 4.f);

    const auto unchanged = output;
    settings.offsetX = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateFollowPlayer(&output, &settings, &input));
    CHECK_EQ(output.applyScale, unchanged.applyScale);
    CHECK_EQ(output.scaleX, unchanged.scaleX);
    CHECK(!evaluateFollowPlayer(nullptr, &settings, &input));
}
