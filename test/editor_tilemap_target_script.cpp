#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("editor.script.composes_live_tile_layer_target_with_field_tool") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        map <- eve.Map();
        layer <- map.newLayer(8, 8, 16.0, 16.0);
        target <- editor.newTileLayerTarget("ground", layer);
        falloff <- editor.newConstantBrushFalloff();
        kernel <- editor.newBoxBrushKernel();
        kernel.setConstantFalloff(falloff);
        operation <- editor.newPaintIntFieldOperation(17);
        tool <- editor.newFieldBrushTool("tile.paint", "Paint Tile");
        tool.setBoxKernel(kernel);
        tool.setPaintIntOperation(operation);
        tool.setRadius(0.5);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        session.bindTileLayerTarget(target);
        activated <- session.activateTool("tile.paint");
        beforeRevision <- target.getRevision();
        session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        painted <- target.readInt(3, 4);
        targetRevision <- target.getRevision();
        undone <- session.undo();
        restored <- target.readInt(3, 4);
        redone <- session.redo();
        replayed <- target.readInt(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK_EQ(vm.find("painted").toInt(), 17);
    CHECK_GT(vm.find("targetRevision").toInt(), vm.find("beforeRevision").toInt());
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toInt(), 0);
    CHECK(vm.find("redone").toBool());
    CHECK_EQ(vm.find("replayed").toInt(), 17);
}
