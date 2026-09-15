#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;
namespace {
std::vector<std::byte> curveBytes() {
    std::vector<std::byte> out;
    auto integer = [&](std::uint32_t value) {
        for (int i = 0; i < 4; ++i) out.push_back(std::byte((value >> (8 * i)) & 255));
    };
    auto number = [&](float value) { integer(std::bit_cast<std::uint32_t>(value)); };
    auto string = [&](std::string_view value) {
        integer(static_cast<std::uint32_t>(value.size()));
        for (char c : value) out.push_back(std::byte(c));
    };
    integer(0x43465645); integer(1); integer(3);
    for (const auto name : {"positive", "negative", "absent"}) {
        string(name); number(1.f);
        const bool absent = std::string_view(name) == "absent";
        integer(absent ? 0 : 1);
        if (absent) continue;
        string("Phase"); integer(2);
        const float value = std::string_view(name) == "positive" ? 1.f : -1.f;
        number(0.f); number(value); number(1.f); number(value);
    }
    return out;
}
}

TEST_CASE("animation.motionSchema.curveFeaturesChangeSelectionWithIdenticalBonePoses") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip positive("positive"), negative("negative"), absent("absent");
    for (auto* clip : {&positive, &negative, &absent}) clip->setDuration(1.f);
    MotionFeatureChannel phase;
    phase.kind = MotionFeatureKind::Curve;
    phase.query = MotionFeatureQuery::Character;
    phase.axes = 1;
    phase.curve = "Phase";
    phase.sampleTime = -.0333f;
    MotionDatabase db(&skeleton);
    REQUIRE(db.setFeatureLayout({30, {phase}}).ok());
    db.addClip(&positive); db.addClip(&negative); db.addClip(&absent);
    auto bytes = curveBytes();
    const std::array<std::string, 3> sources{"positive", "negative", "absent"};
    REQUIRE(db.setFeatureCurves(bytes, sources).ok());
    CHECK(!db.setFeatureCurves(std::span(bytes).first(3), sources).ok());
    CHECK(!db.setFeatureCurves(bytes, std::span(sources).first(2)).ok());
    bytes.clear();
    db.bake();
    CHECK(db.getFeatureSize() == 1);
    CHECK(!db.setFeatureCurves(curveBytes(), sources).ok());
    for (int selected = 0; selected < 3; ++selected) {
        MotionMatcher matcher(&skeleton, &db);
        const std::array<MotionSearchRange, 3> ranges{{{0, 0.f, 0.f}, {1, 0.f, 0.f}, {2, 0.f, 0.f}}};
        REQUIRE(matcher.setCandidateRanges(ranges).ok());
        AnimPose pose(1);
        std::array<MotionFeatureCurveSample, 1> query{{{"Phase", -.0333f, selected == 0 ? 1.f : selected == 1 ? -1.f : 0.f}}};
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}).ok());
        query[0].value = std::numeric_limits<float>::infinity();
        CHECK(!matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == selected);
    }
}

TEST_CASE("animation.motionSchema.curveContinuingPolicyUsesEligiblePlayhead") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimClip positive("positive"), negative("negative");
    positive.setDuration(1.f); negative.setDuration(1.f);
    for (auto policy : {MotionFeatureQuery::Character, MotionFeatureQuery::Continuing}) {
        MotionFeatureChannel phase;
        phase.kind = MotionFeatureKind::Curve;
        phase.axes = 1;
        phase.curve = "Phase";
        phase.query = policy;
        MotionDatabase db(&skeleton);
        REQUIRE(db.setFeatureLayout({30, {phase}}).ok());
        db.addClip(&positive); db.addClip(&negative);
        const std::array<std::string, 2> sources{"positive", "negative"};
        REQUIRE(db.setFeatureCurves(curveBytes(), sources).ok());
        db.bake();
        MotionMatcher matcher(&skeleton, &db);
        AnimPose pose(1);
        std::array<MotionFeatureCurveSample, 1> query{{{"Phase", 0.f, 1.f}}};
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        REQUIRE(matcher.getMatchedClipIndex() == 0);
        query[0].value = -1.f;
        REQUIRE(matcher.setFeatureQuery(pose, pose, 1.f / 60.f, {}, query).ok());
        matcher.search();
        CHECK(matcher.getMatchedClipIndex() == (policy == MotionFeatureQuery::Continuing ? 0 : 1));
    }
}
