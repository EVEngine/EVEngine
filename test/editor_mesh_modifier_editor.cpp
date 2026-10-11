#include "procgen/editor/MeshModifierEditor.h"

#include "zeroerr/unittest.h"

using namespace eve;

TEST_CASE("editor.meshModifierEditor.composesAllToolsAndTracksExternalRevisions") {
    editor::EditorWorkspace workspace("mesh", "Mesh");
    procgen_editor::MeshModifierEditor controller("mesh.main");
    REQUIRE(controller.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 8);
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.viewport");
    REQUIRE(controller.activateTool(workspace, "spline").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.spline");
    CHECK_EQ(workspace.getMode(), "meshModifier.spline");
    REQUIRE(controller.activateTool(workspace, "merge").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.merge");
    REQUIRE(controller.activateTool(workspace, "adhere").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.adhere");
    REQUIRE(controller.observeRevision("graph", 4).ok());
    CHECK_EQ(controller.graphRevision(), std::uint64_t{4});
    CHECK(!controller.observeRevision("graph", 3).ok());
    CHECK(!controller.activateTool(workspace, "unknown").ok());
}
