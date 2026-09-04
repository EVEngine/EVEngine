#include "animation_editor/AnimationClipEditor.h"
#include "editor/EditorWorkspace.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::animation_editor::AnimationClipEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.animation.clip_editor_installs_workspace_and_reverses_mask") {
    AnimationClipEditor editor("preview.walk");
    EditorWorkspace     workspace("animation.preview", "Animation Clip Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("animation.skeleton"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("animation.clip"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("timeline"));

    REQUIRE(editor.setViewport(640.0f, 36.0f, 120.0f).ok());
    CHECK_EQ(editor.trackCount(), 2);
    CHECK(editor.keyCount() >= 4);
    CHECK_EQ(editor.eventCount(), 1);
    CHECK(editor.primitiveCount() > 0);
    CHECK_EQ(editor.preview().documentRevision, editor.revision());

    REQUIRE(editor.seekSeconds(1.0).ok());
    CHECK(std::abs(editor.playhead() - 1.0) < 0.0001);
    double hipsX = 0.0;
    for (const auto& bone : editor.preview().bones)
        if (bone.bone == "Hips") hipsX = bone.positionX;
    CHECK(std::abs(hipsX - 1.0) < 0.05);

    const double before = editor.selectedMaskWeight();
    REQUIRE(editor.setMaskWeight(0.25).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - 0.25) < 0.0001);
    REQUIRE(editor.undo().ok());
    CHECK(std::abs(editor.selectedMaskWeight() - before) < 0.0001);
}

TEST_CASE("editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(editor.setViewport(800.0f, 36.0f, 120.0f).ok());
    REQUIRE(editor.pointerDown(20.0f, 50.0f).ok());
    CHECK_EQ(editor.selectedBone(), std::string("Spine"));

    editor.play();
    REQUIRE(editor.update(0.2).ok());
    CHECK(editor.playhead() > 0.0);
    CHECK(editor.isPlaying());

    auto rejected = editor.setDuration(0.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.duration(), 2.0);
}
