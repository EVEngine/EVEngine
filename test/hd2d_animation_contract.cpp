#include "hd2d/Hd2d.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

TEST_CASE("hd2d.sprite.gridReplacementStopsOldClip") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(18, 23, 10.f);
    sprite.setFrameGrid(2, 1);
    REQUIRE(!sprite.isPlaying());
    sprite.update(1.f);
    REQUIRE_LT(sprite.getFrameIndex(), sprite.getFrameCount());
}

TEST_CASE("hd2d.sprite.invalidTimeDoesNotPoisonWalkClock") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(6, 11, 10.f);
    sprite.update(-10.f);
    sprite.update(std::numeric_limits<float>::quiet_NaN());
    sprite.update(std::numeric_limits<float>::infinity());
    sprite.update(0.21f);
    REQUIRE_EQ(sprite.getFrameIndex(), 8);
}

TEST_CASE("hd2d.sprite.largeTimeRemainsInClip") {
    eve::hd2d::Sprite3D sprite;
    sprite.setFrameGrid(6, 4);
    sprite.play(12, 17, 8.f);
    sprite.update(1073741824.f);
    REQUIRE_GE(sprite.getFrameIndex(), 12);
    REQUIRE_LE(sprite.getFrameIndex(), 17);
    sprite.update(0.125f);
    REQUIRE_GE(sprite.getFrameIndex(), 12);
    REQUIRE_LE(sprite.getFrameIndex(), 17);
}
