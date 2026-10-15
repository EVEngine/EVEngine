#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_voxel_catalog_script.cpp
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

// editor_voxel_target_script.cpp
TEST_CASE("editor.script.composes_voxel_target_volume_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        voxelTargets <- eve.VoxelWorldTargetModule();
        voxel <- eve.Voxel();
        world <- voxel.newWorld();
        target <- voxelTargets.create("world.voxels", world);
        falloff <- editor.newConstantBrushFalloff();
        kernel <- editor.newSphereVolumeBrushKernel();
        kernel.setConstantFalloff(falloff);
        operation <- editor.newPaintIntVolumeOperation(7);
        tool <- editor.newVolumeBrushTool("voxel.paint", "Paint Voxels");
        tool.setSphereKernel(kernel);
        tool.setPaintIntOperation(operation);
        tool.setRadius(0.5);
        session <- editor.newSession();
        added <- session.addVolumeTool(tool);
        voxelTargets.bind(session, target);
        activated <- session.activateTool("voxel.paint");
        beforeRevision <- world.getRevision();
        down <- session.dispatchPointer3D(0, 4, 0, 3.0, 5.0, -2.0, 0.0, 0.0, 0.0, 1.0);
        up <- session.dispatchPointer3D(2, 4, 0, 3.0, 5.0, -2.0, 0.0, 0.0, 0.0, 1.0);
        painted <- world.getVoxel(3, 5, -2);
        afterRevision <- world.getRevision();
        dirtyX <- target.getDirtyMinX();
        dirtyY <- target.getDirtyMinY();
        dirtyZ <- target.getDirtyMinZ();
        undone <- session.undo();
        restored <- world.getVoxel(3, 5, -2);
        redone <- session.redo();
        replayed <- target.readInt3(3, 5, -2);
        beforeSameWrite <- world.getRevision();
        world.setVoxel(3, 5, -2, 7);
        afterSameWrite <- world.getRevision();
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK((vm.find("down").toInt() & 2) != 0);
    CHECK((vm.find("up").toInt() & 4) != 0);
    CHECK_EQ(vm.find("painted").toInt(), 7);
    CHECK_GT(vm.find("afterRevision").toInt(), vm.find("beforeRevision").toInt());
    CHECK_EQ(vm.find("dirtyX").toInt(), 3);
    CHECK_EQ(vm.find("dirtyY").toInt(), 5);
    CHECK_EQ(vm.find("dirtyZ").toInt(), -2);
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toInt(), 0);
    CHECK(vm.find("redone").toBool());
    CHECK_EQ(vm.find("replayed").toInt(), 7);
    CHECK_EQ(vm.find("afterSameWrite").toInt(), vm.find("beforeSameWrite").toInt());
}
