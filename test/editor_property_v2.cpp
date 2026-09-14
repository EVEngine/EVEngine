#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorPropertyModel.h"
#include "editor/EditorPropertyPresenter.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace eve::editor;

namespace {

class TransformPropertyTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit TransformPropertyTarget(unsigned long long initialRevision = 0) : revision_(initialRevision) {}

    TargetId targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion         dirtyRegion() const override { return {}; }
    void               clearDirtyRegion() override {}

    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<TransformPropertyTarget>(*this);
    }

    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        auto* typed = dynamic_cast<TransformPropertyTarget*>(candidate.get());
        if (!typed)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("scene.property.candidate"),
                                             "Compensation candidate has an incompatible type");
        position_ = typed->position_;
        revision_ = typed->revision_;
        return eve::editing::applied<void>();
    }

    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type != "scene.transform.position.set.v1")
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("scene.property.unsupported"),
                                             "Unsupported property operation");
        if (!operation.payload.getIf<EditorValue::Array>())
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("scene.property.invalid-value"),
                                             "Position must be an array");
        position_ = operation.payload;
        ++revision_;
        return eve::editing::applied<void>();
    }

    const EditorValue& position() const { return position_; }

    void externalSet(EditorValue position) {
        position_ = std::move(position);
        ++revision_;
    }

private:
    std::string        id_       = "scene-property-target";
    unsigned long long revision_ = 0;
    EditorValue        position_ = EditorValue::Array{0.0, 0.0, 0.0};
};

class TransformPropertyProvider final : public IPropertyProvider {
public:
    explicit TransformPropertyProvider(TransformPropertyTarget* target) : target_(target) {}

    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision(target_->revision()));
    }

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
            return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("scene.property.unsupported"),
                                                        "Only absolute position changes are supported");
        DomainOperation operation;
        operation.type       = "scene.transform.position.set.v1";
        operation.target     = TargetId(target_->targetId());
        operation.payload    = value;
        operation.inverse    = target_->position();
        operation.hasInverse = true;
        operation.affectedProperties.push_back(path.value());
        return eve::editing::applied<DomainOperation>(std::move(operation));
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override {
        return makeSet(selection, path, EditorValue::Array{0.0, 0.0, 0.0}, PropertySetMode::Absolute);
    }

private:
    TransformPropertyTarget* target_ = nullptr;
};

class LegacyPropertyProvider final : public IPropertyProvider {
public:
    explicit LegacyPropertyProvider(TransformPropertyProvider provider) : provider_(std::move(provider)) {}

    PropertySchema     schema(const SelectionSnapshot& selection) const override { return provider_.schema(selection); }
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        return provider_.read(selection, path);
    }
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override {
        return provider_.makeSet(selection, path, value, mode);
    }
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath&      path) const override {
        return provider_.makeReset(selection, path);
    }

private:
    TransformPropertyProvider provider_;
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
    CHECK(developerIntent.ok());
    CHECK(runtimeIntent.ok());
    CHECK(developerIntent.value().command == runtimeIntent.value().command);
    CHECK(developerIntent.value().payload == runtimeIntent.value().payload);
    CHECK(target.position() != desired);

    auto hidden = runtimePresenter.editIntent(schema, selection, PropertyPath("debug.internal-name"),
                                              EditorValue("changed"), runtime);
    CHECK_EQ(static_cast<int>(hidden.code()), static_cast<int>(EditorStatus::Rejected));
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
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected,
                                                                  RuleId("editor.property.payload"),
                                                                  "Property command payload is incomplete");
                      const auto* path = payload->at("path").getIf<std::string>();
                      if (!path)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("editor.property.path"), "Property path must be a string");
                      auto operation = provider.makeSet(selection, PropertyPath(*path), payload->at("value"),
                                                        PropertySetMode::Absolute);
                      if (!operation.ok()) return EditorResult<CommandPlan>::failure(operation.status());
                      CommandPlan plan;
                      plan.operations.push_back(std::move(operation).takeValue());
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Set property";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.ok())
                          return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("editor.property.begin"),
                                                                         "Could not begin property transaction");
                      for (const DomainOperation& operation : plan.operations) {
                          const auto appended = backend.append(operation);
                          if (!appended.ok())
                              return eve::editing::failed<TransactionReceipt>(
                                  EditorStatus::Failed, RuleId("editor.property.append"),
                                  "Could not append the planned property operation");
                      }
                      return backend.commit();
                  })
              .ok());

    DeveloperPropertyPresenter developerPresenter;
    RuntimePropertyPresenter   gamePresenter;
    EditorSession              developerSession;
    developerSession.setCommandService(&commands);
    developerSession.bindTarget(&target);

    EditorValue firstPosition = EditorValue::Array{4.0, 5.0, 6.0};
    auto        developerIntent =
        developerPresenter.editIntent(schema, selection, PropertyPath("transform.position"), firstPosition);
    auto developerPlan = developerSession.planCommand(developerIntent.value().command, developerIntent.value().payload);
    CHECK(developerPlan.ok());
    CHECK(developerSession.executePlan(developerPlan.value(), developerIntent.value().payload).ok());
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
    auto gamePlan = gameSession.planCommand(gameIntent.value().command, gameIntent.value().payload);
    CHECK(gamePlan.ok());
    CHECK(gameSession.executePlan(gamePlan.value(), gameIntent.value().payload).ok());
    CHECK(target.position() == secondPosition);
    CHECK(backend.canUndo());
}

TEST_CASE("editor.v2.property_schema_rejects_type_range_and_read_only") {
    PropertyDescriptor property;
    property.path            = PropertyPath("speed");
    property.type            = PropertyType::Float;
    property.numeric.minimum = 0.0;
    property.numeric.maximum = 10.0;
    CHECK(validatePropertyValue(property, EditorValue(5.0)).ok());
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue("fast")).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(12.0)).code()),
             static_cast<int>(EditorStatus::Rejected));
    property.flags = PropertyFlag::ReadOnly;
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(5.0)).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.v2.property_model_uses_transaction_backend_for_commit_undo_and_redo") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    bool legacySinkCalled = false;
    model.setEditSink([&](const PropertyEditIntent&) {
        legacySinkCalled = true;
        return eve::editing::applied<void>();
    });

    const EditorValue desired = EditorValue::Array{10.0, 11.0, 12.0};
    const auto        write   = model.write("transform.position", toPresentationValue(desired));
    CHECK(write.accepted);
    CHECK(!legacySinkCalled);
    CHECK(target.position() == desired);
    CHECK(backend.canUndo());

    auto undone = model.undo();
    CHECK(undone.ok());
    CHECK(!undone.value().id.empty());
    CHECK(target.position() != desired);

    auto redone = model.redo();
    CHECK(redone.ok());
    CHECK(!redone.value().id.empty());
    CHECK(target.position() == desired);
}

TEST_CASE("editor.v2.property_model_explicit_transaction_previews_without_mutation") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);
    const EditorValue         desired = EditorValue::Array{13.0, 14.0, 15.0};

    auto begun = model.beginTransaction("Set transform position");
    CHECK(begun.ok());
    const auto staged = model.write("transform.position", toPresentationValue(desired));
    CHECK(staged.accepted);
    CHECK(target.position() != desired);

    auto previewed = model.previewTransaction();
    CHECK(previewed.ok());
    CHECK(target.position() != desired);

    auto committed = model.commitTransaction();
    CHECK(committed.ok());
    CHECK(target.position() == desired);
}

TEST_CASE("editor.v2.property_model_failed_commit_keeps_target_unchanged_and_is_discardable") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    ReadOnlyAuthority         authority;
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);
    const EditorValue         before = target.position();

    const auto failed = model.write("transform.position", toPresentationValue(EditorValue::Array{16.0, 17.0, 18.0}));
    CHECK(!failed.accepted);
    CHECK(target.position() == before);
    CHECK(backend.active());

    auto discarded = model.rollbackTransaction();
    CHECK(discarded.ok());
    CHECK(target.position() == before);
    CHECK(!backend.active());
}

TEST_CASE("editor.v2.property_model_binds_non_zero_provider_revision") {
    TransformPropertyTarget   target(41);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(41));
    const auto write = model.write("transform.position", toPresentationValue(EditorValue::Array{1.0, 2.0, 3.0}));
    CHECK(write.accepted);
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(42));
}

TEST_CASE("editor.v2.property_model_rejects_external_change_until_refresh_rebase") {
    TransformPropertyTarget   target(7);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    const EditorValue external = EditorValue::Array{20.0, 21.0, 22.0};
    target.externalSet(external);
    const EditorValue beforeRejectedWrite = target.position();
    const auto rejected = model.write("transform.position", toPresentationValue(EditorValue::Array{8.0, 9.0, 10.0}));
    CHECK(!rejected.accepted);
    CHECK_EQ(rejected.code, std::string("editor.property.revision-conflict"));
    CHECK(target.position() == beforeRejectedWrite);
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(7));

    const auto refreshed = model.refresh();
    CHECK(refreshed.ok());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(8));
    CHECK(model.read("transform.position") == std::optional<eve::Value>(toPresentationValue(external)));

    const auto accepted = model.write("transform.position", toPresentationValue(EditorValue::Array{8.0, 9.0, 10.0}));
    CHECK(accepted.accepted);
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(9));
}

TEST_CASE("editor.v2.property_model_external_change_conflict_preserves_failed_transaction_state") {
    TransformPropertyTarget   target(12);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    const auto begun = model.beginTransaction("stale property edit");
    CHECK(begun.ok());
    const auto staged = model.write("transform.position", toPresentationValue(EditorValue::Array{30.0, 31.0, 32.0}));
    CHECK(staged.accepted);
    const EditorValue external = EditorValue::Array{40.0, 41.0, 42.0};
    target.externalSet(external);

    const auto failed = model.commitTransaction();
    CHECK(!failed.ok());
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK(target.position() == external);
    CHECK(backend.active());

    const auto discarded = model.rollbackTransaction();
    CHECK(discarded.ok());
    CHECK(target.position() == external);
    CHECK(!backend.active());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(12));
}

TEST_CASE("editor.v2.legacy_property_provider_fails_closed_without_implicit_zero_revision") {
    TransformPropertyTarget   target(0);
    TransformPropertyProvider revisionAware(&target);
    LegacyPropertyProvider    legacy(std::move(revisionAware));
    SelectionSnapshot         selection = sceneSelection(target);
    EditorPropertyModel       model(legacy.schema(selection), selection, &legacy);
    model.setEditSink([](const PropertyEditIntent&) { return eve::editing::applied<void>(); });

    const auto rejected = model.write("transform.position", toPresentationValue(EditorValue::Array{2.0, 3.0, 4.0}));
    CHECK(!rejected.accepted);
    CHECK_EQ(rejected.code, std::string("editor.property.revision-unsupported"));
    const EditorValue expected = EditorValue::Array{2.0, 3.0, 4.0};
    CHECK(target.position() != expected);
}
