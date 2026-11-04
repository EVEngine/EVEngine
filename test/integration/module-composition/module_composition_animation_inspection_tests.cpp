// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"animation"}
#include "animation/AnimClip.h"
#include "animation/AnimInspection.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "graphics/InspectionPlan.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
std::unique_ptr<eve::animation::AnimSkin> inspectionSkin(const char* name = "child") {
    eve::animation::AnimSkinStreamData data;
    data.vertexCount         = 1;
    data.bindPositions       = {0, 0, 0};
    data.bindNormals         = {0, 0, 1};
    data.skeletonBones       = {1};
    data.boneNames           = {name};
    data.inverseBindMatrices = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 1}}};
    data.vertexSkinJoints    = {0, 0, 0, 0};
    data.vertexWeights       = {1, 0, 0, 0};
    auto result              = eve::animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(result.ok());
    return std::move(result.value());
}
}  // namespace

TEST_CASE("animation.inspection.independentPalettesAndEndpoints") {
    using namespace eve::animation;
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("child", 0);
    skeleton.setBindPosition(1, 1, 0, 0);
    AnimClip clip("move");
    clip.setDuration(1);
    clip.setLoop(true);
    clip.addPositionKey(0, 0, 0, 0, 0);
    clip.addPositionKey(0, 1, 4, 0, 0);
    auto     skin = inspectionSkin();
    AnimPose livePose;
    skeleton.applyBindPose(&livePose);
    livePose.local(0).px = 99;
    livePose.computeWorld(&skeleton);
    REQUIRE(skin->updateMatrixPalette(&livePose));
    auto schedule = eve::graphics::planInspectionSamples(0, 1, 2, 1, eve::graphics::InspectionLoop::Once);
    REQUIRE(schedule.ok());
    std::vector<AnimInspectionTime> times;
    for (const auto& time : schedule.value()) times.push_back({time.timelineTime, time.clipTime});
    AnimInspectionBuilder builder(times);
    auto                  result = builder.build(clip, skeleton, *skin);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        ASSERT(result.value()[i].palette[12] == float(i) * 2.f);
        ASSERT(result.value()[i].time.timelineTime == double(i) * .5);
    }
    ASSERT(clip.getLoop());
    ASSERT(skin->getMatrixPaletteElement(0, 12) == 99.f);
    ASSERT(livePose.local(0).px == 99.f);
    auto repeated = builder.build(clip, skeleton, *skin);
    REQUIRE(repeated.ok());
    for (size_t i = 0; i < 3; ++i) ASSERT(result.value()[i].palette == repeated.value()[i].palette);
    auto wrapped = AnimInspectionBuilder({{1, 0}}).build(clip, skeleton, *skin);
    REQUIRE(wrapped.ok());
    ASSERT(wrapped.value()[0].palette[12] == 0.f);
    skin.reset();
    ASSERT(result.value()[2].palette[12] == 4.f);
}

TEST_CASE("animation.inspection.rejectsInvalidSchedulesAndIdentity") {
    using namespace eve::animation;
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("child", 0);
    AnimClip clip("move");
    clip.setDuration(1);
    auto skin = inspectionSkin();
    REQUIRE(!AnimInspectionBuilder({}).build(clip, skeleton, *skin).ok());
    REQUIRE(!AnimInspectionBuilder({{0, 2}}).build(clip, skeleton, *skin).ok());
    REQUIRE(!AnimInspectionBuilder({{0, 0}, {0, 1}}).build(clip, skeleton, *skin).ok());
    REQUIRE(!AnimInspectionBuilder({{-1, 0}}).build(clip, skeleton, *skin).ok());
    REQUIRE(!AnimInspectionBuilder(std::vector<AnimInspectionTime>(17)).build(clip, skeleton, *skin).ok());
    auto wrongSkin = inspectionSkin("different");
    REQUIRE(!AnimInspectionBuilder({{0, 0}}).build(clip, skeleton, *wrongSkin).ok());
    clip.addPositionKey(2, 0, 0, 0, 0);
    REQUIRE(!AnimInspectionBuilder({{0, 0}}).build(clip, skeleton, *skin).ok());
}
