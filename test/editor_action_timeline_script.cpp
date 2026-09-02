#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

TEST_CASE("editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.combat", "Test Combat Editor");
        timelineAsset <- {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:light-attack", durationNs=1000000000,
            animationUri="asset://test/attack.glb#Attack",
            metadata={},
            tracks=[{
                id="test-track:gameplay", label="Gameplay", kind="gameplay",
                muted=false, locked=false, notifies=[],
                states=[{
                    id="test-state:hitbox", type="combat:hitbox-window",
                    startNs=200000000, endNs=500000000,
                    payload={hitbox="weapon.main"}
                }]
            }]
        };
        created <- eve.ActionEditorModule().create("test.asset.light-attack", timelineAsset);
        actionEditor <- created.value;
        configured <- actionEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        viewport <- actionEditor.setViewport(800.0, 36.0, 120.0);
        beforeStart <- actionEditor.getStateStart("test-state:hitbox");
        itemCenterX <- (actionEditor.getItemMinX(0) + actionEditor.getItemMaxX(0)) * 0.5;
        itemCenterY <- (actionEditor.getItemMinY(0) + actionEditor.getItemMaxY(0)) * 0.5;
        pointerDownResult <- actionEditor.pointerDown(itemCenterX, itemCenterY, false);
        pointerMoveResult <- actionEditor.pointerMove(itemCenterX + 80.0);
        pointerUpResult <- actionEditor.pointerUp(itemCenterX + 80.0);
        movedStart <- actionEditor.getStateStart("test-state:hitbox");
        revisionAfterDrag <- actionEditor.getRevision();
        undoResult <- actionEditor.undo();
        restoredStart <- actionEditor.getStateStart("test-state:hitbox");
        missResult <- actionEditor.pointerDown(700.0, 20.0, false);
        draggingAfterMiss <- actionEditor.isDragging();
        seekResult <- actionEditor.seekX(700.0);
        emptySpaceSeekTime <- actionEditor.getPreviewTime();
        actionEditor.seekSeconds(0.0);
        actionEditor.play();
        advanced <- actionEditor.update(1.0);
        previewEventCount <- actionEditor.getEventCount();
        snapshotResult <- actionEditor.snapshot();
        invalidResult <- eve.ActionEditorModule().create("test.asset.invalid", {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:invalid", durationNs=-1,
            animationUri="", metadata={}, tracks=[]
        });
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerDownResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerMoveResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerUpResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedStart").toFloat() > vm.find("beforeStart").toFloat());
    CHECK_EQ(vm.find("revisionAfterDrag").toInt(), 1);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredStart").toFloat() - vm.find("beforeStart").toFloat()), 0.0001F);
    CHECK(vm.find("missResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("draggingAfterMiss").toBool());
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("emptySpaceSeekTime").toFloat() > 0.8F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("previewEventCount").toInt(), 2);
    CHECK(vm.find("snapshotResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidResult").toTable().get<bool>("ok"));
}
