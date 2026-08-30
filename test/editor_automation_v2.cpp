#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "common/RenderCapture.h"
#include "editor/Editor.h"
#include "editor/EditorMaterialTarget.h"
#include "editor/EditorSceneTarget.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/GraphicsCapabilities.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"

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
                .isAccepted());

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
                .isAccepted());

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
    REQUIRE(operation.isAccepted());
    REQUIRE(operation.value.has_value());
    REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}

}  // namespace

TEST_CASE("editor.automation.editing_commands_keep_level_histories_independent") {
    SceneDocumentTarget village("level.village");
    SceneDocumentTarget forest("level.forest");
    addSceneObject(village, "player");
    addSceneObject(forest, "player");

    Editor editor;
    REQUIRE(editor.registerEditingTarget(village).isAccepted());
    REQUIRE(editor.registerEditingTarget(forest).isAccepted());
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

    CHECK(editor.unregisterEditingTarget(TargetId("level.village")).isAccepted());
    CHECK(editor.unregisterEditingTarget(TargetId("level.forest")).isAccepted());
}

TEST_CASE("editor.v2.automation_edits_material_through_the_same_transaction_path") {
    MaterialDocumentTarget material("material.player");
    Editor                 editor;
    REQUIRE(editor.registerEditingTarget(material).isAccepted());
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

    CHECK(editor.unregisterEditingTarget(TargetId("material.player")).isAccepted());
}

TEST_CASE("editor.automation_owns_headless_targets_for_agent_lifecycle") {
    Editor editor;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created = automation->invoke(
        "target-create", R"({"target":"agent.scene","type":"scene","object":"player"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);
    const std::string moved = automation->invoke(
        "execute",
        R"({"target":"agent.scene","command":"scene.transform.set.v1","payload":{"object":"player","position":[4,5,6]}})");
    REQUIRE(moved.find("\"status\":\"applied\"") != std::string::npos);
    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.scene"})");
    CHECK(inspected.find("player") != std::string::npos);
    CHECK(inspected.find("\"x\":4") != std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.scene"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterClose = automation->invoke("inspect", R"({"target":"agent.scene"})");
    CHECK(afterClose.find("not-found") != std::string::npos);
}

TEST_CASE("editor.automation_rx_observation_session_deduplicates_and_cancels") {
    Editor editor;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string started = automation->invoke(
        "observe-start",
        R"({"descriptor":{"observer":"scene-node","host":"world","node":"player"},"event":{"observation":{"status":"ok","x":1},"converged":false}})"
    );
    REQUIRE(started.find("\"status\":\"applied\"") != std::string::npos);
    const std::size_t idStart = started.find("editor.observe.");
    REQUIRE(idStart != std::string::npos);
    const std::size_t idEnd = started.find('"', idStart);
    const std::string sessionId = started.substr(idStart, idEnd - idStart);
    const std::string request = "{\"sessionId\":\"" + sessionId + "\"}";

    CHECK(started.find("\"x\":1") != std::string::npos);
    const std::string initial = automation->invoke("observe-poll", request);
    CHECK(initial.find("\"events\":[]") != std::string::npos);
    REQUIRE(automation->invoke(
                "observe-publish",
                "{\"sessionId\":\"" + sessionId +
                    "\",\"event\":{\"observation\":{\"status\":\"ok\",\"x\":1},\"converged\":false}}")
                .find("\"status\":\"applied\"") != std::string::npos);
    const std::string duplicate = automation->invoke("observe-poll", request);
    CHECK(duplicate.find("\"events\":[]") != std::string::npos);

    REQUIRE(automation->invoke(
                "observe-publish",
                "{\"sessionId\":\"" + sessionId +
                    "\",\"event\":{\"observation\":{\"status\":\"ok\",\"x\":4},\"converged\":true}}")
                .find("\"status\":\"applied\"") != std::string::npos);
    const std::string changed = automation->invoke("observe-poll", request);
    CHECK(changed.find("\"x\":4") != std::string::npos);
    CHECK(changed.find("\"converged\":true") != std::string::npos);

    CHECK(automation->invoke("observe-close", request).find("\"status\":\"applied\"") != std::string::npos);
    CHECK(automation->invoke("observe-describe", request).find("not-found") != std::string::npos);
}

TEST_CASE("editor.automation_binds_live_scene_host_and_preserves_host_ownership") {
    auto* scene = eve::scene::Scene::create();
    REQUIRE(scene != nullptr);
    auto mounted = scene->mountAs("agent-live", eve::scene::node("root", {eve::scene::node("player")}));
    REQUIRE(mounted.ok());
    eve::scene::SceneHost* host = mounted.value();

    Editor editor;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created = automation->invoke(
        "target-create", R"({"target":"agent.live","type":"scene-host","host":"agent-live"})");
    CHECK(created.find("\"status\":\"applied\"") != std::string::npos);
    const std::string moved = automation->invoke(
        "execute",
        R"({"target":"agent.live","command":"scene.transform.set.v1","payload":{"object":"player","position":[40,50,0]}})");
    CHECK(moved.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(host->findById("player").ok());
    CHECK_EQ(host->findById("player").value()->x, 40.f);
    CHECK_EQ(host->findById("player").value()->y, 50.f);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.live"})");
    REQUIRE(undone.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(host->findById("player").value()->x, 0.f);

    const std::string redone = automation->invoke("redo", R"({"target":"agent.live"})");
    REQUIRE(redone.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(host->findById("player").value()->x, 40.f);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.live"})");
    REQUIRE(closed.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(host->findById("player").ok());
}

TEST_CASE("editor.automation_publishes_material_transactions_to_live_renderable") {
    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    renderable->setTint(0.2f, 0.3f, 0.4f, 1.f);
    renderable->setRoughness(0.45f);
    eve::graphics::registerGraphicsCapabilities();
    auto* renderInspection = eve::cap::query<eve::IRenderCapture>();
    REQUIRE(renderInspection != nullptr);
    auto initial = renderInspection->inspectRenderable3D(renderable->id, renderable->generation);
    REQUIRE(initial.ok());
    REQUIRE_EQ(initial.value().tintG, 0.3f);

    Editor editor;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string handle = "{\"target\":\"agent.material\",\"type\":\"material-renderable3d\","
                               "\"entityId\":" + std::to_string(renderable->id) +
                               ",\"generation\":" + std::to_string(renderable->generation) + "}";
    REQUIRE(automation->invoke("target-create", handle).find("\"status\":\"applied\"") !=
            std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.material","command":"material.property.set.v1","payload":{"path":"shading.tint","value":[0.1,0.8,0.2,1]}})");
    REQUIRE(changed.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.8f);
    auto live = renderInspection->inspectRenderable3D(renderable->id, renderable->generation);
    REQUIRE(live.ok());
    REQUIRE_EQ(live.value().tintG, 0.8f);
    REQUIRE_EQ(renderable->meshRenderer()->r, 0.1f);

    const std::string corrected = automation->invoke(
        "execute",
        R"({"target":"agent.material","command":"material.property.set.v1","payload":{"path":"shading.tint","value":[0.1,0.9,0.2,1]}})");
    REQUIRE(corrected.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.9f);

    REQUIRE(automation->invoke("undo", R"({"target":"agent.material"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.8f);
    REQUIRE(automation->invoke("undo", R"({"target":"agent.material"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.3f);
    REQUIRE(automation->invoke("redo", R"({"target":"agent.material"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.8f);
    REQUIRE(automation->invoke("redo", R"({"target":"agent.material"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.9f);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.material"})")
                .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.9f);
    const std::uint32_t id = renderable->id;
    const std::uint32_t generation = renderable->generation;
    ecs::DestroyEntity(renderable);
    auto stale = renderInspection->inspectRenderable3D(id, generation);
    REQUIRE_NOT(stale.ok());
}
