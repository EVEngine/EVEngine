#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/RootMotionPolicy.h"

#include <cmath>
#include <limits>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::animation;

namespace {

std::unique_ptr<AnimSkeleton> makeTwoBoneSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int hip = sk->addBone("hip", root);
    sk->setBindPosition(hip, 0.f, 1.f, 0.f);
    return sk;
}

std::unique_ptr<AnimClip> makeLocomotionClip(float speed, float dur) {
    auto clip = std::make_unique<AnimClip>("walk");
    clip->setLoop(true);
    clip->setSampleRate(20.f);
    clip->setDuration(dur);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, dur, 0.f, 0.f, speed * dur);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, dur, 0.f, 1.f, speed * dur);
    return clip;
}

std::unique_ptr<AnimClip> makeDiagonalClip(float dur) {
    auto clip = std::make_unique<AnimClip>("diag");
    clip->setLoop(false);
    clip->setSampleRate(20.f);
    clip->setDuration(dur);
    clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip->addPositionKey(0, dur, 1.f, 0.5f, 2.f);
    return clip;
}

}  // namespace

TEST_CASE("animation.rootMotionPolicy.locksVerticalAxis") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeDiagonalClip(1.f);
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.lockAxes = RootMotionLockAxes::VerticalY;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getRootMotionX() - 0.5f) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionY()) < 1e-5f);
    CHECK(std::fabs(player.getRootMotionZ() - 1.f) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.bakesUnlockedTranslationIntoPose") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.lockAxes                = RootMotionLockAxes::VerticalY;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.bakeHoldsBindAcrossLoop") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.9f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    player.update(0.2f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
    CHECK(player.getRootMotionZ() > 0.3f);
}

TEST_CASE("animation.rootMotionPolicy.bakeRotationUsesBind") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = std::make_unique<AnimClip>("spin");
    clip->setLoop(true);
    clip->setDuration(1.f);
    clip->addRotationKey(0, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip->addRotationKey(0, 1.f, 0.f, 1.f, 0.f, 0.f);  // 180° around Y
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.bakeRotationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getPose()->getLocalRotationX(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationZ(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationW(0) - 1.f) < 1e-4f);
    player.update(0.5f);
    CHECK(std::fabs(player.getPose()->getLocalRotationY(0)) < 1e-4f);
    CHECK(std::fabs(player.getPose()->getLocalRotationW(0) - 1.f) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.locksApplyAfterCharacterFacing") {
    TransformTRS raw = TransformTRS::identity();
    raw.pz           = 1.f;
    RootMotionPolicy policy;
    policy.lockAxes     = RootMotionLockAxes::X;
    policy.applySpace   = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw = static_cast<float>(M_PI * 0.5);  // local +Z → published +X, then lock X
    const TransformTRS out = applyRootMotionPolicy(raw, policy);
    CHECK(std::fabs(out.px) < 1e-5f);
    CHECK(std::fabs(out.pz) < 1e-5f);

    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeLocomotionClip(1.f, 1.f);
    AnimPlayer player(sk.get());
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.25f);
    CHECK(std::fabs(player.getRootMotionX()) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionZ()) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.crossFadeBlendsPublishedDeltaAndBakesBind") {
    auto walk = makeLocomotionClip(2.f, 1.f);
    auto idle = std::make_unique<AnimClip>("idle-offset");
    idle->setLoop(true);
    idle->setDuration(1.f);
    idle->addPositionKey(0, 0.f, 0.f, 0.f, 5.f);
    idle->addPositionKey(0, 1.f, 0.f, 0.f, 5.f);

    auto sk = makeTwoBoneSkeleton();
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.bakeTranslationIntoPose = true;
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(walk.get());
    player.update(0.5f);
    CHECK(std::fabs(player.getRootMotionZ() - 1.f) < 0.02f);

    player.crossFade(idle.get(), 0.2f);
    player.update(0.1f);
    // Incoming idle has zero travel; outgoing walk still contributes ~0.2 at t=0.5.
    CHECK(player.getRootMotionZ() > 0.05f);
    CHECK(player.getRootMotionZ() < 0.2f);
    // Bake plants on bind, not the idle clip's offset root of 5.
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0)) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.parseRejectsUnknownNames") {
    auto axes = parseRootMotionLockAxes("sideways");
    CHECK(!axes.ok());
    auto space = parseRootMotionApplySpace("world");
    CHECK(!space.ok());
    auto okAxes = parseRootMotionLockAxes("horizontal");
    REQUIRE(okAxes.ok());
    CHECK(okAxes.value() == RootMotionLockAxes::HorizontalXZ);
    auto okSpace = parseRootMotionApplySpace("facing");
    REQUIRE(okSpace.ok());
    CHECK(okSpace.value() == RootMotionApplySpace::CharacterFacing);
}

TEST_CASE("animation.rootMotionPolicy.characterFacingRotatesPlanarDelta") {
    TransformTRS raw = TransformTRS::identity();
    raw.pz           = 1.f;
    RootMotionPolicy policy;
    policy.applySpace   = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw = static_cast<float>(M_PI * 0.5);  // +90° → local +Z becomes +X
    const TransformTRS out = applyRootMotionPolicy(raw, policy);
    CHECK(std::fabs(out.px - 1.f) < 1e-5f);
    CHECK(std::fabs(out.pz) < 1e-5f);
}

TEST_CASE("animation.rootMotionPolicy.playerCharacterFacing") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeLocomotionClip(1.f, 1.f);
    AnimPlayer player(sk.get());
    RootMotionPolicy policy;
    policy.applySpace   = RootMotionApplySpace::CharacterFacing;
    policy.characterYaw = static_cast<float>(M_PI * 0.5);
    REQUIRE(player.setRootMotionPolicy(policy).ok());
    player.play(clip.get());
    player.update(0.25f);
    CHECK(std::fabs(player.getRootMotionX() - 0.25f) < 1e-4f);
    CHECK(std::fabs(player.getRootMotionZ()) < 1e-4f);
}

TEST_CASE("animation.rootMotionPolicy.rejectsNonFiniteYaw") {
    auto             sk = makeTwoBoneSkeleton();
    AnimPlayer       player(sk.get());
    RootMotionPolicy policy;
    policy.characterYaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(!player.setRootMotionPolicy(policy).ok());
    CHECK(!player.setRootMotionCharacterYaw(std::numeric_limits<float>::infinity()).ok());
}

TEST_CASE("animation.rootMotionPolicy.defaultMatchesLegacyExtraction") {
    auto sk   = makeTwoBoneSkeleton();
    auto clip = makeLocomotionClip(2.f, 1.f);
    AnimPlayer player(sk.get());
    player.play(clip.get());
    player.update(0.3f);
    CHECK(std::fabs(player.getRootMotionZ() - 0.6f) < 0.01f);
    CHECK(std::fabs(player.getPose()->getLocalPositionZ(0) - 0.6f) < 0.01f);
}
