#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "editor/Editor.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorValue.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace eve::editor;

TEST_CASE("editor.v2.script_discovers_plans_and_executes_registered_command") {
    Editor* editor   = Editor::create();
    auto&   commands = editor->commandService();

    const CommandId commandId("test.script.place-attraction");
    commands.unregisterCommand(commandId);
    CommandDescriptor descriptor;
    descriptor.id          = commandId;
    descriptor.ownerModule = "test.script";
    descriptor.displayName = "Place Attraction";
    descriptor.category    = "Build";

    bool plannerSawScriptSource = false;
    int  executedAmount         = 0;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      plannerSawScriptSource = request.source == CommandSource::Script;
                      const auto* object     = request.payload.getIf<EditorValue::Object>();
                      if (!object)
                          return EditorResult<CommandPlan>::error(EditorStatus::Rejected, RuleId("test.script.payload"),
                                                                  "Object payload required");
                      const auto amount = object->find("amount");
                      if (amount == object->end() || !amount->second.getIf<int64_t>())
                          return EditorResult<CommandPlan>::error(EditorStatus::Rejected, RuleId("test.script.amount"),
                                                                  "Integer amount required");
                      CommandPlan plan;
                      plan.summary = "Place an attraction from a script-authored request";
                      DomainOperation operation;
                      operation.type        = "test.place";
                      operation.inverseType = "test.unplace";
                      operation.payload     = request.payload;
                      operation.hasInverse  = true;
                      plan.operations.push_back(std::move(operation));
                      return EditorResult<CommandPlan>::applied(std::move(plan));
                  },
                  [&](const CommandRequest& request, const CommandPlan& plan) {
                      const auto& object = *request.payload.getIf<EditorValue::Object>();
                      executedAmount     = static_cast<int>(*object.at("amount").getIf<int64_t>());
                      TransactionReceipt receipt;
                      receipt.id               = TransactionId(plan.id.value());
                      receipt.state            = TransactionState::Committed;
                      receipt.beforeRevision   = plan.baseRevision;
                      receipt.afterRevision    = plan.baseRevision + 1;
                      receipt.authorityReceipt = "local:test.script";
                      return EditorResult<TransactionReceipt>::applied(std::move(receipt));
                  })
              .accepted());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        found <- false;
        for (local i = 0; i < session.getCommandCount(); ++i) {
            if (session.getCommandId(i) == "test.script.place-attraction" &&
                session.getCommandName(i) == "Place Attraction" &&
                session.getCommandCategory(i) == "Build") found = true;
        }
        planned <- session.planCommand("test.script.place-attraction", {
            amount = 7,
            placement = { x = 12.5, y = 4.0 },
            tags = ["ride", "family"]
        });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("found").toBool());
    ssq::Table planned(vm.find("planned"));
    ssq::Table executed(vm.find("executed"));
    CHECK(planned.get<bool>("accepted"));
    CHECK_EQ(planned.get<std::string>("status"), std::string("applied"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("transactionState"), std::string("committed"));
    CHECK_EQ(executed.get<int64_t>("afterRevision"), int64_t{1});
    CHECK(plannerSawScriptSource);
    CHECK_EQ(executedAmount, 7);

    CHECK(commands.unregisterCommand(commandId, "test.script"));
}

TEST_CASE("editor.v2.script_game_injects_command_into_the_same_session_protocol") {
    Editor* editor = Editor::create();
    editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"));

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        placed <- 0;
        registered <- editor.registerScriptCommand(
            "test.script.spawn-tree", "Spawn Tree", "Build",
            function(payload) {
                if (!("count" in payload) || payload.count <= 0) return false;
                placed += payload.count;
                return true;
            });
        session <- editor.newSession();
        planned <- session.planCommand("test.script.spawn-tree", { count = 3 });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("registered").toBool());
    CHECK_EQ(vm.find("placed").toInt(), 3);
    ssq::Table executed(vm.find("executed"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("authorityReceipt"), std::string("script:local"));

    CHECK(editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"),
                                                     "script:test.script.spawn-tree"));
}

TEST_CASE("editor.v2.runtime_builder_example_script_compiles") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "editor-api-v2" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    ssq::VM vm(2048, ssq::Libs::ALL);
    CHECK(!vm.compileSource(source.str().c_str(), "examples/editor-api-v2/main.nut").isEmpty());
}
