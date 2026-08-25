#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <cmath>
#include <memory>

using namespace eve::animation;

TEST_CASE("animation.compression.preservesCurvesWithinTolerance") {
    AnimClip clip("linear");
    clip.setDuration(1.f);
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.f;
        clip.addPositionKey(0, t, 2.f * t, -t, 0.5f * t);
        clip.addRotationKey(0, t, 0.f, 0.f, 0.f, 1.f);
        clip.addScaleKey(0, t, 1.f + t, 1.f, 1.f);
    }

    const int removed = clip.compress(1e-5f, 0.01f, 1e-5f);
    CHECK(removed == 27);
    CHECK(clip.getPositionKeyCount(0) == 2);
    CHECK(clip.getRotationKeyCount(0) == 2);
    CHECK(clip.getScaleKeyCount(0) == 2);

    AnimPose pose;
    clip.sample(0.35f, &pose);
    CHECK(std::fabs(pose.getLocalPositionX(0) - 0.7f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalPositionY(0) + 0.35f) < 1e-5f);
}

TEST_CASE("animation.retarget.preservesBindRelativeMotion") {
    AnimSkeleton source;
    const int sourceRoot = source.addBone("root");
    const int sourceHand = source.addBone("hand", sourceRoot);
    source.setBindPosition(sourceHand, 0.f, 1.f, 0.f);

    AnimSkeleton target;
    const int targetRoot = target.addBone("root");
    const int targetHand = target.addBone("hand", targetRoot);
    target.setBindPosition(targetHand, 0.f, 2.f, 0.f);

    AnimClip clip("wave");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(sourceHand, 0.f, 0.f, 1.f, 0.f);
    clip.addPositionKey(sourceHand, 1.f, 0.5f, 1.f, 0.f);
    clip.addRotationKey(sourceHand, 0.f, 0.f, 0.f, 0.f, 1.f);
    clip.addRotationKey(sourceHand, 1.f, 0.f, 0.f, 0.70710678f, 0.70710678f);
    clip.addEvent(0.5f, "contact");

    std::unique_ptr<AnimClip> retargeted(clip.retarget(&source, &target));
    CHECK(retargeted->getName() == "wave_retargeted");
    CHECK(retargeted->getEventCount() == 1);

    AnimPose pose;
    retargeted->sample(1.f, &pose, &target);
    CHECK(std::fabs(pose.getLocalPositionX(targetHand) - 1.f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalPositionY(targetHand) - 2.f) < 1e-5f);
    CHECK(std::fabs(pose.getLocalRotationZ(targetHand) - 0.70710678f) < 1e-5f);
}
