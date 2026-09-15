#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::animation;

namespace {
MotionFeatureLayout positionLayout() {
    MotionFeatureLayout layout;
    layout.sampleRate = 10;
    MotionFeatureChannel channel;
    channel.kind = MotionFeatureKind::Position;
    channel.source = MotionFeatureSource::Pose;
    channel.query = MotionFeatureQuery::Character;
    channel.bone = 1;
    channel.origin = 0;
    channel.axes = 1;
    channel.weight = 1.f;
    layout.channels.push_back(channel);
    return layout;
}

struct NormalizationFixture {
    AnimSkeleton skeleton;
    AnimClip low{"low"}, high{"high"};
    NormalizationFixture() {
        skeleton.addBone("root"); skeleton.addBone("sample", 0);
        for (auto* clip : {&low, &high}) { clip->setDuration(1.f); clip->setLoop(false); }
        low.addPositionKey(1, 0.f, 0.f, 0.f, 0.f);
        high.addPositionKey(1, 0.f, 10.f, 0.f, 0.f);
    }
    void add(MotionDatabase& database) { database.addClip(&low); database.addClip(&high); }
};
}

TEST_CASE("animation.motionSchema.normalizationRangesPreserveMultiplicityAndSamplingIntervals") {
    NormalizationFixture fixture;
    MotionDatabase database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> ranges{{0, 0.f, 1.f}, {0, 0.f, 1.f}, {1, 0.f, 1.f}};
    auto configured = database.setFeatureNormalizationRanges(ranges);
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
    MotionDatabase database(&fixture.skeleton);
    fixture.add(database);
    REQUIRE(database.setFeatureLayout(positionLayout()).ok());
    std::vector<MotionNormalizationRange> valid{{0, 0.f, 0.f}};
    REQUIRE(database.setFeatureNormalizationRanges(valid).ok());
    for (const auto& invalid : std::vector<MotionNormalizationRange>{
             {-1, 0.f, 1.f}, {2, 0.f, 1.f}, {0, -.1f, 1.f}, {0, .5f, .4f},
             {0, 0.f, 2.f}, {0, 0.f, std::numeric_limits<float>::quiet_NaN()}}) {
        REQUIRE(!database.setFeatureNormalizationRanges(std::span(&invalid, 1)).ok());
    }
    database.bake();
    float high = 0.f;
    database.getFeature(11, &high, 1);
    // The retained one-sample low range has zero deviation and therefore unit scale.
    CHECK(std::abs(high - 10.f) < 1e-5f);
}
