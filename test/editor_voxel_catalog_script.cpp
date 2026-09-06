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
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
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

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("voxel.models"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("voxel.sculpt"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK_EQ(vm.find("cubeFill").toString(), std::string("partial"));
    CHECK_EQ(vm.find("bedFill").toString(), std::string("partial"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("before").toInt(), 64);
    CHECK_EQ(vm.find("screen").toInt(), 64);
    CHECK(vm.find("erased").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("after").toInt(), 63);
    CHECK_EQ(vm.find("partialFill").toString(), std::string("partial"));
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("restored").toInt(), 64);
    CHECK_EQ(vm.find("voxelX0").toInt(), 2);
    CHECK(vm.find("bedVoxels").toInt() > 64);
    CHECK(vm.find("attach").toTable().get<bool>("ok"));
}
