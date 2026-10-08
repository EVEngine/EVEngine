#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/PhysicalBalancePose.h"
#include "common/Status.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace eve::animation;

namespace {

std::unique_ptr<AnimSkeleton> makeBalanceSkeleton() {
    auto sk    = std::make_unique<AnimSkeleton>();
    int  root  = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int spine = sk->addBone("spine", root);
    sk->setBindPosition(spine, 0.f, 1.f, 0.f);
    int chest = sk->addBone("chest", spine);
    sk->setBindPosition(chest, 0.f, 0.8f, 0.f);
    return sk;
}

}  // namespace

TEST_CASE("animation.physicalBalance.matchesTargetWithoutImpulse") {
    auto sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    target.setLocalRotation(1, 0.f, 0.f, 0.f, 1.f);
    REQUIRE(balance.setTargetPose(&target).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanX()) < 1e-5f);
    CHECK(std::fabs(balance.getLeanZ()) < 1e-5f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(1) - 1.f) < 1e-4f);
}

TEST_CASE("animation.physicalBalance.impulseLeansThenRecovers") {
    auto sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    REQUIRE(balance.setRecovery(2.f, 0.45f).ok());
    REQUIRE(balance.setInertia(1.f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(1, 8.f, 0.f, 0.f, 0.f, 1.2f, 0.f).ok());
    balance.update(1.f / 60.f);
    const float firstLean = balance.getLeanZ();
    CHECK(std::fabs(firstLean) > 1e-4f);
    // +X impulse at a point above the support moves COM toward +X.
    CHECK(balance.getCenterOfMassX() > 0.f);

    float maxAbs = std::fabs(firstLean);
    bool  crossed = false;
    float prevVel = balance.getLeanVelocityZ();
    for (int i = 0; i < 180; ++i) {
        balance.update(1.f / 60.f);
        maxAbs = std::max(maxAbs, std::fabs(balance.getLeanZ()));
        if (prevVel * balance.getLeanVelocityZ() < 0.f) crossed = true;
        prevVel = balance.getLeanVelocityZ();
    }
    CHECK(std::fabs(balance.getLeanZ()) < 0.05f);
    CHECK(crossed);
    CHECK(maxAbs >= std::fabs(firstLean));
}

TEST_CASE("animation.physicalBalance.recoilDeflectsHitBone") {
    auto sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setRecoil(3.f, 0.5f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(2, 0.f, 0.f, 4.f, 0.f, 1.8f, 0.1f).ok());
    balance.update(1.f / 60.f);
    const float hitW = balance.getPose()->getLocalRotationW(2);
    CHECK(hitW < 0.999f);
    for (int i = 0; i < 180; ++i) balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(2) - 1.f) < 0.02f);
}

TEST_CASE("animation.physicalBalance.rejectsUnrecoverableGravity") {
    auto sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.setRecovery(0.2f, 0.5f).ok());
    CHECK(!balance.setGravity(500.f).ok());
    CHECK(std::fabs(balance.getRecoveryFrequency() - 2.f) < 1e-6f);
    CHECK(std::fabs(balance.getGravity() - 9.81f) < 1e-5f);
}

TEST_CASE("animation.physicalBalance.rejectsBadImpulse") {
    auto sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.applyImpulse(-1, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    CHECK(!balance.applyImpulse(0, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    auto zero = balance.applyImpulse(0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    REQUIRE(zero.ok());
    CHECK(zero.status().code() == eve::StatusCode::NoOp);
}

TEST_CASE("animation.physicalBalance.followsAnimPlayerTarget") {
    auto sk = makeBalanceSkeleton();
    auto clip = std::make_unique<AnimClip>("idle");
    clip->setDuration(1.f);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, 1.f, 0.f, 1.f, 0.f);
    AnimPlayer player(sk.get());
    player.play(clip.get());
    player.update(0.1f);

    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setTargetPose(player.getPose()).ok());
    REQUIRE(balance.applyImpulse(1, 5.f, 0.f, 0.f, 0.f, 1.f, 0.f).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanZ()) > 1e-4f);
    CHECK(std::fabs(balance.getPose()->getLocalPositionY(1) - 1.f) < 0.05f);
}
