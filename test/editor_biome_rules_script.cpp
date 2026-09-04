#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

TEST_CASE("editor.biome.rules_editor_script_composes_workspace_and_exclusions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.biome", "Test Biome Editor");
        created <- eve.BiomeEditorModule().create("test.asset.forest");
        biomeEditor <- created.value;
        configured <- biomeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        beforePoints <- biomeEditor.getPointCount();
        excluded <- biomeEditor.addExclusion("asset://preview/clearing.spatial");
        afterPoints <- biomeEditor.getPointCount();
        exclusions <- biomeEditor.getExclusionCount();
        undoResult <- biomeEditor.undo();
        restored <- biomeEditor.getPointCount();
        densityReject <- biomeEditor.setLayerDensity(2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("biome.layers"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("biome.rules"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("assets"));
    CHECK(vm.find("beforePoints").toInt() > 0);
    CHECK(vm.find("excluded").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("exclusions").toInt(), 1);
    CHECK(vm.find("afterPoints").toInt() < vm.find("beforePoints").toInt());
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("restored").toInt(), vm.find("beforePoints").toInt());
    CHECK(!vm.find("densityReject").toTable().get<bool>("ok"));
}
