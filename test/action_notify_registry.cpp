#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPrefabBlock.h"
#include "action/ActionPrefabInstances.h"
#include "action/ActionSpatialBlock.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::action::ActionTimelineEvent event(eve::action::ActionTimelineEventKind kind, std::string_view type) {
    return {kind,     id("combat-track:gameplay"),        id("combat-notify:test"),
            id(type), eve::Duration::fromNanoseconds(10), {}};
}

class RecordingHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& incoming,
                             const eve::action::ActionNotifyContext& incomingContext) override {
        ++calls;
        lastType      = incoming.type.format();
        lastExecution = incomingContext.executionId;
        return eve::Result<void>::success();
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext&) override {
        ++updates;
        lastLocalTime = block.localTime;
        return eve::Result<void>::success();
    }

    eve::Result<void> sample(const eve::action::ActionActiveBlock&,
                             const eve::action::ActionNotifyContext& context) const override {
        if (!context.preview || !context.scrubbing)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "sample context is not preview", "context"));
        ++samples;
        return eve::Result<void>::success();
    }

    eve::Result<void> advance(const eve::action::ActionNotifyContext&) override {
        ++advances;
        if (failAdvance)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "advance failed", "handler"));
        return eve::Result<void>::success();
    }

    int                            calls = 0;
    int                            updates = 0;
    mutable int                    samples = 0;
    int                            advances = 0;
    bool                           failAdvance = false;
    std::string                    lastType;
    eve::action::ActionExecutionId lastExecution;
    eve::Duration                  lastLocalTime = eve::Duration::zero();
};

}  // namespace

TEST_CASE("actionNotifyRegistry.builtinsExposeStableEditorContracts") {
    CHECK(eve::cap::query<eve::action::IActionPrefabInstances>() == nullptr);
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const auto descriptors = registry.value().descriptors();
    REQUIRE_EQ(descriptors.size(), 13u);
    CHECK_EQ(descriptors.front().type, "collision:ignore-window");
    CHECK_EQ(descriptors.back().type, "presentation:vfx-state");

    auto damage = registry.value().descriptor("combat:damage");
    REQUIRE(damage.ok());
    CHECK(static_cast<int>(damage.value().shape) == static_cast<int>(eve::action::ActionNotifyShape::Instant));
    CHECK_EQ(damage.value().requiredPayloadFields.size(), 2u);

    auto hitbox = registry.value().descriptor("combat:hitbox-window");
    REQUIRE(hitbox.ok());
    CHECK(static_cast<int>(hitbox.value().shape) == static_cast<int>(eve::action::ActionNotifyShape::State));

    auto prefab = registry.value().descriptor("gameplay:prefab-spawn");
    REQUIRE(prefab.ok());
    CHECK_EQ(prefab.value().displayName, "Spawn Prefab");
    CHECK(static_cast<int>(prefab.value().shape) == static_cast<int>(eve::action::ActionNotifyShape::State));
}

TEST_CASE("entitySpatialResolver.absentProviderFailsObservably") {
    auto resolved = eve::resolveEntitySpatialPose({});
    CHECK(!resolved.ok());
    CHECK(static_cast<int>(resolved.status().code()) == static_cast<int>(eve::StatusCode::NotFound));
}

TEST_CASE("actionBlockRuntime.routesEnterUpdateExitAndSideEffectFreeSample") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(registry
                .registerDescriptor({"project:combat.window", "Window", "Project",
                                     eve::action::ActionNotifyShape::State, {}})
                .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:combat.window", handler).ok());
    eve::action::ActionBlockRuntime runtime(registry);

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId{7};
    advance.totalElapsed = eve::Duration::fromNanoseconds(25);
    advance.timelineEvents.push_back(event(eve::action::ActionTimelineEventKind::StateEnter,
                                           "project:combat.window"));
    advance.activeBlocks.push_back({id("combat-track:gameplay"), id("combat-notify:test"),
                                    id("project:combat.window"), eve::Duration::fromNanoseconds(15),
                                    eve::Duration::fromNanoseconds(40), {}});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    REQUIRE(runtime.apply(advance, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->updates, 1);
    CHECK_EQ(handler->advances, 1);
    CHECK_EQ(handler->lastLocalTime, eve::Duration::fromNanoseconds(15));
    CHECK_EQ(runtime.executionCount(), 1u);

    REQUIRE(runtime.sample(advance.activeBlocks, context).ok());
    CHECK_EQ(handler->samples, 1);
    CHECK_EQ(runtime.executionCount(), 1u);

    context.time = eve::Duration::fromNanoseconds(30);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(handler->calls, 2);
    CHECK_EQ(runtime.executionCount(), 0u);
    auto noOp = runtime.interrupt(context);
    REQUIRE(noOp.ok());
    CHECK_EQ(noOp.status().code(), eve::StatusCode::NoOp);
}

TEST_CASE("actionSpatialBlock.decodesSharedAttachmentContractTransactionally") {
    eve::Value::Object payload{
        {"attachment", "follow_position_only"},
        {"spatialTarget", "target"},
        {"targetIndex", 2},
        {"bone", "hand_r"},
        {"positionOffset", eve::Value::Array{1.0, 2, 3.5}},
        {"rotationOffsetDegrees", eve::Value::Array{0, 90.0, 0}},
        {"scale", eve::Value::Array{1.0, 2.0, 1.0}},
        {"uri", "asset://vfx/sword-trail"},
    };
    auto binding = eve::action::ActionSpatialBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().mode, eve::action::ActionSpatialAttachmentMode::FollowPositionOnly);
    CHECK_EQ(binding.value().target, eve::action::ActionSpatialTarget::Target);
    CHECK_EQ(binding.value().targetIndex, 2u);
    CHECK_EQ(binding.value().bone, "hand_r");
    CHECK_EQ(binding.value().positionOffset.z, 3.5);
    CHECK_EQ(binding.value().rotationOffsetDegrees.y, 90.0);
    CHECK_EQ(binding.value().scale.y, 2.0);

    payload["scale"] = eve::Value::Array{1.0, 0.0, 1.0};
    auto rejected = eve::action::ActionSpatialBinding::fromPayload(payload);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "scale");
}

TEST_CASE("actionPrefabBlock.validatesLifecycleDurationAndSpatialContract") {
    eve::Value::Object payload{
        {"uri", "asset://prefabs/sword-wave.glb"},
        {"lifecycle", "custom_duration"},
        {"customDurationSeconds", 1.25},
        {"attachment", "follow_target"},
        {"bone", "hand_r"},
        {"scale", eve::Value::Array{1.0, 2.0, 1.0}},
    };
    auto binding = eve::action::ActionPrefabSpawnBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().uri, "asset://prefabs/sword-wave.glb");
    CHECK_EQ(binding.value().lifecycle, eve::action::PrefabSpawnLifecycle::CustomDuration);
    CHECK_EQ(binding.value().customDuration, eve::Duration::fromNanoseconds(1250000000));
    CHECK_EQ(binding.value().spatial.bone, "hand_r");
    CHECK_EQ(binding.value().spatial.scale.y, 2.0);

    payload["customDurationSeconds"] = 0.0;
    auto rejected = eve::action::ActionPrefabSpawnBinding::fromPayload(payload);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "customDurationSeconds");

    payload["customDurationSeconds"] = 0.5;
    payload["lifecycle"] = "unknown";
    rejected = eve::action::ActionPrefabSpawnBinding::fromPayload(payload);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "lifecycle");
}

TEST_CASE("actionNotifyRegistry.validatesShapeAndRequiredPayload") {
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());

    auto gameplay = event(eve::action::ActionTimelineEventKind::Notify, "gameplay:event");
    CHECK(!registry.value().validate(gameplay).ok());
    gameplay.payload.emplace("tag", eve::Value("Combat.Action.Hit"));
    CHECK(registry.value().validate(gameplay).ok());

    auto hitbox = event(eve::action::ActionTimelineEventKind::StateEnter, "combat:hitbox-window");
    hitbox.payload.emplace("hitbox", eve::Value("weapon.main"));
    CHECK(registry.value().validate(hitbox).ok());
    hitbox.kind = eve::action::ActionTimelineEventKind::StateExit;
    CHECK(registry.value().validate(hitbox).ok());
    hitbox.kind = eve::action::ActionTimelineEventKind::Notify;
    CHECK(!registry.value().validate(hitbox).ok());

    auto prefab = event(eve::action::ActionTimelineEventKind::StateEnter, "gameplay:prefab-spawn");
    prefab.payload.emplace("uri", eve::Value("asset://prefabs/sword-wave.glb"));
    prefab.payload.emplace("lifecycle", eve::Value("custom_duration"));
    CHECK(!registry.value().validate(prefab).ok());
    prefab.payload.emplace("customDurationSeconds", eve::Value(0.5));
    CHECK(registry.value().validate(prefab).ok());
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{99};
    auto dispatched = registry.value().dispatch(prefab, context);
    CHECK(!dispatched.ok());
    CHECK_EQ(dispatched.status().code(), eve::StatusCode::NotFound);
}

TEST_CASE("actionNotifyRegistry.rejectsInvalidAndDuplicateDescriptors") {
    eve::action::ActionNotifyRegistry   registry;
    eve::action::ActionNotifyDescriptor descriptor{
        "project:combat.custom", "Custom", "Project", eve::action::ActionNotifyShape::Instant, {"value"}};
    CHECK(registry.registerDescriptor(descriptor).ok());
    CHECK(!registry.registerDescriptor(descriptor).ok());
    descriptor.type = "project..invalid";
    CHECK(!registry.registerDescriptor(std::move(descriptor)).ok());
}

TEST_CASE("actionNotifyRegistry.handlerLifecycleIsExplicitAndObservable") {
    eve::action::ActionNotifyRegistry   registry;
    eve::action::ActionNotifyDescriptor descriptor{
        "project:combat.custom", "Custom", "Project", eve::action::ActionNotifyShape::Instant, {}};
    REQUIRE(registry.registerDescriptor(std::move(descriptor)).ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:combat.custom", handler).ok());

    auto routed = event(eve::action::ActionTimelineEventKind::Notify, "project:combat.custom");
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{42};
    REQUIRE(registry.dispatch(routed, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->lastType, "project:combat.custom");
    CHECK_EQ(handler->lastExecution, context.executionId);

    REQUIRE(registry.unregisterHandler("project:combat.custom").ok());
    CHECK(!registry.dispatch(routed, context).ok());
    CHECK(!registry.unregisterHandler("project:combat.custom").ok());
}

TEST_CASE("actionNotifyRegistry.advancesSharedHandlerOnceAndPropagatesFailure") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(registry
                .registerDescriptor({"project:first", "First", "Project",
                                     eve::action::ActionNotifyShape::Instant, {}})
                .ok());
    REQUIRE(registry
                .registerDescriptor({"project:second", "Second", "Project",
                                     eve::action::ActionNotifyShape::Instant, {}})
                .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:first", handler).ok());
    REQUIRE(registry.registerHandler("project:second", handler).ok());

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{43};
    REQUIRE(registry.advanceHandlers(context).ok());
    CHECK_EQ(handler->advances, 1);

    handler->failAdvance = true;
    auto failed = registry.advanceHandlers(context);
    CHECK(!failed.ok());
    CHECK_EQ(failed.status().diagnostics().front().path(), "handler");
    CHECK_EQ(handler->advances, 2);
}
