#include <array>
#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// animation_motion_normalization.cpp
using namespace eve::animation;

namespace {
MotionFeatureLayout positionLayout() {
    MotionFeatureLayout layout;
    layout.sampleRate = 10;
    MotionFeatureChannel channel;
    channel.kind   = MotionFeatureKind::Position;
    channel.source = MotionFeatureSource::Pose;
    channel.query  = MotionFeatureQuery::Character;
    channel.bone   = 1;
    channel.origin = 0;
    channel.axes   = 1;
    channel.weight = 1.f;
    layout.channels.push_back(channel);
    return layout;
}

struct NormalizationFixture {
    AnimSkeleton skeleton;
    AnimClip     low{"low"}, high{"high"};
    NormalizationFixture() {
        skeleton.addBone("root");
        skeleton.addBone("sample", 0);
        for (auto* clip : {&low, &high}) {
            clip->setDuration(1.f);
            clip->setLoop(false);
        }
        low.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
        high.addPositionKey(1, 0.f, 10.f, 0.f, 0.f);
    }
    void add(MotionDatabase& database) {
        database.addClip(&low);
        database.addClip(&high);
    }
};
}  // namespace

TEST_CASE("animation.motionSchema.normalizationRangesPreserveMultiplicityAndSamplingIntervals") {
    NormalizationFixture fixture;
    MotionDatabase       database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> ranges{{0, 0.f, 1.f}, {0, 0.f, 1.f}, {1, 0.f, 1.f}};
    auto                                  configured = database.setFeatureNormalizationRanges(ranges);
    REQUIRE(configured.ok());
    CHECK(configured.value() == 33);
    ranges.clear();
    database.bake();
    float high = 0.f;
    database.getFeature(11, &high, 1);
    CHECK(std::abs(high - 1.5f) < 1e-5f);

    MotionDatabase ordinary(&fixture.skeleton);
    fixture.add(ordinary);
    REQUIRE(ordinary.setFeatureLayout(positionLayout()).ok());
    ordinary.bake();
    ordinary.getFeature(11, &high, 1);
    CHECK(std::abs(high - 1.f) < 1e-5f);
}

TEST_CASE("animation.motionSchema.normalizationRangeValidationIsAtomic") {
    NormalizationFixture fixture;
    MotionDatabase       database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> valid{{0, 0.f, 0.f}};
    REQUIRE(database.setFeatureNormalizationRanges(valid).ok());
    for (const auto& invalid :
         std::vector<MotionNormalizationRange>{{-1, 0.f, 1.f},
                                               {2, 0.f, 1.f},
                                               {0, -.1f, 1.f},
                                               {0, .5f, .4f},
                                               {0, 0.f, 2.f},
                                               {0, 0.f, std::numeric_limits<float>::quiet_NaN()}}) {
        REQUIRE(!database.setFeatureNormalizationRanges(std::span(&invalid, 1)).ok());
    }
    database.bake();
    float high = 0.f;
    database.getFeature(11, &high, 1);
    // The retained one-sample low range has zero deviation and therefore unit scale.
    CHECK(std::abs(high - 10.f) < 1e-5f);
}

// animation_motion_schema.cpp
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
    channel.kind  = MotionFeatureKind::Velocity;
    channel.query = MotionFeatureQuery::Character;
    channel.bone  = 1;
    channel.axes  = 4;
    MotionFeatureLayout layout{20, {channel}};
    MotionDatabase      db(&skeleton);
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels[0].axes = 7;
    CHECK(db.getFeatureSize() == 1);
    db.addClip(&forward);
    db.addClip(&backward);
    db.bake();
    CHECK(db.getFrameCount() == 42);
    CHECK(!db.setFeatureLayout(layout).ok());
    for (int direction : {-1, 1}) {
        MotionMatcher                          matcher(&skeleton, &db);
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
    MotionDatabase       db(&skeleton);
    MotionFeatureChannel channel;
    channel.kind               = MotionFeatureKind::Velocity;
    channel.normalizationGroup = "speed";
    MotionFeatureLayout layout{30, {channel}};
    REQUIRE(db.setFeatureLayout(layout).ok());
    layout.channels.push_back(channel);
    layout.channels.back().normalizeVelocity = true;
    layout.channels.back().axes              = 0;
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    layout.channels.pop_back();
    layout.channels[0].weight = std::numeric_limits<float>::quiet_NaN();
    CHECK(!db.setFeatureLayout(layout).ok());
    CHECK(db.getFeatureSize() == 3);
    CHECK(db.hasFeatureLayout());
}

TEST_CASE("animation.motionSchema.mixedVelocityPoolPreservesAuthoredLengthScale") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("travel");
    clip.setDuration(1.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 2.f, 0.f, 0.f);
    MotionFeatureChannel raw;
    raw.kind                     = MotionFeatureKind::Velocity;
    raw.axes                     = 1;
    raw.characterSpaceVelocity   = false;
    raw.normalizationGroup       = "speed";
    raw.query                    = MotionFeatureQuery::Character;
    auto normalized              = raw;
    normalized.normalizeVelocity = true;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {raw, normalized}, 100.f}).ok());
    db.addClip(&clip);
    db.bake();
    const auto& feature = db.frameAt(15).feature;
    CHECK(std::abs(feature[0] - 1.f) < .0001f);
    CHECK(std::abs(feature[1] + 1.f) < .0001f);
    MotionMatcher matcher(&skeleton, &db);
    AnimPose      current(1), previous(1);
    current.setLocalPosition(0, 1.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.f - 2.f / 60.f, 0.f, 0.f);
    REQUIRE(matcher.setFeatureQuery(current, previous, 1.f / 60.f, {}).ok());
    matcher.search();
    CHECK(matcher.getLastSearchCost() < .000001f);
}

TEST_CASE("animation.motionSchema.playRateMatchesUnnormalizedTrajectorySpeedRatio") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip clip("travel");
    clip.setDuration(2.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 2.f, 2.f, 0.f, 0.f);
    MotionFeatureChannel velocity;
    velocity.kind                   = MotionFeatureKind::Velocity;
    velocity.source                 = MotionFeatureSource::Trajectory;
    velocity.query                  = MotionFeatureQuery::Character;
    velocity.axes                   = 1;
    velocity.sampleTime             = 0.f;
    velocity.characterSpaceVelocity = false;
    velocity.normalizeVelocity      = false;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {velocity}, 100.f}).ok());
    db.addClip(&clip);
    db.bake();
    MotionMatcher                          matcher(&skeleton, &db);
    const std::array<MotionSearchRange, 1> ranges{{MotionSearchRange{0, 0.f, 0.f}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    REQUIRE(matcher.setPlayRateRange(.85f, 1.15f).ok());
    const float oldMin = matcher.getPlayRateMinimum(), oldMax = matcher.getPlayRateMaximum();
    CHECK(!matcher.setPlayRateRange(1.2f, .8f).ok());
    CHECK(matcher.getPlayRateMinimum() == oldMin);
    CHECK(matcher.getPlayRateMaximum() == oldMax);
    AnimPose                                     pose(1);
    std::array<MotionFeatureTrajectorySample, 1> samples{};
    samples[0].vx = 2.f;
    REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
    matcher.search();
    CHECK(std::abs(matcher.getPlayRate() - 1.15f) < .0001f);
    matcher.update(.5f);
    CHECK(std::abs(matcher.getMatchedTime() - .575f) < .0001f);
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
        past.source                 = MotionFeatureSource::Trajectory;
        past.query                  = MotionFeatureQuery::Character;
        past.axes                   = 1;
        past.sampleTime             = -.2f;
        MotionFeatureChannel future = past;
        future.sampleTime           = 1.35f;
        MotionDatabase db(&skeleton);
        REQUIRE(db.setFeatureLayout({20, {past, future}}).ok());
        db.addClip(&forward);
        db.addClip(&backward);
        db.bake();
        CHECK(db.getFrameCount() == (loop ? 40 : 42));
        for (int direction : {-1, 1}) {
            MotionMatcher                          matcher(&skeleton, &db);
            const std::array<MotionSearchRange, 2> ranges{{{0, 0.f, 0.f}, {1, 0.f, 0.f}}};
            REQUIRE(matcher.setCandidateRanges(ranges).ok());
            AnimPose                                     pose(1);
            std::array<MotionFeatureTrajectorySample, 2> samples{};
            samples[0].seconds = -.2f;
            samples[0].x       = -.4f * direction;
            samples[1].seconds = 1.35f;
            samples[1].x       = 2.7f * direction;
            REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, std::span(samples).first(1)).ok());
            samples[1].seconds = samples[0].seconds;
            CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, samples).ok());
            matcher.search();
            CHECK(matcher.getMatchedClipIndex() == (direction == 1 ? 0 : 1));
        }
    }
}

// animation_motion_transition_blocks.cpp
using namespace eve::animation;

namespace {
struct BlockFixture {
    AnimSkeleton   skeleton;
    AnimClip       first{"first"}, second{"second"};
    MotionDatabase database{&skeleton};
    BlockFixture() {
        skeleton.addBone("root");
        for (auto* clip : {&first, &second}) {
            clip->setDuration(1.f);
            clip->setLoop(false);
            clip->addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
            database.addClip(clip);
        }
        database.bake();
    }
};
}  // namespace

TEST_CASE("animation.motionMatching.transitionBlocksPreventEntryButPreserveContinuation") {
    BlockFixture  fixture;
    MotionMatcher continuing(&fixture.skeleton, &fixture.database);
    continuing.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(seed).ok());
    continuing.search();
    continuing.update(.3f);
    const float time = continuing.getMatchedTime();
    REQUIRE(time > .1f);
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, -100.f, false, {{.1f, .8f}}}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(ranges).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 0);
    CHECK(std::abs(continuing.getMatchedTime() - time) < 1e-6f);

    // A fresh search would strongly prefer clip 0's bias, but its selected
    // middle section is protected. This does not remove it from continuation.
    ranges[0].start = .2f;
    ranges[0].end   = .7f;
    MotionMatcher fresh(&fixture.skeleton, &fixture.database);
    REQUIRE(fresh.setCandidateRanges(ranges).ok());
    fresh.search();
    CHECK(fresh.getMatchedClipIndex() == 1);
    REQUIRE(continuing.setCandidateRanges(ranges).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 0);

    // Ordinary membership still applies even to a protected ongoing pose.
    REQUIRE(continuing.setCandidateRanges(std::span(ranges).last(1)).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.transitionBlockBoundariesAreCopiedAndUnioned") {
    BlockFixture fixture;
    const float  start = fixture.database.getFrameTime(3);
    const float  end   = fixture.database.getFrameTime(4);
    REQUIRE(end > start);
    std::vector<MotionSearchRange> ranges{
        {0, start, start, -100.f, false, {{start, end}}}, {0, start, start, -200.f}, {1, 0.f, 0.f}};
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    ranges[0].transitionBlocks.clear();
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    // Exact end is eligible; exact start was blocked, including an overlapping
    // duplicate candidate range with a better score and no block of its own.
    ranges = {{0, end, end, -100.f, false, {{start, end}}}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
    CHECK(matcher.getMatchedTime() == end);
}

TEST_CASE("animation.motionMatching.transitionBlockFailureIsAtomicAndAllBlockedCanRecover") {
    BlockFixture                   fixture;
    MotionMatcher                  matcher(&fixture.skeleton, &fixture.database);
    std::vector<MotionSearchRange> blocked{{0, 0.f, 1.f, 0.f, false, {{0.f, 2.f}}}};
    REQUIRE(matcher.setCandidateRanges(blocked).ok());
    matcher.search();
    CHECK(matcher.getMatchedFrame() == -1);
    for (const auto& invalid : {MotionTransitionBlock{-1.f, 1.f},
                                {1.f, 1.f},
                                {2.f, 1.f},
                                {0.f, std::numeric_limits<float>::infinity()},
                                {std::numeric_limits<float>::quiet_NaN(), 1.f}}) {
        std::vector<MotionSearchRange> attempted{{1, 0.f, 1.f, 0.f, false, {invalid}}};
        REQUIRE(!matcher.setCandidateRanges(attempted).ok());
        matcher.search();
        CHECK(matcher.getMatchedFrame() == -1);
    }
    std::vector<MotionSearchRange> recovered{{1, 0.f, 1.f}};
    REQUIRE(matcher.setCandidateRanges(recovered).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.poseReselectHistoryExpiresAndKeepsContinuationEligible") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    REQUIRE(matcher.setPoseReselectHistory(.3f).ok());
    REQUIRE(!matcher.setPoseReselectHistory(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(std::abs(matcher.getPoseReselectHistory() - .3f) < 1e-6f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.update(.1f);

    std::vector<MotionSearchRange> ranges{{0, .1f, .1f, -100.f, false, {}, 200.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    matcher.update(.31f);
    ranges[1].continuingCostBias = 200.f;
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);
}

TEST_CASE("animation.motionMatching.anyStrictCostImprovementCanSwitch") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, 0.f, false, {}, 1.f}, {1, 0.f, 1.f, .95f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.replacementRangesInvalidateDistantSameClipContinuation") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    REQUIRE(matcher.getMatchedClipIndex() == 0);
    REQUIRE(matcher.getMatchedTime() < .5f);

    // The same asset is still present, but only at a distant phase. It cannot
    // masquerade as continuation of the current out-of-range playhead.
    std::vector<MotionSearchRange> ranges{{0, .8f, 1.f, 10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.authoredCandidateCostOverrideUsesLastActiveWindow") {
    BlockFixture                   fixture;
    MotionMatcher                  matcher(&fixture.skeleton, &fixture.database);
    const float                    time = fixture.database.getFrameTime(3);
    std::vector<MotionSearchRange> ranges{
        {0, time, time, -10.f, false, {}, 0.f, {{0.f, 1.f, -2.f}, {time, time + .01f, 4.f}}}, {1, time, time, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    // The override end is exclusive, so its base -10 cost wins exactly there.
    const float end         = fixture.database.getFrameTime(4);
    ranges[0].start         = end;
    ranges[0].end           = end;
    ranges[0].costOverrides = {{0.f, end, 4.f}};
    ranges[1].start         = end;
    ranges[1].end           = end;
    MotionMatcher boundary(&fixture.skeleton, &fixture.database);
    REQUIRE(boundary.setCandidateRanges(ranges).ok());
    boundary.search();
    CHECK(boundary.getMatchedClipIndex() == 0);
}

TEST_CASE("animation.motionMatching.continuingCostBiasAndOverrideControlRetention") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    REQUIRE(matcher.getMatchedClipIndex() == 0);

    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, 0.f, false, {}, -5.f}, {1, 0.f, 1.f, -1.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 0);

    // The source notify replaces the database continuing bias at this pose.
    ranges[0].continuingCostOverrides = {{.2f, .8f, 5.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.duplicateRangeRetainsCostPairFromWinningCandidate") {
    BlockFixture  fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    std::vector<MotionSearchRange> ranges{
        {0, 0.f, 1.f, -2.f, false, {}, 10.f}, {0, 0.f, 1.f, -1.f, false, {}, -10.f}, {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}
