#include <array>
#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;
namespace {
void bones(AnimSkeleton& skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("left", 0);
    skeleton.addBone("right", 0);
    skeleton.addBone("pelvis", 0);
}
void swing(AnimClip& clip, float direction) {
    clip.setDuration(1.f);
    clip.setSampleRate(60.f);
    clip.addPositionKey(1, 0.f, 0.f, 0.f, -0.5f * direction);
    clip.addPositionKey(1, 1.f, 0.f, 0.f, 0.5f * direction);
}
}  // namespace

TEST_CASE("animation.motionMatching.exhaustedOneShotHandsOffAndDoesNotReselect") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip start("start"), loop("loop");
    start.setDuration(.2f);
    loop.setDuration(.4f);
    loop.setLoop(true);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&start);
    db.addClip(&loop);
    db.bake();
    MotionMatcher matcher(&skeleton, &db);
    // An arbitrarily long search interval must not keep an expired one-shot.
    matcher.setSearchInterval(10.f);
    const std::array<MotionSearchRange, 2> ranges{{{0, 0, .2f, -1.f, true}, {1, 0, .4f, 0.f, false}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    AnimPose                              pose(4);
    std::array<MotionLocomotionSample, 5> samples{};
    REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    REQUIRE(matcher.getMatchedClipIndex() == 0);
    for (int i = 0; i < 15; ++i) {
        matcher.update(1.f / 60.f);
        if (matcher.getMatchedClipIndex() == 0) CHECK(matcher.getMatchedTime() <= start.getDuration());
    }
    CHECK(matcher.getMatchedClipIndex() == 1);
    CHECK(matcher.getMatchedTime() > 0.f);

    MotionMatcher only(&skeleton, &db);
    REQUIRE(only.setCandidateRanges(std::span(ranges).first(1)).ok());
    REQUIRE(only.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    for (int i = 0; i < 60; ++i) only.update(1.f / 60.f);
    CHECK(only.getMatchedClipIndex() == 0);
    CHECK(only.getMatchedTime() == start.getDuration());
    REQUIRE(only.setCandidateRanges(std::span(ranges).last(1)).ok());
    only.update(1.f / 60.f);
    CHECK(only.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.locomotionSeparatesEqualPositionsByFootVelocity") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip forward("swing-forward"), backward("swing-backward");
    swing(forward, 1.f);
    swing(backward, -1.f);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&forward);
    db.addClip(&backward);
    db.bake();
    CHECK(db.getFeatureSize() == 30);
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, .5f, .5f}, {1, .5f, .5f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose current(4), previous(4);
        previous.setLocalPosition(1, 0.f, 0.f, -direction / 60.f);
        std::array<MotionLocomotionSample, 5> samples{};
        REQUIRE(matcher.setLocomotionQuery(current, previous, 1.f / 60.f, samples).ok());
        // Mutating the caller's pose cannot alter the stored query.
        previous.setLocalPosition(1, 0.f, 0.f, 100.f);
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionMatching.locomotionSeparatesIntermediateFacing") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip left("turn-left-return"), right("turn-right-return");
    for (auto* clip : {&left, &right}) {
        const float angle = clip == &left ? .6f : -.6f;
        clip->setDuration(1.1f);
        clip->addRotationKey(0, 0.f, 0.f, 0.f, 0.f, 1.f);
        clip->addRotationKey(0, .35f, 0.f, std::sin(angle * .5f), 0.f, std::cos(angle * .5f));
        clip->addRotationKey(0, .7f, 0.f, 0.f, 0.f, 1.f);
        clip->addRotationKey(0, 1.1f, 0.f, 0.f, 0.f, 1.f);
    }
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&left);
    db.addClip(&right);
    db.bake();
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, 0, 0}, {1, 0, 0}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose                              pose(4);
        std::array<MotionLocomotionSample, 5> samples{};
        samples[2].yaw = direction * .6f;
        REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionMatching.locomotionConfigurationRejectsPartialInputs") {
    AnimSkeleton skeleton;
    bones(skeleton);
    MotionDatabase db(&skeleton);
    REQUIRE(!db.setLocomotionFeatures(1, 1, 3).ok());
    CHECK(!db.hasLocomotionFeatures());
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    AnimClip clip("still");
    clip.setDuration(1.f);
    db.addClip(&clip);
    db.bake();
    REQUIRE(!db.setLocomotionFeatures(2, 1, 3).ok());
    CHECK(db.getFeatureBone(0) == 1);
    MotionMatcher matcher(&skeleton, &db);
    CHECK_THROWS((matcher.search(), false));
    AnimPose                              pose(4), wrong(3);
    std::array<MotionLocomotionSample, 5> samples{};
    REQUIRE(matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(wrong, pose, 1.f / 60.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 0.f, samples).ok());
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, std::span(samples).first(4)).ok());
    samples[3].yaw = std::numeric_limits<float>::infinity();
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    samples[3].yaw = 0.f;
    pose.setLocalPosition(1, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
    REQUIRE(!matcher.setLocomotionQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    CHECK(std::isfinite(matcher.getLastSearchCost()));
}

TEST_CASE("animation.motionMatching.locomotionRootTravelDoesNotBecomeFootSwing") {
    AnimSkeleton skeleton;
    bones(skeleton);
    AnimClip clip("root-travel");
    clip.setDuration(.8f);
    clip.setLoop(true);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, .8f, 0.f, 0.f, 1.6f);
    skeleton.setBindPosition(1, .1f, .05f, 0.f);
    skeleton.setBindPosition(2, -.1f, .05f, 0.f);
    MotionDatabase db(&skeleton);
    REQUIRE(db.setLocomotionFeatures(1, 2, 3).ok());
    db.addClip(&clip);
    db.bake();
    for (int i = 0; i < db.getFrameCount(); ++i) {
        const auto& f = db.frameAt(i);
        CHECK(std::abs(f.velZ - 2.f) < .0001f);
        for (int axis = 22; axis < 28; ++axis) CHECK(std::abs(f.feature[axis]) < .001f);
        for (int axis : {0, 1, 2, 3, 6, 7, 10, 11, 12, 13}) CHECK(std::abs(f.feature[axis]) < .001f);
    }
}
