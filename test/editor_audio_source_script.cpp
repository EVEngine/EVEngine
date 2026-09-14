#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

TEST_CASE("editor.audio.source_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.audio", "Test Audio Editor");
        created <- eve.AudioEditorModule().create("test.asset.tone");
        audioEditor <- created.value;
        configured <- audioEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        viewport <- audioEditor.setViewportWidth(400.0);
        beforeVolume <- audioEditor.getFloat("play.volume");
        volumeResult <- audioEditor.setFloat("play.volume", 0.2);
        afterVolume <- audioEditor.getFloat("play.volume");
        revisionAfter <- audioEditor.getRevision();
        undoResult <- audioEditor.undo();
        restoredVolume <- audioEditor.getFloat("play.volume");
        playResult <- audioEditor.play();
        advanced <- audioEditor.update(0.25);
        playhead <- audioEditor.getPlayhead();
        seekResult <- audioEditor.seekSeconds(0.4);
        seeked <- audioEditor.getPlayhead();
        buckets <- audioEditor.getBucketCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("audio.sources"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("audio.source"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("volumeResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterVolume").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredVolume").toFloat() - vm.find("beforeVolume").toFloat()), 0.0001F);
    CHECK(vm.find("playResult").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("seeked").toFloat() - 0.4F), 0.02F);
    CHECK(vm.find("buckets").toInt() > 8);
    (void)vm.find("revisionAfter");
}
