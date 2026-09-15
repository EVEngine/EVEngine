#include <array>
#include <cmath>
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::animation;
TEST_CASE("animation.pose.twoBoneIkRespectsRotatedScaledParent") {
    AnimSkeleton skeleton;
    skeleton.addBone("parent");
    skeleton.addBone("hip", 0);
    skeleton.addBone("knee", 1);
    skeleton.addBone("ankle", 2);
    skeleton.setBindPosition(0, .5f, .25f, -.3f);
    skeleton.setBindRotation(0, 0.f, .70710678f, 0.f, .70710678f);
    skeleton.setBindScale(0, 1.5f, 1.5f, 1.5f);
    skeleton.setBindPosition(1, 0.f, .1f, 0.f);
    skeleton.setBindPosition(2, 0.f, 1.f, 0.f);
    skeleton.setBindPosition(3, 0.f, 1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 1, 2, 3, .8f, 2.1f, .1f, 1.f));
    CHECK(std::abs(pose.world(3).px - .8f) < .0003f);
    CHECK(std::abs(pose.world(3).py - 2.1f) < .0003f);
    CHECK(std::abs(pose.world(3).pz - .1f) < .0003f);
}
TEST_CASE("animation.pose.twoBoneIkReachesTargetsWithoutIteration") {
    AnimSkeleton skeleton;
    skeleton.addBone("hip");
    skeleton.addBone("knee", 0);
    skeleton.addBone("ankle", 1);
    skeleton.setBindPosition(1, 0, 1, 0);
    skeleton.setBindPosition(2, 0, 1, 0);
    AnimPose pose;
    for (auto target : {std::array{.2f, 1.4f, .1f}, std::array{0.f, 1.99f, 0.f}, std::array{0.f, .01f, 0.f},
                        std::array{-.3f, -1.5f, .2f}}) {
        skeleton.applyBindPose(&pose);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1));
        float       error = 0;
        const auto& p     = pose.world(2);
        error = std::sqrt(std::pow(p.px - target[0], 2.f) + std::pow(p.py - target[1], 2.f) +
                          std::pow(p.pz - target[2], 2.f));
        CHECK(error < .0003f);
        const auto before = pose.world(1);
        REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, target[0], target[1], target[2], 1));
        const auto after = pose.world(1);
        CHECK(std::abs(before.px - after.px) < .0003f);
        CHECK(std::abs(before.py - after.py) < .0003f);
        CHECK(std::abs(before.pz - after.pz) < .0003f);
    }
}
TEST_CASE("animation.pose.twoBoneIkClampsReachAndKeepsZeroWeightPose") {
    AnimSkeleton skeleton;
    skeleton.addBone("hip");
    skeleton.addBone("knee", 0);
    skeleton.addBone("ankle", 1);
    skeleton.setBindPosition(1, 0, 1, 0);
    skeleton.setBindPosition(2, 0, 1, 0);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 1, 0, 1, 0));
    CHECK(std::abs(pose.world(2).py - 2) < 1e-6f);
    REQUIRE(pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 0, 3, 0, 1));
    CHECK(std::abs(pose.world(2).py - 2) < 1e-6f);
    skeleton.setBindPosition(1, 0, 0, 0);
    skeleton.applyBindPose(&pose);
    CHECK(!pose.solveTwoBoneIK(&skeleton, 0, 1, 2, 1, 0, 1, 1));
    CHECK(std::abs(pose.world(2).py - 1) < 1e-6f);
}
