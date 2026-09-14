#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editing/EditingValueJson.h"
#include "scene/editing/SceneTarget.h"

#include <map>

namespace {
using namespace eve::editing;
using namespace eve::scene_editing;

struct Commands final : IEditingCommandRegistry {
    std::map<std::string, EditingCommandPlanner> planners;
    Result<void> registerPlannedCommand(EditingCommandDescriptor descriptor, EditingCommandPlanner planner) override {
        planners.emplace(descriptor.id.value(), std::move(planner));
        return applied<void>();
    }
    Result<std::size_t> unregisterOwner(const std::string&) override {
        const auto count = planners.size();
        planners.clear();
        return applied<std::size_t>(count);
    }
    Result<CommandPlan> plan(IEditableTarget& target, const std::string& command, Value payload) {
        CommandRequest request;
        request.payload = std::move(payload);
        return planners.at(command)(target, request);
    }
};

TEST_CASE("scene.editing.commands_hierarchy_plans_are_reversible_and_backend_neutral") {
    Commands commands;
    REQUIRE(registerEditingCommands(commands).ok());
    for (const auto* id :
         {"scene.object.create.v1", "scene.object.delete.v1", "scene.object.rename.v1", "scene.object.reparent.v1"})
        REQUIRE(commands.planners.contains(id));
    SceneDocumentTarget document("document");
    RuntimeWorldTarget  runtime("runtime");
    for (SceneTargetBase* target :
         {static_cast<SceneTargetBase*>(&document), static_cast<SceneTargetBase*>(&runtime)}) {
        auto create =
            commands.plan(*target, "scene.object.create.v1",
                          Value::Object{{"object", "cube"}, {"name", "Cube"}, {"position", Value::Array{1, 2, 3}}});
        REQUIRE(create.ok());
        REQUIRE(!target->sceneObject(ObjectId("cube")).ok());
        REQUIRE_EQ(create.value().operations.size(), std::size_t(1));
        REQUIRE(target->applyDomainOperation(create.value().operations.front()).ok());
        REQUIRE_EQ(target->readTransform(ObjectId("cube")).value().y, 2.0);
        auto rename =
            commands.plan(*target, "scene.object.rename.v1", Value::Object{{"object", "cube"}, {"name", "Renamed"}});
        REQUIRE(rename.ok());
        auto op = rename.value().operations.front();
        REQUIRE(target->applyDomainOperation(op).ok());
        if (!op.inverseType.empty()) op.type = op.inverseType;
        op.payload = op.inverse;
        REQUIRE(target->applyDomainOperation(op).ok());
        REQUIRE_EQ(target->sceneObject(ObjectId("cube")).value().name, std::string("Cube"));
        auto cycle =
            commands.plan(*target, "scene.object.reparent.v1", Value::Object{{"object", "cube"}, {"parent", "cube"}});
        REQUIRE(!cycle.ok());
        auto remove = commands.plan(*target, "scene.object.delete.v1", Value::Object{{"object", "cube"}});
        REQUIRE(remove.ok());
        REQUIRE(target->applyDomainOperation(remove.value().operations.front()).ok());
        REQUIRE(target->sceneChildren({}).empty());
    }
}

TEST_CASE("scene.editing.snapshot_restore_is_validated_atomic_and_reversible") {
    SceneDocumentTarget target("scene");
    auto                create = target.makeCreate({ObjectId("root"), {}, "Root", {}});
    REQUIRE(create.ok());
    REQUIRE(target.applyDomainOperation(create.value()).ok());
    const auto before  = target.snapshotValue();
    auto       decoded = editorValueFromJson(editorValueToJson(before));
    REQUIRE(decoded.ok());
    auto restore = target.makeRestore(decoded.value());
    REQUIRE(restore.ok());
    REQUIRE(target.applyDomainOperation(restore.value()).ok());
    REQUIRE_EQ(editorValueToJson(before), editorValueToJson(target.snapshotValue()));
    auto invalid                                       = decoded.value();
    (*invalid.getIf<Value::Object>())["schemaVersion"] = 99;
    const auto revision                                = target.revision();
    REQUIRE(!target.makeRestore(invalid).ok());
    REQUIRE_EQ(target.revision(), revision);
    invalid       = decoded.value();
    auto& objects = *invalid.getIf<Value::Object>()->at("objects").getIf<Value::Array>();
    (*objects.front().getIf<Value::Object>())["parent"] = "root";
    REQUIRE(!target.makeRestore(invalid).ok());
    REQUIRE_EQ(editorValueToJson(before), editorValueToJson(target.snapshotValue()));
}
}  // namespace
