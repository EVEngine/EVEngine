#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorWorkspace.h"

using namespace eve::editor;

TEST_CASE("editor.workspace.composes_project_panels_without_ui_dependencies") {
    EditorWorkspace workspace("world-authoring", "World Authoring");
    CHECK_EQ(workspace.getId(), std::string("world-authoring"));
    CHECK_EQ(workspace.getMode(), std::string("edit"));

    REQUIRE(workspace.registerPanel("scene", "Scene", "center", 0));
    REQUIRE(workspace.registerPanel("hierarchy", "Hierarchy", "left", 10));
    REQUIRE(workspace.registerPanel("assets", "Assets", "bottom", 20));
    REQUIRE(workspace.registerPanel("inspector", "Inspector", "right", 30));
    CHECK(!workspace.registerPanel("scene", "Duplicate", "floating", 0));
    CHECK(!workspace.registerPanel("bad", "Bad", "unknown", 0));
    CHECK_EQ(workspace.getPanelCount(), 4);

    REQUIRE(workspace.setPanelCapability("scene", "scene.viewport"));
    REQUIRE(workspace.setPanelContext("scene", "scene"));
    REQUIRE(workspace.movePanel("assets", "left", 20));
    REQUIRE(workspace.setPanelSingleton("assets", false));
    REQUIRE(workspace.setPanelVisible("inspector", false));
    CHECK(!workspace.activatePanel("inspector"));
    REQUIRE(workspace.activatePanel("scene"));
    CHECK_EQ(workspace.getActivePanel(), std::string("scene"));
    CHECK(workspace.getRevision() > 0);

    workspace.setRegionSize("left", 240.f);
    workspace.setRegionSize("right", 300.f);
    workspace.setRegionSize("top", 40.f);
    workspace.setRegionSize("bottom", 180.f);
    workspace.layout(1280.f, 720.f);
    CHECK_EQ(workspace.getRegionX("center"), 240.f);
    CHECK_EQ(workspace.getRegionY("center"), 40.f);
    CHECK_EQ(workspace.getRegionW("center"), 740.f);
    CHECK_EQ(workspace.getRegionH("center"), 500.f);
}

TEST_CASE("editor.workspace.shares_selection_and_focus_channels_across_presenters") {
    EditorWorkspace workspace("runtime-builder", "Runtime Builder");
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-1", "rts.unit", false));
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-2", "rts.unit", true));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionItem("world", 0), std::string("unit-1"));
    CHECK_EQ(workspace.getSelectionType("world", 1), std::string("rts.unit"));
    CHECK_EQ(workspace.getPrimarySelection("world"), std::string("unit-2"));
    CHECK(workspace.getSelectionSequence("world") >= static_cast<std::uint64_t>(2));

    REQUIRE(workspace.select("assets", "asset", "project", "mat-grass", "material", false));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionCount("assets"), 1);
    CHECK(!workspace.select("world", "invalid", "level-1", "x", "bad", false));

    REQUIRE(workspace.focus("world", "scene-viewport", "unit-2"));
    CHECK_EQ(workspace.getFocusedSurface("world"), std::string("scene-viewport"));
    REQUIRE(workspace.clearSelection("world"));
    CHECK_EQ(workspace.getSelectionCount("world"), 0);
}
