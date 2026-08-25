#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("editor.script.composes_heightmap_target_field_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        procgen <- eve.Procgen();
        heightmap <- procgen.newHeightmap(8, 8);
        target <- editor.newHeightmapTarget("terrain", heightmap);
        falloff <- editor.newSmoothBrushFalloff();
        kernel <- editor.newCircleBrushKernel();
        kernel.setSmoothFalloff(falloff);
        operation <- editor.newAddScalarFieldOperation();
        tool <- editor.newFieldBrushTool("terrain.raise", "Raise Terrain");
        tool.setCircleKernel(kernel);
        tool.setAddScalarOperation(operation);
        tool.setRadius(1.5);
        tool.setStrength(0.25);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        session.bindHeightmapTarget(target);
        activated <- session.activateTool("terrain.raise");
        down <- session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        up <- session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        raised <- target.readScalar(3, 4);
        revisionAfterStroke <- target.getRevision();
        undone <- session.undo();
        restored <- target.readScalar(3, 4);
        redone <- session.redo();
        replayed <- target.readScalar(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK((vm.find("down").toInt() & 2) != 0);
    CHECK((vm.find("up").toInt() & 4) != 0);
    CHECK_GT(vm.find("raised").toFloat(), 0.2f);
    CHECK_GT(vm.find("revisionAfterStroke").toInt(), 0);
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toFloat(), 0.f);
    CHECK(vm.find("redone").toBool());
    CHECK_GT(vm.find("replayed").toFloat(), 0.2f);
}
