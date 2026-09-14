#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>

TEST_CASE("editor.procgen.script_editor_script_loads_schema_and_keeps_failed_preview") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.procgen", "Test Procgen Editor");
        created <- eve.ProcgenEditorModule().create("test.asset.forest");
        pcg <- created.value;
        configured <- pcg.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        schema <- [
            { key="seed", kind="int", min=1, max=9999, defaultValue=42, label="Seed" },
            { key="density", kind="float", min=0.1, max=1.0, step=0.05, defaultValue=0.8, label="Density" }
        ];
        loaded <- pcg.loadModule("game:/generators/forest.nut", "forest", "Forest", "points", schema);
        procgen <- eve.Procgen();
        pointsResult <- procgen.newPointSet();
        points <- pointsResult.value;
        points.add(1.0, 0.0, 2.0);
        points.add(3.0, 0.0, 4.0);
        published <- pcg.publishPreview(points, "trees", pcg.getRevision());
        beforePoints <- pcg.getPointCount();
        density <- pcg.setFloat("density", 0.4);
        failed <- pcg.failPreview("boom", pcg.getRevision());
        afterFail <- pcg.getPointCount();
        previewRev <- pcg.getPreviewRevision();
        undoResult <- pcg.undo();
        restoredDensity <- pcg.getFloat("density");
        rangeReject <- pcg.setFloat("density", 2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("procgen.modules"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("procgen.script"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("stages"));
    CHECK(vm.find("loaded").toTable().get<bool>("ok"));
    CHECK(vm.find("published").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("beforePoints").toInt(), 2);
    CHECK(vm.find("density").toTable().get<bool>("ok"));
    CHECK(vm.find("failed").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterFail").toInt(), 2);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK(std::abs(vm.find("restoredDensity").toFloat() - 0.8f) < 0.0001f);
    CHECK(!vm.find("rangeReject").toTable().get<bool>("ok"));
}
