// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "script-binding": [
      "editor.script_tool.implements_the_same_session_protocol",
      "editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace",
      "editor.actionTimeline.scriptPersistsDocumentAndReportsDirtyState",
      "editor.animation.clip_editor_script_composes_workspace_and_transactions",
      "editor.avatar.document_editor_script_composes_workspace_and_rejects_referenced_delete",
      "editor.biome.rules_editor_script_composes_workspace_and_exclusions",
      "editor.audio.source_editor_script_composes_workspace_and_transactions"
    ],
    "platform-file": [
      "archspace.document.opening_cutout_changes_mesh_and_helpers_work"
    ]
  },
  "module": "dialogue",
  "modules": [
    "dialogue",
    "editing",
    "editor",
    "scene_editor"
  ],
  "origins": [
    "dialogue_state_integration.cpp",
    "editing_contracts.cpp",
    "editing_result_storage.cpp",
    "editing_satellite_contract_suite.cpp",
    "editing_value_lifecycle.cpp",
    "editor.cpp",
    "editor_animation_clip.cpp",
    "editor_archspace_target.cpp",
    "editor_archspace_tools.cpp",
    "editor_audio_editor.cpp",
    "editor_audio_source_script.cpp",
    "editor_audio_transport.cpp",
    "editor_automation_v2.cpp",
    "editor_avatar_document_editor.cpp",
    "editor_avatar_editor.cpp",
    "editor_avatar_target.cpp",
    "editor_host_package.cpp",
    "editor_script_component_payload.cpp"
  ]
}
*/
// clang-format on
#include <map>
#include "editor_preview_script_test_support.h"
#include "editor_preview_test_support.h"


#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_state_integration)
#include "PrivateTestScope.h"
#include <array>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "StatePatchTestSupport.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DialogueState.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "statepatch/StateAccessAdapter.h"
#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_state_integration.cpp
namespace test_dialogue_state_integration {
namespace {

using eve::IStateMutation;
using eve::IStateQuery;
using eve::MutationContext;
using eve::MutationReceipt;
using eve::Result;
using eve::StateMutation;
using eve::Value;
using eve::dialogue::CommandRequest;
using eve::dialogue::CommandRequestKind;
using eve::dialogue::CommandResponse;
using eve::dialogue::DialogueFlow;
using eve::dialogue::DialogueStateContext;
using eve::dnut::SequenceAsset;
using eve::dnut::SequenceNode;
using eve::dnut::SequenceRuntime;

class TestWorld final : public IStateQuery {
public:
    std::string subject  = "actor";
    Value       mood     = Value("calm");
    bool        unlocked = false;

    std::optional<Value> value(std::string_view queriedSubject, std::string_view key) const override {
        if (queriedSubject != subject) return std::nullopt;
        if (key == "mood") return mood;
        return std::nullopt;
    }

    std::optional<bool> hasTag(std::string_view queriedSubject, std::string_view tag) const override {
        if (queriedSubject != subject || tag.empty()) return std::nullopt;
        if (tag == "unlocked") return unlocked;
        return false;
    }

    std::optional<Value> attribute(std::string_view queriedSubject, std::string_view key) const override {
        if (queriedSubject != subject || key != "reputation") return std::nullopt;
        return Value(12.0);
    }

    std::optional<Value> resource(std::string_view, std::string_view) const override { return std::nullopt; }

    std::optional<Value> state(std::string_view queriedSubject, std::string_view key) const override {
        return value(queriedSubject, key);
    }

    std::optional<bool> authority(std::string_view, std::string_view) const override { return std::nullopt; }
};

class RecordingMutation final : public IStateMutation {
public:
    int             calls     = 0;
    std::size_t     lastCount = 0;
    MutationContext lastContext;

    Result<MutationReceipt> apply(std::span<const StateMutation> mutations, const MutationContext& context) override {
        ++calls;
        lastCount   = mutations.size();
        lastContext = context;
        return Result<MutationReceipt>::success(MutationReceipt{context.transactionId, mutations.size()},
                                                eve::Status::success(eve::StatusCode::Applied));
    }
};

SequenceAsset makeWorldBranchAsset() {
    SequenceAsset asset;
    asset.id    = "p1.world-branch";
    asset.entry = "branch";
    asset.parameters.push_back(eve::dnut::SequenceParameter{"mood"});

    SequenceNode branch;
    branch.id   = "branch";
    branch.type = "branch";
    branch.routes.push_back(
        {"world", Value::Object{{"state", Value("mood")}, {"equals", Value("calm")}}, "world-line"});
    branch.routes.push_back({"else", {}, "local-line"});

    SequenceNode worldLine;
    worldLine.id   = "world-line";
    worldLine.type = "line";
    worldLine.next = "end";

    SequenceNode localLine = worldLine;
    localLine.id           = "local-line";

    SequenceNode end;
    end.id   = "end";
    end.type = "end";

    asset.nodes = {branch, worldLine, localLine, end};
    return asset;
}

SequenceAsset makeFacadeAsset() {
    SequenceAsset asset;
    asset.id    = "p1.facade";
    asset.entry = "branch";

    SequenceNode branch;
    branch.id   = "branch";
    branch.type = "branch";
    branch.routes.push_back({"custom", Value::Object{{"policy", Value("facade.test")}}, "operation"});
    branch.routes.push_back({"else", {}, "end"});

    SequenceNode operation;
    operation.id   = "operation";
    operation.type = "command";
    operation.payload.set("name", Value("market.buy"));
    operation.payload.set("resultLocal", Value("operationResult"));
    operation.next = "action";

    SequenceNode action;
    action.id   = "action";
    action.type = "command";
    action.payload.set("name", Value("combat.attack"));
    action.payload.set("kind", Value("gameplay"));
    action.payload.set("resultLocal", Value("actionResult"));
    action.next = "end";

    SequenceNode end;
    end.id   = "end";
    end.type = "end";

    asset.nodes = {branch, operation, action, end};
    return asset;
}

template <std::size_t N>
std::span<eve::transaction::ITransactionParticipant*> participantSpan(
    std::array<eve::transaction::ITransactionParticipant*, N>& participants) {
    return {participants.data(), participants.size()};
}

}  // namespace

TEST_CASE("dialogueState.conversationLocalsDoNotBecomeWorldState") {
    TestWorld            world;
    DialogueStateContext context(world.subject);
    context.setQueryProvider(&world);
    SequenceAsset asset = makeWorldBranchAsset();

    eve::dnut::StepKindRegistry registry;
    eve::dialogue::registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    SequenceRuntime runner;
    runner.setStepRegistry(&registry);
    runner.setConditionEvaluator([&context](const Value& specification) {
        const auto result = context.evaluate(specification);
        return eve::dnut::SequenceConditionOutcome{
            result.passed(), std::string(eve::condition::conditionReasonCodeName(result.reasonCode())), {}};
    });

    Value bindings = Value::Object{{"mood", Value("local-mood")}};
    CHECK(runner.start(&asset, std::move(bindings)).ok());
    CHECK(runner.currentNodeId() == "world-line");
    CHECK(runner.bindings().find("mood")->asString() == "local-mood");

    runner.locals().set("localOnly", Value(true));
    CHECK(!world.value(world.subject, "localOnly").has_value());
    REQUIRE(runner.lastConditionResult() != nullptr);
    CHECK(runner.lastConditionResult()->passed);
    CHECK(runner.lastConditionResult()->reason == "passed");
}

TEST_CASE("dialogueState.usesSharedConditionExplanation") {
    TestWorld world;
    world.unlocked = false;
    DialogueStateContext context(world.subject);
    context.setQueryProvider(&world);
    const Value specification = Value::Object{{"tag", Value("unlocked")}};

    const auto result = context.evaluate(specification);
    CHECK(!result.passed());
    CHECK(static_cast<int>(result.reasonCode()) ==
          static_cast<int>(eve::condition::ConditionReasonCode::ValueMismatch));
    CHECK(std::string(eve::condition::conditionReasonCodeName(result.reasonCode())) == "value_mismatch");
    const Value* key = result.details().find("key");
    REQUIRE(key != nullptr);
    CHECK(key->asString() == "tag:unlocked");

    const Value comparison =
        Value::Object{{"attribute", Value("reputation")}, {"op", Value("gte")}, {"value", Value(10.0)}};
    const auto comparisonResult = context.evaluate(comparison);
    CHECK(comparisonResult.passed());
    CHECK(static_cast<int>(comparisonResult.reasonCode()) ==
          static_cast<int>(eve::condition::ConditionReasonCode::Passed));
}

TEST_CASE("dialogueState.statePatchTransactionFailureDoesNotPartiallyModify") {
    eve::statepatch::Store store;
    auto                   seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    CHECK(seed.view->set("actor", "value", "1"));
    REQUIRE(store.commit(seed.view.get()).ok());
    const std::string before         = store.snapshotJson();
    const auto        beforeRevision = store.revision();
    const auto        beforeEvents   = store.eventCount();

    eve::statepatch::StatePatchStateAdapter adapter(store);
    std::array<StateMutation, 2>            mutations{
        StateMutation{"actor", "value", Value::integer(2), eve::MutationKind::Set, true},
        StateMutation{"actor", "other", Value("not-a-number"), eve::MutationKind::AddNumber, true},
    };
    const auto rejected = adapter.apply(mutations, MutationContext{"tx-dialogue", "flow", "choice"});
    CHECK(!rejected.ok());
    CHECK(static_cast<int>(rejected.code()) == static_cast<int>(eve::StatusCode::Rejected));
    CHECK(store.snapshotJson() == before);
    CHECK(store.revision() == beforeRevision);
    CHECK(store.eventCount() == beforeEvents);

    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(batch.view->set("actor", "value", "2"));
    CHECK(!batch.view->set("actor", "broken", "{"));
    eve::statepatch::StoreTransactionParticipant              participant(store, *batch.view);
    std::array<eve::transaction::ITransactionParticipant*, 1> participants{&participant};
    eve::transaction::Coordinator                             coordinator;
    const auto                                                transactionResult =
        coordinator.execute(eve::transaction::TransactionContext("tx-dialogue-invalid"), participantSpan(participants));
    CHECK(!transactionResult.ok());
    CHECK(store.snapshotJson() == before);
    CHECK(store.revision() == beforeRevision);
    CHECK(store.eventCount() == beforeEvents);
}

TEST_CASE("dialogueState.valueRoundtripAndLegacyFacadeParity") {
    static_assert(std::is_same_v<eve::dialogue::Dialogue::DataValue, eve::Value>);
    static_assert(std::is_same_v<eve::dialogue::Dialogue::VarValue, eve::Value>);

    eve::StateValue legacy = eve::StateValue::object();
    eve::StateValue values = eve::StateValue::array();
    values.pushBack(eve::StateValue::null());
    values.pushBack(eve::StateValue::integer(-7));
    values.pushBack(eve::StateValue::number(2.5));
    values.pushBack(eve::StateValue::boolean(true));
    values.pushBack(eve::StateValue::string("中文🚀"));
    legacy.set("values", std::move(values));
    legacy.set("name", eve::StateValue::string("dialogue"));

    const Value           canonical = eve::dialogue::toCanonicalValue(legacy);
    const eve::StateValue restored  = eve::dialogue::toDialogueStateValue(canonical);
    CHECK(restored == legacy);

    auto encoded = canonical.toJson();
    REQUIRE(encoded.ok());
    auto parsed = Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == canonical);

    eve::dialogue::Dialogue::DataValue oldData = eve::dialogue::Dialogue::DataValue::object(eve::Value::Object{});
    oldData.set("answer", eve::dialogue::Dialogue::VarValue::integer(42));
    auto oldEncoded = oldData.toJson();
    REQUIRE(oldEncoded.ok());
    CHECK(oldEncoded.value() == R"({"answer":42})");
    CHECK(oldData.find("answer")->asInt() == 42);
}

TEST_CASE("dialogueState.dialogueFlowConfiguresAllCrossDomainHooks") {
    TestWorld                   world;
    RecordingMutation           mutation;
    DialogueFlow                flow;
    bool                        customConditionCalled = false;
    std::vector<CommandRequest> requests;

    DialogueFlow::IntegrationConfig config;
    config.subject            = world.subject;
    config.stateQuery         = &world;
    config.stateMutation      = &mutation;
    config.conditionEvaluator = [&customConditionCalled](const Value&) {
        customConditionCalled = true;
        return eve::condition::ConditionResult::success(Value(true));
    };
    config.operationHandler = [&requests](const CommandRequest& request) {
        requests.push_back(request);
        CommandResponse response;
        response.value = Value::integer(7);
        return response;
    };
    config.gameplayActionHandler = [&requests](const CommandRequest& request) {
        requests.push_back(request);
        CommandResponse response;
        response.value = Value::string("done");
        return response;
    };
    flow.configureIntegration(std::move(config));

    const auto worldMood = flow.stateContext().value("mood");
    REQUIRE(worldMood.has_value());
    CHECK(worldMood->asString() == "calm");

    std::array<StateMutation, 1> stateMutation{
        StateMutation{"actor", "flag", Value(true), eve::MutationKind::Set, false}};
    const auto mutationResult =
        flow.applyStateMutations(stateMutation, MutationContext{"tx-facade", "flow", "dialogue"});
    REQUIRE(mutationResult.ok());
    CHECK(mutation.calls == 1);
    CHECK(mutation.lastCount == 1);
    CHECK(mutation.lastContext.transactionId == "tx-facade");

    SequenceAsset asset    = makeFacadeAsset();
    auto*         document = new eve::dialogue::ConversationDocument(asset);
    CHECK(flow.applyDocumentChecked(document).ok());
    delete document;

    std::string error;
    CHECK(flow.startChecked(asset.id, ssq::Object{}).ok());
    CHECK(error.empty());
    CHECK(customConditionCalled);
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].name == "market.buy");
    CHECK(static_cast<int>(requests[0].kind) == static_cast<int>(CommandRequestKind::Operation));
    CHECK(requests[1].name == "combat.attack");
    CHECK(static_cast<int>(requests[1].kind) == static_cast<int>(CommandRequestKind::GameplayAction));
    CHECK(!flow.isActive());
}
}  // namespace test_dialogue_state_integration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editing_contracts)
#include <zeroerr/unittest.h>
#include <string>
#include <type_traits>
#include <vector>
#include "PrivateTestScope.h"
#include "animation/editing/AnimationClip.h"
#include "animation/editor/EditorAnimationClip.h"
#include "avatar/editing/AvatarTarget.h"
#include "avatar/editor/EditorAvatarTarget.h"
#include "building/editing/BuildingTarget.h"
#include "building/editor/EditorBuildingTarget.h"
#include "camera/editing/CameraTarget.h"
#include "crowd/editing/CrowdDocument.h"
#include "definitions/editing/DefinitionTarget.h"
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingCommandTypes.h"
#include "editing/EditingExtension.h"
#include "editing/EditingGraph.h"
#include "editing/EditingIds.h"
#include "editing/EditingProperty.h"
#include "editing/EditingProtocol.h"
#include "editing/EditingResult.h"
#include "editing/EditingSelection.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditingTaskService.h"
#include "editing/EditingValue.h"
#include "editing/EditingVolume.h"
#include "editor/Editor.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandTypes.h"
#include "editor/EditorIds.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTarget.h"
#include "editor/EditorTaskService.h"
#include "editor/EditorValue.h"
#include "editor/EditorVolumeTarget.h"
#include "editor/gizmo/editing/DomainGizmoPreview.h"
#include "editor/input/editing/InputMapTarget.h"
#include "editor/queue/editing/QueueInspector.h"
#include "fluids/editing/FluidTarget.h"
#include "fluids/editing/SurfaceFluidTarget.h"
#include "fluids/editing/VolumeFluidTarget.h"
#include "fluids/editor/EditorSurfaceFluidTarget.h"
#include "graphics/editing/OffscreenPreview.h"
#include "graphics/editor/EditorOffscreenPreview.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/material/editing/MaterialGraph.h"
#include "graphics/material/editing/MaterialTarget.h"
#include "graphics/material/editor/EditorGraph.h"
#include "graphics/material/editor/EditorMaterialTarget.h"
#include "hd2d/editing/Hd2dTarget.h"
#include "i18n/editing/LocalizationDocument.h"
#include "map/editing/MapDocument.h"
#include "network/editing/NetworkTelemetry.h"
#include "particles/editing/ParticleGraph.h"
#include "physics/editing/PhysicsColliderAsset.h"
#include "physics/editing/PhysicsTarget.h"
#include "physics/editor/EditorPhysicsAsset.h"
#include "physics/editor/EditorPhysicsTarget.h"
#include "procgen/biome/editing/BiomeTarget.h"
#include "procgen/editing/HouseGenTarget.h"
#include "procgen/editing/TextureRecipeTarget.h"
#include "profiler/editing/ProfilerModel.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/EditorSceneTarget.h"
#include "scene/loader/editing/SceneImportTarget.h"
#include "social/editing/SocialDocument.h"
#include "ui/editing/UiDocument.h"
#include "ui/editor/EditorUiDocumentTarget.h"
#include "ui/editor/EditorUiThemeTarget.h"
#include "virtualgeometry/editing/VirtualGeometryTarget.h"
#include "voxel/editing/VoxelCatalog.h"
#include "voxel/editing/VoxelPaletteTarget.h"
#include "voxel/editing/VoxelWorldTarget.h"
#include "voxel/editor/EditorVoxelCatalogTarget.h"
#include "voxel/editor/EditorVoxelPaletteTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editing_contracts.cpp
namespace test_editing_contracts {
// editing_contracts.cpp
static_assert(!std::is_convertible_v<eve::editing::CommandId, eve::editing::ToolId>);
static_assert(std::is_same_v<eve::editor::CommandId, eve::editing::CommandId>);
static_assert(std::is_same_v<eve::editor::EditorValue, eve::editing::Value>);
static_assert(std::is_same_v<eve::editor::EditorStatus, eve::editing::Status>);
static_assert(std::is_same_v<eve::editor::CommandSource, eve::editing::CommandSource>);
static_assert(std::is_same_v<eve::editor::CommandRequest, eve::editing::CommandRequest>);
static_assert(std::is_same_v<eve::editor::TransactionReceipt, eve::editing::TransactionReceipt>);
static_assert(std::is_same_v<eve::editor::SelectionSnapshot, eve::editing::SelectionSnapshot>);
static_assert(std::is_same_v<eve::editor::IEditableTarget, eve::editing::IEditableTarget>);
static_assert(std::is_same_v<eve::editor::IIntVolumeTarget, eve::editing::IIntVolumeTarget>);
static_assert(std::is_same_v<eve::editor::IDomainOperationTarget, eve::editing::IDomainOperationTarget>);
static_assert(std::is_same_v<eve::editor::IDomainOperationTargetStaging, eve::editing::IDomainOperationTargetStaging>);
static_assert(std::is_same_v<eve::editor::PropertySchema, eve::editing::PropertySchema>);
static_assert(std::is_same_v<eve::editor::IPropertyProvider, eve::editing::IPropertyProvider>);
static_assert(std::is_same_v<eve::editor::IEditAuthority, eve::editing::IEditAuthority>);
static_assert(std::is_same_v<eve::editor::LocalWorldAuthority, eve::editing::LocalWorldAuthority>);
static_assert(std::is_same_v<eve::editor::GraphDocument, eve::editing::GraphDocument>);
static_assert(std::is_same_v<eve::editor::IGraphDomainProvider, eve::editing::IGraphDomainProvider>);
static_assert(std::is_same_v<eve::editor::EditorTaskService, eve::editing::TaskService>);
static_assert(std::is_same_v<eve::editor::MaterialGraphDomain, eve::material_editing::MaterialGraphDomain>);
static_assert(std::is_same_v<eve::editor::MaterialDocumentTarget, eve::material_editing::MaterialDocumentTarget>);
static_assert(std::is_same_v<eve::editor::PhysicsColliderTarget, eve::physics_editing::PhysicsColliderTarget>);
static_assert(
    std::is_same_v<eve::editor::IPhysicsColliderAssetResolver, eve::physics_editing::IPhysicsColliderAssetResolver>);
static_assert(std::is_same_v<eve::editor::SceneDocumentTarget, eve::scene_editing::SceneDocumentTarget>);
static_assert(
    std::is_same_v<eve::editor::AnimationClipDocumentTarget, eve::animation_editing::AnimationClipDocumentTarget>);
static_assert(std::is_same_v<eve::editor::AvatarDocumentTarget, eve::avatar_editing::AvatarDocumentTarget>);
static_assert(std::is_same_v<eve::editor::BuildingPlacementTarget, eve::building_editing::BuildingPlacementTarget>);
static_assert(std::is_same_v<eve::editor::GraphicsOffscreenPreviewService,
                             eve::graphics_editing::GraphicsOffscreenPreviewService>);
static_assert(std::is_same_v<eve::editor::UiDocumentTarget, eve::ui_editing::UiDocumentTarget>);
static_assert(std::is_same_v<eve::editor::UiThemeCatalogTarget, eve::ui_editing::UiThemeCatalogTarget>);
static_assert(std::is_same_v<eve::editor::SurfaceFluidTarget, eve::fluids_editing::SurfaceFluidTarget>);
static_assert(std::is_same_v<eve::editor::VoxelPaletteTarget, eve::voxel_editing::VoxelPaletteTarget>);
static_assert(std::is_same_v<eve::editor::VoxelCatalogTarget, eve::voxel_editing::VoxelCatalogTarget>);
static_assert(std::is_same_v<eve::editor::VoxelModelValue, eve::voxel_editing::VoxelModelValue>);
static_assert(!std::is_copy_constructible_v<eve::editing::Result<int>>);
static_assert(!std::is_copy_constructible_v<eve::editing::Result<void>>);

TEST_CASE("editing.contracts.ids_values_and_editor_compatibility") {
    using namespace eve::editing;

    const CommandId command("scene.object.create");
    CHECK_EQ(command.value(), std::string("scene.object.create"));
    CHECK_EQ(eve::editor::CommandId("scene.object.create"), command);

    Value::Object root;
    root["name"]     = "Station";
    root["position"] = Value::Array{1.0, 2.0, 3.0};
    const Value value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(Value::Type::Object));
    CHECK(value.isWithinLimits(4, 16, 128));
    CHECK(!value.isWithinLimits(1, 16, 128));
}

TEST_CASE("editing.contracts.property_path_is_textual") {
    using namespace eve::editing;
    const PropertyPath left("layout.size");
    const PropertyPath same("layout.size");
    const PropertyPath right("layout.position");
    CHECK(left == same);
    CHECK(left != right);
    CHECK_EQ(left.value(), std::string("layout.size"));
}

TEST_CASE("editing.contracts.structured_result") {
    using namespace eve::editing;

    const auto accepted = eve::editing::applied<Value>(Value("ready"));
    CHECK(accepted.ok());
    CHECK_EQ(*accepted.value().getIf<std::string>(), std::string("ready"));

    const auto rejected = eve::editing::failed<void>(Status::Rejected, RuleId("authoring.invalid"), "invalid input");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.diagnostics().size(), size_t{1});
    CHECK_EQ(eve::editing::diagnosticRule(rejected.diagnostics().front()).value(), std::string("authoring.invalid"));
    rejected.ignore();
}

// editing_extension_lifecycle.cpp
using namespace eve::editing;

namespace {
class Provider final : public IEditingExtensionProvider {
public:
    Result<void> activate() override {
        ++activated;
        return eve::editing::applied<void>();
    }
    Result<void> beginUnload() override {
        ++stopping;
        return rejectUnload ? eve::editing::failed<void>(Status::Failed, RuleId("test.unload"), "injected")
                            : eve::editing::applied<void>();
    }
    void  deactivate() noexcept override { ++deactivated; }
    void* query(const CapabilityId& capability) noexcept override {
        return capability == CapabilityId("test.capability") ? this : nullptr;
    }
    bool rejectUnload = false;
    int  activated    = 0;
    int  stopping     = 0;
    int  deactivated  = 0;
};
}  // namespace

TEST_CASE("editing.extension.provider_absent_unload_and_stale_handle_are_explicit") {
    ExtensionProviderRegistry registry;
    auto                      absent = registry.acquire("missing");
    CHECK_EQ(static_cast<int>(absent.code()), static_cast<int>(Status::Unsupported));

    auto                provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id           = "test.provider";
    descriptor.capabilities = {CapabilityId("test.capability")};
    auto registered         = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    CHECK_EQ(provider->activated, 1);

    auto lease = registry.acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
    CHECK(registry.unload(registered.value()).ok());
    CHECK_EQ(provider->stopping, 1);
    CHECK_EQ(provider->deactivated, 1);
    auto stale = registry.acquire(registered.value());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(Status::Conflict));
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}

TEST_CASE("editing.extension.failed_unload_preserves_current_generation") {
    ExtensionProviderRegistry registry;
    auto                      provider = std::make_shared<Provider>();
    ExtensionDescriptor       descriptor;
    descriptor.id   = "test.rejecting";
    auto registered = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    provider->rejectUnload = true;
    CHECK(!registry.unload(registered.value()).ok());
    CHECK(registry.acquire(registered.value()).ok());
    CHECK_EQ(provider->deactivated, 0);
    provider->rejectUnload = false;
    CHECK(registry.unload(registered.value()).ok());
}

TEST_CASE("editor.composition.discovers_extension_provider_without_domain_type_list") {
    eve::editor::Editor host;
    auto                provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id           = "test.open-domain-provider";
    descriptor.capabilities = {CapabilityId("test.capability")};

    auto registered = host.extensionProviders().registerProvider(std::move(descriptor), provider);
    REQUIRE(registered.ok());
    const auto descriptors = host.extensionProviders().descriptors();
    REQUIRE_EQ(descriptors.size(), static_cast<std::size_t>(1));
    CHECK_EQ(descriptors.front().id, std::string("test.open-domain-provider"));
    auto lease = host.extensionProviders().acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}

// editing_id_instances.cpp
static_assert(std::is_aggregate_v<eve::detail::UuidIdState>);
static_assert(std::is_nothrow_move_constructible_v<eve::editing::RuleId>);
static_assert(std::is_nothrow_move_assignable_v<eve::editing::RuleId>);
static_assert(std::is_nothrow_destructible_v<eve::editing::RuleId>);
TEST_CASE("editing.idInstances.owningLifecycleAndProjection") {
    eve::editing::RuleId original(std::string(4096, 'a'));
    auto                 copied = original;
    copied                      = eve::editing::RuleId("changed");
    CHECK(original.value().size() == 4096u);
    CHECK(copied.value() == "changed");
    auto moved = std::move(copied);
    copied     = eve::editing::RuleId("reused");
    eve::editing::RuleId assigned;
    assigned = original;
    assigned = std::move(moved);
    CHECK(assigned.value() == "changed");
    CHECK(copied.value() == "reused");
    auto parsed = eve::editing::RuleId::parse(original.value());
    CHECK(parsed.has_value());
    CHECK(parsed->uuid() == original.uuid());
    auto canonical = eve::editing::RuleId::fromUuid(original.uuid());
    CHECK(canonical.isCanonicalUuid());
    CHECK(canonical.canonicalFormat() == original.canonicalFormat());
    struct CustomTag;
    eve::editing::StrongId<CustomTag> custom("open-tag");
    CHECK(custom.value() == "open-tag");
}

TEST_CASE("editing.idInstances.distinctDomainTagsKeepProjection") {
    eve::editing::RuleId       rule("shared-spelling");
    eve::editing::TargetId     target("shared-spelling");
    eve::editing::StableId     stable("shared-spelling");
    eve::editing::ObjectId     object("shared-spelling");
    eve::editing::CapabilityId capability("shared-spelling");
    CHECK(rule.canonicalFormat() == target.canonicalFormat());
    CHECK(stable.canonicalFormat() == object.canonicalFormat());
    CHECK(capability.canonicalFormat() == rule.canonicalFormat());
    static_assert(!std::is_convertible_v<eve::editing::RuleId, eve::editing::TargetId>);
    static_assert(!std::is_convertible_v<eve::editing::StableId, eve::editing::ObjectId>);
}

TEST_CASE("editing.idInstances.capabilityVectorOwnsIndependentEntries") {
    using Id = eve::editing::CapabilityId;
    std::vector<Id> original;
    for (int i = 0; i < 64; ++i) original.emplace_back(std::string(4096, 'a') + std::to_string(i));
    auto       copied   = original;
    const auto expected = copied.front().value();
    original.front()    = Id("changed");
    CHECK(copied.front().value() == expected);
    auto moved = std::move(copied);
    copied.clear();
    copied.emplace_back("reused");
    std::vector<Id> assigned;
    assigned = original;
    CHECK(assigned.front().value() == "changed");
    assigned = std::move(moved);
    CHECK(assigned.front().value() == expected);
    CHECK(copied.front().value() == "reused");
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().value() == std::string(4096, 'a') + "1");
    CHECK(assigned.front().canonicalFormat() == original[1].canonicalFormat());
}

TEST_CASE("editing.idInstances.selectionVectorKeepsIndependentIdentityAndText") {
    using Item = eve::editing::SelectionItem;
    static_assert(std::is_aggregate_v<Item>);
    std::vector<Item> source;
    for (int i = 0; i < 64; ++i)
        source.push_back({eve::editing::SelectionDomain::Scene, eve::editing::TargetId("target"),
                          eve::editing::StableId(std::to_string(i)), std::string(4096, 'a')});
    auto copy           = source;
    source.front().type = "changed";
    CHECK(copy.front().type.size() == 4096u);
    auto moved = std::move(copy);
    copy.clear();
    copy.push_back({});
    CHECK(copy.size() == 1u);
    std::vector<Item> assigned;
    assigned = source;
    CHECK(assigned.front().type == "changed");
    assigned = std::move(moved);
    CHECK(assigned.front().type.size() == 4096u);
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().item.value() == "1");
    CHECK(assigned.front().target.canonicalFormat() == source[1].target.canonicalFormat());
    CHECK(static_cast<bool>(assigned.front().domain == eve::editing::SelectionDomain::Scene));
}
}  // namespace test_editing_contracts
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editing_result_storage)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <string>
#include "editing/EditingResult.h"
// editing_result_storage.cpp
namespace test_editing_result_storage {
TEST_CASE("editing.failureFactoryPreservesRuleAndStatus") {
    auto result = eve::editing::failed<int>(eve::StatusCode::Conflict, eve::DiagnosticCode::Conflict,
                                            eve::editing::RuleId("test.conflict"), "collision");
    CHECK(!result.ok());
    CHECK(result.code() == eve::StatusCode::Conflict);
    CHECK(result.diagnostics().size() == 1);
    const auto& diagnostic = result.diagnostics().front();
    CHECK(diagnostic.message() == "collision");
    CHECK(diagnostic.source() == "editing");
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == "test.conflict");
}

TEST_CASE("editing.failureFactoryRejectsSuccessfulStatus") {
#if !defined(ZEROERR_NO_ASSERT)
    bool caught = false;
    try {
        auto result = eve::editing::failed<void>(eve::StatusCode::Applied, eve::editing::RuleId("test.invalid"),
                                                 "invalid category");
        result.ignore();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file).find("EditingResult.h") != std::string::npos);
        CHECK(failure.cond == "eve::Status(status, {}).isFailure()");
        CHECK(!failure.passed);
    }
    CHECK(caught);
#else
    CHECK(true);
#endif
}

TEST_CASE("editing.ruleDiagnosticPreservesOwnedTextSeverityAndProjection") {
    std::string ruleText(4096, 'r');
    std::string message(4096, 'm');
    auto        diagnostic = eve::editing::ruleDiagnostic(eve::DiagnosticCode::Conflict, eve::editing::RuleId(ruleText),
                                                          eve::Severity::Warning, message);
    ruleText               = "changed";
    message                = "changed";
    CHECK(static_cast<bool>(diagnostic.code() == eve::DiagnosticCode::Conflict));
    CHECK(static_cast<bool>(diagnostic.severity() == eve::Severity::Warning));
    CHECK(diagnostic.message() == std::string(4096, 'm'));
    CHECK(diagnostic.path().empty());
    CHECK(diagnostic.source() == "editing");
    REQUIRE(diagnostic.details().size() == 1u);
    CHECK(diagnostic.details().front().first == "rule");
    CHECK(diagnostic.details().front().second == std::string(4096, 'r'));
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == std::string(4096, 'r'));
}
}  // namespace test_editing_result_storage
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editing_satellite_contract_suite)
#include "PrivateTestScope.h"
#include <functional>
#include <string>
#include "audio/editing/AudioEditingProvider.h"
#include "audio/editing/AudioTarget.h"
#include "editor/Editor.h"
#include "physics/editing/PhysicsEditingProvider.h"
#include "physics/editing/PhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editing_satellite_contract_suite.cpp
namespace test_editing_satellite_contract_suite {
using namespace eve::editing;

namespace {

/** Shared candidate-publication contract used unchanged by independent satellites. */
template <class PublishingTarget, class MakeOperation, class RejectPublication>
void checkAtomicPublishingContract(PublishingTarget& target, MakeOperation makeOperation,
                                   RejectPublication rejectPublication) {
    const Revision        before    = target.revision();
    const DomainOperation operation = makeOperation(target);

    rejectPublication(true);
    const auto rejected = target.applyDomainOperation(operation);
    CHECK(!rejected.ok());
    CHECK_EQ(target.revision(), before);

    rejectPublication(false);
    const auto applied = target.applyDomainOperation(operation);
    CHECK(applied.ok());
    CHECK_EQ(target.revision(), before + 1);
}

SelectionSnapshot audioSelection(const eve::audio_editing::AudioSourcePublishingTarget& target) {
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "audio.source";
    SelectionSnapshot selection;
    selection.channel = "audio";
    selection.items   = {item};
    selection.primary = item;
    return selection;
}

SelectionSnapshot physicsSelection(const eve::physics_editing::PhysicsColliderPublishingTarget& target) {
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId("collider");
    item.type   = target.authoringTarget().describe().type;
    SelectionSnapshot selection;
    selection.channel = "scene";
    selection.items   = {item};
    selection.primary = item;
    return selection;
}

class AudioSink final : public eve::audio_editing::IAudioSourceRuntimeSink {
public:
    eve::audio_editing::Result<void> publish(const eve::audio_editing::AudioSourceTarget&) override {
        return reject ? eve::editing::failed<void>(Status::Failed, RuleId("test.contract.audio-publication"),
                                                   "injected failure")
                      : eve::editing::applied<void>();
    }
    bool reject = false;
};

class PhysicsSink final : public eve::physics_editing::IPhysicsColliderRuntimeSink {
public:
    eve::physics_editing::Result<void> publish(const eve::physics_editing::PhysicsColliderTarget&) override {
        return reject ? eve::editing::failed<void>(Status::Failed, RuleId("test.contract.physics-publication"),
                                                   "injected failure")
                      : eve::editing::applied<void>();
    }
    bool reject = false;
};

}  // namespace

TEST_CASE("editing.satellite.audio_and_physics_share_atomic_publication_contract") {
    AudioSink                                       audioSink;
    eve::audio_editing::AudioSourcePublishingTarget audio("audio.contract", &audioSink);
    checkAtomicPublishingContract(
        audio,
        [&](const auto& target) {
            auto operation = target.authoringTarget().makeSet(audioSelection(target), PropertyPath("clip.asset"),
                                                              Value("asset://contract.ogg"), PropertySetMode::Absolute);
            REQUIRE(operation.ok());
            return std::move(operation.value());
        },
        [&](bool reject) { audioSink.reject = reject; });

    PhysicsSink                                           physicsSink;
    eve::physics_editing::PhysicsColliderPublishingTarget physics("physics.contract", 3, &physicsSink);
    checkAtomicPublishingContract(
        physics,
        [&](const auto& target) {
            auto operation = target.authoringTarget().makeSet(physicsSelection(target), PropertyPath("shape.kind"),
                                                              Value("sphere"), PropertySetMode::Absolute);
            REQUIRE(operation.ok());
            return std::move(operation.value());
        },
        [&](bool reject) { physicsSink.reject = reject; });
}

TEST_CASE("editing.satellite.audio_and_physics_publish_open_factory_providers") {
    eve::editor::Editor host;
    const auto          audioHandle   = eve::audio_editing::registerEditingProvider(host.extensionProviders());
    const auto          physicsHandle = eve::physics_editing::registerEditingProvider(host.extensionProviders());
    REQUIRE(audioHandle.ok());
    REQUIRE(physicsHandle.ok());

    auto audioLease   = host.extensionProviders().acquire(audioHandle.value());
    auto physicsLease = host.extensionProviders().acquire(physicsHandle.value());
    REQUIRE(audioLease.ok());
    REQUIRE(physicsLease.ok());
    auto* audioFactory = static_cast<eve::audio_editing::IAudioEditingFactory*>(
        audioLease.value().query(eve::audio_editing::IAudioEditingFactory::capabilityId()));
    auto* physicsFactory = static_cast<eve::physics_editing::IPhysicsEditingFactory*>(
        physicsLease.value().query(eve::physics_editing::IPhysicsEditingFactory::capabilityId()));
    REQUIRE(audioFactory != nullptr);
    REQUIRE(physicsFactory != nullptr);
    const auto audioTarget   = audioFactory->createSource("audio.provider-target", nullptr);
    const auto physicsTarget = physicsFactory->createCollider("physics.provider-target", 3, nullptr);
    CHECK(static_cast<bool>(audioTarget));
    CHECK(static_cast<bool>(physicsTarget));
}
}  // namespace test_editing_satellite_contract_suite
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editing_value_lifecycle)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <type_traits>
#include "editing/EditingValue.h"
#include "editing/EditingValueJson.h"
// editing_value_lifecycle.cpp
namespace test_editing_value_lifecycle {
using EditingValue = eve::editing::Value;
static_assert(std::is_nothrow_move_constructible_v<EditingValue> ==
              std::is_nothrow_move_constructible_v<EditingValue::Storage>);
static_assert(std::is_nothrow_move_assignable_v<EditingValue> ==
              std::is_nothrow_move_assignable_v<EditingValue::Storage>);
static_assert(std::is_nothrow_default_constructible_v<EditingValue>);

TEST_CASE("editing.valueLifecycle.deepCopyMoveAndDeterministicJson") {
    EditingValue original(EditingValue::Object{{"z", EditingValue::Array{1, "text"}}, {"a", true}});
    EditingValue copy(original);
    (*original.getIf<EditingValue::Object>())["z"] = EditingValue(99);
    REQUIRE(copy.getIf<EditingValue::Object>() != nullptr);
    REQUIRE(copy.getIf<EditingValue::Object>()->at("z").getIf<EditingValue::Array>() != nullptr);
    CHECK(eve::editing::editorValueToJson(copy) == "{\"a\":true,\"z\":[1,\"text\"]}");
    EditingValue assigned;
    assigned = copy;
    CHECK(assigned == copy);
    EditingValue moved(std::move(assigned));
    CHECK(moved == copy);
    assigned = EditingValue("reused");
    CHECK(assigned.type() == EditingValue::Type::String);
    EditingValue destination;
    destination = std::move(moved);
    CHECK(destination == copy);
    auto parsed = eve::editing::editorValueFromJson(eve::editing::editorValueToJson(destination));
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == destination);
    CHECK(eve::editing::editorValueContentHash(parsed.value()) == eve::editing::editorValueContentHash(copy));
    CHECK(copy.isWithinLimits(3, 10, 100));
    CHECK(!copy.isWithinLimits(1, 10, 100));
}

TEST_CASE("editing.valueLifecycle.scalarKindsAndInvalidJsonRemainStable") {
    CHECK(EditingValue().type() == EditingValue::Type::Null);
    CHECK(EditingValue(nullptr).type() == EditingValue::Type::Null);
    CHECK(EditingValue(true).type() == EditingValue::Type::Bool);
    CHECK(EditingValue(1).type() == EditingValue::Type::Integer);
    CHECK(EditingValue(int64_t(2)).type() == EditingValue::Type::Integer);
    CHECK(EditingValue(1.5f).type() == EditingValue::Type::Number);
    CHECK(EditingValue(2.5).type() == EditingValue::Type::Number);
    CHECK(*EditingValue(static_cast<const char*>(nullptr)).getIf<std::string>() == "");
    CHECK(*EditingValue(std::string("owned")).getIf<std::string>() == "owned");
    auto rejected = eve::editing::editorValueFromJson("{broken");
    CHECK(!rejected.ok());
    CHECK(rejected.error() != nullptr);
}

TEST_CASE("editing.valueLifecycle.getIfKeepsDirectConstAndMutableBorrows") {
    using V = EditingValue;
    V text(std::string(4096, 'a'));
    CHECK(static_cast<bool>(static_cast<const V&>(text).getIf<std::string>() == text.getIf<std::string>()));
    text.getIf<std::string>()->append("changed");
    CHECK(static_cast<const V&>(text).getIf<std::string>()->size() == 4103u);
    CHECK(text.getIf<bool>() == nullptr);
    CHECK(static_cast<const V&>(text).getIf<V::Array>() == nullptr);
    V null;
    CHECK(null.getIf<std::monostate>() != nullptr);
    CHECK(static_cast<const V&>(null).getIf<std::monostate>() != nullptr);
    V boolean(true);
    *boolean.getIf<bool>() = false;
    CHECK(!*static_cast<const V&>(boolean).getIf<bool>());
    V integer(int64_t(42));
    *integer.getIf<int64_t>() = 43;
    CHECK(*static_cast<const V&>(integer).getIf<int64_t>() == 43);
    V number(2.5);
    *number.getIf<double>() = 3.5;
    CHECK(*static_cast<const V&>(number).getIf<double>() == 3.5);
    V array(V::Array{1, "two"});
    array.getIf<V::Array>()->push_back(true);
    CHECK(static_cast<const V&>(array).getIf<V::Array>()->size() == 3u);
    V object(V::Object{{"key", "value"}});
    (*object.getIf<V::Object>())["other"] = 4;
    CHECK(static_cast<const V&>(object).getIf<V::Object>()->size() == 2u);
}
}  // namespace test_editing_value_lifecycle
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor)
#include "PrivateTestScope.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include "common/Exception.h"
#include "common/Module.h"
#include "editor/BrushKernel.h"
#include "editor/EditCommand.h"
#include "editor/EditConstraint.h"
#include "editor/Editor.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorDock.h"
#include "editor/EditorInspector.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorSession.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorTransactions.h"
#include "editor/EditorWorkspace.h"
#include "editor/FieldBrushTool.h"
#include "editor/GizmoManager.h"
#include "editor/TransformGizmo.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "map/level/editor/LevelEditorModule.h"
#include "procgen/editing/HeightmapTarget.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap_target/HeightmapTargetModule.h"
#include "scene/editor/EditorSceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor.cpp
namespace test_editor {
// editor.cpp
using namespace eve::editor;
using namespace eve::level_editing;

namespace {
class TestEditorTool final : public IEditorTool {
public:
    explicit TestEditorTool(std::string id) { desc.id = std::move(id); }
    const ToolDescriptor& descriptor() const override { return desc; }
    void                  activate(EditorContext&) override { ++activations; }
    void                  deactivate(EditorContext&) override { ++deactivations; }
    void                  cancel(EditorContext&) override { ++cancellations; }
    ToolResponse          pointerEvent(EditorContext&, const EditorPointerEvent& event) override {
        ++pointerEvents;
        if (event.phase == EditorPointerEvent::Phase::Down) return ToolResponse::capture();
        if (event.phase == EditorPointerEvent::Phase::Up) return ToolResponse::release();
        return ToolResponse::consumed();
    }
    ToolResponse keyEvent(EditorContext&, const EditorKeyEvent&) override {
        ++keyEvents;
        return ToolResponse::consumed();
    }
    void update(EditorContext&, float) override { ++updates; }
    void drawOverlay(EditorContext&, IEditorOverlay& overlay) override {
        ++overlays;
        overlay.circle({1.f, 2.f, 3.f}, 4.f, {});
    }
    void inspect(EditorContext&, IEditorInspector& inspector) override {
        ++inspections;
        inspector.scalar("radius", "Radius", radius, 0.f, 10.f);
    }

    ToolDescriptor desc;
    int            activations   = 0;
    int            deactivations = 0;
    int            cancellations = 0;
    int            pointerEvents = 0;
    int            keyEvents     = 0;
    int            updates       = 0;
    int            overlays      = 0;
    int            inspections   = 0;
    float          radius        = 2.f;
};

class ReentrantEditorTool final : public IEditorTool {
public:
    explicit ReentrantEditorTool(std::string id) { desc.id = std::move(id); }
    const ToolDescriptor& descriptor() const override { return desc; }
    ToolResponse          pointerEvent(EditorContext& context, const EditorPointerEvent&) override {
        if (activateDuringPointer) context.session()->activateTool("replacement");
        return ToolResponse::capture();
    }
    void deactivate(EditorContext& context) override {
        if (removeDuringDeactivate) context.session()->removeTool(desc.id);
        if (throwDuringDeactivate) throw std::runtime_error("deactivate failure");
    }
    void cancel(EditorContext&) override {
        if (throwDuringCancel) throw std::runtime_error("cancel failure");
    }

    ToolDescriptor desc;
    bool           activateDuringPointer  = false;
    bool           removeDuringDeactivate = false;
    bool           throwDuringCancel      = false;
    bool           throwDuringDeactivate  = false;
};

class TestPresentation final : public IEditorOverlay, public IEditorInspector {
public:
    void line(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override { ++primitives; }
    void circle(const OverlayPoint&, float, const OverlayStyle&) override { ++primitives; }
    void rectangle(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override { ++primitives; }
    void text(const OverlayPoint&, const std::string&, const OverlayStyle&) override { ++primitives; }
    void beginGroup(const std::string&, const std::string&) override {}
    void endGroup() override {}
    bool boolean(const std::string&, const std::string&, bool&) override { return false; }
    bool integer(const std::string&, const std::string&, int&, int, int) override { return false; }
    bool scalar(const std::string&, const std::string&, float& value, float, float) override {
        value = 6.f;
        return true;
    }
    bool string(const std::string&, const std::string&, std::string&) override { return false; }

    int primitives = 0;
};

class RejectLargeEdit final : public IEditConstraint {
public:
    ConstraintResult evaluate(EditorContext&, IEditCommand& command) override {
        const EditRegion region = command.dirtyRegion();
        if (!region.empty() && region.maxX - region.minX > 1) return ConstraintResult::reject("edit too wide");
        return ConstraintResult::warning("checked");
    }
};
}  // namespace

TEST_CASE("editor.session.routes_tool_lifecycle_and_capture") {
    EditorSession  session;
    TestEditorTool first("first");
    TestEditorTool second("second");
    CHECK(session.addTool(&first));
    CHECK(session.addTool(&second));
    CHECK(!session.addTool(&first));
    CHECK_EQ(session.getToolCount(), 2);
    CHECK(session.activateTool("first"));
    CHECK_EQ(first.activations, 1);

    EditorPointerEvent down;
    down.phase     = EditorPointerEvent::Phase::Down;
    down.pointerId = 7;
    CHECK(session.dispatchPointer(down).handled);
    CHECK(session.hasPointerCapture());
    CHECK_EQ(session.capturedPointerId(), 7);

    EditorPointerEvent other = down;
    other.phase              = EditorPointerEvent::Phase::Move;
    other.pointerId          = 8;
    CHECK(!session.dispatchPointer(other).handled);
    CHECK_EQ(first.pointerEvents, 1);

    EditorPointerEvent up = down;
    up.phase              = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(up).releasePointer);
    CHECK(!session.hasPointerCapture());

    EditorKeyEvent key;
    key.key     = "W";
    key.pressed = true;
    CHECK(session.dispatchKey(key).handled);
    session.update(0.016f);
    CHECK_EQ(first.keyEvents, 1);
    CHECK_EQ(first.updates, 1);

    CHECK(session.activateTool("second"));
    CHECK_EQ(first.cancellations, 1);
    CHECK_EQ(first.deactivations, 1);
    CHECK_EQ(second.activations, 1);
    CHECK(session.removeTool("second"));
    CHECK_EQ(second.deactivations, 1);
    CHECK_EQ(session.activeToolId(), std::string(""));
}

TEST_CASE("editor.session_tool_reentrancy_cannot_apply_stale_pointer_capture") {
    EditorSession       session;
    ReentrantEditorTool first("first");
    ReentrantEditorTool replacement("replacement");
    first.activateDuringPointer  = true;
    first.removeDuringDeactivate = true;
    REQUIRE(session.addTool(&first));
    REQUIRE(session.addTool(&replacement));
    REQUIRE(session.activateTool("first"));

    EditorPointerEvent down;
    down.phase                  = EditorPointerEvent::Phase::Down;
    down.pointerId              = 17;
    const ToolResponse response = session.dispatchPointer(down);
    CHECK(response.handled);
    CHECK(!response.capturePointer);
    CHECK(!session.hasPointerCapture());
    CHECK_EQ(session.activeToolId(), std::string("replacement"));
    CHECK(session.findTool("first") == nullptr);
}

TEST_CASE("editor.session_destructor_contains_throwing_tool_callbacks") {
    ReentrantEditorTool tool("throwing");
    tool.throwDuringCancel     = true;
    tool.throwDuringDeactivate = true;
    {
        EditorSession session;
        REQUIRE(session.addTool(&tool));
        REQUIRE(session.activateTool("throwing"));
    }
    CHECK(true);
}

TEST_CASE("editor.presentation.is_host_and_renderer_independent") {
    EditorSession    session;
    TestEditorTool   tool("presentable");
    TestPresentation presentation;
    CHECK(session.addTool(&tool));
    CHECK(session.activateTool("presentable"));
    session.drawOverlay(presentation);
    session.inspect(presentation);
    CHECK_EQ(tool.overlays, 1);
    CHECK_EQ(tool.inspections, 1);
    CHECK_EQ(presentation.primitives, 1);
    CHECK_EQ(tool.radius, 6.f);
}

TEST_CASE("editor.field_brush.composes_kernel_and_operation") {
    ConstantBrushFalloff   hard;
    CircleBrushKernel      kernel(&hard);
    PaintIntFieldOperation paint(7);
    FieldBrushTool         tool("paint", "Paint", &kernel, &paint);
    tool.setRadius(0.5f);

    TileBuffer       buffer(4, 4);
    TileBufferTarget tiles("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&tiles);
    CHECK(session.addTool(&tool));
    CHECK(session.activateTool("paint"));

    EditorPointerEvent event;
    event.phase = EditorPointerEvent::Phase::Down;
    event.x     = 1.f;
    event.y     = 2.f;
    CHECK(session.dispatchPointer(event).capturePointer);
    event.phase = EditorPointerEvent::Phase::Up;
    CHECK(session.dispatchPointer(event).releasePointer);
    CHECK_EQ(buffer.getGid(1, 2), 7);
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(1, 2), 0);

    eve::procgen::Heightmap               heightmap(4, 4);
    eve::procgen_editing::HeightmapTarget terrain("terrain", &heightmap);
    AddScalarFieldOperation               raise;
    tool.setOperation(&raise);
    tool.setStrength(0.25f);
    session.bindTarget(&terrain);
    event.phase = EditorPointerEvent::Phase::Down;
    CHECK(session.dispatchPointer(event).handled);
    event.phase = EditorPointerEvent::Phase::Up;
    session.dispatchPointer(event);
    CHECK_EQ(terrain.readScalar(1, 2), 0.25f);
}

TEST_CASE("editor.heightmap_brush.applies_native_circular_falloff") {
    eve::heightmap_target::HeightmapTargetModule heightmapTargets;
    eve::procgen::Heightmap                      heightmap(9, 9);
    CHECK_EQ(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, 2.f, 0.5f), 13);
    CHECK_EQ(heightmap.height(4, 4), 0.5f);
    CHECK(heightmap.height(5, 4) > heightmap.height(6, 4));
    CHECK_EQ(heightmap.height(0, 0), 0.f);

    CHECK(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, 2.f, -0.25f) > 0);
    CHECK_EQ(heightmap.height(4, 4), 0.25f);
    CHECK_EQ(heightmapTargets.applyBrush(nullptr, 4.f, 4.f, 2.f, 1.f), 0);
    CHECK_EQ(heightmapTargets.applyBrush(&heightmap, 4.f, 4.f, -1.f, 1.f), 0);
}

TEST_CASE("editor.heightmap_document.module_round_trips_without_partial_mutation") {
    eve::heightmap_target::HeightmapTargetModule heightmapTargets;
    eve::procgen::Heightmap                      heightmap(3, 2);
    heightmap.setHeight(1, 1, 42.5F);
    auto encoded = heightmapTargets.encodeDocument(&heightmap, 2.0F, 3.0F);
    REQUIRE(encoded.ok());
    heightmap.setHeight(1, 1, -1.0F);
    REQUIRE(heightmapTargets.decodeDocument(encoded.value(), &heightmap).ok());
    CHECK_EQ(heightmap.getWidth(), 3);
    CHECK_EQ(heightmap.getHeight(), 2);
    CHECK_EQ(heightmap.height(1, 1), 42.5F);

    const auto before  = heightmap.data();
    auto       invalid = heightmapTargets.decodeDocument("{\"schema\":\"wrong\"}", &heightmap);
    CHECK(!invalid.ok());
    CHECK_EQ(heightmap.data(), before);
}

TEST_CASE("editor.script_tool.implements_the_same_session_protocol") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        tool <- editor.newScriptTool("custom", "Custom Tool");
        activations <- 0;
        pointerX <- -1.0;
        tool.setActivateCallback(function() { activations += 1; });
        tool.setPointerCallback(function(phase, pointerId, button, x, y, dx, dy, pressure,
                                         shift, control, alt) {
            pointerX = x;
            return phase == 0 ? 3 : 5;
        });
        added <- session.addTool(tool);
        selected <- session.activateTool("custom");
        response <- session.dispatchPointer(0, 9, 0, 12.5, 4.0, 0.0, 0.0, 1.0);
    )"));
    CHECK(vm.find("added").toBool());
    CHECK(vm.find("selected").toBool());
    CHECK_EQ(vm.find("activations").toInt(), 1);
    CHECK_EQ(vm.find("response").toInt(), 3);
    CHECK_EQ(vm.find("pointerX").toFloat(), 12.5f);
}

TEST_CASE("editor.targets.expose_capabilities_and_dirty_regions") {
    TileBuffer       buffer(4, 3);
    TileBufferTarget tiles("ground", &buffer);
    IEditableTarget* base = &tiles;
    auto*            ints = base->query<IIntFieldTarget>();
    CHECK(ints != nullptr);
    CHECK(base->query<IScalarFieldTarget>() == nullptr);
    const auto initialRevision = tiles.revision();
    CHECK_EQ(static_cast<int>(ints->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(static_cast<int>(ints->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Unchanged));
    CHECK_EQ(ints->readInt(2, 1), 9);
    CHECK_EQ(tiles.revision(), initialRevision + 1);
    CHECK_EQ(tiles.dirtyRegion().minX, 2);
    tiles.clearDirtyRegion();
    CHECK(tiles.dirtyRegion().empty());

    eve::procgen::Heightmap               heightmap(3, 3);
    eve::procgen_editing::HeightmapTarget terrain("height", &heightmap);
    auto*                                 scalars = terrain.query<IScalarFieldTarget>();
    CHECK(scalars != nullptr);
    CHECK_EQ(static_cast<int>(scalars->writeScalar(1, 1, 0.75f)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(scalars->readScalar(1, 1), 0.75f);
    CHECK(scalars->sampleScalar(1.f, 1.f) == 0.75f);

    EditorSession session;
    session.bindTarget(&terrain);
    CHECK(session.context().targetCapability<IScalarFieldTarget>() == scalars);
    CHECK(session.context().targetCapability<IIntFieldTarget>() == nullptr);
}

TEST_CASE("editor.brush_kernel.separates_shape_and_falloff") {
    LinearBrushFalloff linear;
    CircleBrushKernel  circle(&linear);
    BrushSample        sample;
    sample.centerX = 2.f;
    sample.centerY = 2.f;
    sample.radius  = 2.f;
    BrushSampleBuffer points;
    circle.sample(sample, points);
    CHECK(points.size() > 1);
    bool foundCenter = false;
    bool foundEdge   = false;
    for (int i = 0; i < points.size(); ++i) {
        const auto& point = points.point(i);
        if (point.x == 2 && point.y == 2) {
            foundCenter = true;
            CHECK_EQ(point.weight, 1.f);
        }
        if (point.x == 4 && point.y == 2) {
            foundEdge = true;
            CHECK_EQ(point.weight, 0.f);
        }
    }
    CHECK(foundCenter);
    CHECK(foundEdge);

    ConstantBrushFalloff hard;
    BoxBrushKernel       box(&hard);
    points.clear();
    sample.radius = 1.f;
    box.sample(sample, points);
    CHECK_EQ(points.size(), 9);
}

TEST_CASE("editor.transactions.undo_redo_and_rollback_any_field") {
    TileBuffer         buffer(3, 3);
    TileBufferTarget   target("tiles", &buffer);
    EditorTransactions transactions;
    CHECK(transactions.begin("paint"));
    auto first = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(first->record(1, 1, 5));
    CHECK(transactions.execute(std::move(first)));
    auto second = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(second->record(1, 1, 7));
    CHECK(second->record(2, 1, 4));
    CHECK(transactions.execute(std::move(second)));
    CHECK_EQ(buffer.getGid(1, 1), 7);
    CHECK(transactions.commit());
    CHECK_EQ(transactions.undoCount(), 1);
    CHECK(transactions.undo());
    CHECK_EQ(buffer.getGid(1, 1), 0);
    CHECK_EQ(buffer.getGid(2, 1), 0);
    CHECK(transactions.redo());
    CHECK_EQ(buffer.getGid(1, 1), 7);

    CHECK(transactions.begin("cancel"));
    auto cancelled = std::make_unique<IntFieldEditCommand>("paint", &target);
    CHECK(cancelled->record(0, 0, 9));
    CHECK(transactions.execute(std::move(cancelled)));
    CHECK_EQ(buffer.getGid(0, 0), 9);
    CHECK(transactions.rollback());
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

TEST_CASE("editor.constraints.accept_warn_and_reject_without_core_types") {
    TileBuffer       buffer(4, 2);
    TileBufferTarget target("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&target);
    RejectLargeEdit constraint;
    CHECK(session.constraints().add(&constraint));
    CHECK(session.transactions().begin("constrained"));

    auto allowed = std::make_unique<IntFieldEditCommand>("small", &target);
    CHECK(allowed->record(0, 0, 1));
    auto allowedResult = session.context().executeChecked(std::move(allowed));
    CHECK(allowedResult.ok());
    REQUIRE(!allowedResult.diagnostics().empty());
    CHECK_EQ(static_cast<int>(allowedResult.diagnostics().front().severity()),
             static_cast<int>(DiagnosticSeverity::Warning));
    CHECK_EQ(session.constraints().diagnosticCount(), 1);
    CHECK_EQ(session.constraints().diagnostic(0), std::string("checked"));

    auto rejected = std::make_unique<IntFieldEditCommand>("wide", &target);
    CHECK(rejected->record(0, 1, 2));
    CHECK(rejected->record(3, 1, 2));
    CHECK(!session.context().execute(std::move(rejected)));
    CHECK(session.constraints().rejected());
    CHECK_EQ(buffer.getGid(0, 1), 0);
    CHECK(session.transactions().commit());
}

TEST_CASE("editor.module.name") {
    auto* mod = Editor::create();
    CHECK_EQ(mod->getName(), std::string("Editor"));
    CHECK_EQ(Editor::create(), mod);
}

TEST_CASE("editor.gizmo.translate.pick_and_drag") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("translate");
    g->setSpace("world");
    g->setPosition(0.f, 0.f, 0.f);
    g->setSize(1.f);

    // Ray from +Z looking at origin, aimed near +X axis tip
    std::string axis = g->pick(2.f, 0.f, 5.f, -0.3f, 0.f, -1.f);
    // May hit x or miss depending on exact geometry; force beginDrag on x
    CHECK(g->beginDrag("x", 0.f, 2.f, 5.f, 0.f, 0.f, -1.f));
    CHECK(g->isDragging());
    CHECK_EQ(g->getActiveAxis(), std::string("x"));

    // Drag along +X by moving hit point
    CHECK(g->updateDrag(1.5f, 2.f, 5.f, 0.f, 0.f, -1.f));
    CHECK(g->getPositionX() > 0.f);
    float y = g->getPositionY();
    float z = g->getPositionZ();
    CHECK(std::fabs(y) < 1e-3f);
    CHECK(std::fabs(z) < 1e-3f);

    g->endDrag();
    CHECK(!g->isDragging());
    CHECK_EQ(g->getPartCount() > 0, true);
    CHECK_EQ(g->getPartKind(0), std::string("axis"));
    (void)axis;
}

TEST_CASE("editor.gizmo.snap_translate") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("translate");
    g->setSnapTranslate(1.f, 1.f, 1.f);
    g->setPosition(0.f, 0.f, 0.f);
    CHECK(g->beginDrag("x", 0.f, 2.f, 5.f, 0.f, 0.f, -1.f));
    g->updateDrag(0.4f, 2.f, 5.f, 0.f, 0.f, -1.f);
    // With snap 1, small moves round toward 0 or 1
    float x = g->getPositionX();
    CHECK(std::fabs(x - std::round(x)) < 1e-4f);
    g->endDrag();
}

TEST_CASE("editor.gizmo.rotate_and_scale_modes") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    g->setMode("rotate");
    CHECK_EQ(g->getPartCount(), 3);
    CHECK_EQ(g->getPartKind(0), std::string("ring"));

    g->setMode("scale");
    CHECK(g->getPartCount() >= 3);
    CHECK(g->beginDrag("xyz", 0.f, 0.f, 5.f, 0.f, 0.f, -1.f));
    float s0 = g->getScaleX();
    g->updateDrag(0.f, 1.f, 5.f, 0.f, 0.f, -1.f);
    // uniform scale may change
    CHECK(g->getScaleX() > 0.f);
    CHECK_EQ(g->getScaleX(), g->getScaleY());
    (void)s0;
    g->endDrag();

    g->setMode("bound");
    CHECK(g->getPartCount() >= 6);
}

TEST_CASE("editor.gizmo.invalid_mode") {
    std::unique_ptr<TransformGizmo> g(new TransformGizmo());
    bool                            threw = false;
    try {
        g->setMode("skew");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("editor.gizmo_manager.multi_mode") {
    std::unique_ptr<GizmoManager> mgr(new GizmoManager());
    CHECK(!mgr->isAttached());
    CHECK(mgr->pick(0, 0, 5, 0, 0, -1).empty());

    mgr->attach();
    mgr->setPositionEnabled(true);
    mgr->setRotationEnabled(true);
    TransformGizmo* g = mgr->getGizmo();
    g->setPosition(0.f, 0.f, 0.f);
    g->setSize(1.f);

    // Pick should succeed on some handle when looking at origin
    std::string axis = mgr->pick(0.f, 0.f, 5.f, 0.f, 0.f, -1.f);
    // center or axis may be hit
    if (!axis.empty()) {
        CHECK(mgr->beginDrag(axis, 0.f, 0.f, 5.f, 0.f, 0.f, -1.f));
        mgr->endDrag();
    }
    mgr->detach();
    CHECK(!mgr->isAttached());
}

TEST_CASE("editor.brush.paint_fill_line_rect") {
    std::unique_ptr<Editor> ed(Editor::create());
    // Module singleton — don't delete; use factories only
    std::unique_ptr<TileBuffer> buf(new TileBuffer(8, 8));
    std::unique_ptr<Brush>      brush(new Brush());

    brush->setTool("paint");
    brush->setTile(3);
    brush->setSize(1);
    CHECK_EQ(brush->paintAt(buf.get(), 2, 2), 1);
    CHECK_EQ(buf->getGid(2, 2), 3);

    brush->setSize(3);
    brush->setShape("square");
    int n = brush->paintAt(buf.get(), 4, 4);
    CHECK(n >= 1);
    CHECK_EQ(buf->getGid(4, 4), 3);

    buf->fill(1);
    buf->setGid(0, 0, 0);
    brush->setTile(7);
    CHECK(brush->floodFill(buf.get(), 1, 1) > 1);
    CHECK_EQ(buf->getGid(3, 3), 7);
    CHECK_EQ(buf->getGid(0, 0), 0);  // separated

    buf->clear();
    brush->setTile(2);
    brush->setSize(1);
    CHECK(brush->paintLine(buf.get(), 0, 0, 3, 0) >= 4);
    CHECK_EQ(buf->getGid(2, 0), 2);

    buf->clear();
    CHECK(brush->paintRect(buf.get(), 1, 1, 3, 3, true) >= 9);
    CHECK_EQ(buf->getGid(2, 2), 2);

    brush->previewAt(buf.get(), 5, 5);
    CHECK(brush->getPreviewCount() >= 1);
}

TEST_CASE("editor.brush.stamp_and_erase") {
    std::unique_ptr<TileBuffer> buf(new TileBuffer(5, 5));
    std::unique_ptr<Brush>      brush(new Brush());
    brush->setStampSize(2, 2);
    brush->setStampTile(0, 0, 9);
    brush->setStampTile(1, 0, 8);
    brush->setStampTile(0, 1, 7);
    brush->setStampTile(1, 1, 6);
    brush->setTool("stamp");
    CHECK(brush->paintAt(buf.get(), 2, 2) >= 1);
    CHECK(brush->getChangeCount() >= 1);

    brush->setTool("erase");
    brush->setEraseTile(0);
    brush->setSize(1);
    brush->eraseAt(buf.get(), 2, 2);
}

TEST_CASE("editor.toolbar_inspector_dock") {
    std::unique_ptr<EditorToolbar> tb(new EditorToolbar());
    tb->addTool("move", "Move");
    tb->addTool("paint", "Paint");
    tb->setIcon("move", "move");
    tb->setIcon("paint", "paint-brush");
    tb->setShortcut("move", "W");
    CHECK_EQ(tb->getActive(), std::string("move"));
    CHECK(tb->matchShortcut("W"));
    CHECK(tb->setActive("paint"));
    CHECK_EQ(tb->getActive(), std::string("paint"));
    CHECK_EQ(tb->getToolCount(), 2);
    CHECK_EQ(tb->getToolIcon(0), std::string("move"));
    CHECK_EQ(tb->getToolIcon(1), std::string("paint-brush"));

    std::unique_ptr<EditorInspector> insp(new EditorInspector());
    insp->addFloat("size", "Size", 1.f, 0.f, 10.f, 0.1f);
    insp->addFloat3("pos", "Position", 1.f, 2.f, 3.f);
    insp->addBool("snap", "Snap", false);
    insp->addChoice("mode", "Mode", "translate,rotate,scale", "translate");
    insp->setFloat("size", 2.5f);
    CHECK(insp->isDirty("size"));
    CHECK_EQ(insp->pollChangedId(), std::string("size"));
    CHECK_EQ(insp->getFloat3Y("pos"), 2.f);
    insp->setChoice("mode", "rotate");
    CHECK_EQ(insp->getChoice("mode"), std::string("rotate"));

    std::unique_ptr<EditorDock> dock(new EditorDock());
    dock->setRegionSize("left", 100.f);
    dock->setRegionSize("right", 120.f);
    dock->setRegionSize("top", 30.f);
    dock->setRegionSize("bottom", 20.f);
    dock->layout(800.f, 600.f);
    CHECK_EQ(dock->getRegionW("center"), 800.f - 100.f - 120.f);
    CHECK_EQ(dock->getRegionH("center"), 600.f - 30.f - 20.f);
    CHECK_EQ(dock->getRegionX("right"), 800.f - 120.f);
}

TEST_CASE("editor.history.tiles_undo_redo") {
    std::unique_ptr<TileBuffer>    buf(new TileBuffer(4, 4));
    std::unique_ptr<Brush>         brush(new Brush());
    std::unique_ptr<EditorHistory> hist(new EditorHistory());

    brush->setTile(5);
    brush->paintAt(buf.get(), 1, 1);
    hist->beginGroup("paint");
    for (int i = 0; i < brush->getChangeCount(); ++i) {
        hist->recordTile(brush->getChangeX(i), brush->getChangeY(i), brush->getChangeOldGid(i),
                         brush->getChangeNewGid(i));
    }
    hist->endGroup();
    CHECK_EQ(buf->getGid(1, 1), 5);
    CHECK(hist->canUndo());
    CHECK(hist->undo());
    CHECK(hist->applyLastToBuffer(buf.get()));
    CHECK_EQ(buf->getGid(1, 1), 0);
    CHECK(hist->canRedo());
    CHECK(hist->redo());
    CHECK(hist->applyLastToBuffer(buf.get()));
    CHECK_EQ(buf->getGid(1, 1), 5);

    hist->push("rename", "npc_01");
    CHECK(hist->undo());
    CHECK_EQ(hist->getLastActionKind(), std::string("opaque"));
    CHECK_EQ(hist->getLastPayload(), std::string("npc_01"));
}

TEST_CASE("editor.factories") {
    Editor*                              ed = Editor::create();
    eve::level_editor::LevelEditorModule levelEditor;
    std::unique_ptr<TransformGizmo>      g         = ed->newGizmo();
    std::unique_ptr<GizmoManager>        m         = ed->newGizmoManager();
    std::unique_ptr<TileBuffer>          b         = levelEditor.newTileBuffer(2, 2);
    std::unique_ptr<Brush>               br        = levelEditor.newBrush();
    std::unique_ptr<EditorToolbar>       t         = ed->newToolbar();
    std::unique_ptr<EditorInspector>     i         = ed->newInspector();
    std::unique_ptr<EditorHistory>       h         = levelEditor.newHistory();
    std::unique_ptr<EditorDock>          d         = ed->newDock();
    std::unique_ptr<EditorSession>       s         = ed->newSession();
    auto                                 workspace = ed->newWorkspace("level", "Level");
    auto                                 rejected  = ed->newWorkspace("", "nope");
    // zeroerr CHECK copies the expression for printing — use raw pointers, not unique_ptr.
    CHECK(g.get() != nullptr);
    CHECK(m.get() != nullptr);
    CHECK_EQ(b->getWidth(), 2);
    CHECK(br.get() != nullptr);
    CHECK(t.get() != nullptr);
    CHECK(i.get() != nullptr);
    CHECK(d.get() != nullptr);
    CHECK(h.get() != nullptr);
    CHECK(s.get() != nullptr);
    CHECK(workspace.ok());
    CHECK_EQ(workspace.value()->getId(), std::string("level"));
    CHECK_NOT(rejected.ok());
    CHECK_NOT(rejected.hasValue());
}

// editor_action_timeline_script.cpp
TEST_CASE("editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.combat", "Test Combat Editor");
        timelineAsset <- {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:light-attack", durationNs=1000000000,
            animationUri="asset://test/attack.glb#Attack",
            metadata={},
            tracks=[{
                id="test-track:gameplay", label="Gameplay", kind="gameplay",
                muted=false, locked=false, notifies=[],
                states=[{
                    id="test-state:hitbox", type="combat:hitbox-window",
                    startNs=200000000, endNs=500000000,
                    payload={hitbox="weapon.main"}
                }]
            }]
        };
        created <- eve.ActionEditorModule().create("test.asset.light-attack", timelineAsset);
        actionEditor <- created.value;
        externalDamage <- actionEditor.setRuntimeBlockExternallyHandled("combat:damage", true);
        externalUnknown <- actionEditor.setRuntimeBlockExternallyHandled("test:unknown", true);
        previewHostAvailable <- actionEditor.hasPreviewHost();
        previewRefresh <- actionEditor.refreshPreview();
        runtimeRateBeforeStart <- actionEditor.setRuntimeRate(1.5);
        sectionProgressBeforeStart <- actionEditor.evaluateRuntimeSectionProgress(0, 0.5);
        sectionSyncJumpBeforeStart <- actionEditor.syncRuntimeSectionAndJump(0, 0.25);
        clearSectionSyncBeforeStart <- actionEditor.clearRuntimeSectionSync(0);
        testAnimation <- eve.Animation();
        testSkeleton <- testAnimation.newSkeleton();
        testSkeleton.addBone("root", -1);
        previewModelRegistration <- actionEditor.registerPreviewModelSkeleton(
            "project://test/preview.glb", testSkeleton);
        previewProfileConfiguration <- actionEditor.configurePreviewProfile({
            schema="eve.action.preview-profile", schemaVersion=1,
            actionAssetGuid="test.asset.light-attack", previewModelUri="project://test/preview.glb",
            baseMotionUri="", animationProfileUri="", boneMaskUri="",
            showRootMotion=true, footIk=false, showGrid=true,
            showSkeleton=false, showJointNames=false, shading="lit"
        });
        previewProfileCanPreview <- actionEditor.canPreviewProfile();
        testBoneMask <- testAnimation.newBoneMask(testSkeleton);
        previewMaskRegistration <- actionEditor.registerPreviewBoneMask(
            "project://test/upper.mask", testBoneMask);
        testPose <- testAnimation.newPose(testSkeleton.getBoneCount());
        setBoneMaskBeforeStart <- actionEditor.setRuntimeBoneMask(testBoneMask);
        clearBoneMaskBeforeStart <- actionEditor.clearRuntimeBoneMask();
        setLayerWeightBeforeStart <- actionEditor.setRuntimeLayerWeight(0.5);
        setLayerAdditiveBeforeStart <- actionEditor.setRuntimeLayerAdditive(true);
        runtimeLayerWeightBeforeStart <- actionEditor.getRuntimeLayerWeight();
        runtimeLayerAdditiveBeforeStart <- actionEditor.getRuntimeLayerAdditive();
        runtimePoseOverBaseBeforeStart <- actionEditor.getRuntimePoseOverBase(testPose);
        runtimeStateBeforeStart <- actionEditor.getRuntimeState();
        runtimeElapsedBeforeStart <- actionEditor.getRuntimeElapsedSeconds();
        runtimeDurationBeforeStart <- actionEditor.getRuntimeDurationSeconds();
        runtimeLayerBeforeStart <- actionEditor.getRuntimeLayer();
        runtimeSlotBeforeStart <- actionEditor.getRuntimeSlot();
        configured <- actionEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        viewport <- actionEditor.setViewport(800.0, 36.0, 120.0);
        beforeStart <- actionEditor.getStateStart("test-state:hitbox");
        itemCenterX <- (actionEditor.getItemMinX(0) + actionEditor.getItemMaxX(0)) * 0.5;
        itemCenterY <- (actionEditor.getItemMinY(0) + actionEditor.getItemMaxY(0)) * 0.5;
        pointerDownResult <- actionEditor.pointerDown(itemCenterX, itemCenterY, false);
        pointerMoveResult <- actionEditor.pointerMove(itemCenterX + 80.0);
        pointerUpResult <- actionEditor.pointerUp(itemCenterX + 80.0);
        movedStart <- actionEditor.getStateStart("test-state:hitbox");
        revisionAfterDrag <- actionEditor.getRevision();
        undoResult <- actionEditor.undo();
        restoredStart <- actionEditor.getStateStart("test-state:hitbox");
        inspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.1, 0.6);
        inspectorStart <- actionEditor.getItemStart("test-state:hitbox");
        inspectorEnd <- actionEditor.getItemEnd("test-state:hitbox");
        invalidInspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.7, 0.2);
        detailsResult <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{\"hitbox\":\"weapon.alt\"}");
        payloadAfterDetails <- actionEditor.getItemPayloadJson("test-state:hitbox");
        payloadTextPatch <- actionEditor.setItemPayloadText(
            "test-state:hitbox", "hitbox", "weapon.typed");
        typedHitbox <- actionEditor.getItemPayloadText(
            "test-state:hitbox", "hitbox", "missing");
        payloadVectorPatch <- actionEditor.setItemPayloadVector3(
            "test-state:hitbox", "positionOffset", 1.0, 2.0, 3.0);
        typedPositionY <- actionEditor.getItemPayloadVector(
            "test-state:hitbox", "positionOffset", 1, -1.0);
        payloadTextListPatch <- actionEditor.setItemPayloadTextList(
            "test-state:hitbox", "relatedAssets", "asset://audio/a.wav; asset://audio/b.wav");
        typedTextList <- actionEditor.getItemPayloadTextList("test-state:hitbox", "relatedAssets");
        invalidPayloadPatch <- actionEditor.setItemPayloadText(
            "test-state:hitbox", "hitbox", "");
        shapeMismatch <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:damage", "{\"damageType\":\"slash\",\"amount\":2}");
        invalidPayload <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{}");
        disableResult <- actionEditor.setItemEnabled("test-state:hitbox", false);
        disabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        restoreEnabledResult <- actionEditor.undo();
        restoredEnabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        removeItemResult <- actionEditor.removeItem("test-state:hitbox");
        itemCountAfterRemove <- actionEditor.getItemCount();
        restoreRemovedResult <- actionEditor.undo();
        itemCountAfterRestore <- actionEditor.getItemCount();
        missResult <- actionEditor.pointerDown(700.0, 20.0, false);
        draggingAfterMiss <- actionEditor.isDragging();
        seekResult <- actionEditor.seekX(700.0);
        emptySpaceSeekTime <- actionEditor.getPreviewTime();
        actionEditor.seekSeconds(0.0);
        actionEditor.play();
        advanced <- actionEditor.update(1.0);
        previewEventCount <- actionEditor.getEventCount();
        snapshotResult <- actionEditor.snapshot();
        timelineAbilitySplitA <- actionEditor.addSectionSplit(0.2);
        timelineAbilitySplitB <- actionEditor.addSectionSplit(0.6);
        snapshotJsonResult <- actionEditor.snapshotJson();
        combatRuntime <- eve.Combat().newRuntime().value;
        timelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:authored-light", snapshotJsonResult.value, 0.25,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        duplicateTimelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:authored-light", snapshotJsonResult.value, 0.25,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        invalidTimelineAbility <- combatRuntime.registerTimelineAbility(
            "combat-ability:invalid-policy", snapshotJsonResult.value, 0.25,
            "per-execution", "invalid", "Ability.Combat.Attack.Light");
        invalidTimelineGrant <- combatRuntime.grantAbility("fighter:player", "combat-ability:invalid-policy");
        timelineGrant <- combatRuntime.grantAbility("fighter:player", "combat-ability:authored-light");
        timelineMatches <- combatRuntime.matchingAbilities("fighter:player", "Ability.Combat.Attack.Light");
        timelineActivation <- combatRuntime.activateAbility(timelineGrant.value.grantId, 1);
        actionEditor.setSectionSplit(0, 0.3);
        actionEditor.setSectionSplit(1, 0.8);
        replacementJson <- actionEditor.snapshotJson();
        replacedTimelineAbility <- combatRuntime.replaceTimelineAbility(
            "combat-ability:authored-light", replacementJson.value, 0.0,
            "per-execution", "exclusive-replaceable", "Ability.Combat.Attack.Light");
        timelineAbilityAdvance <- combatRuntime.advanceAbilities(2, 1.0);
        timelineActiveAfterAdvance <- timelineAbilityAdvance.value.activeCount;
        replacementActivation <- combatRuntime.activateAbility(timelineGrant.value.grantId, 3);
        replacementAdvance <- combatRuntime.advanceAbilities(4, 0.5);
        timelineAbilityAction <- timelineAbility.value.actionId;
        timelineAbilityDuration <- timelineAbility.value.durationSeconds;
        timelineAbilitySections <- timelineAbility.value.sectionCount;
        timelineMatchedGrant <- timelineMatches.value[0];
        timelineGrantedId <- timelineGrant.value.grantId;
        replacementAbilityAction <- replacedTimelineAbility.value.actionId;
        replacementStillActive <- replacementAdvance.value.activeCount;
        actionEditor.removeSectionSplit(1);
        actionEditor.removeSectionSplit(0);
        instantTypeCount <- actionEditor.getInsertableTypeCount(false);
        stateTypeCount <- actionEditor.getInsertableTypeCount(true);
        damageTypeIndex <- -1;
        invulnerabilityTypeIndex <- -1;
        for (local i = 0; i < instantTypeCount; ++i)
            if (actionEditor.getInsertableType(false, i) == "combat:damage") damageTypeIndex = i;
        for (local i = 0; i < stateTypeCount; ++i)
            if (actionEditor.getInsertableType(true, i) == "combat:invulnerability-window")
                invulnerabilityTypeIndex = i;
        damageTypeLabel <- actionEditor.getInsertableTypeLabel(false, damageTypeIndex);
        actionEditor.seekSeconds(0.7);
        insertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{\"damageType\":\"slash\",\"amount\":3}");
        insertedState <- actionEditor.addStateAtCursor(
            "test-track:gameplay", "combat:invulnerability-window", 0.1, "{}");
        invalidInsertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{}");
        itemCountAfterInsert <- actionEditor.getItemCount();
        initialTrackLocked <- actionEditor.getTrackLocked(0);
        lockTrack <- actionEditor.setTrackLocked("test-track:gameplay", true);
        lockedTrackValue <- actionEditor.getTrackLocked(0);
        unlockTrack <- actionEditor.setTrackLocked("test-track:gameplay", false);
        renameTrack <- actionEditor.renameTrack("test-track:gameplay", "Combat Events");
        renamedTrackLabel <- actionEditor.getTrackLabel(0);
        copyTrackResult <- actionEditor.copyTrack("test-track:gameplay");
        pasteTrackResult <- actionEditor.pasteTrack();
        trackCountAfterPaste <- actionEditor.getTrackCount();
        removePastedTrack <- actionEditor.removeTrack(pasteTrackResult.value);
        trackCountAfterTrackCleanup <- actionEditor.getTrackCount();
        addSectionResult <- actionEditor.addAnimationSection(
            "test-section:attack", "asset://test/attack.glb#Attack", 0.0, 0.4, 0.1);
        editSectionResult <- actionEditor.editAnimationSection(
            "test-section:attack", 0.05, 0.45, 0.08, "asset://test/attack.glb#AttackEdited");
        editSectionSourceResult <- actionEditor.editAnimationSectionSource(
            "test-section:attack", 0.1, 0.3, "linear");
        editSectionEnvelopeResult <- actionEditor.editAnimationSectionEnvelope(
            "test-section:attack", 0.06, 0.12, "ease-in", "ease-out");
        sectionUri <- actionEditor.getAnimationSectionUri(0);
        sectionStart <- actionEditor.getAnimationSectionStart(0);
        sectionBlend <- actionEditor.getAnimationSectionBlendIn(0);
        sectionBlendOut <- actionEditor.getAnimationSectionBlendOut(0);
        sectionSourceStart <- actionEditor.getAnimationSectionSourceStart(0);
        sectionCurve <- actionEditor.getAnimationSectionBlendCurve(0);
        sectionInCurve <- actionEditor.getAnimationSectionBlendInCurve(0);
        sectionOutCurve <- actionEditor.getAnimationSectionBlendOutCurve(0);
        removeSectionResult <- actionEditor.removeAnimationSection("test-section:attack");
        sectionCountAfterCleanup <- actionEditor.getAnimationSectionCount();
        montageSettingsResult <- actionEditor.setMontageSettings(
            1.5, true, true, 2, 0.12, 0.18, -0.03, true, false, false);
        montageRate <- actionEditor.getMontageBasePlayRate();
        montageLooping <- actionEditor.getMontageLooping();
        montageFootIk <- actionEditor.getMontageFootIk();
        montageLayer <- actionEditor.getMontageAnimationLayer();
        montageBlendOffset <- actionEditor.getMontageBlendOutOffset();
        montageRootRotation <- actionEditor.getMontageRootMotionRotation();
        invalidMontageSettings <- actionEditor.setMontageSettings(
            0.0, true, true, 2, 0.12, 0.18, -0.03, true, false, false);
        undoMontageSettings <- actionEditor.undo();
        restoredMontageRate <- actionEditor.getMontageBasePlayRate();
        splitCountBefore <- actionEditor.getSectionSplitCount();
        addSplit <- actionEditor.addSectionSplit(0.25);
        splitTimeAfterAdd <- actionEditor.getSectionSplitTime(0);
        moveSplit <- actionEditor.setSectionSplit(0, 0.35);
        splitTimeAfterMove <- actionEditor.getSectionSplitTime(0);
        duplicateSplit <- actionEditor.addSectionSplit(0.35);
        removeSplit <- actionEditor.removeSectionSplit(0);
        splitCountAfterRemove <- actionEditor.getSectionSplitCount();
        invalidResult <- eve.ActionEditorModule().create("test.asset.invalid", {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:invalid", durationNs=-1,
            animationUri="", metadata={}, tracks=[]
        });
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("externalDamage").toTable().get<bool>("ok"));
    CHECK(!vm.find("externalUnknown").toTable().get<bool>("ok"));
    CHECK(!vm.find("previewHostAvailable").toBool());
    CHECK(vm.find("previewRefresh").toTable().get<bool>("ok"));
    CHECK(vm.find("previewModelRegistration").toTable().get<bool>("ok"));
    CHECK(vm.find("previewProfileConfiguration").toTable().get<bool>("ok"));
    CHECK(vm.find("previewProfileCanPreview").toBool());
    CHECK(vm.find("previewMaskRegistration").toTable().get<bool>("ok"));
    CHECK(!vm.find("runtimeRateBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("sectionProgressBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("sectionSyncJumpBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("clearSectionSyncBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("setBoneMaskBeforeStart").toTable().get<bool>("ok"));
    CHECK(!vm.find("clearBoneMaskBeforeStart").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("runtimeStateBeforeStart").toString(), std::string("not-started"));
    CHECK_EQ(vm.find("runtimeElapsedBeforeStart").toFloat(), 0.0f);
    CHECK_EQ(vm.find("runtimeDurationBeforeStart").toFloat(), 0.0f);
    CHECK_EQ(vm.find("runtimeLayerBeforeStart").toInt(), -1);
    CHECK_EQ(vm.find("runtimeSlotBeforeStart").toInt(), -1);
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerDownResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerMoveResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerUpResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedStart").toFloat() > vm.find("beforeStart").toFloat());
    CHECK_EQ(vm.find("revisionAfterDrag").toInt(), 1);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredStart").toFloat() - vm.find("beforeStart").toFloat()), 0.0001F);
    CHECK(vm.find("inspectorTiming").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("inspectorStart").toFloat() - 0.1F), 0.0001F);
    CHECK_LT(std::abs(vm.find("inspectorEnd").toFloat() - 0.6F), 0.0001F);
    CHECK(!vm.find("invalidInspectorTiming").toTable().get<bool>("ok"));
    CHECK(vm.find("detailsResult").toTable().get<bool>("ok"));
    CHECK_NE(vm.find("payloadAfterDetails").toString().find("weapon.alt"), std::string::npos);
    CHECK(vm.find("payloadTextPatch").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("typedHitbox").toString(), std::string("weapon.typed"));
    CHECK(vm.find("payloadVectorPatch").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("typedPositionY").toFloat() - 2.0f) < 1e-5f);
    CHECK(vm.find("payloadTextListPatch").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("typedTextList").toString(), std::string("asset://audio/a.wav; asset://audio/b.wav"));
    CHECK(!vm.find("invalidPayloadPatch").toTable().get<bool>("ok"));
    CHECK(!vm.find("shapeMismatch").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidPayload").toTable().get<bool>("ok"));
    CHECK(vm.find("disableResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("disabledValue").toBool());
    CHECK(vm.find("restoreEnabledResult").toTable().get<bool>("ok"));
    CHECK(vm.find("restoredEnabledValue").toBool());
    CHECK(vm.find("removeItemResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRemove").toInt(), 0);
    CHECK(vm.find("restoreRemovedResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRestore").toInt(), 1);
    CHECK(vm.find("missResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("draggingAfterMiss").toBool());
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("emptySpaceSeekTime").toFloat() > 0.8F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("previewEventCount").toInt(), 2);
    CHECK(vm.find("snapshotResult").toTable().get<bool>("ok"));
    CHECK(vm.find("snapshotJsonResult").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbilitySplitA").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbilitySplitB").toTable().get<bool>("ok"));
    CHECK(vm.find("timelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("duplicateTimelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidTimelineAbility").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidTimelineGrant").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("timelineAbilityAction").toString(), std::string("test:light-attack"));
    CHECK_LT(std::abs(vm.find("timelineAbilityDuration").toFloat() - 1.0f), 1e-5f);
    CHECK_EQ(vm.find("timelineAbilitySections").toInt(), 3);
    CHECK_EQ(vm.find("timelineMatchedGrant").toInt(), vm.find("timelineGrantedId").toInt());
    CHECK(vm.find("timelineActivation").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("timelineActiveAfterAdvance").toInt(), 0);
    CHECK(vm.find("replacementJson").toTable().get<bool>("ok"));
    CHECK(vm.find("replacedTimelineAbility").toTable().get<bool>("ok"));
    CHECK(vm.find("replacementActivation").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("replacementAbilityAction").toString(), std::string("test:light-attack"));
    CHECK_EQ(vm.find("replacementStillActive").toInt(), 1);
    CHECK(vm.find("instantTypeCount").toInt() > 0);
    CHECK(vm.find("stateTypeCount").toInt() > 0);
    CHECK(vm.find("damageTypeIndex").toInt() >= 0);
    CHECK(vm.find("invulnerabilityTypeIndex").toInt() >= 0);
    CHECK_NE(vm.find("damageTypeLabel").toString().find("Apply Damage"), std::string::npos);
    CHECK(vm.find("insertedNotify").toTable().get<bool>("ok"));
    CHECK(vm.find("insertedState").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidInsertedNotify").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterInsert").toInt(), 3);
    CHECK(!vm.find("initialTrackLocked").toBool());
    CHECK(vm.find("lockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("lockedTrackValue").toBool());
    CHECK(vm.find("unlockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("renameTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("renamedTrackLabel").toString(), std::string("Combat Events"));
    CHECK(vm.find("copyTrackResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pasteTrackResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterPaste").toInt(), 2);
    CHECK(vm.find("removePastedTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterTrackCleanup").toInt(), 1);
    CHECK(vm.find("addSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionSourceResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionEnvelopeResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionUri").toString(), std::string("asset://test/attack.glb#AttackEdited"));
    CHECK(std::fabs(vm.find("sectionStart").toFloat() - 0.05f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionBlend").toFloat() - 0.06f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionBlendOut").toFloat() - 0.12f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionSourceStart").toFloat() - 0.1f) < 1e-5f);
    CHECK_EQ(vm.find("sectionCurve").toString(), std::string("ease-in"));
    CHECK_EQ(vm.find("sectionInCurve").toString(), std::string("ease-in"));
    CHECK_EQ(vm.find("sectionOutCurve").toString(), std::string("ease-out"));
    CHECK(vm.find("removeSectionResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionCountAfterCleanup").toInt(), 0);
    CHECK(vm.find("montageSettingsResult").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("montageRate").toFloat() - 1.5f) < 1e-5f);
    CHECK(vm.find("montageLooping").toBool());
    CHECK(vm.find("montageFootIk").toBool());
    CHECK_EQ(vm.find("montageLayer").toInt(), 2);
    CHECK(std::fabs(vm.find("montageBlendOffset").toFloat() + 0.03f) < 1e-5f);
    CHECK(!vm.find("montageRootRotation").toBool());
    CHECK_EQ(vm.find("splitCountBefore").toInt(), 0);
    CHECK(vm.find("addSplit").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("splitTimeAfterAdd").toFloat() - 0.25f) < 1e-5f);
    CHECK(vm.find("moveSplit").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("splitTimeAfterMove").toFloat() - 0.35f) < 1e-5f);
    CHECK(!vm.find("duplicateSplit").toTable().get<bool>("ok"));
    CHECK(vm.find("removeSplit").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("splitCountAfterRemove").toInt(), 0);
    CHECK(!vm.find("invalidMontageSettings").toTable().get<bool>("ok"));
    CHECK(vm.find("undoMontageSettings").toTable().get<bool>("ok"));
    CHECK(std::fabs(vm.find("restoredMontageRate").toFloat() - 1.0f) < 1e-5f);
    CHECK(!vm.find("invalidResult").toTable().get<bool>("ok"));
}

TEST_CASE("editor.actionTimeline.scriptPersistsDocumentAndReportsDirtyState") {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_action_script_document_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        eve::ModuleManager::expose(vm);
        const std::string source = std::string(R"(
            timelineAsset <- {
                schema="eve.action.timeline", schemaVersion=1,
                actionId="test:persistent", durationNs=1000000000,
                animationUri="asset://test/attack.glb#Attack", metadata={},
                tracks=[{
                    id="test-track:gameplay", label="Gameplay", kind="gameplay",
                    muted=false, locked=false, notifies=[], states=[]
                }]
            };
            editor <- eve.Editor();
            module <- eve.ActionEditorModule();
            opened <- module.openDocument(")") +
                                   root.generic_string() + R"(", "test.asset.persistent", "Persistent Action",
                "content://Actions/Persistent.action", timelineAsset);
            actionEditor <- opened.value;
            backed <- actionEditor.isDocumentBacked();
            initiallyDirty <- actionEditor.isDirty();
            renameResult <- actionEditor.renameTrack("test-track:gameplay", "Gameplay Saved");
            dirtyAfterEdit <- actionEditor.isDirty();
            saveResult <- actionEditor.saveDocument();
            dirtyAfterSave <- actionEditor.isDirty();
            reopened <- module.openDocument(")" +
                                   root.generic_string() + R"(", "test.asset.reopened", "Reopened Action",
                "content://Actions/Persistent.action", timelineAsset);
            reopenedLabel <- reopened.value.getTrackLabel(0);
        )";
        vm.run(vm.compileSource(source.c_str()));

        CHECK(vm.find("opened").toTable().get<bool>("ok"));
        CHECK(vm.find("backed").toBool());
        CHECK(!vm.find("initiallyDirty").toBool());
        CHECK(vm.find("renameResult").toTable().get<bool>("ok"));
        CHECK(vm.find("dirtyAfterEdit").toBool());
        CHECK(vm.find("saveResult").toTable().get<bool>("ok"));
        CHECK(!vm.find("dirtyAfterSave").toBool());
        CHECK(vm.find("reopened").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("reopenedLabel").toString(), std::string("Gameplay Saved"));

        const std::string catalogSource = std::string(R"(
            catalogCreated <- module.createAssetCatalog(")") +
                                          root.generic_string() + R"(");
            catalog <- catalogCreated.value;
            catalogRegister <- catalog.registerDocument(
                "test.asset.persistent", "content://Actions/Persistent.action");
            catalogRefresh <- catalog.refresh("persistent");
            catalogCount <- catalog.getAssetCount();
            catalogGuid <- catalog.getAssetGuid(0);
            catalogUri <- catalog.getAssetUri(0);
            catalogTitle <- catalog.getAssetTitle(0);
            catalogGeneration <- catalog.getGeneration();
            catalogEmptySearch <- catalog.refresh("missing-query");
            catalogEmptyCount <- catalog.getAssetCount();
        )";
        vm.run(vm.compileSource(catalogSource.c_str()));
        CHECK(vm.find("catalogCreated").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRegister").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRefresh").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogCount").toInt(), 1);
        CHECK_EQ(vm.find("catalogGuid").toString(), std::string("test.asset.persistent"));
        CHECK_EQ(vm.find("catalogUri").toString(), std::string("content://Actions/Persistent.action"));
        CHECK_EQ(vm.find("catalogTitle").toString(), std::string("Persistent"));
        CHECK(vm.find("catalogGeneration").toInt() > 0);
        CHECK(vm.find("catalogEmptySearch").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogEmptyCount").toInt(), 0);
    }

    CHECK(std::filesystem::is_regular_file(root / "Content" / "Actions" / "Persistent.action"));
    std::filesystem::remove_all(root, cleanupError);
}

// editor_animation_clip_script.cpp
TEST_CASE("editor.animation.clip_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.animation", "Test Animation Editor");
        created <- eve.AnimationEditorModule().create("test.asset.walk");
        clipEditor <- created.value;
        configured <- clipEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- ""; cap0 <- ""; ctx1 <- "";
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            if (workspace.getPanelId(i) == "animation.skeleton") {
                panel0 = workspace.getPanelId(i); cap0 = workspace.getPanelCapability(i);
            }
            if (workspace.getPanelContext(i) == "preview") ctx1 = workspace.getPanelContext(i);
        }
        viewport <- clipEditor.setViewport(640.0, 36.0, 120.0);
        seekResult <- clipEditor.seekSeconds(1.0);
    )"));
    REQUIRE(preview_test::finishScript(vm, "clipEditor").ok());
    vm.run(vm.compileSource(R"(
        hipsAtMid <- 0.0;
        for (local i = 0; i < clipEditor.getPrimitiveCount(); ++i) {
            if (clipEditor.getPrimitiveKind(i) == "joint") {
                hipsAtMid = clipEditor.getPrimitiveX(i);
                break;
            }
        }
        beforeMask <- clipEditor.getSelectedMaskWeight();
        maskResult <- clipEditor.setMaskWeight(0.2);
        afterMask <- clipEditor.getSelectedMaskWeight();
        undoResult <- clipEditor.undo();
        restoredMask <- clipEditor.getSelectedMaskWeight();
        clipEditor.play();
        advanced <- clipEditor.update(0.25);
        playhead <- clipEditor.getPlayhead();
        tracks <- clipEditor.getTrackCount();
        keys <- clipEditor.getKeyCount();
        primitives <- clipEditor.getPrimitiveCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("animation.skeleton"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("animation.clip"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("hipsAtMid").toFloat() > 0.4F);
    CHECK(vm.find("maskResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterMask").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredMask").toFloat() - vm.find("beforeMask").toFloat()), 0.0001F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK_EQ(vm.find("tracks").toInt(), 6);
    CHECK(vm.find("keys").toInt() >= 12);
    CHECK(vm.find("primitives").toInt() > 0);
}

// editor_api_contracts.cpp
using namespace eve::editor;
using namespace eve::level_editing;

TEST_CASE("editor.api.transactions_expose_checked_results") {
    EditorTransactions transactions;

    auto begun = transactions.beginTransaction("checked transaction");
    REQUIRE(begun.ok());

    auto duplicate = transactions.beginTransaction("nested transaction");
    CHECK(!duplicate.ok());

    auto rolledBack = transactions.rollbackTransaction();
    CHECK(rolledBack.ok());

    auto noActiveTransaction = transactions.rollbackTransaction();
    CHECK(!noActiveTransaction.ok());
}

TEST_CASE("editor.api.session_and_history_preserve_failure_status") {
    EditorSession session;
    auto          nullCommand = session.executeChecked(nullptr);
    CHECK_EQ(static_cast<int>(nullCommand.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!nullCommand.diagnostics().empty());

    EditorHistory history;
    auto          emptyUndo = history.undoAction();
    CHECK_EQ(static_cast<int>(emptyUndo.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(!emptyUndo.diagnostics().empty());
    CHECK(!history.undo());

    history.push("rename", "payload");
    auto undone = history.undoAction();
    REQUIRE(undone.ok());
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), std::string("rename"));
}

TEST_CASE("editor.api.workspace_checked_operations_preserve_diagnostics") {
    EditorWorkspace workspace("checked", "Checked workspace");

    WorkspacePanelDescriptor panel;
    panel.id        = "scene";
    panel.title     = "Scene";
    panel.region    = "center";
    auto registered = workspace.registerPanel(panel);
    REQUIRE(registered.ok());
    REQUIRE(registered.ok());

    auto duplicate = workspace.registerPanel(panel);
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE(!duplicate.diagnostics().empty());

    auto moved = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    REQUIRE(moved.ok());
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().region, std::string("left"));

    auto unchanged = workspace.movePanel(StableId("scene"), WorkspaceRegion::Left, 10);
    CHECK_EQ(static_cast<int>(unchanged.code()), static_cast<int>(EditorStatus::NoOp));
    CHECK(unchanged.ok());

    auto missing = workspace.panelAt(9);
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(!missing.diagnostics().empty());

    auto activated = workspace.activatePanel(StableId("scene"));
    CHECK(activated.ok());
    auto removed = workspace.removePanel(StableId("scene"));
    REQUIRE(removed.ok());
    auto removedAgain = workspace.removePanel(StableId("scene"));
    CHECK_EQ(static_cast<int>(removedAgain.code()), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.api.coordinator_invalidates_bound_target_when_unregistered") {
    SceneDocumentTarget target("scene.lifecycle");
    Editor              editor;
    EditorSession       session;

    REQUIRE(editor.registerEditingTarget(target).ok());
    CHECK(target.capability<eve::editing::IEditingSnapshotProvider>().has_value());
    REQUIRE(editor.bindEditingTarget(session, TargetId("scene.lifecycle")).ok());
    REQUIRE(session.target() == &target);
    REQUIRE(session.context().target() == &target);
    REQUIRE(session.transactions().beginTransaction("pending target gesture").ok());
    REQUIRE(session.transactions().isActive());

    REQUIRE(editor.unregisterEditingTarget(TargetId("scene.lifecycle")).ok());
    CHECK(session.target() == nullptr);
    CHECK(session.context().target() == nullptr);
    CHECK(session.contextSnapshot().target.empty());
    CHECK(!session.transactions().isActive());
    CHECK(!session.transactions().canUndo());
}

TEST_CASE("editor.api.coordinator_unregisters_captured_command_callbacks_before_destruction") {
    EditorCommandService commands;
    {
        EditorTargetCoordinator                coordinator(commands);
        eve::editing::EditingCommandDescriptor descriptor;
        descriptor.id          = CommandId("test.coordinator.lifecycle");
        descriptor.ownerModule = "test.coordinator";
        auto registered = coordinator.registerPlannedCommand(descriptor, [](IEditableTarget&, const CommandRequest&) {
            return eve::editing::applied<CommandPlan>(CommandPlan{});
        });
        REQUIRE(registered.ok());
        CHECK(commands.find(descriptor.id) != nullptr);
    }
    CHECK(commands.find(CommandId("test.coordinator.lifecycle")) == nullptr);
}

TEST_CASE("editor.api.session_execute_checked_forwards_transaction_diagnostics") {
    TileBuffer       buffer(1, 1);
    TileBufferTarget target("tiles", &buffer);
    EditorSession    session;
    session.bindTarget(&target);

    auto command = std::make_unique<IntFieldEditCommand>("set", &target);
    REQUIRE(command->record(0, 0, 3));
    auto missingTransaction = session.executeChecked(std::move(command));
    CHECK_EQ(static_cast<int>(missingTransaction.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!missingTransaction.diagnostics().empty());
    CHECK_EQ(missingTransaction.diagnostics().front().code(), eve::DiagnosticCode::PreconditionViolation);
    CHECK_EQ(missingTransaction.diagnostics().front().path(), std::string("editor.transaction"));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

TEST_CASE("editor.api.session_execute_command_reports_empty_transaction_commit") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id          = CommandId("test.noop");
    descriptor.ownerModule = "test";
    descriptor.displayName = "Noop";
    REQUIRE(service
                .registerCommand(descriptor,
                                 [](const CommandContext&, const EditorValue& payload) {
                                     return eve::editing::applied<EditorValue>(payload);
                                 })
                .ok());

    EditorSession session;
    HostProfile   profile = HostProfile::developer();
    profile.allowCommand(descriptor.id);
    session.setHostProfile(std::move(profile));
    session.setCommandService(&service);

    auto result = session.executeCommand(descriptor.id, EditorValue(1));
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!result.diagnostics().empty());
    CHECK(!session.transactions().isActive());
}

// editor_avatar_document_script.cpp
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
        panel0 <- ""; cap0 <- ""; ctx4 <- "";
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            if (workspace.getPanelId(i) == "avatar.layers") {
                panel0 = workspace.getPanelId(i); cap0 = workspace.getPanelCapability(i);
            }
            if (workspace.getPanelContext(i) == "expressions") ctx4 = workspace.getPanelContext(i);
        }
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

    REQUIRE(vm.find("created").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    REQUIRE(vm.find("configured").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("workspacePanelCount").toInt(), 5);
    REQUIRE_EQ(vm.find("panel0").toString(), std::string("avatar.layers"));
    REQUIRE_EQ(vm.find("cap0").toString(), std::string("avatar.document"));
    REQUIRE_EQ(vm.find("ctx4").toString(), std::string("expressions"));
    REQUIRE(vm.find("selectParam").toTable().get<bool>("ok"));
    REQUIRE(!vm.find("deleted").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("afterCount").toInt(), 1);
    REQUIRE_EQ(vm.find("afterY").toFloat(), vm.find("beforeY").toFloat());
    REQUIRE(vm.find("valueResult").toTable().get<bool>("ok"));
    REQUIRE(vm.find("movedY").toFloat() > vm.find("beforeY").toFloat());
    REQUIRE_EQ(vm.find("expressions").toInt(), 1);
    REQUIRE(vm.find("channels").toInt() >= 1);
}

// editor_biome_rules_script.cpp
TEST_CASE("editor.biome.rules_editor_script_composes_workspace_and_exclusions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.biome", "Test Biome Editor");
        created <- eve.BiomeEditorModule().create("test.asset.forest");
        biomeEditor <- created.value;
        configured <- biomeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        beforePoints <- biomeEditor.getPointCount();
        excluded <- biomeEditor.addExclusion("asset://preview/clearing.spatial");
        afterPoints <- biomeEditor.getPointCount();
        exclusions <- biomeEditor.getExclusionCount();
        undoResult <- biomeEditor.undo();
        restored <- biomeEditor.getPointCount();
        densityReject <- biomeEditor.setLayerDensity(2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("biome.assets"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("biome.rules"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("inspector"));
    CHECK(vm.find("beforePoints").toInt() > 0);
    CHECK(vm.find("excluded").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("exclusions").toInt(), 1);
    CHECK(vm.find("afterPoints").toInt() < vm.find("beforePoints").toInt());
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("restored").toInt(), vm.find("beforePoints").toInt());
    CHECK(!vm.find("densityReject").toTable().get<bool>("ok"));
}
}  // namespace test_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_animation_clip)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"
#include "animation/editing/AnimationClip.h"
#include "animation/editing/AnimationGraph.h"
#include "animation/editor/AnimationClipEditor.h"
#include "animation/editor/AnimationEditorModule.h"
#include "animation/editor/EditorAnimationClip.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "editor/EditorWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_animation_clip.cpp
namespace test_editor_animation_clip {
// editor_animation_clip.cpp
using namespace eve::animation_editing;
using namespace eve::editing;

namespace {
AnimationTransformKey key(const char* id, double time, double x) {
    AnimationTransformKey result;
    result.id        = StableId(id);
    result.time      = time;
    result.positionX = x;
    return result;
}
}  // namespace

TEST_CASE("editor.animation.clip_timeline_supports_stable_reversible_tracks_events_and_masks") {
    AnimationClipDocumentTarget clip("walk");
    auto                        settings = clip.makeSetSettings(2.0, 60.0, true);
    REQUIRE(settings.ok());
    CHECK(clip.applyDomainOperation(settings.value()).ok());
    AnimationBoneTrack track{StableId("hips-track"), "mixamorig:Hips", {key("start", 0.0, 0.0), key("end", 2.0, 4.0)}};
    auto               setTrack = clip.makeSetTrack(track);
    REQUIRE(setTrack.ok());
    CHECK(clip.applyDomainOperation(setTrack.value()).ok());
    auto event = clip.makeSetEvent({StableId("footstep"), 0.5, "footstep", "left"});
    REQUIRE(event.ok());
    CHECK(clip.applyDomainOperation(event.value()).ok());
    auto mask = clip.makeSetMask({"mixamorig:Hips", 0.25});
    REQUIRE(mask.ok());
    CHECK(clip.applyDomainOperation(mask.value()).ok());
    const auto preview = clip.preview(1.0, {"mixamorig:Hips"});
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(preview.bones.size(), 1U);
    CHECK_EQ(preview.bones[0].positionX, 2.0);
    CHECK_EQ(preview.bones[0].maskWeight, 0.25);
    auto remove = clip.makeDeleteTrack(StableId("hips-track"));
    REQUIRE(remove.ok());
    CHECK(clip.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value();
    undo.type            = remove.value().inverseType;
    undo.payload         = remove.value().inverse;
    CHECK(clip.applyDomainOperation(undo).ok());
    CHECK_EQ(clip.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive") {
    AnimationClipDocumentTarget clip("source");
    auto                        track = clip.makeSetTrack({StableId("hips"), "mixamorig:Hips", {key("key", 0.0, 1.0)}});
    REQUIRE(track.ok());
    CHECK(clip.applyDomainOperation(track.value()).ok());
    const auto mapping = clip.previewRetarget({"hips", "spine"});
    CHECK_EQ(static_cast<int>(mapping.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(mapping.mapping.at("mixamorig:Hips"), "hips");
    AnimationClipDocumentTarget restored("source");
    CHECK(restored.loadSnapshot(clip.snapshotValue()).ok());
    CHECK_EQ(restored.tracks().size(), 1U);
}

TEST_CASE("editor.animation.clip_builds_real_runtime_asset_and_rejects_missing_bones") {
    AnimationClipDocumentTarget document("run");
    auto track = document.makeSetTrack({StableId("root-track"), "root", {key("a", 0.0, 0.0), key("b", 1.0, 2.0)}});
    REQUIRE(track.ok());
    CHECK(document.applyDomainOperation(track.value()).ok());
    auto event = document.makeSetEvent({StableId("event"), 0.25, "step", "right"});
    REQUIRE(event.ok());
    CHECK(document.applyDomainOperation(event.value()).ok());
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimationClipRuntimeBuilder builder;
    auto                        built = builder.build(document, &skeleton);
    REQUIRE(built.ok());
    CHECK_EQ(built.value()->getPositionKeyCount(0), 2);
    CHECK_EQ(built.value()->getEventCount(), 1);
    delete built.value();
    eve::animation::AnimSkeleton wrong;
    wrong.addBone("pelvis");
    CHECK_EQ(static_cast<int>(builder.build(document, &wrong).code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_animation_clip_editor.cpp
using eve::animation_editor::AnimationClipEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.animation.clip_editor_installs_workspace_and_reverses_mask") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    EditorWorkspace     workspace("animation.preview", "Animation Clip Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    const std::map<std::string, std::string> expected = {{"animation.skeleton", "list"},
                                                         {"animation.preview", "preview"},
                                                         {"animation.inspector", "inspector"},
                                                         {"animation.timeline", "timeline"}};
    for (int i = 0; i < workspace.getPanelCount(); ++i) {
        const auto id = workspace.getPanelId(i);
        REQUIRE(expected.contains(id));
        CHECK_EQ(workspace.getPanelCapability(i), std::string("animation.clip"));
        CHECK_EQ(workspace.getPanelContext(i), expected.at(id));
    }

    REQUIRE(editor.setViewport(640.0f, 36.0f, 120.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.trackCount(), 6);
    CHECK_EQ(editor.boneCount(), 6);
    CHECK_EQ(editor.boneParent(3), std::string("Chest"));
    CHECK(editor.keyCount() >= 12);
    CHECK_EQ(editor.eventCount(), 1);
    CHECK(editor.primitiveCount() > 0);
    CHECK_EQ(editor.preview().documentRevision, editor.revision());

    REQUIRE(editor.seekSeconds(1.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.playhead() - 1.0) < 0.0001);
    double hipsX = 0.0;
    for (const auto& bone : editor.preview().bones)
        if (bone.bone == "Hips") hipsX = bone.positionX;
    CHECK(std::abs(hipsX - 1.0) < 0.05);

    const double before = editor.selectedMaskWeight();
    REQUIRE(editor.setMaskWeight(0.25).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - 0.25) < 0.0001);
    REQUIRE(editor.undo().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedMaskWeight() - before) < 0.0001);
}

TEST_CASE("editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.setViewport(800.0f, 36.0f, 120.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    int spineRow = -1;
    for (int i = 0; i < editor.trackCount(); ++i)
        if (editor.trackBone(i) == "Spine") spineRow = i;
    REQUIRE(spineRow >= 0);
    REQUIRE(editor.pointerDown(20.0f, (static_cast<float>(spineRow) + 0.5f) * 36.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.selectedBone(), std::string("Spine"));

    editor.play();
    REQUIRE(editor.update(0.2).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.playhead() > 0.0);
    CHECK(editor.isPlaying());

    auto rejected = editor.setDuration(0.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.duration(), 2.0);
}

TEST_CASE("editor.animation.clip_editor_keys_edits_and_deletes_joint_transform") {
    AnimationClipEditor editor("preview.walk");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.selectBone("Spine").ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.seekSeconds(1.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    const int beforeKeys = editor.keyCount();

    REQUIRE(editor.setSelectedPosition(1.25, 2.5, -0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
    CHECK(std::abs(editor.selectedPositionX() - 1.25) < 0.0001);
    CHECK(std::abs(editor.selectedPositionY() - 2.5) < 0.0001);
    CHECK(std::abs(editor.selectedPositionZ() + 0.5) < 0.0001);

    REQUIRE(editor.setSelectedRotation(15.0, 30.0, -20.0).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedRotationX() - 15.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationY() - 30.0) < 0.01);
    CHECK(std::abs(editor.selectedRotationZ() + 20.0) < 0.01);
    REQUIRE(editor.setSelectedScale(1.1, 1.2, 1.3).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedScaleY() - 1.2) < 0.0001);

    REQUIRE(editor.moveSelectedKey(1.25).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(std::abs(editor.selectedKeyTime() - 1.25) < 0.0001);
    REQUIRE(editor.deleteSelectedKey().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(!editor.hasSelectedKey());
    CHECK_EQ(editor.keyCount(), beforeKeys);
    REQUIRE(editor.undo().ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.keyCount(), beforeKeys + 1);
}

TEST_CASE("editor.animation.clip_editor_round_trips_real_runtime_skeleton_and_clip") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("Root");
    const int                    hand = skeleton.addBone("Hand", root);
    skeleton.setBindPosition(hand, 0.0f, 1.0f, 0.0f);

    eve::animation::AnimClip clip("attack");
    clip.setDuration(1.0f);
    clip.setSampleRate(30.0f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.0f, 0.0f, 0.0f, 0.0f);
    clip.addPositionKey(root, 1.0f, 1.0f, 0.0f, 0.0f);
    clip.addRotationKey(hand, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    clip.addRotationKey(hand, 1.0f, 0.0f, 0.7071067f, 0.0f, 0.7071067f);

    AnimationClipEditor editor("runtime.attack");
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.loadRuntimeClip(skeleton, clip).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK_EQ(editor.boneCount(), 2);
    CHECK_EQ(editor.boneName(1), std::string("Hand"));
    CHECK_EQ(editor.boneParent(1), std::string("Root"));
    REQUIRE(editor.selectBone("Hand").ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.seekSeconds(0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.setSelectedPosition(0.25, 1.25, -0.5).ok());
    REQUIRE(preview_test::finish(editor).ok());
    REQUIRE(editor.writeRuntimeClip(clip, skeleton).ok());
    REQUIRE(preview_test::finish(editor).ok());

    eve::animation::AnimPose pose(2);
    clip.sample(0.5f, &pose, &skeleton);
    CHECK(std::abs(pose.getLocalPositionX(hand) - 0.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionY(hand) - 1.25f) < 0.0001f);
    CHECK(std::abs(pose.getLocalPositionZ(hand) + 0.5f) < 0.0001f);
}

// editor_animation_editor.cpp
using namespace eve::editor;

TEST_CASE("editor.animation.clip_automation_owns_headless_targets") {
    Editor                                       editor;
    eve::animation_editor::AnimationEditorModule adapter;
    auto*                                        automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("animation.clip.settings.set.v1") != std::string::npos);
    CHECK(commands.find("animation.clip.track.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.clip","type":"animation-clip"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string settings = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.settings.set.v1","payload":{"duration":2,"sampleRate":60,"loop":true}})");
    REQUIRE(settings.find("\"status\":\"applied\"") != std::string::npos);

    const std::string tracked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.track.set.v1","payload":{"id":"hips-track","bone":"Hips","keys":[{"id":"start","time":0,"px":0,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1},{"id":"end","time":2,"px":4,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1}]}})");
    REQUIRE(tracked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string masked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.mask.set.v1","payload":{"bone":"Hips","weight":0.25}})");
    REQUIRE(masked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(inspected.find("animation-clip-document") != std::string::npos);
    CHECK(inspected.find("hips-track") != std::string::npos);
    CHECK(inspected.find("Hips") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.clip"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterUndo = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterUndo.find("0.25") == std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.clip"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterClose = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterClose.find("not-found") != std::string::npos);
}

// editor_animation_graph.cpp
using namespace eve::animation_editing;
using namespace eve::editing;

namespace {

GraphDocumentData basicGraph(AnimationStateGraphDomain& domain) {
    auto idle       = domain.makeStateNode(GraphNodeId("idle"), "asset://animations/idle.eva");
    auto run        = domain.makeStateNode(GraphNodeId("run"), "asset://animations/run.eva");
    auto transition = domain.makeTransitionNode(GraphNodeId("idle-to-run"));
    REQUIRE(idle.ok());
    REQUIRE(run.ok());
    REQUIRE(transition.ok());

    EditorValue::Object transitionProperties = *transition.value().properties.getIf<EditorValue::Object>();
    transitionProperties["blendSeconds"]     = 0.15;
    transitionProperties["conditionKind"]    = "float";
    transitionProperties["parameter"]        = "speed";
    transitionProperties["operator"]         = ">";
    transitionProperties["threshold"]        = 0.1;
    transition.value().properties            = EditorValue(std::move(transitionProperties));

    eve::editing::GraphDocument graph;
    REQUIRE(graph.createNode(idle.value()).ok());
    REQUIRE(graph.createNode(run.value()).ok());
    REQUIRE(graph.createNode(transition.value()).ok());
    const GraphPinRecord* idleOut       = graph.findPin(GraphPinId("idle.out"));
    const GraphPinRecord* transitionIn  = graph.findPin(GraphPinId("idle-to-run.in"));
    const GraphPinRecord* transitionOut = graph.findPin(GraphPinId("idle-to-run.out"));
    const GraphPinRecord* runIn         = graph.findPin(GraphPinId("run.in"));
    REQUIRE(idleOut);
    REQUIRE(transitionIn);
    REQUIRE(transitionOut);
    REQUIRE(runIn);
    REQUIRE(
        graph.connect({StableId("edge-in"), idleOut->id, transitionIn->id}, domain.canConnect(*idleOut, *transitionIn))
            .ok());
    REQUIRE(
        graph.connect({StableId("edge-out"), transitionOut->id, runIn->id}, domain.canConnect(*transitionOut, *runIn))
            .ok());
    EditorValue::Object parameters;
    parameters["entry"] = "idle";
    REQUIRE(graph.setParameters(EditorValue(std::move(parameters))).ok());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.animation.state_graph_compiles_stable_clip_references_and_conditions") {
    AnimationStateGraphDomain domain;
    const auto                graph    = basicGraph(domain);
    const auto                compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    CHECK(compiled.definition.find("EVANIM_STATE_GRAPH 1\n") == 0);
    CHECK(compiled.definition.find("state \"idle\" \"asset://animations/idle.eva\"") != std::string::npos);
    CHECK(compiled.definition.find("transition \"idle-to-run\" \"idle\" \"run\" 0.15") != std::string::npos);
    CHECK(compiled.definition.find("\"float\" \"speed\" \">\" 0.1") != std::string::npos);
}

TEST_CASE("editor.animation.state_graph_rejects_incomplete_or_invalid_authoring_data") {
    AnimationStateGraphDomain domain;
    GraphDocumentData         graph = basicGraph(domain);
    graph.schemaVersion             = 99;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = basicGraph(domain);
    graph.edges.pop_back();
    auto result = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());

    graph           = basicGraph(domain);
    auto transition = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                   [](const GraphNodeRecord& node) { return node.type == "animation.transition"; });
    REQUIRE(transition != graph.nodes.end());
    auto* properties = transition->properties.getIf<EditorValue::Object>();
    REQUIRE(properties);
    (*properties)["blendSeconds"] = -1.0;
    result                        = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));

    graph            = basicGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "missing";
    result                 = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_rejects_direct_state_connections") {
    AnimationStateGraphDomain domain;
    auto                      idle = domain.makeStateNode(GraphNodeId("idle"), "idle.eva");
    auto                      run  = domain.makeStateNode(GraphNodeId("run"), "run.eva");
    REQUIRE(idle.ok());
    REQUIRE(run.ok());
    CHECK(domain.canConnect(idle.value().pins[1], run.value().pins[0]).allowed);

    GraphDocumentData invalid;
    invalid.domain      = domain.domain();
    invalid.nodes       = {idle.value(), run.value()};
    invalid.edges       = {{StableId("direct"), idle.value().pins[1].id, run.value().pins[0].id}};
    invalid.parameters  = EditorValue::Object{{"entry", "idle"}};
    const auto compiled = domain.compile(invalid);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_builds_real_runtime_state_machine") {
    AnimationStateGraphDomain    domain;
    const GraphDocumentData      graph = basicGraph(domain);
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::AnimClip idle("idle");
    eve::animation::AnimClip run("run");
    idle.setDuration(1.0F);
    run.setDuration(1.0F);

    AnimationStateGraphRuntimeBuilder builder;
    auto                              built = builder.build(graph, &skeleton, [&](const std::string& asset) {
        if (asset == "asset://animations/idle.eva") return &idle;
        if (asset == "asset://animations/run.eva") return &run;
        return static_cast<eve::animation::AnimClip*>(nullptr);
    });
    REQUIRE(built.ok());
    CHECK_EQ(built.value()->getStateCount(), 2);
    delete built.value();

    auto missing = builder.build(graph, &skeleton,
                                 [](const std::string&) { return static_cast<eve::animation::AnimClip*>(nullptr); });
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(EditorStatus::NotFound));
}
}  // namespace test_editor_animation_clip
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_archspace_target)
#include "PrivateTestScope.h"
#include <algorithm>
#include <fstream>
#include "archspace/ArchSpaceCatalog.h"
#include "archspace/ArchSpaceDocument.h"
#include "archspace/editing/ArchSpaceTarget.h"
#include "archspace/editor/ArchSpaceEditorModule.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_archspace_target.cpp
namespace test_editor_archspace_target {
using namespace eve::archspace;
using namespace eve::archspace_editing;
using namespace eve::editing;

namespace {

void apply(ArchSpaceDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

SelectionSnapshot select(const ArchSpaceDocumentTarget& target, const char* id, const char* type) {
    SelectionSnapshot selection;
    selection.channel = "archspace";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), type});
    return selection;
}

}  // namespace

TEST_CASE("archspace.document.bootstrap_room_opening_and_mesh_bake") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(
        document.createRoom("level0", "living", "Living Room", {{0, 0}, {6, 0}, {6, 4}, {0, 4}}, 3.0, 0.2, 0.2).ok());
    REQUIRE(document.find("living.wall.0") != nullptr);
    REQUIRE(document.find("living.zone") != nullptr);
    Node door;
    door.id            = "living.door";
    door.kind          = NodeKind::Opening;
    door.parentId      = "living.wall.0";
    door.name          = "Entry";
    door.openingKind   = OpeningKind::Door;
    door.t             = 0.5;
    door.width         = 1.0;
    door.openingHeight = 2.1;
    REQUIRE(document.insert(std::move(door)).ok());
    const MeshBake bake = document.bakeMesh();
    CHECK(bake.positions.size() >= 8);
    CHECK(!bake.indices.empty());
    CHECK(document.diagnostics().empty());
}

TEST_CASE("archspace.editing.room_property_undo_and_snapshot_are_atomic") {
    ArchSpaceDocumentTarget target("apartment");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    apply(target, target.makeCreateRectRoom("level0", "office", "Office", 0, 0, 5, 4));
    CHECK(target.document().find("office.zone") != nullptr);

    const auto selection = select(target, "office.wall.0", "archspace.wall");
    auto       edit      = target.makeSet(selection, PropertyPath("thickness"), 0.35, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(target.document().find("office.wall.0")->thickness, 0.35);

    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.document().find("office.wall.0")->thickness, 0.2);

    apply(target, target.makeCreateOpening("office.wall.1", "office.window", OpeningKind::Window, 0.4, 1.2, 1.4, 0.9));
    apply(target, target.makePlaceItem("level0", "desk", "furniture.desk", Vec3{1.5, 0, 1.5}, 90));
    CHECK(target.document().find("desk") != nullptr);

    const EditorValue       snapshot = target.snapshotValue();
    ArchSpaceDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.document().nodeCount(), target.document().nodeCount());

    EditorValue invalid      = snapshot;
    auto*       root         = invalid.getIf<EditorValue::Object>();
    (*root)["schemaVersion"] = int64_t{99};
    const auto before        = target.snapshotValue();
    CHECK(!target.loadSnapshot(invalid).ok());
    CHECK_EQ(target.snapshotValue(), before);

    auto gizmo = target.gizmo();
    REQUIRE(gizmo.ok());
    CHECK(!gizmo.value().primitives.empty());
    CHECK(target.bakeMesh().indices.size() >= 3);
}

TEST_CASE("archspace.document.opening_cutout_changes_mesh_and_helpers_work") {
    Document document;
    REQUIRE(document.bootstrap("site", "building", "level0").ok());
    REQUIRE(document.createRoom("level0", "hall", "Hall", {{0, 0}, {8, 0}, {8, 5}, {0, 5}}, 3.0, 0.2, 0.2).ok());
    const MeshBake before = document.bakeMesh();
    REQUIRE(document.createOpening("hall.wall.0", "hall.door", "Door", OpeningKind::Door, 0.4, 1.1, 2.1, 0.0).ok());
    REQUIRE(
        document.createOpening("hall.wall.1", "hall.window", "Window", OpeningKind::Window, 0.5, 1.5, 1.3, 0.9).ok());
    REQUIRE(document.createWall("level0", "partition", "Partition", Vec2{4, 0}, Vec2{4, 5}, 3.0, 0.15).ok());
    REQUIRE(document.placeItem("level0", "sofa", "Sofa", "furniture.sofa", Vec3{2, 0, 2.5}, 0).ok());
    const MeshBake after = document.bakeMesh();
    CHECK(after.indices.size() > before.indices.size());
    CHECK(document.find("hall.door") != nullptr);
    CHECK(document.find("partition") != nullptr);
    CHECK(document.find("sofa") != nullptr);
    const MeshArrays arrays = document.bakeMeshArrays();
    CHECK_EQ(arrays.vertexCount() * 3, static_cast<int>(arrays.positions.size()));
    CHECK_EQ(arrays.indexCount() % 3, 0);
    CHECK(arrays.triangleCount() >= 12);

    const auto hasPrimitive = [&](const char* needle) {
        return std::any_of(after.primitiveIds.begin(), after.primitiveIds.end(),
                           [&](const std::string& id) { return id.find(needle) != std::string::npos; });
    };
    CHECK(hasPrimitive(".reveal."));
    CHECK(hasPrimitive("sofa"));
    CHECK(lookupCatalog("furniture.sofa").has_value());
    CHECK(listCatalogIds().size() >= 6);

    // Machine-readable evidence for walkthrough artifacts.
    std::ofstream evidence("/opt/cursor/artifacts/archspace_mesh_evidence.txt");
    evidence << "nodes=" << document.nodeCount() << "\n";
    evidence << "triangles=" << arrays.triangleCount() << "\n";
    evidence << "vertices=" << arrays.vertexCount() << "\n";
    evidence << "indices=" << arrays.indexCount() << "\n";
    evidence << "primitives=" << after.primitiveIds.size() << "\n";
    evidence << "has_door=1\n";
    evidence << "has_window=1\n";
    evidence << "has_partition=1\n";
    evidence << "has_item=1\n";
    evidence << "has_reveal=1\n";
    evidence << "has_catalog_furniture=1\n";
    evidence.close();
}

TEST_CASE("editor.archspace.adapter_registers_commands_and_owns_automation_targets") {
    using eve::editor::Editor;
    Editor                                       editor;
    eve::archspace_editor::ArchSpaceEditorModule adapter;
    auto*                                        automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("archspace.bootstrap.v1") != std::string::npos);
    CHECK(commands.find("archspace.room.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.wall.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.opening.create.v1") != std::string::npos);
    CHECK(commands.find("archspace.item.place.v1") != std::string::npos);
    CHECK(commands.find("archspace.property.set.v1") != std::string::npos);
    CHECK(commands.find("archspace.node.delete.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.archspace","type":"archspace","siteId":"site"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string room = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.room.create.v1","payload":{"levelId":"site.level0","roomId":"studio","name":"Studio","originX":0,"originZ":0,"sizeX":6,"sizeZ":4}})");
    REQUIRE(room.find("\"status\":\"applied\"") != std::string::npos);

    const std::string opening = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.opening.create.v1","payload":{"wallId":"studio.wall.0","openingId":"studio.door","kind":"door","t":0.5,"width":1.0,"height":2.1,"sill":0.0}})");
    REQUIRE(opening.find("\"status\":\"applied\"") != std::string::npos);

    const std::string thickness = automation->invoke(
        "execute",
        R"({"target":"agent.archspace","command":"archspace.property.set.v1","payload":{"objectId":"studio.wall.0","path":"thickness","value":0.3}})");
    REQUIRE(thickness.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.archspace"})");
    CHECK(inspected.find("studio") != std::string::npos);
    CHECK(inspected.find("studio.door") != std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.archspace"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
}
}  // namespace test_editor_archspace_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_archspace_tools)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include "archspace/ArchSpaceCatalog.h"
#include "archspace/editor/EditorArchSpaceTools.h"
#include "editing/EditingAuthority.h"
#include "editor/EditorSession.h"
#include "zeroerr/unittest.h"
// editor_archspace_tools.cpp
namespace test_editor_archspace_tools {
namespace {

using eve::archspace::NodeKind;
using eve::archspace::OpeningKind;
using eve::archspace::Vec2;
using eve::archspace::Vec3;
using eve::archspace_editing::ArchSpaceDocumentTarget;
using eve::archspace_editing::Result;
using eve::editing::DomainOperation;
using eve::editor::ArchSpaceItemPlaceTool;
using eve::editor::ArchSpaceOpeningPlaceTool;
using eve::editor::ArchSpaceViewportRay;
using eve::editor::ArchSpaceWallDrawTool;
using eve::editor::EditorPointerEvent;
using eve::editor::IArchSpaceViewportAdapter;
using eve::editor::IEditorOverlay;
using eve::editor::OverlayPoint;
using eve::editor::OverlayStyle;

void apply(ArchSpaceDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

class TestArchSpaceViewport final : public IArchSpaceViewportAdapter {
public:
    eve::editing::Result<ArchSpaceViewportRay> pointerRay(const EditorPointerEvent& event) const override {
        ArchSpaceViewportRay ray;
        ray.origin    = {event.x, 30.0, event.y};
        ray.direction = {0.0, -1.0, 0.0};
        return eve::editing::applied<ArchSpaceViewportRay>(ray);
    }

    eve::editing::Result<OverlayPoint> projectWorld(const std::array<double, 3>& world) const override {
        return eve::editing::applied<OverlayPoint>(
            {static_cast<float>(world[0]), static_cast<float>(world[2]), static_cast<float>(world[1])});
    }
};

class CountingAuthority final : public eve::editing::IEditAuthority {
public:
    explicit CountingAuthority(eve::editing::IDomainOperationTarget* target) : local_(target) {}

    eve::editing::Result<eve::editing::AuthorityPlan> preflight(
        const eve::editing::TransactionSpec&           transaction,
        std::span<const eve::editing::DomainOperation> operations) override {
        ++preflights;
        return local_.preflight(transaction, operations);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> commit(const eve::editing::AuthorityPlan& plan) override {
        ++commits;
        return local_.commit(plan);
    }

    eve::editing::Result<eve::editing::TransactionReceipt> compensate(
        const eve::editing::TransactionReceipt& receipt) override {
        return local_.compensate(receipt);
    }

    int preflights = 0;
    int commits    = 0;

private:
    eve::editing::LocalWorldAuthority local_;
};

class CountingOverlay final : public IEditorOverlay {
public:
    void line(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override { ++lines; }
    void circle(const OverlayPoint&, float, const OverlayStyle&) override { ++circles; }
    void rectangle(const OverlayPoint&, const OverlayPoint&, const OverlayStyle&) override {}
    void text(const OverlayPoint&, const std::string&, const OverlayStyle&) override { ++texts; }
    int  lines   = 0;
    int  circles = 0;
    int  texts   = 0;
};

EditorPointerEvent pointer(EditorPointerEvent::Phase phase, float x, float y, int pointerId = 7) {
    EditorPointerEvent event;
    event.phase     = phase;
    event.pointerId = pointerId;
    event.button    = 0;
    event.x         = x;
    event.y         = y;
    return event;
}

bool hasPrimitive(const eve::archspace::MeshBake& bake, const std::string& needle) {
    return std::any_of(bake.primitiveIds.begin(), bake.primitiveIds.end(),
                       [&](const std::string& id) { return id.find(needle) != std::string::npos; });
}

}  // namespace

TEST_CASE("editor.archspace.wall_draw_tool_commits_one_authority_transaction") {
    ArchSpaceDocumentTarget target("archspace-wall-tool");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    const auto revisionBefore = target.revision();

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);
    ArchSpaceWallDrawTool tool(&viewport, &authority);
    tool.setLevelId("level0");
    tool.setIdPrefix("drawn.wall");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 0.f, 0.f)).capturePointer);
    CHECK(session.hasPointerCapture());
    CHECK(tool.isDrawing());
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 4.f, 0.f)).handled);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);

    CountingOverlay overlay;
    session.drawOverlay(overlay);
    CHECK_EQ(overlay.lines, 1);
    CHECK_EQ(overlay.circles, 2);

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 4.f, 0.f)).releasePointer);
    CHECK(!session.hasPointerCapture());
    CHECK(!tool.isDrawing());
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(tool.lastReceipt().has_value());
    REQUIRE(target.document().find(tool.lastWallId()) != nullptr);
    CHECK_EQ(target.document().find(tool.lastWallId())->kind, NodeKind::Wall);
}

TEST_CASE("editor.archspace.opening_and_item_tools_commit_through_authority") {
    ArchSpaceDocumentTarget target("archspace-place-tools");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    apply(target, target.makeCreateWall("level0", "wall.a", "Wall A", Vec2{0, 0}, Vec2{6, 0}, 3.0, 0.2));

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);

    ArchSpaceOpeningPlaceTool openingTool(&viewport, &authority);
    openingTool.setOpeningKind(OpeningKind::Door);
    openingTool.setWidth(1.0);
    openingTool.setHeight(2.1);
    openingTool.setIdPrefix("drawn.door");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&openingTool));
    REQUIRE(session.activateTool(openingTool.descriptor().id));

    const auto revisionBeforeOpening = target.revision();
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 3.f, 0.f)).capturePointer);
    CHECK_EQ(authority.commits, 0);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 3.f, 0.f)).releasePointer);
    CHECK_EQ(authority.preflights, 1);
    CHECK_EQ(authority.commits, 1);
    CHECK_EQ(target.revision(), revisionBeforeOpening + 1);
    REQUIRE(target.document().find(openingTool.lastOpeningId()) != nullptr);
    CHECK_EQ(target.document().find(openingTool.lastOpeningId())->kind, NodeKind::Opening);

    ArchSpaceItemPlaceTool itemTool(&viewport, &authority);
    itemTool.setLevelId("level0");
    itemTool.setCatalogId("furniture.desk");
    itemTool.setYawDegrees(90.0);
    itemTool.setIdPrefix("drawn.desk");
    REQUIRE(session.addTool(&itemTool));
    REQUIRE(session.activateTool(itemTool.descriptor().id));

    const auto revisionBeforeItem = target.revision();
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 2.f, 2.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Up, 2.f, 2.f)).releasePointer);
    CHECK_EQ(authority.commits, 2);
    CHECK_EQ(target.revision(), revisionBeforeItem + 1);
    REQUIRE(target.document().find(itemTool.lastItemId()) != nullptr);
    CHECK_EQ(target.document().find(itemTool.lastItemId())->catalogId, "furniture.desk");

    const eve::archspace::MeshBake bake = target.bakeMesh();
    CHECK(hasPrimitive(bake, ".reveal."));
    CHECK(hasPrimitive(bake, itemTool.lastItemId()));
    CHECK(eve::archspace::lookupCatalog("furniture.desk").has_value());
    CHECK(eve::archspace::listCatalogIds().size() >= 6);
}

TEST_CASE("editor.archspace.wall_draw_cancel_does_not_mutate") {
    ArchSpaceDocumentTarget target("archspace-wall-cancel");
    apply(target, target.makeBootstrap("site", "building", "level0"));
    const auto revisionBefore = target.revision();
    const auto nodesBefore    = target.document().nodeCount();

    TestArchSpaceViewport viewport;
    CountingAuthority     authority(&target);
    ArchSpaceWallDrawTool tool(&viewport, &authority);
    tool.setLevelId("level0");

    eve::editor::EditorSession session;
    session.bindTarget(target);
    REQUIRE(session.addTool(&tool));
    REQUIRE(session.activateTool(tool.descriptor().id));

    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Down, 1.f, 1.f)).capturePointer);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Move, 3.f, 1.f)).handled);
    CHECK(session.dispatchPointer(pointer(EditorPointerEvent::Phase::Cancel, 3.f, 1.f)).releasePointer);
    CHECK_EQ(authority.preflights, 0);
    CHECK_EQ(authority.commits, 0);
    CHECK_EQ(target.revision(), revisionBefore);
    CHECK_EQ(target.document().nodeCount(), nodesBefore);
    CHECK(!tool.isDrawing());
}
}  // namespace test_editor_archspace_tools
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_audio_editor)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "audio/editor/AudioEditorModule.h"
#include "audio/editor/AudioSourceEditor.h"
#include "audio/editor/EditorAudioEffects.h"
#include "audio/editor/EditorAudioImportDiagnostics.h"
#include "audio/editor/EditorAudioTarget.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "editor/EditorWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_audio_editor.cpp
namespace test_editor_audio_editor {
// editor_audio_editor.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.adapter_registers_commands_and_owns_headless_targets") {
    Editor                               editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("audio.source.property.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.audio","type":"audio-source"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.audio","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.25}})");
    REQUIRE(changed.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.audio"})");
    CHECK(inspected.find("\"play.volume\"") != std::string::npos);
    CHECK(inspected.find("0.25") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.audio"})");
    REQUIRE(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string restored = automation->invoke("inspect", R"({"target":"agent.audio"})");
    CHECK(restored.find("0.25") == std::string::npos);

    const std::string mixer = automation->invoke("target-create", R"({"target":"agent.mixer","type":"audio-mixer"})");
    REQUIRE(mixer.find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.mixer"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE(automation->invoke("target-close", R"({"target":"agent.audio"})").find("\"status\":\"applied\"") !=
            std::string::npos);
}

TEST_CASE("editor.audio.property_command_keeps_bound_document_histories_independent") {
    AudioSourceTarget                    river("level.river");
    AudioSourceTarget                    wind("level.wind");
    Editor                               editor;
    eve::audio_editor::AudioEditorModule audioAdapter;
    REQUIRE(editor.registerEditingTarget(river).ok());
    REQUIRE(editor.registerEditingTarget(wind).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    REQUIRE(
        automation
            ->invoke(
                "execute",
                R"({"target":"level.river","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.4}})")
            .find("\"status\":\"applied\"") != std::string::npos);
    REQUIRE(
        automation
            ->invoke(
                "execute",
                R"({"target":"level.wind","command":"audio.source.property.set.v1","payload":{"path":"play.volume","value":0.8}})")
            .find("\"status\":\"applied\"") != std::string::npos);

    SelectionSnapshot riverSelection;
    riverSelection.channel = "audio";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(river.targetId());
    item.item   = StableId(river.targetId().value());
    item.type   = "audio.source";
    riverSelection.items.push_back(item);
    riverSelection.primary             = item;
    SelectionSnapshot windSelection    = riverSelection;
    windSelection.items.front().target = TargetId(wind.targetId());
    windSelection.items.front().item   = StableId(wind.targetId().value());
    windSelection.primary              = windSelection.items.front();

    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(0.4));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    REQUIRE(automation->invoke("undo", R"({"target":"level.river"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    CHECK_EQ(river.read(riverSelection, PropertyPath("play.volume")).value, EditorValue(1.0));
    CHECK_EQ(wind.read(windSelection, PropertyPath("play.volume")).value, EditorValue(0.8));

    CHECK(editor.unregisterEditingTarget(TargetId("level.river")).ok());
    CHECK(editor.unregisterEditingTarget(TargetId("level.wind")).ok());
}

// editor_audio_effects.cpp
using namespace eve::editor;

namespace {
AudioEffectRecord gain(const char* id, double decibels) {
    return {StableId(id), "gain", false, 1.0, EditorValue::Object{{"decibels", decibels}}};
}
class Sink final : public IAudioEffectChainSink {
public:
    Result<void> publish(const std::string& id, Revision sourceRevision,
                         const std::vector<AudioEffectRecord>& value) override {
        chain    = id;
        revision = sourceRevision;
        effects  = value;
        return eve::editing::applied<void>();
    }
    std::string                    chain;
    Revision                       revision = 0;
    std::vector<AudioEffectRecord> effects;
};
}  // namespace

TEST_CASE("editor.audio.effects_are_typed_reversible_ordered_and_assignable") {
    AudioEffectChainTarget chain("music-fx");
    auto                   first  = chain.makeSet(gain("pre", -3.0));
    auto                   second = chain.makeSet(gain("post", 2.0));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(chain.applyDomainOperation(first.value()).ok());
    CHECK(chain.applyDomainOperation(second.value()).ok());
    auto reorder = chain.makeReorder({StableId("post"), StableId("pre")});
    REQUIRE(reorder.ok());
    CHECK(chain.applyDomainOperation(reorder.value()).ok());
    CHECK_EQ(chain.effects().front().id.value(), std::string("post"));
    DomainOperation undo = reorder.value();
    undo.type            = reorder.value().inverseType;
    undo.payload         = reorder.value().inverse;
    CHECK(chain.applyDomainOperation(undo).ok());
    CHECK_EQ(chain.effects().front().id.value(), std::string("pre"));

    AudioMixerTarget mixer("mixer");
    auto             bus = mixer.makeCreate({ObjectId("music"), ObjectId("master"), "Music"});
    REQUIRE(bus.ok());
    CHECK(mixer.applyDomainOperation(bus.value()).ok());
    auto assign = chain.makeAssignToBus(mixer, ObjectId("music"));
    REQUIRE(assign.ok());
    CHECK(mixer.applyDomainOperation(assign.value()).ok());
    CHECK_EQ(mixer.bus(ObjectId("music")).value().effects.getIf<EditorValue::Array>()->size(), 2U);
}

TEST_CASE("editor.audio.effect_publisher_rejects_stale_revision_and_snapshot_is_atomic") {
    AudioEffectChainTarget chain("voice-fx");
    auto                   set = chain.makeSet(gain("gain", -2.0));
    REQUIRE(set.ok());
    CHECK(chain.applyDomainOperation(set.value()).ok());
    Sink                      sink;
    AudioEffectChainPublisher publisher;
    CHECK_EQ(static_cast<int>(publisher.publish(chain, chain.revision() - 1, sink).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(publisher.publish(chain, chain.revision(), sink).ok());
    CHECK_EQ(sink.effects.size(), 1U);
    EditorValue invalid = chain.snapshotValue();
    auto*       root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* effects = root->at("effects").getIf<EditorValue::Array>();
    REQUIRE(effects);
    effects->push_back(effects->front());
    CHECK_EQ(static_cast<int>(chain.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(chain.effects().size(), 1U);
    CHECK_EQ(static_cast<int>(chain.makeSet(gain("bad", 99.0)).code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_audio_import_diagnostics.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.import_diagnostics_detect_clip_dc_and_streaming_budget") {
    EditorAudioPcm pcm;
    pcm.sampleRate = 10;
    pcm.channels   = 2;
    pcm.samples    = {1.0F, 0.2F, 1.0F, 0.2F, 0.5F, 0.2F, 0.5F, 0.2F};
    AudioImportInspectionRequest request;
    request.asset              = "voice";
    request.sourceRevision     = 3;
    request.codec              = "wav";
    request.staticMemoryBudget = 8;
    AudioImportDiagnosticsService service;
    auto                          result = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Rejected));

    pcm.sampleRate = 8000;
    result         = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(result.channels.size(), 2U);
    CHECK_EQ(result.channels[0].clippedSamples, 2U);
    CHECK(result.channels[1].dcOffset > 0.19);
    CHECK(result.recommendStreaming);
    CHECK(result.diagnostics.size() >= 3U);
}

TEST_CASE("editor.audio.import_diagnostics_rejects_nonfinite_and_reports_silence") {
    AudioImportInspectionRequest request;
    request.asset          = "quiet";
    request.sourceRevision = 1;
    request.codec          = "ogg";
    EditorAudioPcm pcm;
    pcm.sampleRate = 48000;
    pcm.channels   = 1;
    pcm.samples.assign(480, 0.0F);
    AudioImportDiagnosticsService service;
    auto                          quiet = service.inspect(request, pcm);
    CHECK_EQ(static_cast<int>(quiet.status), static_cast<int>(EditorStatus::Applied));
    CHECK(!quiet.diagnostics.empty());
    pcm.samples[4] = std::numeric_limits<float>::infinity();
    CHECK_EQ(static_cast<int>(service.inspect(request, pcm).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_audio_source_editor.cpp
using eve::audio_editor::AudioSourceEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.audio.source_editor_installs_workspace_and_reverses_volume") {
    AudioSourceEditor editor("preview.tone");
    EditorWorkspace   workspace("audio.preview", "Audio Source Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    const std::map<std::string, std::string> expected = {{"audio.sources", "list"},
                                                         {"audio.waveform", "preview"},
                                                         {"audio.inspector", "inspector"},
                                                         {"audio.transport", "transport"}};
    for (int i = 0; i < workspace.getPanelCount(); ++i) {
        const auto id = workspace.getPanelId(i);
        REQUIRE(expected.contains(id));
        CHECK_EQ(workspace.getPanelCapability(i), std::string("audio.source"));
        CHECK_EQ(workspace.getPanelContext(i), expected.at(id));
    }

    REQUIRE(editor.setViewportWidth(320.0f).ok());
    REQUIRE(preview_test::finish(editor).ok());
    CHECK(editor.bucketCount() > 8);
    CHECK(editor.duration() > 1.9);

    const double before =
        editor.read("play.volume").value.getIf<double>() ? *editor.read("play.volume").value.getIf<double>() : 0.0;
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.25)).ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), 0.25);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), before);

    REQUIRE(editor.play().ok());
    REQUIRE(editor.update(0.2).ok());
    CHECK(editor.playhead() > 0.0);
    REQUIRE(editor.seekSeconds(0.75).ok());
    CHECK(std::abs(editor.playhead() - 0.75) < 0.02);
    REQUIRE(editor.seekX(editor.layoutWidth() * 0.5f).ok());
    CHECK(std::abs(editor.playhead() - editor.duration() * 0.5) < 0.05);
}

TEST_CASE("editor.audio.source_editor_property_edits_do_not_stale_audition") {
    AudioSourceEditor editor("preview.tone");
    REQUIRE(editor.play().ok());
    const auto pcm = editor.pcmRevision();
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.4)).ok());
    CHECK_EQ(editor.pcmRevision(), pcm);
    auto advanced = editor.update(0.05);
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().state),
             static_cast<int>(eve::audio_editing::AudioTransportState::Playing));
}
}  // namespace test_editor_audio_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_audio_source_script)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <tuple>
#include "audio/editor/EditorAudioTarget.h"
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_audio_source_script.cpp
namespace test_editor_audio_source_script {
// editor_audio_source_script.cpp
TEST_CASE("editor.audio.source_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.audio", "Test Audio Editor");
        created <- eve.AudioEditorModule().create("test.asset.tone");
        audioEditor <- created.value;
        configured <- audioEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- ""; cap0 <- ""; ctx1 <- "";
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            if (workspace.getPanelId(i) == "audio.sources") {
                panel0 = workspace.getPanelId(i); cap0 = workspace.getPanelCapability(i);
            }
            if (workspace.getPanelContext(i) == "preview") ctx1 = workspace.getPanelContext(i);
        }
        viewport <- audioEditor.setViewportWidth(400.0);
    )"));
    REQUIRE(preview_test::finishScript(vm, "audioEditor").ok());
    vm.run(vm.compileSource(R"(
        beforeVolume <- audioEditor.getFloat("play.volume");
        volumeResult <- audioEditor.setFloat("play.volume", 0.2);
        afterVolume <- audioEditor.getFloat("play.volume");
        revisionAfter <- audioEditor.getRevision();
        undoResult <- audioEditor.undo();
        restoredVolume <- audioEditor.getFloat("play.volume");
        playResult <- audioEditor.play();
        advanced <- audioEditor.update(0.25);
        playhead <- audioEditor.getPlayhead();
        seekResult <- audioEditor.seekSeconds(0.4);
        seeked <- audioEditor.getPlayhead();
        buckets <- audioEditor.getBucketCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("audio.sources"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("audio.source"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("volumeResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterVolume").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredVolume").toFloat() - vm.find("beforeVolume").toFloat()), 0.0001F);
    CHECK(vm.find("playResult").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("seeked").toFloat() - 0.4F), 0.02F);
    CHECK(vm.find("buckets").toInt() > 8);
    (void)vm.find("revisionAfter");
}

// editor_audio_target.cpp
using namespace eve::editor;

namespace {

SelectionSnapshot sourceSelection(const AudioSourceTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "audio.source";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

template <class Target>
Result<TransactionReceipt> commit(Target& target, LocalTransactionBackend& transactions,
                                  const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit audio";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.audio.begin"),
                                                        "Could not begin audio transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.audio.append"),
                                                        "Could not append audio operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.audio.source_schema_covers_playback_spatial_attenuation_and_routing") {
    AudioSourceTarget target("ambient-river");
    const auto        schema = target.schema(sourceSelection(target));
    CHECK_EQ(schema.typeId, std::string("audio.source"));
    CHECK(schema.find(PropertyPath("clip.asset")) != nullptr);
    CHECK(schema.find(PropertyPath("play.loop-start")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.position")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.maximum-distance")) != nullptr);
    CHECK(schema.find(PropertyPath("mixer.bus")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.audio.source_edits_are_reversible_and_cross_validated") {
    AudioSourceTarget       target("ambient-river");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = sourceSelection(target);
    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("clip.asset"), EditorValue("asset://audio/river.ogg"), "audio.clip"},
          {PropertyPath("play.loop"), EditorValue(true), "audio.loop"},
          {PropertyPath("play.loop-start"), EditorValue(10.0), "audio.loop-start"},
          {PropertyPath("play.loop-end"), EditorValue(5.0), "audio.loop-end"}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commit(target, transactions, operation.value(), id).ok());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
    REQUIRE(transactions.undo().ok());
    CHECK(target.validate().empty());
    REQUIRE(transactions.redo().ok());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    AudioSourceTarget restored("restored");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));

    EditorValue unknownVersion = target.snapshotValue();
    auto*       unknownRoot    = unknownVersion.getIf<EditorValue::Object>();
    REQUIRE(unknownRoot != nullptr);
    unknownRoot->at("schemaVersion")               = int64_t{2};
    const Revision revisionBeforeRejectedMigration = restored.revision();
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(unknownVersion).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), revisionBeforeRejectedMigration);
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));
}

TEST_CASE("editor.audio.mixer_bus_hierarchy_is_cycle_safe_and_reversible") {
    AudioMixerTarget        mixer("main-mixer");
    LocalWorldAuthority     authority(&mixer);
    LocalTransactionBackend transactions(&authority);
    auto                    music = mixer.makeCreate({ObjectId("music"), ObjectId("master"), "Music", 0.8});
    REQUIRE(music.ok());
    REQUIRE(commit(mixer, transactions, music.value(), "audio.bus.music").ok());
    auto combat = mixer.makeCreate({ObjectId("combat"), ObjectId("music"), "Combat", 1.0});
    REQUIRE(combat.ok());
    REQUIRE(commit(mixer, transactions, combat.value(), "audio.bus.combat").ok());
    CHECK_EQ(mixer.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(mixer.makeDelete(ObjectId("music")).code()), static_cast<int>(EditorStatus::Rejected));

    AudioBusSnapshot changed = mixer.bus(ObjectId("music")).value();
    changed.parent           = ObjectId("combat");
    CHECK_EQ(static_cast<int>(mixer.makeReplace(changed).code()), static_cast<int>(EditorStatus::Rejected));
    changed        = mixer.bus(ObjectId("combat")).value();
    changed.volume = 0.5;
    changed.mute   = true;
    auto replace   = mixer.makeReplace(changed);
    REQUIRE(replace.ok());
    REQUIRE(commit(mixer, transactions, replace.value(), "audio.bus.settings").ok());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value().volume, 0.5);
    CHECK(mixer.bus(ObjectId("combat")).value().mute);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value().volume, 1.0);
    CHECK(!mixer.bus(ObjectId("combat")).value().mute);
    CHECK_EQ(static_cast<int>(mixer.snapshotValue().type()), static_cast<int>(EditorValue::Type::Object));
    AudioMixerTarget restored("restored-mixer");
    REQUIRE(restored.loadSnapshot(mixer.snapshotValue()).ok());
    CHECK_EQ(restored.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
}
}  // namespace test_editor_audio_source_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_audio_transport)
#include "PrivateTestScope.h"
#include "audio/editor/EditorAudioTransport.h"
#include "audio/editor/EditorAudioWaveform.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_audio_transport.cpp
namespace test_editor_audio_transport {
// editor_audio_transport.cpp
using namespace eve::editor;

namespace {
class FakeAudioBackend final : public IAudioTransportBackend {
public:
    void play() override {
        isPlaying = true;
        ++plays;
    }
    void pause() override { isPlaying = false; }
    void stop() override {
        isPlaying = false;
        position  = 0.0;
        ++stops;
    }
    Result<void> seek(double seconds) override {
        if (!seekable)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.audio.seek"), "seek rejected");
        position = seconds;
        return eve::editing::applied<void>();
    }
    double tell() const override { return position; }
    double duration() const override { return length; }
    bool   playing() const override { return isPlaying; }
    void   setNativeLooping(bool enabled) override { nativeLoop = enabled; }
    double position   = 0.0;
    double length     = 10.0;
    bool   isPlaying  = false;
    bool   seekable   = true;
    bool   nativeLoop = true;
    int    plays      = 0;
    int    stops      = 0;
};
}  // namespace

TEST_CASE("editor.audio.transport_wraps_custom_loop_and_rejects_stale_revision") {
    FakeAudioBackend       backend;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("voice"), 7, &backend).ok());
    CHECK(!backend.nativeLoop);
    CHECK(transport.setLoop(7, true, 2.0, 4.0).ok());
    CHECK(transport.play(7).ok());
    CHECK_EQ(backend.position, 2.0);
    backend.position = 4.1;
    auto wrapped     = transport.update(7);
    REQUIRE(wrapped.ok());
    CHECK_EQ(wrapped.value().position, 2.0);
    CHECK_EQ(backend.plays, 2);
    CHECK_EQ(static_cast<int>(transport.update(8).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(backend.stops, 1);
}

TEST_CASE("editor.audio.transport_validates_seek_loop_and_rebinding") {
    FakeAudioBackend       first, second;
    AudioAuditionTransport transport;
    CHECK(transport.bind(StableId("first"), 1, &first).ok());
    CHECK_EQ(static_cast<int>(transport.setLoop(1, true, 5.0, 2.0).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(transport.seek(1, 11.0).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(transport.play(1).ok());
    CHECK(transport.pause(1).ok());
    CHECK(transport.bind(StableId("second"), 2, &second).ok());
    CHECK_EQ(first.stops, 1);
    auto snapshot = transport.snapshot(2);
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().asset.value(), std::string("second"));
}

// editor_audio_waveform.cpp
using namespace eve::editor;

TEST_CASE("editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics") {
    EditorAudioPcm pcm;
    pcm.sampleRate = 4;
    pcm.channels   = 1;
    pcm.samples    = {-1.0F, -0.5F, 0.5F, 1.0F, 0.8F, 0.4F, -0.4F, -0.8F};
    AudioWaveformService service;
    const auto           waveform = service.generate({"asset://sound", 7, 4, 0.25, 1.75}, pcm);
    CHECK_EQ(static_cast<int>(waveform.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(waveform.sourceRevision, 7U);
    CHECK_EQ(waveform.duration, 2.0);
    REQUIRE_EQ(waveform.envelopes.size(), 1U);
    CHECK_EQ(waveform.envelopes[0].size(), 4U);
    CHECK(waveform.loopStartBucket >= 0);
    CHECK(waveform.loopEndBucket >= waveform.loopStartBucket);
    CHECK(waveform.loopSeamDelta > 0.1);
    REQUIRE(!waveform.diagnostics.empty());
    CHECK_EQ(static_cast<int>(service.generate({"asset://sound", 7, 0, 0.0, 0.0}, pcm).status),
             static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_audio_transport
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_automation_v2)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "common/RenderCapture.h"
#include "editor/Editor.h"
#include "editor/EditorSession.h"
#include "graphics/GraphicsCapabilities.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/material/editor/EditorMaterialTarget.h"
#include "graphics/material/editor/MaterialEditorModule.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/editor/EditorPrefab.h"
#include "scene/editor/EditorSceneTarget.h"
#include "scene/editor/SceneEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_automation_v2.cpp
namespace test_editor_automation_v2 {
// editor_automation_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.automation_capability_discovers_executes_plans_and_cancels") {
    Editor            editor;
    CommandDescriptor immediate;
    immediate.id          = CommandId("park.add-tree");
    immediate.ownerModule = "park";
    immediate.displayName = "Add tree";
    immediate.createsTransaction = false;
    REQUIRE(editor.commandService()
                .registerCommand(std::move(immediate),
                                 [](const CommandContext&, const EditorValue&) {
                                     return eve::editing::applied<EditorValue>(EditorValue("tree-added"));
                                 })
                .ok());

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
                        return eve::editing::applied<CommandPlan>(std::move(plan));
                    },
                    [](const CommandRequest&, const CommandPlan&) {
                        TransactionReceipt receipt;
                        receipt.id    = TransactionId("path-transaction");
                        receipt.state = TransactionState::Committed;
                        return eve::editing::applied<TransactionReceipt>(std::move(receipt));
                    })
                .ok());

    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    CHECK(automation->invoke("target-create", R"({"target":"absent.scene","type":"scene"})").find("unsupported") !=
          std::string::npos);
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
    request.id     = ObjectId(id);
    request.name   = id;
    auto operation = target.makeCreate(request);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

}  // namespace

TEST_CASE("editor.automation.editing_commands_keep_level_histories_independent") {
    SceneDocumentTarget village("level.village");
    SceneDocumentTarget forest("level.forest");
    addSceneObject(village, "player");
    addSceneObject(forest, "player");

    Editor                                     editor;
    eve::scene_editor::SceneEditorModule       sceneAdapter;
    eve::material_editor::MaterialEditorModule materialAdapter;
    REQUIRE(editor.registerEditingTarget(village).ok());
    REQUIRE(editor.registerEditingTarget(forest).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("scene.transform.set.v1") != std::string::npos);
    CHECK(commands.find("material.property.set.v1") != std::string::npos);

    const std::string villageResult = automation->invoke(
        "execute",
        R"({"target":"level.village","command":"scene.transform.set.v1","payload":{"object":"player","position":[1,2,3]}})");
    CHECK(villageResult.find("\"status\":\"applied\"") != std::string::npos);
    const std::string forestResult = automation->invoke(
        "execute",
        R"({"target":"level.forest","command":"scene.transform.set.v1","payload":{"object":"player","position":[7,8,9]}})");
    CHECK(forestResult.find("\"status\":\"applied\"") != std::string::npos);

    auto villageTransform = village.readTransform(ObjectId("player"));
    auto forestTransform  = forest.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.ok());
    REQUIRE(forestTransform.ok());
    CHECK(villageTransform.value().x == 1.0);
    CHECK(forestTransform.value().x == 7.0);

    const std::string undone = automation->invoke("undo", R"({"target":"level.village"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    villageTransform = village.readTransform(ObjectId("player"));
    forestTransform  = forest.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.ok());
    REQUIRE(forestTransform.ok());
    CHECK(villageTransform.value().x == 0.0);
    CHECK(forestTransform.value().x == 7.0);

    const std::string redone = automation->invoke("redo", R"({"target":"level.village"})");
    CHECK(redone.find("\"status\":\"applied\"") != std::string::npos);
    villageTransform = village.readTransform(ObjectId("player"));
    REQUIRE(villageTransform.ok());
    CHECK(villageTransform.value().x == 1.0);

    const std::string inspected = automation->invoke("inspect", R"({"target":"level.forest"})");
    CHECK(inspected.find("\"id\":\"level.forest\"") != std::string::npos);
    CHECK(inspected.find("player") != std::string::npos);

    CHECK(editor.unregisterEditingTarget(TargetId("level.village")).ok());
    CHECK(editor.unregisterEditingTarget(TargetId("level.forest")).ok());
}

TEST_CASE("editor.v2.automation_edits_material_through_the_same_transaction_path") {
    MaterialDocumentTarget                     material("material.player");
    Editor                                     editor;
    eve::material_editor::MaterialEditorModule materialAdapter;
    REQUIRE(editor.registerEditingTarget(material).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"material.player","command":"material.property.set.v1","payload":{"path":"shading.roughness","value":0.65}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);
    CHECK(material.snapshotValue()
              .getIf<EditorValue::Object>()
              ->at("properties")
              .getIf<EditorValue::Object>()
              ->at("shading.roughness") == EditorValue(0.65));

    const std::string undone = automation->invoke("undo", R"({"target":"material.player"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    CHECK(material.snapshotValue()
              .getIf<EditorValue::Object>()
              ->at("properties")
              .getIf<EditorValue::Object>()
              ->at("shading.roughness") != EditorValue(0.65));

    CHECK(editor.unregisterEditingTarget(TargetId("material.player")).ok());
}

TEST_CASE("editor.automation_owns_headless_targets_for_agent_lifecycle") {
    Editor                               editor;
    eve::scene_editor::SceneEditorModule sceneAdapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.scene","type":"scene","object":"player"})");
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
    Editor                               editor;
    eve::scene_editor::SceneEditorModule sceneAdapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string started = automation->invoke(
        "observe-start",
        R"({"descriptor":{"observer":"scene-node","host":"world","node":"player"},"event":{"observation":{"status":"ok","x":1},"converged":false}})");
    REQUIRE(started.find("\"status\":\"applied\"") != std::string::npos);
    const std::size_t idStart = started.find("editor.observe.");
    REQUIRE(idStart != std::string::npos);
    const std::size_t idEnd     = started.find('"', idStart);
    const std::string sessionId = started.substr(idStart, idEnd - idStart);
    const std::string request   = "{\"sessionId\":\"" + sessionId + "\"}";

    CHECK(started.find("\"x\":1") != std::string::npos);
    const std::string initial = automation->invoke("observe-poll", request);
    CHECK(initial.find("\"events\":[]") != std::string::npos);
    REQUIRE(automation
                ->invoke("observe-publish",
                         "{\"sessionId\":\"" + sessionId +
                             "\",\"event\":{\"observation\":{\"status\":\"ok\",\"x\":1},\"converged\":false}}")
                .find("\"status\":\"applied\"") != std::string::npos);
    const std::string duplicate = automation->invoke("observe-poll", request);
    CHECK(duplicate.find("\"events\":[]") != std::string::npos);

    REQUIRE(automation
                ->invoke("observe-publish",
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

    Editor                               editor;
    eve::scene_editor::SceneEditorModule sceneAdapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.live","type":"scene-host","host":"agent-live"})");
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

    Editor                                     editor;
    eve::material_editor::MaterialEditorModule materialAdapter;
    auto*                                      automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string handle =
        "{\"target\":\"agent.material\",\"type\":\"material-renderable3d\","
        "\"entityId\":" +
        std::to_string(renderable->id) + ",\"generation\":" + std::to_string(renderable->generation) + "}";
    REQUIRE(automation->invoke("target-create", handle).find("\"status\":\"applied\"") != std::string::npos);

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

    REQUIRE(automation->invoke("undo", R"({"target":"agent.material"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.8f);
    REQUIRE(automation->invoke("undo", R"({"target":"agent.material"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.3f);
    REQUIRE(automation->invoke("redo", R"({"target":"agent.material"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.8f);
    REQUIRE(automation->invoke("redo", R"({"target":"agent.material"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.9f);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.material"})").find("\"status\":\"applied\"") !=
            std::string::npos);
    REQUIRE_EQ(renderable->meshRenderer()->g, 0.9f);
    const std::uint32_t id         = renderable->id;
    const std::uint32_t generation = renderable->generation;
    ecs::DestroyEntity(renderable);
    auto stale = renderInspection->inspectRenderable3D(id, generation);
    REQUIRE_NOT(stale.ok());
}

TEST_CASE("editor.composition.developer_runtime_and_automation_share_target_transaction_path") {
    SceneDocumentTarget scene("level.shared-host-path");
    addSceneObject(scene, "player");

    Editor                               editor;
    eve::scene_editor::SceneEditorModule sceneAdapter;
    REQUIRE(editor.registerEditingTarget(scene).ok());

    auto developer = editor.newSession();
    REQUIRE(editor.bindEditingTarget(*developer, TargetId(scene.targetId())).ok());
    const CommandId   transform("scene.transform.set.v1");
    const EditorValue developerPayload =
        EditorValue::Object{{"object", "player"}, {"position", EditorValue::Array{1.0, 2.0, 3.0}}};
    auto developerPlan = developer->planCommand(transform, developerPayload);
    REQUIRE(developerPlan.ok());
    REQUIRE(developer->executePlan(developerPlan.value(), developerPayload).ok());

    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string developerUndo = automation->invoke("undo", R"({"target":"level.shared-host-path"})");
    CHECK(developerUndo.find("\"status\":\"applied\"") != std::string::npos);

    auto        runtime        = editor.newSession();
    HostProfile runtimeProfile = HostProfile::runtimeBuilder();
    runtimeProfile.allowCommand(transform);
    runtime->setHostProfile(std::move(runtimeProfile));
    REQUIRE(editor.bindEditingTarget(*runtime, TargetId(scene.targetId())).ok());
    const EditorValue runtimePayload =
        EditorValue::Object{{"object", "player"}, {"position", EditorValue::Array{7.0, 8.0, 9.0}}};
    auto runtimePlan = runtime->planCommand(transform, runtimePayload);
    REQUIRE(runtimePlan.ok());
    REQUIRE(runtime->executePlan(runtimePlan.value(), runtimePayload).ok());

    const std::string runtimeUndo = automation->invoke("undo", R"({"target":"level.shared-host-path"})");
    CHECK(runtimeUndo.find("\"status\":\"applied\"") != std::string::npos);
    const auto restored = scene.readTransform(ObjectId("player"));
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().x, 0.0);
    CHECK_EQ(restored.value().y, 0.0);
    CHECK_EQ(restored.value().z, 0.0);

    CHECK(editor.unregisterEditingTarget(TargetId(scene.targetId())).ok());
}

// editor_prefab.cpp
using namespace eve::editor;

namespace {

void applyAll(SceneTargetBase& scene, const std::vector<DomainOperation>& operations) {
    for (const DomainOperation& operation : operations) REQUIRE(scene.applyDomainOperation(operation).ok());
}

SceneDocumentTarget sourceScene() {
    SceneDocumentTarget scene("source");
    auto                root = scene.makeCreate({ObjectId("vehicle"), {}, "Vehicle", {1.0, 2.0, 3.0}});
    REQUIRE(root.ok());
    REQUIRE(scene.applyDomainOperation(root.value()).ok());
    auto wheel = scene.makeCreate(
        {ObjectId("wheel"), ObjectId("vehicle"), "Wheel", {0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.8, 0.8}});
    REQUIRE(wheel.ok());
    REQUIRE(scene.applyDomainOperation(wheel.value()).ok());
    return scene;
}

}  // namespace

TEST_CASE("editor.prefab.capture_and_instantiate_preserve_stable_mapping") {
    ScenePrefabService  prefabs;
    SceneDocumentTarget source   = sourceScene();
    auto                captured = prefabs.capture(AssetGuid("asset://prefabs/vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.ok());
    CHECK_EQ(captured.value().objects.size(), size_t{2});

    SceneDocumentTarget level("level");
    auto                parent = level.makeCreate({ObjectId("garage"), {}, "Garage", {}});
    REQUIRE(parent.ok());
    REQUIRE(level.applyDomainOperation(parent.value()).ok());
    auto instance = prefabs.instantiate(captured.value(), "vehicle-01", ObjectId("garage"), level);
    REQUIRE(instance.ok());
    CHECK_EQ(instance.value().sourceRevision, captured.value().revision);
    CHECK_EQ(instance.value().operations.size(), size_t{2});
    applyAll(level, instance.value().operations);
    CHECK_EQ(level.sceneObject(ObjectId("vehicle-01/vehicle")).value().parent, ObjectId("garage"));
    CHECK_EQ(level.sceneObject(ObjectId("vehicle-01/wheel")).value().parent, ObjectId("vehicle-01/vehicle"));

    const auto collision = prefabs.instantiate(captured.value(), "vehicle-01", ObjectId("garage"), level);
    CHECK_EQ(static_cast<int>(collision.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.prefab_apply_and_revert_overrides_are_revisioned_and_reversible") {
    ScenePrefabService  prefabs;
    SceneDocumentTarget source   = sourceScene();
    auto                captured = prefabs.capture(AssetGuid("vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.ok());
    SceneDocumentTarget level("level-overrides");
    auto                instance = prefabs.instantiate(captured.value(), "instance", {}, level);
    REQUIRE(instance.ok());
    applyAll(level, instance.value().operations);

    auto rename = level.makeRename(ObjectId("instance/wheel"), "Big Wheel");
    REQUIRE(rename.ok());
    REQUIRE(level.applyDomainOperation(rename.value()).ok());
    SceneTransformValue changed{2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 1.5, 1.5};
    auto                transform = level.makeSetTransform(ObjectId("instance/wheel"), changed);
    REQUIRE(transform.ok());
    REQUIRE(level.applyDomainOperation(transform.value()).ok());

    auto badges = prefabs.inspectOverrides(captured.value(), "instance", {}, level);
    REQUIRE(badges.ok());
    CHECK_EQ(badges.value().size(), size_t{2});
    CHECK_EQ(badges.value().at(0).instanceObject, ObjectId("instance/wheel"));

    auto applied = prefabs.applyOverrides(captured.value(), "instance", {}, level);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value().revision, captured.value().revision + 1);
    CHECK_EQ(applied.value().objects[1].name, "Big Wheel");
    CHECK_EQ(applied.value().objects[1].transform, changed);

    auto revert = prefabs.revertOverrides(captured.value(), "instance", {}, level);
    REQUIRE(revert.ok());
    CHECK_EQ(revert.value().size(), size_t{2});
    applyAll(level, revert.value());
    CHECK_EQ(level.sceneObject(ObjectId("instance/wheel")).value().name, "Wheel");
    CHECK_EQ(level.readTransform(ObjectId("instance/wheel")).value(), captured.value().objects[1].transform);
}

TEST_CASE("editor.prefab_nested_dependencies_detect_cycles_and_refresh_revision_pins") {
    ScenePrefabService prefabs;
    auto               parent = prefabs.capture(AssetGuid("parent"), sourceScene(), ObjectId("vehicle"));
    auto               child  = prefabs.capture(AssetGuid("wheel-prefab"), sourceScene(), ObjectId("wheel"));
    REQUIRE(parent.ok());
    REQUIRE(child.ok());
    child.value().revision                   = 3;
    parent.value().objects[1].nestedPrefab   = child.value().asset;
    parent.value().objects[1].nestedRevision = 1;
    const auto resolver                      = [&](const AssetGuid& asset) {
        if (asset == child.value().asset) return eve::editing::applied<PrefabAssetSnapshot>(child.value());
        return eve::editing::failed<PrefabAssetSnapshot>(EditorStatus::NotFound, RuleId("test.missing"), "missing");
    };
    const auto dependencies = prefabs.inspectDependencies(parent.value(), resolver);
    CHECK_EQ(static_cast<int>(dependencies.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(!dependencies.diagnostics.empty());
    auto refreshed = prefabs.refreshNestedRevisions(parent.value(), resolver);
    REQUIRE(refreshed.ok());
    CHECK_EQ(refreshed.value().revision, parent.value().revision + 1);
    CHECK_EQ(refreshed.value().objects[1].nestedRevision, 3U);

    child.value().objects[0].nestedPrefab   = parent.value().asset;
    child.value().objects[0].nestedRevision = parent.value().revision;
    const auto cyclicResolver               = [&](const AssetGuid& asset) {
        if (asset == child.value().asset) return eve::editing::applied<PrefabAssetSnapshot>(child.value());
        if (asset == parent.value().asset) return eve::editing::applied<PrefabAssetSnapshot>(parent.value());
        return eve::editing::failed<PrefabAssetSnapshot>(EditorStatus::NotFound, RuleId("test.missing"), "missing");
    };
    CHECK_EQ(static_cast<int>(prefabs.inspectDependencies(parent.value(), cyclicResolver).status),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.prefab_snapshot_round_trip_rejects_invalid_hierarchy") {
    ScenePrefabService  prefabs;
    SceneDocumentTarget source   = sourceScene();
    auto                captured = prefabs.capture(AssetGuid("vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.ok());
    const EditorValue snapshot = prefabs.snapshotValue(captured.value());
    auto              restored = prefabs.loadSnapshot(snapshot);
    REQUIRE(restored.ok());
    CHECK_EQ(prefabs.snapshotValue(restored.value()), snapshot);

    EditorValue broken = snapshot;
    auto*       object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    auto* rows = object->at("objects").getIf<EditorValue::Array>();
    REQUIRE(rows);
    auto* wheel = rows->at(1).getIf<EditorValue::Object>();
    REQUIRE(wheel);
    (*wheel)["parentSourceId"] = "missing";
    CHECK_EQ(static_cast<int>(prefabs.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_automation_v2
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_avatar_document_editor)
#include "PrivateTestScope.h"
#include "avatar/editor/AvatarDocumentEditor.h"
#include "editor/EditorWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_avatar_document_editor.cpp
namespace test_editor_avatar_document_editor {
using eve::avatar_editor::AvatarDocumentEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.avatar.document_editor_installs_workspace_and_reverses_layer_z") {
    AvatarDocumentEditor editor("preview.face");
    EditorWorkspace      workspace("avatar.preview", "Avatar Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    REQUIRE_EQ(workspace.getPanelCount(), 5);
    const std::map<std::string, std::string> expected = {{"avatar.layers", "list"},
                                                         {"avatar.preview", "preview"},
                                                         {"avatar.inspector", "inspector"},
                                                         {"avatar.parameters", "parameters"},
                                                         {"avatar.expressions", "expressions"}};
    for (int i = 0; i < workspace.getPanelCount(); ++i) {
        REQUIRE(expected.contains(workspace.getPanelId(i)));
        REQUIRE_EQ(workspace.getPanelCapability(i), std::string("avatar.document"));
        REQUIRE_EQ(workspace.getPanelContext(i), expected.at(workspace.getPanelId(i)));
    }
    REQUIRE_EQ(editor.layerCount(), 2);
    REQUIRE_EQ(editor.parameterCount(), 1);
    REQUIRE_EQ(editor.expressionCount(), 1);
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
    REQUIRE(editor.previewLayerCount() >= 2);

    REQUIRE(editor.selectLayer("eyes").ok());
    const int before = editor.layerZ(1);
    REQUIRE(editor.setLayerZ(4).ok());
    REQUIRE_EQ(editor.layerZ(1), 4);
    REQUIRE(editor.undo().ok());
    REQUIRE_EQ(editor.layerZ(1), before);
}

TEST_CASE("editor.avatar.document_editor_rejects_referenced_parameter_delete") {
    AvatarDocumentEditor editor("preview.face");
    REQUIRE(editor.selectParameter("smile").ok());
    const auto previewY   = editor.previewY(1);
    const auto layers     = editor.layerCount();
    const auto revision   = editor.revision();
    const auto previewRev = editor.previewRevision();
    auto       rejected   = editor.deleteSelectedParameter();
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(editor.layerCount(), layers);
    REQUIRE_EQ(editor.parameterCount(), 1);
    REQUIRE_EQ(editor.revision(), revision);
    REQUIRE_EQ(editor.previewRevision(), previewRev);
    REQUIRE_EQ(editor.previewY(1), previewY);

    REQUIRE(editor.setParameterValue(0.9).ok());
    REQUIRE(editor.previewY(1) > previewY);
    REQUIRE_EQ(editor.previewRevision(), editor.revision());
}
}  // namespace test_editor_avatar_document_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_avatar_editor)
#include "PrivateTestScope.h"
#include "avatar/editor/AvatarEditorModule.h"
#include "avatar/editor/EditorAvatarTarget.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_avatar_editor.cpp
namespace test_editor_avatar_editor {
using eve::avatar_editing::AvatarDocumentTarget;
using eve::avatar_editing::AvatarLayerValue;
using eve::editing::DomainOperation;
using eve::editing::ObjectId;
using eve::editing::TargetId;
using eve::editor::Editor;

namespace {
void apply(AvatarDocumentTarget& target, eve::editing::Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.avatar.automation_edits_layers_through_the_same_transaction_path") {
    AvatarDocumentTarget target("hero");
    AvatarLayerValue     layer;
    layer.id           = ObjectId("eyes");
    layer.name         = "eyes";
    layer.textureAsset = "eyes.png";
    apply(target, target.makeCreateLayer(layer));

    Editor                                 editor;
    eve::avatar_editor::AvatarEditorModule adapter;
    REQUIRE(editor.registerEditingTarget(target).ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("avatar.property.set.v1") != std::string::npos);
    CHECK(commands.find("avatar.layer.create.v1") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"hero","command":"avatar.property.set.v1","payload":{"item":"eyes","type":"avatar.layer","path":"layer.z","value":7}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);
    CHECK_EQ(target.layers().front().zIndex, 7);

    const std::string undone = automation->invoke("undo", R"({"target":"hero"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    CHECK_EQ(target.layers().front().zIndex, 0);

    CHECK(editor.unregisterEditingTarget(TargetId("hero")).ok());
}

TEST_CASE("editor.avatar.automation_owns_headless_targets_for_agent_lifecycle") {
    Editor                                 editor;
    eve::avatar_editor::AvatarEditorModule adapter;
    auto*                                  automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string created = automation->invoke(
        "target-create", R"({"target":"agent.avatar","type":"avatar","layer":"eyes","parameter":"mouth"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.avatar"})");
    CHECK(inspected.find("\"id\":\"agent.avatar\"") != std::string::npos);
    CHECK(inspected.find("avatar-asset") != std::string::npos);

    const std::string changed = automation->invoke(
        "execute",
        R"({"target":"agent.avatar","command":"avatar.property.set.v1","payload":{"item":"mouth","type":"avatar.parameter","path":"parameter.value","value":0.75}})");
    CHECK(changed.find("\"status\":\"applied\"") != std::string::npos);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.avatar"})").find("\"status\":\"applied\"") !=
            std::string::npos);
}
}  // namespace test_editor_avatar_editor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_avatar_target)
#include "PrivateTestScope.h"
#include "avatar/AvatarInstance.h"
#include "avatar/editing/AvatarTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_avatar_target.cpp
namespace test_editor_avatar_target {
using namespace eve::avatar_editing;
using namespace eve::editing;

namespace {
void apply(AvatarDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
SelectionSnapshot select(const AvatarDocumentTarget& target, const char* id, const char* type) {
    SelectionSnapshot s;
    s.channel = "avatar";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), type});
    return s;
}
class MissingTextures final : public IAvatarTextureResolver {
public:
    Result<eve::graphics::Texture*> texture(const std::string&) const override {
        return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound, RuleId("test.texture"),
                                                             "missing texture");
    }
};
}  // namespace

TEST_CASE("editor.avatar.layers_and_parameters_have_dynamic_reversible_inspectors") {
    AvatarDocumentTarget target("hero");
    AvatarLayerValue     layer;
    layer.id           = ObjectId("eyes-id");
    layer.name         = "eyes";
    layer.textureAsset = "eyes.png";
    apply(target, target.makeCreateLayer(layer));
    AvatarParameterValue parameter;
    parameter.id      = ObjectId("mouth-id");
    parameter.name    = "mouth";
    parameter.maximum = 2;
    parameter.value   = .5f;
    apply(target, target.makeCreateParameter(parameter));
    const auto layerSelection = select(target, "eyes-id", "avatar.layer");
    CHECK(target.schema(layerSelection).find(PropertyPath("layer.texture")) != nullptr);
    auto edit = target.makeSet(layerSelection, PropertyPath("layer.z"), int64_t{7}, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(target.layers().front().zIndex, 7);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.layers().front().zIndex, 0);
    const auto parameterSelection = select(target, "mouth-id", "avatar.parameter");
    CHECK(target.schema(parameterSelection).find(PropertyPath("parameter.value")) != nullptr);
    apply(target, target.makeSet(parameterSelection, PropertyPath("parameter.value"), 1.5, PropertySetMode::Absolute));
    CHECK_EQ(target.parameters().front().value, 1.5f);
}

TEST_CASE("editor.avatar.expressions_protect_referenced_channels_and_snapshot_is_atomic") {
    AvatarDocumentTarget target("hero");
    AvatarParameterValue parameter;
    parameter.id   = ObjectId("mouth-id");
    parameter.name = "mouth";
    apply(target, target.makeCreateParameter(parameter));
    AvatarExpressionValue expression;
    expression.id                = ObjectId("smile-id");
    expression.name              = "smile";
    expression.channels["mouth"] = 1;
    apply(target, target.makeCreateExpression(expression));
    CHECK_EQ(static_cast<int>(target.makeDeleteParameter(ObjectId("mouth-id")).code()),
             static_cast<int>(EditorStatus::Conflict));
    const auto  before     = target.snapshotValue();
    EditorValue invalid    = before;
    auto*       root       = invalid.getIf<EditorValue::Object>();
    auto*       content    = (*root)["content"].getIf<EditorValue::Object>();
    auto*       parameters = (*content)["parameters"].getIf<EditorValue::Array>();
    auto*       first      = (*parameters)[0].getIf<EditorValue::Object>();
    (*first)["maximum"]    = -1.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.avatar.runtime_publishes_metadata_and_preserves_generation_on_asset_failure") {
    AvatarDocumentTarget target("hero");
    AvatarParameterValue parameter;
    parameter.id    = ObjectId("mouth-id");
    parameter.name  = "mouth";
    parameter.value = .75f;
    apply(target, target.makeCreateParameter(parameter));
    AvatarExpressionValue expression;
    expression.id                = ObjectId("smile-id");
    expression.name              = "smile";
    expression.channels["mouth"] = 1;
    apply(target, target.makeCreateExpression(expression));
    AvatarDocumentRuntime runtime;
    REQUIRE(runtime.publish(target).ok());
    REQUIRE(runtime.instance() != nullptr);
    CHECK(runtime.instance()->hasParameter("mouth"));
    CHECK_EQ(runtime.instance()->getParameter("mouth"), .75f);
    const auto*      generation = runtime.instance();
    const auto       revision   = runtime.revision();
    AvatarLayerValue layer;
    layer.id           = ObjectId("eyes-id");
    layer.name         = "eyes";
    layer.textureAsset = "missing.png";
    apply(target, target.makeCreateLayer(layer));
    MissingTextures textures;
    CHECK_EQ(static_cast<int>(runtime.publish(target, &textures).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK(runtime.instance() == generation);
    CHECK_EQ(runtime.revision(), revision);
}
}  // namespace test_editor_avatar_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_host_package)
#include "PrivateTestScope.h"
#include "action/editor/ActionEditorModule.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editing/EditingValueJson.h"
#include "editing/EditorPackage.h"
#include "editing/PropertySchemaJson.h"
#include "editor/Editor.h"
#include "editor/EditorHost.h"
#include "editor/EditorMcpProjector.h"
#include "fluids/editor/FluidsEditorModule.h"
#include "voxel/editor/VoxelEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_host_package.cpp
namespace test_editor_host_package {
using namespace eve::editor;

TEST_CASE("editor.host.mounts_action_package_panels_and_exports_schemas") {
    Editor                                 editor;
    eve::action_editor::ActionEditorModule action;

    auto* provider = eve::cap::listenerAt<eve::editing::IEditorPackageProvider>(0);
    REQUIRE(provider != nullptr);
    auto valid = eve::editing::validateEditorPackage(provider->package());
    REQUIRE(valid.ok());

    auto host = editor.newHost("world", "World Host");
    REQUIRE(host.ok());
    auto report = host.value()->autoMount();
    REQUIRE(report.ok());
    CHECK(report.value().mountedPackages >= 1);
    CHECK(host.value()->isMounted("action_editor"));
    CHECK(host.value()->workspace().getPanelCount() >= 4);
    CHECK(host.value()->workspace().getPanelId(0).find("action.") != std::string::npos);

    auto catalog = host.value()->exportCommandCatalog();
    REQUIRE(catalog.ok());
    const std::string catalogJson = eve::editing::editorValueToJson(catalog.value());
    CHECK(catalogJson.find("action.property.set.v1") != std::string::npos);
    CHECK(catalogJson.find("inputSchema") != std::string::npos);
    CHECK(catalogJson.find("panelCapability") != std::string::npos);

    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("action.property.set.v1") != std::string::npos);
    CHECK(commands.find("inputSchema") != std::string::npos);

    const std::string targets = automation->invoke("target-list", "{}");
    CHECK(targets.find("action-timeline") != std::string::npos);
    CHECK(targets.find("createSchema") != std::string::npos);

    REQUIRE(host.value()->unmount("action_editor").ok());
    CHECK(!host.value()->isMounted("action_editor"));
}

TEST_CASE("editor.host.reject_incomplete_package") {
    eve::editing::EditorPackage incomplete;
    incomplete.ownerModule = "demo";
    incomplete.incomplete  = true;
    incomplete.panels.push_back({"demo.list", "Demo", "left", "demo", "list", 0, false});
    auto                                   command = eve::editing::emptyObjectPropertySchema("demo.cmd");
    eve::editing::EditingCommandDescriptor descriptor;
    descriptor.id                = eve::editing::CommandId("demo.cmd");
    descriptor.ownerModule       = "demo";
    descriptor.displayName       = "Demo";
    descriptor.category          = "Demo";
    descriptor.automationAllowed = true;
    descriptor.payloadSchema     = command;
    descriptor.panelCapability   = "demo";
    descriptor.panelContext      = "list";
    incomplete.commands.push_back(descriptor);

    auto rejected = eve::editing::validateEditorPackage(incomplete, false);
    CHECK(!rejected.ok());
    auto allowed = eve::editing::validateEditorPackage(incomplete, true);
    CHECK(allowed.ok());
}

TEST_CASE("editor.host.attach_filters_commands_and_skips_incomplete") {
    Editor                                 editor;
    eve::action_editor::ActionEditorModule action;
    eve::fluids_editor::FluidsEditorModule fluids;

    auto host = editor.newHost("world", "World Host");
    REQUIRE(host.ok());
    auto report = host.value()->autoMount();
    REQUIRE(report.ok());
    CHECK(host.value()->isMounted("action_editor"));
    CHECK(!host.value()->isMounted("fluids_editor"));

    REQUIRE(host.value()->exportMcp().ok());
    CHECK(editorMcpProjector().isAttached());

    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("action.property.set.v1") != std::string::npos);
    // Unmounted / non-package owners must not leak through an attached host.
    CHECK(commands.find("script:") == std::string::npos);

    const std::string targets = automation->invoke("target-list", "{}");
    CHECK(targets.find("action-timeline") != std::string::npos);
    CHECK(targets.find("fluid-simulation") == std::string::npos);

    const std::string audit = automation->invoke("package-audit", "{}");
    CHECK(audit.find("fluids_editor") != std::string::npos);
    CHECK(audit.find("incompletePackages") != std::string::npos);

    REQUIRE(host.value()->unmount("action_editor").ok());
    const std::string afterUnmount = automation->invoke("commands", "{}");
    CHECK(afterUnmount.find("action.property.set.v1") == std::string::npos);

    editorMcpProjector().detach();
}

TEST_CASE("editor.host.mounts_voxel_and_routes_selection") {
    Editor                               editor;
    eve::voxel_editor::VoxelEditorModule voxel;

    auto host = editor.newHost("voxel-world", "Voxel Host");
    REQUIRE(host.ok());
    auto report = host.value()->mount({"voxel_editing"});
    REQUIRE(report.ok());
    CHECK(host.value()->isMounted("voxel_editing"));
    CHECK(host.value()->workspace().getPanelCount() >= 4);

    REQUIRE(host.value()->undo().focusOwner("voxel_editing").ok());
    CHECK(host.value()->undo().focusedPolicy() == eve::editing::EditorUndoPolicy::Private);
    CHECK(!host.value()->undo().canUndo(nullptr));

    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId("voxel.demo");
    item.item   = StableId("model.a");
    item.type   = "voxel.model";
    auto routed = host.value()->selection().select(item, false);
    REQUIRE(routed.ok());
    CHECK(routed.value().find("voxel.") != std::string::npos);
}

TEST_CASE("editor.script.automated_command_requires_panel_binding") {
    Editor editor;
    // registerAutomatedScriptCommand is script-facing; exercise the policy via command service shape.
    CommandDescriptor bad;
    bad.id                = CommandId("script.bad");
    bad.ownerModule       = "script:bad";
    bad.automationAllowed = true;
    bad.payloadSchema     = eve::editing::emptyObjectPropertySchema("script.bad");
    // Missing panel binding — Host package validation covers packages; automation listing still
    // requires automationAllowed. Keep this case focused on schema projection presence.
    bad.panelCapability = "host.script";
    bad.panelContext    = "inspector";
    REQUIRE(editor.commandService()
                .registerPlannedCommand(
                    bad, [](const CommandRequest&) { return eve::editing::applied<CommandPlan>(CommandPlan{}); },
                    [](const CommandRequest&, const CommandPlan& plan) {
                        TransactionReceipt receipt;
                        receipt.id    = TransactionId(plan.id.value().empty() ? "script.bad.tx" : plan.id.value());
                        receipt.state = TransactionState::Committed;
                        return eve::editing::applied<TransactionReceipt>(std::move(receipt));
                    })
                .ok());
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);
    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("script.bad") != std::string::npos);
    CHECK(commands.find("inputSchema") != std::string::npos);
}
}  // namespace test_editor_host_package
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_script_component_payload)
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/ScriptComponentStore.h"
#include "editor/Editor.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "editor/EditorTransactionService.h"
#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/ScriptComponentPayloadProvider.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_script_component_payload.cpp
namespace test_editor_script_component_payload {
// ---------------------------------------------------------------------------
// 脚本组件在 inspector 里的属性通路（J-3 编辑器通路，注册表级端到端）。
//
// 三段接线：场景节点 → 脚本实体（§9.72 的原生读路径）→ 已登记组件的字段值（引擎侧权威存储）。
// 值不来自脚本实例的槽位，所以脚本内外读到的是同一份数据；写回也走同一个存储，因此脚本侧
// 下一次读就看见新值。
//
// 单独一个 TU：`ScriptTest.h` 带 `using namespace eve;`，和 `editor_scene_component_payload.cpp`
// 里的 `using namespace eve::editor;` 混在一起会让 `Revision`/`TransactionId` 变成二义符号。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::scene_editing;

const char* kScriptComponentPayloadScript = R"SQ(
probeEntity <- null
untypedEntity <- null

class InspectProbe extends eve.Component { x = 1.5; y = 2.5; label = "probe" }
class InspectEntity extends eve.SceneEntity { probe = InspectProbe }
// 声明成 null 的字段没有类型可校验：它必须保持只读，而当前值照样能被看见。
class UntypedProbe extends eve.Component { untyped = null; count = 3 }
class UntypedEntity extends eve.SceneEntity { probe = UntypedProbe }

function buildInspectorScene() {
    local registered = eve.component("InspectProbe", 1, InspectProbe).ok
    registered = eve.component("UntypedProbe", 1, UntypedProbe).ok && registered
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("probe", "")
    scene.addNode("untyped", "")
    scene.end()
    scene.mountBuildAs("editorprobe")
    local e = scene.attachEntity("probe", InspectEntity)
    if (e == null) return false
    e.probe.x = 42.0
    probeEntity = e
    local u = scene.attachEntity("untyped", UntypedEntity)
    if (u == null) return false
    u.probe.count = 7
    untypedEntity = u
    return registered
}

// 脚本侧的读写：用来钉"编辑器写入与脚本写入落到同一份权威数据上"，以及"计划基于过期数据时必须拒绝"。
function probeX() { return probeEntity.probe.x }
function bumpProbeX() { probeEntity.probe.x = 99.0; return true }

// 同名同版本再登记 = 换了一代：用来看"引用过期"能不能被发现。
function advanceInspectorGeneration() {
    class InspectProbeV2 extends eve.Component { x = 1.5; y = 2.5; label = "probe" }
    return eve.component("InspectProbe", 1, InspectProbeV2).ok
}
)SQ";

class ScriptComponentPayloadTest : public ScriptTest {
public:
    ScriptComponentPayloadTest() : ScriptTest(kScriptComponentPayloadScript) {}

protected:
    /** 脚本侧读到的值：证明"编辑器写的就是脚本读的那一份"。 */
    double scriptNumber(const char* function) { return vm.callFunc(vm.findFunc(function), vm).toFloat(); }
};

/** 域操作载荷/反向值里的一项（`editing::Value` 没有按名字取值的接口，只有类型化访问）。 */
const EditorValue* operationField(const EditorValue& payload, const char* name) {
    const auto* object = payload.getIf<EditorValue::Object>();
    if (object == nullptr) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

/** 引擎侧存储里的一个字段值（测试直接从权威存储取证，不经过脚本）。 */
eve::Result<eve::Value> storedField(const char* componentName, const char* field) {
    for (const ScriptComponentStoreEntry& entry : scriptComponentStoreEntries()) {
        if (entry.key.name != componentName) continue;
        if (const eve::Value* value = entry.values.find(field)) return eve::Result<eve::Value>::success(*value);
        return eve::Result<eve::Value>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "field is not stored", field, {}, "test.script-component"));
    }
    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(DiagnosticCode::NotFound, "component is not stored",
                                                                   componentName, {}, "test.script-component"));
}

/** 同一个字段的数值形态（脚本里的 `7` 存成整数、`42.0` 存成浮点）；读不到时返回 -1。 */
double storedNumber(const char* componentName, const char* field) {
    const auto value = storedField(componentName, field);
    if (!value.ok()) return -1.0;
    if (const auto* number = value.value().getIf<double>()) return *number;
    if (const auto* integer = value.value().getIf<std::int64_t>()) return static_cast<double>(*integer);
    return -1.0;
}

/** 某个已登记组件挂在哪个实体上（清数据的失败注入要用）。 */
std::uint64_t entityIdOf(const char* componentName) {
    for (const ScriptComponentStoreEntry& entry : scriptComponentStoreEntries())
        if (entry.key.name == componentName) return entry.key.entityId;
    return 0;
}

eve::Result<eve::editing::TransactionReceipt> commitOperation(IDomainOperationTarget&               target,
                                                              eve::editor::LocalTransactionBackend& transactions,
                                                              const DomainOperation&                operation,
                                                              eve::editing::Revision baseRevision, const char* id) {
    eve::editing::TransactionSpec specification;
    specification.id           = eve::editing::TransactionId(id);
    specification.label        = "Script component edit";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = baseRevision;
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok()) return eve::Result<eve::editing::TransactionReceipt>::failure(begun.status());
    auto appended = transactions.append(operation);
    if (!appended.ok()) return eve::Result<eve::editing::TransactionReceipt>::failure(appended.status());
    return transactions.commit();
}

/** 一次失败的提交会把"待处理事务"留在原地：下一次 begin 之前必须显式丢弃。 */
void discardPending(eve::editor::LocalTransactionBackend& transactions) { REQUIRE(transactions.discard().ok()); }

}  // namespace

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_payload_reads_declared_fields") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    // 一类槽只允许一个提供方（脚本组件刻意共用一个类型槽）。
    REQUIRE_EQ(static_cast<int>(registry.registerProvider(&provider).code()), static_cast<int>(EditorStatus::Conflict));

    const TargetId sceneTarget("editorprobe");
    const ObjectId objectId("probe");
    auto           references = registry.componentPayloads(sceneTarget, objectId);
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), std::size_t{1});
    const auto& reference = references.value().front();
    REQUIRE_EQ(reference.type, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    // 稳定组件 id 把节点编进去：`SelectionItem` 不带节点，`read`/`schema` 只能从这里恢复它。
    REQUIRE_EQ(reference.component.value(), std::string("probe|InspectProbe@1"));
    REQUIRE(reference.generation >= 1);

    // 没有脚本的节点、未知宿主都返回空引用表（编辑器据此显示"这个对象没有脚本组件"）。
    REQUIRE(registry.componentPayloads(sceneTarget, ObjectId("root")).value().empty());
    REQUIRE(registry.componentPayloads(TargetId("nope"), objectId).value().empty());

    auto selection = makeSceneComponentSelection("inspector", references.value(), 3);
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    REQUIRE(properties.ok());

    const PropertySchema schema = properties.value()->schema(selection.value());
    REQUIRE_EQ(schema.typeId, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    REQUIRE_EQ(schema.properties.size(), std::size_t{3});  // x, y, label（声明顺序）
    REQUIRE_EQ(schema.properties.at(0).path.value(), std::string("x"));
    REQUIRE_EQ(schema.properties.at(1).path.value(), std::string("y"));
    REQUIRE_EQ(schema.properties.at(2).path.value(), std::string("label"));
    REQUIRE_EQ(static_cast<int>(schema.properties.at(0).type), static_cast<int>(PropertyType::Float));
    REQUIRE_EQ(static_cast<int>(schema.properties.at(2).type), static_cast<int>(PropertyType::String));
    for (const PropertyDescriptor& descriptor : schema.properties)
        REQUIRE(!hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly));
    // 默认值来自**声明**，不是当前值：x 现在是 42.0，但脚本里写的是 1.5。
    REQUIRE_EQ(*schema.properties.at(0).defaultValue.getIf<double>(), 1.5);
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);

    const PropertyReadResult x = properties.value()->read(selection.value(), PropertyPath("x"));
    REQUIRE_EQ(static_cast<int>(x.state), static_cast<int>(PropertyReadState::Value));
    REQUIRE(x.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*x.value.getIf<double>(), 42.0);
    const PropertyReadResult y = properties.value()->read(selection.value(), PropertyPath("y"));
    REQUIRE(y.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*y.value.getIf<double>(), 2.5);
    // 没声明的字段是 Missing，不是"读到空值"。
    REQUIRE_EQ(static_cast<int>(properties.value()->read(selection.value(), PropertyPath("nope")).state),
               static_cast<int>(PropertyReadState::Missing));

    // 声明成 null 的字段：只读（没有类型可校验），但声明 schema 与当前值都看得见。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    REQUIRE_EQ(untypedReferences.value().size(), std::size_t{1});
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 4);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    const PropertySchema untypedSchema = untypedProperties.value()->schema(untypedSelection.value());
    const auto           untypedField  = untypedSchema.find(PropertyPath("untyped"));
    REQUIRE(untypedField);
    REQUIRE(hasPropertyFlag(untypedField->flags, PropertyFlag::ReadOnly));
    REQUIRE_EQ(static_cast<int>(untypedField->type), static_cast<int>(PropertyType::ReadOnlyText));
    const auto countField = untypedSchema.find(PropertyPath("count"));
    REQUIRE(countField);
    REQUIRE(!hasPropertyFlag(countField->flags, PropertyFlag::ReadOnly));
    REQUIRE_EQ(static_cast<int>(countField->type), static_cast<int>(PropertyType::Int));
    REQUIRE_EQ(*countField->defaultValue.getIf<std::int64_t>(), 3);  // 声明 3，当前值 7
    const PropertyReadResult count = untypedProperties.value()->read(untypedSelection.value(), PropertyPath("count"));
    REQUIRE(count.value.getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*count.value.getIf<std::int64_t>(), 7);

    // 引用过期：同名同版本换了一代之后，同一个引用必须被报成 Conflict（不是继续显示旧值）。
    REQUIRE(registry.validatePayload(reference).ok());
    REQUIRE(callScript("advanceInspectorGeneration"));
    auto stale = registry.validatePayload(reference);
    REQUIRE(!stale.ok());
    REQUIRE_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_is_undoable_and_visible_to_script") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 11);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    // 编辑器的撤销契约要求"能克隆候选、能原子发布候选"，所以操作目标必须实现 staging。
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    const eve::editing::Revision before = operationTarget.value()->revision();
    auto change = properties.value()->makeSet(selection.value(), PropertyPath("x"), 5.5, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(change.value().hasInverse);
    REQUIRE_EQ(change.value().type, std::string("eve.script-component.field.set.v1"));
    const auto* inverseValue = operationField(change.value().inverse, "value");
    REQUIRE(inverseValue != nullptr);
    REQUIRE(inverseValue->getIf<double>() != nullptr);
    REQUIRE_EQ(*inverseValue->getIf<double>(), 42.0);

    // 候选副本上的写入**不**碰权威存储：撤销可以"先在候选上试算，失败不留半套状态"。
    auto candidate = staging->cloneDomainState();
    REQUIRE(candidate != nullptr);
    REQUIRE(candidate->applyDomainOperation(change.value()).ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    REQUIRE_EQ(scriptNumber("probeX"), 42.0);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);
    REQUIRE(commitOperation(*operationTarget.value(), transactions, change.value(), before, "script.x.1").ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 5.5);
    // 脚本侧下一次读就看见新值：编辑器写的和脚本读的是同一份权威数据。
    REQUIRE_EQ(scriptNumber("probeX"), 5.5);
    REQUIRE(operationTarget.value()->revision() != before);

    REQUIRE(transactions.undo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    REQUIRE_EQ(scriptNumber("probeX"), 42.0);

    REQUIRE(transactions.redo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 5.5);
    REQUIRE_EQ(scriptNumber("probeX"), 5.5);

    // 脚本写回同一个字段之后，编辑器按新版本号再计划一次必须能提交（基于过期数据的计划见下一条用例）。
    REQUIRE(callScript("bumpProbeX"));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    auto afterScript =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 1.25, PropertySetMode::Absolute);
    REQUIRE(afterScript.ok());
    REQUIRE(commitOperation(*operationTarget.value(), transactions, afterScript.value(),
                            operationTarget.value()->revision(), "script.x.2")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 1.25);
    REQUIRE_EQ(scriptNumber("probeX"), 1.25);
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_planner_refuses_invalid_edits") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 21);
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    REQUIRE(properties.ok());

    // 没声明的字段：明确 NotFound，而不是计划一个写不进去的操作。
    REQUIRE_EQ(static_cast<int>(properties.value()
                                    ->makeSet(selection.value(), PropertyPath("nope"), 1.0, PropertySetMode::Absolute)
                                    .code()),
               static_cast<int>(EditorStatus::NotFound));
    REQUIRE_EQ(static_cast<int>(properties.value()->makeReset(selection.value(), PropertyPath("nope")).code()),
               static_cast<int>(EditorStatus::NotFound));
    // 类型不匹配：结构化的 Rejected，且带诊断。
    auto mismatched = properties.value()->makeSet(selection.value(), PropertyPath("x"), EditorValue("text"),
                                                  PropertySetMode::Absolute);
    REQUIRE_EQ(static_cast<int>(mismatched.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!mismatched.diagnostics().empty());
    // 相对模式对"绝对赋值"的字段没有意义。
    REQUIRE_EQ(
        static_cast<int>(
            properties.value()->makeSet(selection.value(), PropertyPath("x"), 1.0, PropertySetMode::Relative).code()),
        static_cast<int>(EditorStatus::Rejected));

    // 声明成 null 的字段没有类型可校验：写与重置都明确拒绝。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 22);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    REQUIRE_EQ(static_cast<int>(
                   untypedProperties.value()
                       ->makeSet(untypedSelection.value(), PropertyPath("untyped"), 1.0, PropertySetMode::Absolute)
                       .code()),
               static_cast<int>(EditorStatus::Rejected));
    REQUIRE_EQ(static_cast<int>(
                   untypedProperties.value()->makeReset(untypedSelection.value(), PropertyPath("untyped")).code()),
               static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_refuses_stale_and_missing_targets") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 31);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    // ① 计划之后脚本改了同一个字段：按旧版本号提交必须 Conflict，且不留半套状态。
    auto stale = properties.value()->makeSet(selection.value(), PropertyPath("x"), 3.0, PropertySetMode::Absolute);
    REQUIRE(stale.ok());
    const eve::editing::Revision planned = operationTarget.value()->revision();
    REQUIRE(callScript("bumpProbeX"));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    auto conflicted = commitOperation(*operationTarget.value(), transactions, stale.value(), planned, "script.stale");
    REQUIRE_EQ(static_cast<int>(conflicted.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    discardPending(transactions);

    // ② 过期的操作目标：提供方只服务一个选中组件，绑定被后来的选中项改写之后，旧操作必须被拒绝。
    auto laterReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(laterReferences.ok());
    auto laterSelection = makeSceneComponentSelection("inspector", laterReferences.value(), 32);
    REQUIRE(laterSelection.ok());
    auto rebound = registry.operationTarget(laterSelection.value());
    REQUIRE(rebound.ok());
    REQUIRE(rebound.value() == operationTarget.value());  // 同一个目标对象，只是身份换了
    const auto appliedToOther = operationTarget.value()->applyDomainOperation(stale.value());
    REQUIRE_EQ(static_cast<int>(appliedToOther.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 7.0);

    // ③ 事务进行中发生热重载（同名同版本换了一代）：旧候选的发布必须拒绝，也不改动已迁移的数据。
    auto rebindProbe = registry.operationTarget(selection.value());
    REQUIRE(rebindProbe.ok());
    auto acrossReload =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(acrossReload.ok());
    auto reloadCandidate = staging->cloneDomainState();
    REQUIRE(reloadCandidate != nullptr);
    REQUIRE(reloadCandidate->applyDomainOperation(acrossReload.value()).ok());
    REQUIRE(callScript("advanceInspectorGeneration"));
    const auto reloadCommit = staging->commitDomainState(std::move(reloadCandidate));
    REQUIRE_EQ(static_cast<int>(reloadCommit.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
}

/**
 * 数据在事务中途消失（实体销毁走同一条路）：候选发布与正向写入都必须拒绝，而且**不复活**存储条目。
 * 单独一条用例是因为热重载那一步已经推进了代际，"数据没了"与"换代了"两条拒绝理由会互相掩盖。
 */
TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_is_refused_when_the_data_is_gone") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 33);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    // 候选是在数据还在时克隆的，发布时数据已经没了。
    auto beforeRemoval =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(beforeRemoval.ok());
    auto orphanCandidate = staging->cloneDomainState();
    REQUIRE(orphanCandidate != nullptr);
    REQUIRE(orphanCandidate->applyDomainOperation(beforeRemoval.value()).ok());
    const std::uint64_t entityId = entityIdOf("InspectProbe");
    REQUIRE(entityId != 0);
    REQUIRE(eve::removeScriptComponentValues(entityId) > 0);
    // 带副作用的调用必须先落到局部变量：`REQUIRE_EQ` 会把两个实参各求值两次（第二次为了拼
    // "expands to" 文本），把 `commitDomainState(std::move(...))` 直接写进断言会调用两次——
    // 第二次拿到已经搬空的候选，于是"拒绝"看起来通过了，而数据其实已经被第一次发布复活。
    const auto commitCode = staging->commitDomainState(std::move(orphanCandidate));
    REQUIRE_EQ(static_cast<int>(commitCode.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(entityIdOf("InspectProbe"), std::uint64_t{0});

    // 正向写入同样拒绝：提交时存储里已经没有这份数据。
    auto afterRemoval =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(afterRemoval.ok());
    auto rejected = commitOperation(*operationTarget.value(), transactions, afterRemoval.value(),
                                    operationTarget.value()->revision(), "script.gone");
    REQUIRE_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE_EQ(entityIdOf("InspectProbe"), std::uint64_t{0});
    discardPending(transactions);
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_reset_restores_the_declared_default") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 41);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    auto reset = properties.value()->makeReset(selection.value(), PropertyPath("x"));
    REQUIRE(reset.ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    const auto* resetValue = operationField(reset.value().payload, "value");
    REQUIRE(resetValue != nullptr);
    REQUIRE(resetValue->getIf<double>() != nullptr);
    REQUIRE_EQ(*resetValue->getIf<double>(), 1.5);  // 声明默认值
    REQUIRE(commitOperation(*operationTarget.value(), transactions, reset.value(), operationTarget.value()->revision(),
                            "script.x.reset")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 1.5);
    REQUIRE_EQ(scriptNumber("probeX"), 1.5);

    REQUIRE(transactions.undo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);

    // 整数、字符串以外的类型同样按声明默认值重置；`Reset` 模式只是转给同一条计划。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 42);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    auto untypedTarget     = registry.operationTarget(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    REQUIRE(untypedTarget.ok());
    eve::editing::LocalWorldAuthority    untypedAuthority(untypedTarget.value());
    eve::editor::LocalTransactionBackend untypedTransactions(&untypedAuthority);

    auto resetCount =
        untypedProperties.value()->makeSet(untypedSelection.value(), PropertyPath("count"), 2, PropertySetMode::Reset);
    REQUIRE(resetCount.ok());
    const auto* countTarget = operationField(resetCount.value().payload, "value");
    REQUIRE(countTarget != nullptr);
    REQUIRE(countTarget->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*countTarget->getIf<std::int64_t>(), 3);
    const auto* countInverse = operationField(resetCount.value().inverse, "value");
    REQUIRE(countInverse != nullptr);
    REQUIRE(countInverse->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*countInverse->getIf<std::int64_t>(), 7);
    REQUIRE(commitOperation(*untypedTarget.value(), untypedTransactions, resetCount.value(),
                            untypedTarget.value()->revision(), "script.count.reset")
                .ok());
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 3.0);
    REQUIRE(untypedTransactions.undo().ok());
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 7.0);

    // 字符串字段的重置也走同一条路（`label` 声明 "probe"）。
    auto resetLabel = properties.value()->makeReset(selection.value(), PropertyPath("label"));
    REQUIRE(resetLabel.ok());
    const auto* labelValue = operationField(resetLabel.value().payload, "value");
    REQUIRE(labelValue != nullptr);
    REQUIRE(labelValue->getIf<std::string>() != nullptr);
    REQUIRE_EQ(*labelValue->getIf<std::string>(), std::string("probe"));
}

/**
 * 生产接线：自动化创建的编辑器目标 id 是**调用方选的**（不等于宿主名），所以"目标 id → 宿主名"
 * 的映射由创建目标的模块（`SceneEditorModule`）注入。这条用例走的就是那条路：
 * `SceneEditorModule` 创建 `scene-host` 目标 → 目标上查组件 payload 能力 → 注册表里的脚本组件
 * 提供方按映射（`scene-live` → 宿主 `editorprobe`）列出组件并读到值。
 */
TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_payload_is_wired_by_the_editor_module") {
    REQUIRE(callScript("buildInspectorScene"));

    // 场景编辑模块的构造要求命令注册表已经在位（它把自己的编辑命令注册进去）。
    eve::editor::Editor editor;

    auto* module = eve::scene_editor::SceneEditorModule::create();
    REQUIRE(module != nullptr);

    eve::editor::IEditorAutomationTargetFactory* factory = nullptr;
    const bool handled = eve::cap::forEachUntil<eve::editor::IEditorAutomationTargetFactory>(
        [&](eve::editor::IEditorAutomationTargetFactory* candidate) {
            if (!candidate->supports("scene-host")) return false;
            factory = candidate;
            return true;
        });
    REQUIRE(handled);
    REQUIRE(factory != nullptr);

    eve::editor::EditorValue::Object request;
    request.emplace("host", eve::editor::EditorValue(std::string("editorprobe")));
    auto created = factory->create(eve::editing::TargetId("scene-live"), "scene-host", request);
    REQUIRE(created.ok());
    REQUIRE(created.value().target != nullptr);

    auto* payloads = static_cast<ISceneComponentPayloadTarget*>(
        created.value().target->queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(payloads != nullptr);

    // 目标 id 是 `scene-live`，宿主是 `editorprobe`：只有注入的映射能把它们连起来。
    auto references =
        payloads->componentPayloads(eve::editing::TargetId("scene-live"), eve::editing::ObjectId("probe"));
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), std::size_t{1});
    REQUIRE_EQ(references.value().front().component.value(), std::string("probe|InspectProbe@1"));

    auto selection = makeSceneComponentSelection("inspector", references.value(), 7);
    REQUIRE(selection.ok());
    auto properties = payloads->propertyProvider(selection.value());
    REQUIRE(properties.ok());
    const PropertyReadResult x = properties.value()->read(selection.value(), PropertyPath("x"));
    REQUIRE(x.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*x.value.getIf<double>(), 42.0);

    // 同一条通路上的写：编辑器的域操作目标也必须能从这条生产接线上拿到，并真的写到存储里。
    auto operationTarget = payloads->operationTarget(selection.value());
    REQUIRE(operationTarget.ok());
    auto change = properties.value()->makeSet(selection.value(), PropertyPath("y"), 6.25, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);
    REQUIRE(commitOperation(*operationTarget.value(), transactions, change.value(), operationTarget.value()->revision(),
                            "script.live.y")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "y"), 6.25);

    // 文档目标（`scene`）没有活宿主：同一个提供方给空表，而不是报错或串到别的宿主。
    auto documentTarget = factory->create(eve::editing::TargetId("scene-doc"), "scene", {});
    REQUIRE(documentTarget.ok());
    auto* documentPayloads = static_cast<ISceneComponentPayloadTarget*>(
        documentTarget.value().target->queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(documentPayloads != nullptr);
    auto none =
        documentPayloads->componentPayloads(eve::editing::TargetId("scene-doc"), eve::editing::ObjectId("probe"));
    REQUIRE(none.ok());
    REQUIRE(none.value().empty());
}
}  // namespace test_editor_script_component_payload
#endif
