#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

TEST_CASE("editor.animation.clip_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.animation", "Test Animation Editor");
        created <- eve.AnimationEditorModule().create("test.asset.walk");
        clipEditor <- created.value;
        configured <- clipEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        viewport <- clipEditor.setViewport(640.0, 36.0, 120.0);
        seekResult <- clipEditor.seekSeconds(1.0);
        hipsAtMid <- 0.0;
        for (local i = 0; i < clipEditor.getPrimitiveCount(); ++i) {
            if (clipEditor.getPrimitiveKind(i) == "joint") {
                hipsAtMid = clipEditor.getPrimitiveX(i);
                break;
            }
        }
        beforeMask <- clipEditor.getSelectedMaskWeight();
        maskResult <- clipEditor.setMaskWeight(0.2);
        afterMask <- clipEditor.getSelectedMaskWeight();
        undoResult <- clipEditor.undo();
        restoredMask <- clipEditor.getSelectedMaskWeight();
        clipEditor.play();
        advanced <- clipEditor.update(0.25);
        playhead <- clipEditor.getPlayhead();
        tracks <- clipEditor.getTrackCount();
        keys <- clipEditor.getKeyCount();
        primitives <- clipEditor.getPrimitiveCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("animation.skeleton"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("animation.clip"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("hipsAtMid").toFloat() > 0.4F);
    CHECK(vm.find("maskResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterMask").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredMask").toFloat() - vm.find("beforeMask").toFloat()), 0.0001F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK_EQ(vm.find("tracks").toInt(), 2);
    CHECK(vm.find("keys").toInt() >= 4);
    CHECK(vm.find("primitives").toInt() > 0);
}
