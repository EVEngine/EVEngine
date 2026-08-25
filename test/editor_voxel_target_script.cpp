#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("editor.script.composes_voxel_target_volume_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        voxel <- eve.Voxel();
        world <- voxel.newWorld();
        target <- editor.newVoxelWorldTarget("world.voxels", world);
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
        session.bindVoxelWorldTarget(target);
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
