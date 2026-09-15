#include <array>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;

TEST_CASE("animation.motionSchema.velocitySelectionCopiesQueryAndRejectsInvalidReplacement") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("foot", 0);
    AnimClip forward("forward"), backward("backward");
    for (auto* clip : {&forward, &backward}) {
        const float direction = clip == &forward ? 1.f : -1.f;
        clip->setDuration(1.f);
        clip->addPositionKey(1, 0.f, 0.f, 0.f, -.5f * direction);
        clip->addPositionKey(1, 1.f, 0.f, 0.f, .5f * direction);
    }
    MotionFeatureChannel channel;
    channel.kind = MotionFeatureKind::Velocity;
    channel.query = MotionFeatureQuery::Character;
    channel.bone = 1;
    channel.axes = 4;
    MotionFeatureLayout layout{20, {channel}};
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels[0].axes = 7;
    CHECK(db.getFeatureSize() == 1);
    db.addClip(&forward);
    db.addClip(&backward);
    db.bake();
    CHECK(db.getFrameCount() == 42);
    CHECK(!db.setFeatureLayout(layout).ok());
    for (int direction : {-1, 1}) {
        MotionMatcher matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 2> ranges{{{0, .5f, .5f}, {1, .5f, .5f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose current(2), previous(2);
        previous.setLocalPosition(1, 0.f, 0.f, -direction / 60.f);
        REQUIRE(matcher.setFeatureQuery(current, previous, 1.f / 60.f, {}).ok());
        previous.setLocalPosition(1, 0.f, 0.f, 100.f);
        CHECK(!matcher.setFeatureQuery(current, previous, 0.f, {}).ok());
        CHECK(!matcher.setQueryPose(current).ok());
        const std::array<MotionTrajectorySample, 3> basic{};
        CHECK(!matcher.setTrajectory(basic).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
    }
}

TEST_CASE("animation.motionSchema.layoutFailurePreservesConfiguration") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    MotionDatabase db(&skeleton);
    MotionFeatureChannel channel;
    channel.kind = MotionFeatureKind::Velocity;
    channel.normalizationGroup = "speed";
    MotionFeatureLayout layout{30, {channel}};
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels.push_back(channel);
    layout.channels.back().normalizeVelocity = true;
    layout.channels.back().axes = 0;
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    layout.channels.pop_back();
    layout.channels[0].weight = std::numeric_limits<float>::quiet_NaN();
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    CHECK(db.hasFeatureLayout());
}

TEST_CASE("animation.motionSchema.mixedVelocityPoolPreservesAuthoredLengthScale") {
    AnimSkeleton skeleton;skeleton.addBone("root");
    AnimClip clip("travel");clip.setDuration(1.f);
    clip.addPositionKey(0,0.f,0.f,0.f,0.f);clip.addPositionKey(0,1.f,2.f,0.f,0.f);
    MotionFeatureChannel raw;
    raw.kind=MotionFeatureKind::Velocity;raw.axes=1;raw.characterSpaceVelocity=false;
    raw.normalizationGroup="speed";raw.query=MotionFeatureQuery::Character;
    auto normalized=raw;normalized.normalizeVelocity=true;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30,{raw,normalized},100.f}).ok());
    db.addClip(&clip);db.bake();
    const auto& feature=db.frameAt(15).feature;
    CHECK(std::abs(feature[0]-1.f)<.0001f);
    CHECK(std::abs(feature[1]+1.f)<.0001f);
    MotionMatcher matcher(&skeleton,&db);
    AnimPose current(1),previous(1);current.setLocalPosition(0,1.f,0.f,0.f);
    previous.setLocalPosition(0,1.f-2.f/60.f,0.f,0.f);
    REQUIRE(matcher.setFeatureQuery(current,previous,1.f/60.f,{}).ok());
    matcher.search();CHECK(matcher.getLastSearchCost()<.000001f);
}

TEST_CASE("animation.motionSchema.playRateMatchesUnnormalizedTrajectorySpeedRatio") {
    AnimSkeleton skeleton;skeleton.addBone("root");
    AnimClip clip("travel");clip.setDuration(2.f);
    clip.addPositionKey(0,0.f,0.f,0.f,0.f);clip.addPositionKey(0,2.f,2.f,0.f,0.f);
    MotionFeatureChannel velocity;
    velocity.kind=MotionFeatureKind::Velocity;velocity.source=MotionFeatureSource::Trajectory;
    velocity.query=MotionFeatureQuery::Character;velocity.axes=1;velocity.sampleTime=0.f;
    velocity.characterSpaceVelocity=false;velocity.normalizeVelocity=false;
    MotionDatabase db(&skeleton);REQUIRE(db.setFeatureLayout({30,{velocity},100.f}).ok());
    db.addClip(&clip);db.bake();
    MotionMatcher matcher(&skeleton,&db);
    const std::array<MotionSearchRange,1> ranges{{MotionSearchRange{0,0.f,0.f}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    REQUIRE(matcher.setPlayRateRange(.85f,1.15f).ok());
    const float oldMin=matcher.getPlayRateMinimum(),oldMax=matcher.getPlayRateMaximum();
    CHECK(!matcher.setPlayRateRange(1.2f,.8f).ok());
    CHECK(matcher.getPlayRateMinimum()==oldMin);CHECK(matcher.getPlayRateMaximum()==oldMax);
    AnimPose pose(1);std::array<MotionFeatureTrajectorySample,1> samples{};samples[0].vx=2.f;
    REQUIRE(matcher.setFeatureQuery(pose,pose,1.f/60.f,samples).ok());
    matcher.search();CHECK(std::abs(matcher.getPlayRate()-1.15f)<.0001f);
    matcher.update(.5f);CHECK(std::abs(matcher.getMatchedTime()-.575f)<.0001f);
}

TEST_CASE("animation.motionSchema.arbitraryTrajectoryTimesSelectTravelAcrossClipBoundaries") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    for (bool loop : {false, true}) {
        AnimClip forward("forward"), backward("backward");
        for (auto* clip : {&forward, &backward}) {
            clip->setDuration(1.f);
            clip->setLoop(loop);
            clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
            clip->addPositionKey(0, 1.f, clip == &forward ? 2.f : -2.f, 0.f, 0.f);
        }
        MotionFeatureChannel past;
        past.source = MotionFeatureSource::Trajectory;
        past.query = MotionFeatureQuery::Character;
        past.axes = 1;
        past.sampleTime = -.2f;
        MotionFeatureChannel future = past;
        future.sampleTime = 1.35f;
        MotionDatabase db(&skeleton);
        REQUIRE(db.setFeatureLayout({20, {past, future}}).ok());
        db.addClip(&forward);
        db.addClip(&backward);
        db.bake();
        CHECK(db.getFrameCount() == (loop ? 40 : 42));
        for (int direction : {-1, 1}) {
            MotionMatcher matcher(&skeleton, &db);
            const std::array<MotionSearchRange, 2> ranges{{{0, 0.f, 0.f}, {1, 0.f, 0.f}}};
            REQUIRE(matcher.setCandidateRanges(ranges).ok());
            AnimPose pose(1);
            std::array<MotionFeatureTrajectorySample, 2> samples{};
            samples[0].seconds = -.2f;
            samples[0].x = -.4f * direction;
            samples[1].seconds = 1.35f;
            samples[1].x = 2.7f * direction;
            REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, std::span(samples).first(1)).ok());
            samples[1].seconds = samples[0].seconds;
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            matcher.search();
            CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
        }
    }
}
