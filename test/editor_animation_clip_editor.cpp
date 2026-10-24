#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/editor/AnimationClipEditor.h"
#include "editor/EditorWorkspace.h"
#include "editor_preview_test_support.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <map>

using eve::animation_editor::AnimationClipEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.animation.clip_editor_installs_workspace_and_reverses_mask") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    EditorWorkspace     workspace("animation.preview", "Animation Clip Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    const std::map<std::string, std::string> expected = {{"animation.skeleton", "list"},
                                                         {"animation.preview", "preview"},
                                                         {"animation.inspector", "inspector"},
                                                         {"animation.timeline", "timeline"}};
    for (int i = 0; i < workspace.getPanelCount(); ++i) {
        const auto id = workspace.getPanelId(i);
        REQUIRE(expected.contains(id));
        CHECK_EQ(workspace.getPanelCapability(i), std::string("animation.clip"));
        CHECK_EQ(workspace.getPanelContext(i), expected.at(id));
    }

    REQUIRE(editor.setViewport(640.0f, 36.0f, 120.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.trackCount(), 6);
    CHECK_EQ(editor.boneCount(), 6);
    CHECK_EQ(editor.boneParent(3), std::string("Chest"));
    CHECK(editor.keyCount() >= 12);
    CHECK_EQ(editor.eventCount(), 1);
    CHECK(editor.primitiveCount() > 0);
    CHECK_EQ(editor.preview().documentRevision, editor.revision());

    REQUIRE(editor.seekSeconds(1.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.playhead() - 1.0) < 0.0001);
    double hipsX = 0.0;
    for (const auto& bone : editor.preview().bones)
        if (bone.bone == "Hips") hipsX = bone.positionX;
    CHECK(std::abs(hipsX - 1.0) < 0.05);

    const double before = editor.selectedMaskWeight();
    REQUIRE(editor.setMaskWeight(0.25).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - 0.25) < 0.0001);
    REQUIRE(editor.undo().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - before) < 0.0001);
}

TEST_CASE("editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.setViewport(800.0f, 36.0f, 120.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    int spineRow = -1;
    for (int i = 0; i < editor.trackCount(); ++i)
        if (editor.trackBone(i) == "Spine") spineRow = i;
    REQUIRE(spineRow >= 0);
    REQUIRE(editor.pointerDown(20.0f, (static_cast<float>(spineRow) + 0.5f) * 36.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.selectedBone(), std::string("Spine"));

    editor.play();
    REQUIRE(editor.update(0.2).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.playhead() > 0.0);
    CHECK(editor.isPlaying());

    auto rejected = editor.setDuration(0.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.duration(), 2.0);
}

TEST_CASE("editor.animation.clip_editor_keys_edits_and_deletes_joint_transform") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.selectBone("Spine").ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.seekSeconds(1.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    const int beforeKeys = editor.keyCount();

    REQUIRE(editor.setSelectedPosition(1.25, 2.5, -0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
    CHECK(std::abs(editor.selectedPositionX() - 1.25) < 0.0001);
    CHECK(std::abs(editor.selectedPositionY() - 2.5) < 0.0001);
    CHECK(std::abs(editor.selectedPositionZ() + 0.5) < 0.0001);

    REQUIRE(editor.setSelectedRotation(15.0, 30.0, -20.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedRotationX() - 15.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationY() - 30.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationZ() + 20.0) < 0.01);
    REQUIRE(editor.setSelectedScale(1.1, 1.2, 1.3).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedScaleY() - 1.2) < 0.0001);

    REQUIRE(editor.moveSelectedKey(1.25).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedKeyTime() - 1.25) < 0.0001);
    REQUIRE(editor.deleteSelectedKey().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(!editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys);
    REQUIRE(editor.undo().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
}

TEST_CASE("editor.animation.clip_editor_round_trips_real_runtime_skeleton_and_clip") {
    eve::animation::AnimSkeleton skeleton;
    const int root = skeleton.addBone("Root");
    const int hand = skeleton.addBone("Hand", root);
    skeleton.setBindPosition(hand, 0.0f, 1.0f, 0.0f);

    eve::animation::AnimClip clip("attack");
    clip.setDuration(1.0f);
    clip.setSampleRate(30.0f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.0f, 0.0f, 0.0f, 0.0f);
    clip.addPositionKey(root, 1.0f, 1.0f, 0.0f, 0.0f);
    clip.addRotationKey(hand, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    clip.addRotationKey(hand, 1.0f, 0.0f, 0.7071067f, 0.0f, 0.7071067f);

    AnimationClipEditor editor("runtime.attack");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.loadRuntimeClip(skeleton, clip).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.boneCount(), 2);
    CHECK_EQ(editor.boneName(1), std::string("Hand"));
    CHECK_EQ(editor.boneParent(1), std::string("Root"));
    REQUIRE(editor.selectBone("Hand").ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.seekSeconds(0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.setSelectedPosition(0.25, 1.25, -0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.writeRuntimeClip(clip, skeleton).ok());
    REQUIRE(preview_test::finish(editor).ok());

    eve::animation::AnimPose pose(2);
    clip.sample(0.5f, &pose, &skeleton);
    CHECK(std::abs(pose.getLocalPositionX(hand) - 0.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionY(hand) - 1.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionZ(hand) + 0.5f) < 0.0001f);
}
