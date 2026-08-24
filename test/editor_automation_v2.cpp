#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

using namespace eve::editor;

TEST_CASE("editor.v2.automation_capability_discovers_executes_plans_and_cancels") {
    Editor            editor;
    CommandDescriptor immediate;
    immediate.id          = CommandId("park.add-tree");
    immediate.ownerModule = "park";
    immediate.displayName = "Add tree";
    REQUIRE(editor.commandService()
                .registerCommand(std::move(immediate),
                                 [](const CommandContext&, const EditorValue&) {
                                     return EditorResult<EditorValue>::applied(EditorValue("tree-added"));
                                 })
                .accepted());

    CommandDescriptor planned;
    planned.id          = CommandId("park.build-path");
    planned.ownerModule = "park";
    planned.displayName = "Build path";
    REQUIRE(editor.commandService()
                .registerPlannedCommand(
                    std::move(planned),
                    [](const CommandRequest&) {
                        CommandPlan plan;
                        plan.summary = EditorValue("path-plan");
                        return EditorResult<CommandPlan>::applied(std::move(plan));
                    },
                    [](const CommandRequest&, const CommandPlan&) {
                        TransactionReceipt receipt;
                        receipt.id    = TransactionId("path-transaction");
                        receipt.state = TransactionState::Committed;
                        return EditorResult<TransactionReceipt>::applied(std::move(receipt));
                    })
                .accepted());

    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("park.add-tree") != std::string::npos);
    CHECK(commands.find("park.build-path") != std::string::npos);

    const std::string executed = automation->invoke("execute", R"({"command":"park.add-tree","payload":{}})");
    CHECK(executed.find("\"status\":\"applied\"") != std::string::npos);
    CHECK(executed.find("transactionId") != std::string::npos);

    const std::string plannedJson = automation->invoke("plan", R"({"command":"park.build-path","payload":{}})");
    CHECK(plannedJson.find("planId") != std::string::npos);
    const std::size_t valueStart = plannedJson.find("park.build-path.plan.");
    REQUIRE(valueStart != std::string::npos);
    const std::size_t valueEnd  = plannedJson.find('"', valueStart);
    const std::string planId    = plannedJson.substr(valueStart, valueEnd - valueStart);
    const std::string cancelled = automation->invoke("cancel", "{\"planId\":\"" + planId + "\"}");
    CHECK(cancelled.find("\"status\":\"applied\"") != std::string::npos);
    const std::string commitAfterCancel = automation->invoke("commit", "{\"planId\":\"" + planId + "\"}");
    CHECK(commitAfterCancel.find("not-found") != std::string::npos);
}
