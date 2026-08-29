#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "editor/EditorMaterialTarget.h"
#include "editor/EditorSceneTarget.h"

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

namespace {

void addSceneObject(SceneDocumentTarget& target, const char* id) {
    CreateSceneObjectRequest request;
    request.id   = ObjectId(id);
    request.name = id;
    auto operation = target.makeCreate(request);
    REQUIRE(operation.accepted());
    REQUIRE(operation.value.has_value());
    REQUIRE(target.applyDomainOperation(*operation.value).accepted());
}

}  // namespace

TEST_CASE("editor.automation.editing_commands_keep_level_histories_independent") {
    SceneDocumentTarget village("level.village");
    SceneDocumentTarget forest("level.forest");
    addSceneObject(village, "player");
    addSceneObject(forest, "player");

    Editor editor;
    REQUIRE(editor.registerEditingTarget(village).accepted());
    REQUIRE(editor.registerEditingTarget(forest).accepted());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("scene.transform.set.v1") != std::string::npos);
    CHECK(commands.find("material.property.set.v1") != std::string::npos);

    const std::string villageResult = automation->invoke(
        "execute",
        R"({"target":"level.village","command":"scene.transform.set.v1","payload":{"object":"player","position":[1,2,3]}})"
    );
    CHECK(villageResult.find("\"status\":\"applied\"") != std::string::npos);
    const std::string forestResult = automation->invoke(
        "execute",
        R"({"target":"level.forest","command":"scene.transform.set.v1","payload":{"object":"player","position":[7,8,9]}})"
    );
    CHECK(forestResult.find("\"status\":\"applied\"") != std::string::npos);

    auto villageTransform = village.readTransform(ObjectId("player"));
    auto forestTransform  = forest.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.value.has_value());
    REQUIRE(forestTransform.value.has_value());
    CHECK(villageTransform.value->x == 1.0);
    CHECK(forestTransform.value->x == 7.0);

    const std::string undone = automation->invoke("undo", R"({"target":"level.village"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    villageTransform = village.readTransform(ObjectId("player"));
    forestTransform  = forest.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.value.has_value());
    REQUIRE(forestTransform.value.has_value());
    CHECK(villageTransform.value->x == 0.0);
    CHECK(forestTransform.value->x == 7.0);

    const std::string redone = automation->invoke("redo", R"({"target":"level.village"})");
    CHECK(redone.find("\"status\":\"applied\"") != std::string::npos);
    villageTransform = village.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.value.has_value());
    CHECK(villageTransform.value->x == 1.0);

    const std::string inspected = automation->invoke("inspect", R"({"target":"level.forest"})");
    CHECK(inspected.find("\"id\":\"level.forest\"") != std::string::npos);
    CHECK(inspected.find("player") != std::string::npos);

    CHECK(editor.unregisterEditingTarget(TargetId("level.village")).accepted());
    CHECK(editor.unregisterEditingTarget(TargetId("level.forest")).accepted());
}

TEST_CASE("editor.v2.automation_edits_material_through_the_same_transaction_path") {
    MaterialDocumentTarget material("material.player");
    Editor                 editor;
    REQUIRE(editor.registerEditingTarget(material).accepted());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"material.player","command":"material.property.set.v1","payload":{"path":"shading.roughness","value":0.65}})"
    );
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);
    CHECK(material.snapshotValue().getIf<EditorValue::Object>()->at("shading.roughness") == EditorValue(0.65));

    const std::string undone = automation->invoke("undo", R"({"target":"material.player"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    CHECK(material.snapshotValue().getIf<EditorValue::Object>()->at("shading.roughness") != EditorValue(0.65));

    CHECK(editor.unregisterEditingTarget(TargetId("material.player")).accepted());
}
