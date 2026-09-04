#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "avatar/AvatarInstance.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

TEST_CASE("avatar.attachment_point samples an owned world-pose snapshot") {
    eve::animation::AnimSkeleton skeleton;
    const int root = skeleton.addBone("Root", -1);
    const int blade = skeleton.addBone("BladeRoot", root);
    skeleton.setBindPosition(blade, 0.f, 2.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    eve::avatar::AvatarInstance avatar("vroid");
    REQUIRE(avatar.bindAnimPlayer(&player));
    REQUIRE(avatar.mapHumanoidBone("weaponRoot", "BladeRoot"));
    avatar.setPosition3D(10.f, 20.f, 30.f);
    avatar.setScale3D(2.f, 3.f, 4.f);
    avatar.setRotation3D(1.57079632679f, 0.f, 0.f);
    avatar.update(0.f);

    auto semantic = avatar.sampleAttachmentPoint("weaponRoot", {1.f, 0.f, 0.f});
    REQUIRE(semantic.ok());
    CHECK(std::fabs(semantic.value().x - 10.f) < 1e-4f);
    CHECK(std::fabs(semantic.value().y - 26.f) < 1e-4f);
    CHECK(std::fabs(semantic.value().z - 28.f) < 1e-4f);
    auto native = avatar.sampleAttachmentPoint("BladeRoot", {1.f, 0.f, 0.f});
    REQUIRE(native.ok());
    CHECK(std::fabs(native.value().x - semantic.value().x) < 1e-5f);
    REQUIRE(!avatar.sampleAttachmentPoint("missing").ok());

    eve::animation::AnimSkeleton replacementSkeleton;
    replacementSkeleton.addBone("Replacement", -1);
    eve::animation::AnimPlayer replacementPlayer(&replacementSkeleton);
    REQUIRE(avatar.bindAnimPlayer(&replacementPlayer));
    REQUIRE(!avatar.sampleAttachmentPoint("BladeRoot").ok());
    avatar.update(0.f);
    REQUIRE(!avatar.sampleAttachmentPoint("BladeRoot").ok());

    avatar.release();
    REQUIRE(!avatar.sampleAttachmentPoint("Replacement").ok());
}
