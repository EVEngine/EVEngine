#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::animation;

namespace {
struct BlockFixture {
    AnimSkeleton skeleton;
    AnimClip first{"first"}, second{"second"};
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
}

TEST_CASE("animation.motionMatching.transitionBlocksPreventEntryButPreserveContinuation") {
    BlockFixture fixture;
    MotionMatcher continuing(&fixture.skeleton, &fixture.database);
    continuing.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(seed).ok());
    continuing.search();
    continuing.update(.3f);
    const float time = continuing.getMatchedTime();
    REQUIRE(time > .1f);
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, -100.f, false, {{.1f, .8f}}},
                                         {1, 0.f, 1.f, 0.f}};
    REQUIRE(continuing.setCandidateRanges(ranges).ok());
    continuing.search();
    CHECK(continuing.getMatchedClipIndex() == 0);
    CHECK(std::abs(continuing.getMatchedTime() - time) < 1e-6f);

    // A fresh search would strongly prefer clip 0's bias, but its selected
    // middle section is protected. This does not remove it from continuation.
    ranges[0].start = .2f;
    ranges[0].end = .7f;
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
    const float start = fixture.database.getFrameTime(3);
    const float end = fixture.database.getFrameTime(4);
    REQUIRE(end > start);
    std::vector<MotionSearchRange> ranges{{0, start, start, -100.f, false, {{start, end}}},
                                         {0, start, start, -200.f},
                                         {1, 0.f, 0.f}};
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
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    std::vector<MotionSearchRange> blocked{{0, 0.f, 1.f, 0.f, false, {{0.f, 2.f}}}};
    REQUIRE(matcher.setCandidateRanges(blocked).ok());
    matcher.search();
    CHECK(matcher.getMatchedFrame() == -1);
    for (const auto& invalid : {MotionTransitionBlock{-1.f, 1.f}, {1.f, 1.f}, {2.f, 1.f},
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
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    REQUIRE(matcher.setPoseReselectHistory(.3f).ok());
    REQUIRE(!matcher.setPoseReselectHistory(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(std::abs(matcher.getPoseReselectHistory() - .3f) < 1e-6f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.update(.1f);

    std::vector<MotionSearchRange> ranges{{0, .1f, .1f, -100.f, false, {}, 200.f},
                                         {1, 0.f, 1.f, 0.f}};
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
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    std::vector<MotionSearchRange> ranges{{0, 0.f, 1.f, 0.f, false, {}, 1.f},
                                         {1, 0.f, 1.f, .95f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}

TEST_CASE("animation.motionMatching.replacementRangesInvalidateDistantSameClipContinuation") {
    BlockFixture fixture;
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
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    const float time = fixture.database.getFrameTime(3);
    std::vector<MotionSearchRange> ranges{
        {0, time, time, -10.f, false, {}, 0.f, {{0.f, 1.f, -2.f}, {time, time + .01f, 4.f}}},
        {1, time, time, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);

    // The override end is exclusive, so its base -10 cost wins exactly there.
    const float end = fixture.database.getFrameTime(4);
    ranges[0].start = end;
    ranges[0].end = end;
    ranges[0].costOverrides = {{0.f, end, 4.f}};
    ranges[1].start = end;
    ranges[1].end = end;
    MotionMatcher boundary(&fixture.skeleton, &fixture.database);
    REQUIRE(boundary.setCandidateRanges(ranges).ok());
    boundary.search();
    CHECK(boundary.getMatchedClipIndex() == 0);
}

TEST_CASE("animation.motionMatching.continuingCostBiasAndOverrideControlRetention") {
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    REQUIRE(matcher.getMatchedClipIndex() == 0);

    std::vector<MotionSearchRange> ranges{
        {0, 0.f, 1.f, 0.f, false, {}, -5.f},
        {1, 0.f, 1.f, -1.f}};
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
    BlockFixture fixture;
    MotionMatcher matcher(&fixture.skeleton, &fixture.database);
    matcher.setSearchInterval(10.f);
    std::vector<MotionSearchRange> seed{{0, 0.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(seed).ok());
    matcher.search();
    matcher.update(.3f);
    std::vector<MotionSearchRange> ranges{
        {0, 0.f, 1.f, -2.f, false, {}, 10.f},
        {0, 0.f, 1.f, -1.f, false, {}, -10.f},
        {1, 0.f, 1.f, 0.f}};
    REQUIRE(matcher.setCandidateRanges(ranges).ok());
    matcher.search();
    CHECK(matcher.getMatchedClipIndex() == 1);
}
