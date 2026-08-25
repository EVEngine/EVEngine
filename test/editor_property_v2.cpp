#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorPropertyPresenter.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <string>

using namespace eve::editor;

namespace {

class TransformPropertyTarget final : public IDomainOperationTarget {
public:
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion         dirtyRegion() const override { return {}; }
    void               clearDirtyRegion() override {}

    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type != "scene.transform.position.set.v1")
            return EditorResult<void>::error(EditorStatus::Unsupported, RuleId("scene.property.unsupported"),
                                             "Unsupported property operation");
        if (!operation.payload.getIf<EditorValue::Array>())
            return EditorResult<void>::error(EditorStatus::Rejected, RuleId("scene.property.invalid-value"),
                                             "Position must be an array");
        position_ = operation.payload;
        ++revision_;
        return EditorResult<void>::applied();
    }

    const EditorValue& position() const { return position_; }

private:
    std::string        id_       = "scene-property-target";
    unsigned long long revision_ = 0;
    EditorValue        position_ = EditorValue::Array{0.0, 0.0, 0.0};
};

class TransformPropertyProvider final : public IPropertyProvider {
public:
    explicit TransformPropertyProvider(TransformPropertyTarget* target) : target_(target) {}

    PropertySchema schema(const SelectionSnapshot&) const override {
        PropertySchema result;
        result.typeId = "scene.transform";
        PropertyDescriptor position;
        position.path           = PropertyPath("transform.position");
        position.displayNameKey = "editor.transform.position";
        position.category       = "transform";
        position.type           = PropertyType::Vec3;
        position.flags          = PropertyFlag::Runtime | PropertyFlag::MultiEdit;
        position.defaultValue   = EditorValue::Array{0.0, 0.0, 0.0};
        result.properties.push_back(std::move(position));

        PropertyDescriptor debug;
        debug.path           = PropertyPath("debug.internal-name");
        debug.displayNameKey = "editor.debug.internal-name";
        debug.category       = "debug";
        debug.type           = PropertyType::String;
        debug.flags          = PropertyFlag::Advanced | PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
        debug.defaultValue   = "SceneObject";
        result.properties.push_back(std::move(debug));
        return result;
    }

    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath& path) const override {
        if (path == PropertyPath("transform.position")) return {PropertyReadState::Value, target_->position(), {}};
        if (path == PropertyPath("debug.internal-name"))
            return {PropertyReadState::Value, EditorValue("SceneObject"), {}};
        return {};
    }

    EditorResult<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath& path, const EditorValue& value,
                                          PropertySetMode mode) const override {
        if (path != PropertyPath("transform.position") || mode != PropertySetMode::Absolute)
            return EditorResult<DomainOperation>::error(EditorStatus::Unsupported, RuleId("scene.property.unsupported"),
                                                        "Only absolute position changes are supported");
        DomainOperation operation;
        operation.type       = "scene.transform.position.set.v1";
        operation.target     = TargetId(target_->targetId());
        operation.payload    = value;
        operation.inverse    = target_->position();
        operation.hasInverse = true;
        operation.affectedProperties.push_back(path.value());
        return EditorResult<DomainOperation>::applied(std::move(operation));
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override {
        return makeSet(selection, path, EditorValue::Array{0.0, 0.0, 0.0}, PropertySetMode::Absolute);
    }

private:
    TransformPropertyTarget* target_ = nullptr;
};

SelectionSnapshot sceneSelection(const TransformPropertyTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "global.scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId("object-1");
    item.type   = "scene.object";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.v2.property_presenters_share_schema_and_command_intent") {
    TransformPropertyTarget    target;
    TransformPropertyProvider  provider(&target);
    SelectionSnapshot          selection = sceneSelection(target);
    PropertySchema             schema    = provider.schema(selection);
    DeveloperPropertyPresenter developerPresenter;
    RuntimePropertyPresenter   runtimePresenter;
    HostProfile                runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(CommandId("editor.property.set"));

    PropertyPresentation developer = developerPresenter.present(schema, selection, provider);
    PropertyPresentation game      = runtimePresenter.present(schema, selection, provider, runtime);
    CHECK_EQ(developer.rows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(game.rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(game.rows.front().descriptor.path.value(), std::string("transform.position"));

    EditorValue desired = EditorValue::Array{1.0, 2.0, 3.0};
    auto        developerIntent =
        developerPresenter.editIntent(schema, selection, PropertyPath("transform.position"), desired);
    auto runtimeIntent =
        runtimePresenter.editIntent(schema, selection, PropertyPath("transform.position"), desired, runtime);
    CHECK(developerIntent.accepted());
    CHECK(runtimeIntent.accepted());
    CHECK(developerIntent.value->command == runtimeIntent.value->command);
    CHECK(developerIntent.value->payload == runtimeIntent.value->payload);
    CHECK(target.position() != desired);

    auto hidden = runtimePresenter.editIntent(schema, selection, PropertyPath("debug.internal-name"),
                                              EditorValue("changed"), runtime);
    CHECK_EQ(static_cast<int>(hidden.status), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.v2.developer_and_game_property_ui_execute_same_command") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    PropertySchema            schema    = provider.schema(selection);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorCommandService      commands;

    CommandDescriptor descriptor;
    descriptor.id               = CommandId("editor.property.set");
    descriptor.ownerModule      = "editor";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      const auto* payload = request.payload.getIf<EditorValue::Object>();
                      if (!payload || !payload->contains("path") || !payload->contains("value"))
                          return EditorResult<CommandPlan>::error(EditorStatus::Rejected,
                                                                  RuleId("editor.property.payload"),
                                                                  "Property command payload is incomplete");
                      const auto* path = payload->at("path").getIf<std::string>();
                      if (!path)
                          return EditorResult<CommandPlan>::error(
                              EditorStatus::Rejected, RuleId("editor.property.path"), "Property path must be a string");
                      auto operation = provider.makeSet(selection, PropertyPath(*path), payload->at("value"),
                                                        PropertySetMode::Absolute);
                      if (!operation.accepted()) {
                          EditorResult<CommandPlan> failed;
                          failed.status      = operation.status;
                          failed.diagnostics = std::move(operation.diagnostics);
                          return failed;
                      }
                      CommandPlan plan;
                      plan.operations.push_back(std::move(*operation.value));
                      return EditorResult<CommandPlan>::applied(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Set property";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.accepted())
                          return EditorResult<TransactionReceipt>::error(begun.status, RuleId("editor.property.begin"),
                                                                         "Could not begin property transaction");
                      for (const DomainOperation& operation : plan.operations) backend.append(operation);
                      return backend.commit();
                  })
              .accepted());

    DeveloperPropertyPresenter developerPresenter;
    RuntimePropertyPresenter   gamePresenter;
    EditorSession              developerSession;
    developerSession.setCommandService(&commands);
    developerSession.bindTarget(&target);

    EditorValue firstPosition = EditorValue::Array{4.0, 5.0, 6.0};
    auto        developerIntent =
        developerPresenter.editIntent(schema, selection, PropertyPath("transform.position"), firstPosition);
    auto developerPlan = developerSession.planCommand(developerIntent.value->command, developerIntent.value->payload);
    CHECK(developerPlan.accepted());
    CHECK(developerSession.executePlan(*developerPlan.value, developerIntent.value->payload).accepted());
    CHECK(target.position() == firstPosition);

    HostProfile runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(descriptor.id);
    EditorSession gameSession;
    gameSession.setHostProfile(runtime);
    gameSession.setCommandService(&commands);
    gameSession.bindTarget(&target);
    EditorValue secondPosition = EditorValue::Array{7.0, 8.0, 9.0};
    auto        gameIntent =
        gamePresenter.editIntent(schema, selection, PropertyPath("transform.position"), secondPosition, runtime);
    auto gamePlan = gameSession.planCommand(gameIntent.value->command, gameIntent.value->payload);
    CHECK(gamePlan.accepted());
    CHECK(gameSession.executePlan(*gamePlan.value, gameIntent.value->payload).accepted());
    CHECK(target.position() == secondPosition);
    CHECK(backend.canUndo());
}

TEST_CASE("editor.v2.property_schema_rejects_type_range_and_read_only") {
    PropertyDescriptor property;
    property.path            = PropertyPath("speed");
    property.type            = PropertyType::Float;
    property.numeric.minimum = 0.0;
    property.numeric.maximum = 10.0;
    CHECK(validatePropertyValue(property, EditorValue(5.0)).accepted());
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue("fast")).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(12.0)).status),
             static_cast<int>(EditorStatus::Rejected));
    property.flags = PropertyFlag::ReadOnly;
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(5.0)).status),
             static_cast<int>(EditorStatus::Rejected));
}
