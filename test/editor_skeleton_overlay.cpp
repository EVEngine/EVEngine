#include "editor/EditorSkeletonOverlay.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::editor;

namespace {

const EditorGizmoPrimitive* primitive(const EditorGizmoSnapshot& snapshot,
                                      const std::string& id) {
    const auto found = std::find_if(snapshot.primitives.begin(), snapshot.primitives.end(),
                                    [&](const auto& value) { return value.id == id; });
    return found == snapshot.primitives.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("editor.animation.skeleton_overlay_builds_lines_axes_constraints_retarget_and_mask") {
    SkeletonOverlayBone root;
    root.id = StableId("root");
    root.name = "root";
    root.position = {0.0, 0.0, 0.0};

    SkeletonOverlayBone spine;
    spine.id = StableId("spine");
    spine.parent = root.id;
    spine.name = "spine";
    spine.position = {0.0, 1.0, 0.0};
    spine.selected = true;
    spine.constrained = true;
    spine.minimumAngles = {-0.5, -0.25};
    spine.maximumAngles = {0.5, 0.75};
    spine.retarget = SkeletonRetargetState::Matched;

    SkeletonOverlayBone hand;
    hand.id = StableId("hand");
    hand.parent = spine.id;
    hand.name = "hand";
    hand.position = {1.0, 1.0, 0.0};
    hand.retarget = SkeletonRetargetState::Unmatched;
    hand.maskWeight = 0.0;

    auto overlay = SkeletonOverlayBuilder().build("avatar", 17, {root, spine, hand});
    REQUIRE_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(overlay.targetRevision, Revision{17});
    CHECK_EQ(overlay.primitives.size(), size_t{10});
    REQUIRE(primitive(overlay, "spine:bone"));
    CHECK_EQ(primitive(overlay, "spine:bone")->length, 1.0);
    REQUIRE(primitive(overlay, "spine:axis-x"));
    REQUIRE(primitive(overlay, "spine:constraint-yaw"));
    REQUIRE(primitive(overlay, "hand:joint"));
    CHECK(primitive(overlay, "hand:joint")->dashed);
    CHECK(primitive(overlay, "hand:joint")->color[0] < 0.3);
    CHECK(!primitive(overlay, "hand:axis-x"));
}

TEST_CASE("editor.animation.skeleton_overlay_rejects_cycles_missing_parents_and_budget") {
    SkeletonOverlayBone first;
    first.id = StableId("first");
    first.parent = StableId("second");
    SkeletonOverlayBone second;
    second.id = StableId("second");
    second.parent = StableId("first");
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("cycle", 1, {first, second}).status),
             static_cast<int>(EditorStatus::Failed));

    first.parent = StableId("missing");
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("missing", 1, {first}).status),
             static_cast<int>(EditorStatus::Failed));

    first.parent = {};
    SkeletonOverlayOptions options;
    options.maximumBones = 0;
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("budget", 1, {first}, options).status),
             static_cast<int>(EditorStatus::Failed));

    options.maximumBones = 1;
    first.maskWeight = 1.5;
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("mask", 1, {first}, options).status),
             static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.runtime_skeleton_adapter_uses_world_pose_and_stable_names") {
    eve::animation::AnimSkeleton skeleton;
    const int root = skeleton.addBone("root");
    const int hand = skeleton.addBone("hand", root);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    pose.setLocalPosition(root, 2.f, 3.f, 4.f);
    pose.setLocalPosition(hand, 0.f, 2.f, 0.f);

    auto overlay = AnimationSkeletonOverlayAdapter().build(
        &skeleton, &pose, "runtime-avatar", 9, "hand",
        {{"root", "hips"}, {"hand", ""}}, {{"hand", 0.5}});
    REQUIRE_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(primitive(overlay, "root:joint"));
    CHECK_EQ(primitive(overlay, "root:joint")->position[0], 2.0);
    REQUIRE(primitive(overlay, "hand:joint"));
    CHECK_EQ(primitive(overlay, "hand:joint")->position[1], 5.0);
    CHECK(primitive(overlay, "hand:joint")->dashed);
    REQUIRE(primitive(overlay, "hand:axis-z"));
}
