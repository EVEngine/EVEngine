// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "editor",
  "modules": [
    "editor",
    "audio_editor",
    "animation_editor",
    "avatar_editor",
    "voxel_editor"
  ],
  "default": "script-binding"
}
*/
// clang-format on
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
void scriptContract(const char* module, const char* setup, const char* edit, const char* query,
                    zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    ssq::VM vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.set("factoryName", std::string(module));
    vm.run(vm.compileSource(R"(
        frameworkEditor <- eve.Editor();
        factory <- eve[factoryName]();
        firstCreated <- factory.create("contract.script.first");
        secondCreated <- factory.create("contract.script.second");
        first <- firstCreated.value; second <- secondCreated.value;
        secondRevision <- second.getRevision();
        workspace <- frameworkEditor.newWorkspace("contract.script", "Contract workspace");
        mounted <- first.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        ids <- {}; uniquePanels <- true;
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            local id = workspace.getPanelId(i);
            if (id in ids) uniquePanels = false;
            ids[id] <- workspace.getPanelContext(i);
        }
    )"));
    REQUIRE(vm.find("firstCreated").toTable().get<bool>("ok"));
    REQUIRE(vm.find("secondCreated").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("firstCreated").toTable().get<std::string>("ownership"), std::string("owned"));
    REQUIRE_EQ(vm.find("secondCreated").toTable().get<std::string>("ownership"), std::string("owned"));
    REQUIRE(vm.find("mounted").toTable().get<bool>("ok"));
    REQUIRE(vm.find("uniquePanels").toBool());
    auto ids = vm.find("ids").toTable();
    if (std::string(module) == "AvatarEditorModule") {
        REQUIRE_EQ(vm.find("workspacePanelCount").toInt(), 5);
        REQUIRE_EQ(ids.get<std::string>("avatar.layers"), std::string("list"));
        REQUIRE_EQ(ids.get<std::string>("avatar.preview"), std::string("preview"));
        REQUIRE_EQ(ids.get<std::string>("avatar.inspector"), std::string("inspector"));
        REQUIRE_EQ(ids.get<std::string>("avatar.parameters"), std::string("parameters"));
        REQUIRE_EQ(ids.get<std::string>("avatar.expressions"), std::string("expressions"));
    } else {
        REQUIRE_EQ(vm.find("workspacePanelCount").toInt(), 4);
        REQUIRE_EQ(ids.get<std::string>("voxel.models"), std::string("list"));
        REQUIRE_EQ(ids.get<std::string>("voxel.viewport"), std::string("preview"));
        REQUIRE_EQ(ids.get<std::string>("voxel.tools"), std::string("inspector"));
        REQUIRE_EQ(ids.get<std::string>("voxel.inspector"), std::string("inspector"));
    }
    vm.run(vm.compileSource(setup));
    REQUIRE(vm.find("selected").toTable().get<bool>("ok"));
    vm.run(vm.compileSource(query));
    const auto original = vm.find("value").toInt();
    vm.run(vm.compileSource(edit));
    REQUIRE(vm.find("edited").toTable().get<bool>("ok"));
    vm.run(vm.compileSource(query));
    REQUIRE_NE(vm.find("value").toInt(), original);
    vm.run(vm.compileSource("undone <- first.undo();"));
    REQUIRE(vm.find("undone").toTable().get<bool>("ok"));
    vm.run(vm.compileSource(query));
    REQUIRE_EQ(vm.find("value").toInt(), original);
    vm.run(vm.compileSource("redone <- first.redo(); isolated <- !second.canUndo() && !second.canRedo();"));
    REQUIRE(vm.find("redone").toTable().get<bool>("ok"));
    REQUIRE(vm.find("isolated").toBool());
    vm.run(vm.compileSource(query));
    REQUIRE_NE(vm.find("value").toInt(), original);
    vm.run(vm.compileSource(R"(
        first = null; firstCreated = null; collectgarbage();
        survivorWorkspace <- frameworkEditor.newWorkspace("contract.survivor", "Survivor workspace");
        survivorMounted <- second.configureWorkspace(survivorWorkspace);
        survivorIntact <- second.getRevision() == secondRevision && !second.canUndo() && !second.canRedo();
    )"));
    REQUIRE(vm.find("survivorMounted").toTable().get<bool>("ok"));
    REQUIRE(vm.find("survivorIntact").toBool());
}
}  // namespace
TEST_CASE("editor.contract.avatar_real_script_workspace_and_history") {
    scriptContract(
        "AvatarEditorModule", R"(selected <- first.selectLayer("eyes");)", "edited <- first.setLayerZ(14);",
        R"(value <- 0; for (local i = 0; i < first.getLayerCount(); ++i) if (first.getLayerId(i) == "eyes") value = first.getLayerZ(i);)",
        _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editor.contract.voxel_real_script_workspace_and_history") {
    scriptContract("VoxelEditorModule", R"(selected <- first.selectModel("cube");)",
                   "edited <- first.setVoxel(0, 0, 0, true);", "value <- first.getVoxelCount();",
                   _ZEROERR_TEST_CONTEXT);
}
