#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

TEST_CASE("editor.avatar.document_editor_script_composes_workspace_and_rejects_referenced_delete") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.avatar", "Test Avatar Editor");
        created <- eve.AvatarEditorModule().create("test.asset.face");
        avatarEditor <- created.value;
        configured <- avatarEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx4 <- workspace.getPanelContext(4);
        selectParam <- avatarEditor.selectParameter("smile");
        beforeY <- avatarEditor.getPreviewY(1);
        deleted <- avatarEditor.deleteSelectedParameter();
        afterCount <- avatarEditor.getParameterCount();
        afterY <- avatarEditor.getPreviewY(1);
        valueResult <- avatarEditor.setParameterValue(0.8);
        movedY <- avatarEditor.getPreviewY(1);
        expressions <- avatarEditor.getExpressionCount();
        channels <- avatarEditor.getExpressionChannelCount(0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 5);
    CHECK_EQ(vm.find("panel0").toString(), std::string("avatar.layers"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("avatar.document"));
    CHECK_EQ(vm.find("ctx4").toString(), std::string("expressions"));
    CHECK(vm.find("selectParam").toTable().get<bool>("ok"));
    CHECK(!vm.find("deleted").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterCount").toInt(), 1);
    CHECK_EQ(vm.find("afterY").toFloat(), vm.find("beforeY").toFloat());
    CHECK(vm.find("valueResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedY").toFloat() > vm.find("beforeY").toFloat());
    CHECK_EQ(vm.find("expressions").toInt(), 1);
    CHECK(vm.find("channels").toInt() >= 1);
}
