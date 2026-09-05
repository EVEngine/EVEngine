#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "voxel_editor/VoxelCatalogEditor.h"
#include "voxel_editor/VoxelEditorModule.h"

using eve::editor::Editor;

TEST_CASE("editor.voxel.catalog_automation_owns_headless_targets") {
    Editor editor;
    eve::voxel_editor::VoxelEditorModule adapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("voxel.model.create.v1") != std::string::npos);
    CHECK(commands.find("voxel.model.voxel.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.voxel","type":"voxel-catalog"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string model = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.create.v1","payload":{"id":"brick","name":"brick","sizeX":4,"sizeY":4,"sizeZ":4}})");
    REQUIRE(model.find("\"status\":\"applied\"") != std::string::npos);

    const std::string voxel = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.voxel.set.v1","payload":{"item":"brick","x":1,"y":2,"z":3,"occupied":true}})");
    REQUIRE(voxel.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.voxel"})");
    CHECK(inspected.find("voxel-catalog") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.voxel"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.voxel"})")
                .find("\"status\":\"applied\"") != std::string::npos);
}

TEST_CASE("editor.voxel.sculpt_editor_attaches_inside_canvas_and_undoes") {
    eve::voxel_editor::VoxelCatalogEditor editor("asset.preview.sculpt");
    CHECK_EQ(editor.modelSizeX(), 8);
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setVoxel(1, 2, 2, true).ok());
    CHECK_EQ(editor.voxelCount(), 65);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setViewport(640.f, 400.f).ok());
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerDown(320.f, 200.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerWorldRay(-2.f, 4.f, 4.f, 1.f, 0.f, 0.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
}
