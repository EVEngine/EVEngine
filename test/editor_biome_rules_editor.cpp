#include "biome_editor/BiomeRulesEditor.h"
#include "editor/EditorWorkspace.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::biome_editor::BiomeRulesEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.biome.rules_editor_installs_workspace_and_previews_deterministically") {
    BiomeRulesEditor editor("preview.forest");
    EditorWorkspace  workspace("biome.preview", "Biome Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("biome.layers"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("biome.rules"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("assets"));
    CHECK(editor.pointCount() > 0);
    CHECK_EQ(editor.previewRevision(), editor.revision());
    const int first = editor.pointCount();
    REQUIRE(editor.setSeed(42).ok());
    CHECK_EQ(editor.pointCount(), first);

    REQUIRE(editor.selectLayer("forest").ok());
    const float density = editor.layerDensity(0);
    REQUIRE(editor.setLayerDensity(0.5).ok());
    CHECK(std::abs(editor.layerDensity(0) - 0.5f) < 0.0001f);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.layerDensity(0), density);
}

TEST_CASE("editor.biome.rules_editor_exclusions_reduce_points_and_reject_invalid_density") {
    BiomeRulesEditor editor("preview.forest");
    REQUIRE(editor.selectLayer("forest").ok());
    const int before = editor.pointCount();
    REQUIRE(editor.addExclusion("asset://preview/clearing.spatial").ok());
    CHECK_EQ(editor.exclusionCount(), 1);
    CHECK(editor.pointCount() < before);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.exclusionCount(), 0);
    CHECK_EQ(editor.pointCount(), before);

    const auto revision = editor.previewRevision();
    auto       rejected = editor.setLayerDensity(2.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK_EQ(editor.pointCount(), before);
}
