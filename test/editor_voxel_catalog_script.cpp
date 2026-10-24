#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

TEST_CASE("editor.voxel.catalog_editor_script_composes_workspace_and_occupancy") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.voxel", "Test Voxel Editor");
        created <- eve.VoxelEditorModule().create("test.asset.blocks");
        voxelEditor <- created.value;
        configured <- voxelEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- ""; cap0 <- ""; ctx1 <- "";
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            if (workspace.getPanelId(i) == "voxel.models") {
                panel0 = workspace.getPanelId(i); cap0 = workspace.getPanelCapability(i);
            }
            if (workspace.getPanelContext(i) == "preview") ctx1 = workspace.getPanelContext(i);
        }
        cubeFill <- voxelEditor.getModelFill(0);
        bedFill <- voxelEditor.getModelFill(1);
        viewport <- voxelEditor.setViewport(640.0, 400.0);
        before <- voxelEditor.getVoxelCount();
        screen <- voxelEditor.getScreenVoxelCount();
        erased <- voxelEditor.setVoxel(2, 2, 2, false);
        after <- voxelEditor.getVoxelCount();
        partialFill <- voxelEditor.getModelFill(0);
        undoResult <- voxelEditor.undo();
        restored <- voxelEditor.getVoxelCount();
        voxelX0 <- voxelEditor.getVoxelX(0);
        voxelEditor.selectModel("bed");
        bedVoxels <- voxelEditor.getVoxelCount();
        attach <- voxelEditor.setTool("attach");
    )"));

    REQUIRE(vm.find("created").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    REQUIRE(vm.find("configured").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("workspacePanelCount").toInt(), 4);
    REQUIRE_EQ(vm.find("panel0").toString(), std::string("voxel.models"));
    REQUIRE_EQ(vm.find("cap0").toString(), std::string("voxel.sculpt"));
    REQUIRE_EQ(vm.find("ctx1").toString(), std::string("preview"));
    REQUIRE_EQ(vm.find("cubeFill").toString(), std::string("partial"));
    REQUIRE_EQ(vm.find("bedFill").toString(), std::string("partial"));
    REQUIRE(vm.find("viewport").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("before").toInt(), 64);
    REQUIRE_EQ(vm.find("screen").toInt(), 64);
    REQUIRE(vm.find("erased").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("after").toInt(), 63);
    REQUIRE_EQ(vm.find("partialFill").toString(), std::string("partial"));
    REQUIRE(vm.find("undoResult").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("restored").toInt(), 64);
    REQUIRE_EQ(vm.find("voxelX0").toInt(), 2);
    REQUIRE(vm.find("bedVoxels").toInt() > 64);
    REQUIRE(vm.find("attach").toTable().get<bool>("ok"));
}
