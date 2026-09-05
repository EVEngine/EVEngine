#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

TEST_CASE("editor.ui.theme_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.ui-theme", "Test Theme Editor");
        created <- eve.UiEditorModule().create("test.asset.themes");
        themeEditor <- created.value;
        configured <- themeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        createdCustom <- themeEditor.createFromPreset("studio", "Studio", "dark");
        beforeRounding <- themeEditor.getFloat("geometry.frameRounding");
        roundingResult <- themeEditor.setFloat("geometry.frameRounding", 9.0);
        afterRounding <- themeEditor.getFloat("geometry.frameRounding");
        undoResult <- themeEditor.undo();
        restoredRounding <- themeEditor.getFloat("geometry.frameRounding");
        themeCount <- themeEditor.getThemeCount();
        selected <- themeEditor.getSelectedId();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 3);
    CHECK_EQ(vm.find("panel0").toString(), std::string("ui.themes"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("ui.theme"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("createdCustom").toTable().get<bool>("ok"));
    CHECK(vm.find("roundingResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterRounding").toFloat() - 9.0F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredRounding").toFloat() - vm.find("beforeRounding").toFloat()), 0.0001F);
    CHECK_EQ(vm.find("themeCount").toInt(), 3);
    CHECK_EQ(vm.find("selected").toString(), std::string("studio"));
}
