#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ability_asset) || \
    defined(EVE_TEST_ORIGIN_ability_controller) || defined(EVE_TEST_ORIGIN_ability_system)
#include "PrivateTestScope.h"
#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "action/AbilityRuntime.h"
#include "common/Diagnostic.h"
#include "zeroerr/unittest.h"

#include <optional>
#include <string>
#include <utility>

namespace action_ability_contract {
eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::AbilityDefinition ability(
    const char* name, eve::action::AbilityInstancingPolicy instancing = eve::action::AbilityInstancingPolicy::PerOwner,
    eve::action::AbilityActivationGroup group = eve::action::AbilityActivationGroup::Independent) {
    eve::action::AbilityDefinition result;
    result.id              = eve::LogicalId::fromParts("ability", name).value();
    result.action.id       = eve::LogicalId::fromParts("action", name).value();
    result.instancing      = instancing;
    result.activationGroup = group;
    return result;
}

// ability_asset.cpp
TEST_CASE("action.abilityAsset.roundTripPreservesIdentityTimingCostAndTriggers") {
    auto source          = ability("fire");
    source.action.timing = {eve::Duration::fromNanoseconds(100), eve::Duration::fromNanoseconds(200),
                            eve::Duration::fromNanoseconds(300)};
    source.cooldown      = eve::Duration::fromNanoseconds(400);
    auto cost            = eve::resource::CostSpec::from({{"mana", 12}});
    REQUIRE(cost.ok());
    source.action.cost      = cost.value();
    source.action.effectIds = {"effect:burn", "effect:impact"};
    source.triggers         = {{"Event.Combat.Cast", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(source.validate().ok());
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto decoded = eve::action::decodeAbilityAsset(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().id == source.id);
    CHECK(decoded.value().action.id == source.action.id);
    CHECK(decoded.value().action.timing.active == source.action.timing.active);
    CHECK(decoded.value().cooldown == source.cooldown);
    REQUIRE(decoded.value().action.cost.has_value());
    CHECK_EQ(decoded.value().action.cost->size(), std::size_t{1});
    CHECK(decoded.value().action.effectIds == source.action.effectIds);
    REQUIRE_EQ(decoded.value().triggers.size(), std::size_t{1});
    CHECK_EQ(decoded.value().triggers[0].gameplayTag, std::string("Event.Combat.Cast"));
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    CHECK(runtime.registerDefinition(std::move(decoded).takeValue()).ok());
}

TEST_CASE("action.abilityAsset.rejectsFutureVersionMalformedIdentityAndInvalidDuration") {
    auto source  = ability("fire");
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto future = encoded.value();
    future.set("schemaVersion", 999);
    CHECK(!eve::action::decodeAbilityAsset(future).ok());
    auto malformed = encoded.value();
    malformed.set("action", eve::Value::Object{});
    CHECK(!eve::action::decodeAbilityAsset(malformed).ok());
    source.cooldown = eve::Duration::fromNanoseconds(-1);
    CHECK(!source.validate().ok());
}

// ability_controller.cpp
struct ControllerFixture {
    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities{actions};
    eve::action::AbilityControllerRuntime controller{abilities};
    eve::action::AbilityDefinition        definition = ability("strike");
    eve::action::AbilityGrantId           grant;

    ControllerFixture() {
        REQUIRE(abilities.registerDefinition(definition).ok());
        auto created = abilities.grant("fighter", definition.id);
        REQUIRE(created.ok());
        grant = created.value();
    }

    eve::action::AbilityIntent intent() const {
        eve::action::AbilityIntent value;
        value.grantId          = grant;
        value.request.actionId = definition.action.id;
        return value;
    }
};

class ScriptedSource final : public eve::action::IAbilityIntentSource {
public:
    eve::Result<std::optional<eve::action::AbilityIntent>> nextIntent(eve::SimulationTick tick) override {
        seen = tick;
        if (fail)
            return eve::Result<std::optional<eve::action::AbilityIntent>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "source failed"));
        auto next = pending;
        pending.reset();
        return eve::Result<std::optional<eve::action::AbilityIntent>>::success(std::move(next));
    }
    bool                                      fail = false;
    eve::SimulationTick                       seen;
    std::optional<eve::action::AbilityIntent> pending;
};

TEST_CASE("action.controller.playerQueueConsumesOneIntentWithInjectedTick") {
    ControllerFixture                     f;
    eve::action::PlayerAbilityIntentQueue queue;
    auto                                  empty = f.controller.processNext(queue, eve::SimulationTick(3));
    REQUIRE(empty.ok());
    CHECK(!empty.value().has_value());
    REQUIRE(queue.enqueue(f.intent()).ok());
    CHECK_EQ(queue.pendingCount(), std::size_t{1});
    auto activated = f.controller.processNext(queue, eve::SimulationTick(17));
    REQUIRE(activated.ok());
    REQUIRE(activated.value().has_value());
    CHECK_EQ(queue.pendingCount(), std::size_t{0});
    auto* execution = f.actions.find(activated.value()->executionId);
    REQUIRE(execution != nullptr);
    CHECK(execution->request().requestedTick == eve::SimulationTick(17));
}

TEST_CASE("action.controller.sourceFailureAndRejectedIntentDoNotMutateAbilityState") {
    ControllerFixture f;
    ScriptedSource    source;
    source.pending = f.intent();
    source.fail    = true;
    CHECK(!f.controller.processNext(source, eve::SimulationTick(1)).ok());
    CHECK(source.pending.has_value());
    CHECK(f.abilities.activeActivations().empty());
    source.fail                      = false;
    source.pending->request.actionId = id("action:wrong");
    CHECK(!f.controller.processNext(source, eve::SimulationTick(2)).ok());
    CHECK(!source.pending.has_value());
    CHECK(f.abilities.activeActivations().empty());
}

// ability_system.cpp
TEST_CASE("action.ability.grantsPerOwnerIdentityAndDeterministicCooldown") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        definition = ability("dash");
    definition.cooldown                    = eve::Duration::fromNanoseconds(100);
    REQUIRE(runtime.registerDefinition(definition).ok());
    CHECK(!runtime.registerDefinition(definition).ok());
    auto grant = runtime.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    auto first = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!first.value().instanceId.isZero());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(2)).ok());
    REQUIRE(runtime.advanceCooldowns(eve::Duration::fromNanoseconds(99)).ok());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(3)).ok());
    REQUIRE(runtime.advanceCooldowns(eve::Duration::fromNanoseconds(1)).ok());
    CHECK(!runtime.activate(grant.value(), eve::SimulationTick(4)).ok());
    REQUIRE(runtime.cancel(first.value().id, eve::SimulationTick(5)).ok());
    auto second = runtime.activate(grant.value(), eve::SimulationTick(6));
    REQUIRE(second.ok());
    CHECK(second.value().instanceId == first.value().instanceId);
    CHECK(!runtime.advanceCooldowns(eve::Duration::fromNanoseconds(-1)).ok());
}

TEST_CASE("action.ability.hotReloadKeepsActiveSnapshotAndGrant") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        original = ability("strike", eve::action::AbilityInstancingPolicy::PerExecution);
    original.action.timing.active        = eve::Duration::fromNanoseconds(100);
    REQUIRE(runtime.registerDefinition(original).ok());
    auto grant = runtime.grant("fighter", original.id);
    REQUIRE(grant.ok());
    auto first = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    auto replacement                 = original;
    replacement.action.id            = id("action:strike-v2");
    replacement.action.timing.active = eve::Duration::fromNanoseconds(250);
    REQUIRE(runtime.replaceDefinition(replacement).ok());
    CHECK(actions.find(first.value().executionId)->definition().id == original.action.id);
    REQUIRE(runtime.cancel(first.value().id, eve::SimulationTick(2)).ok());
    auto second = runtime.activate(grant.value(), eve::SimulationTick(3));
    REQUIRE(second.ok());
    CHECK(actions.find(second.value().executionId)->definition().id == replacement.action.id);
    auto incompatible       = replacement;
    incompatible.instancing = eve::action::AbilityInstancingPolicy::PerOwner;
    CHECK(!runtime.replaceDefinition(incompatible).ok());
}

TEST_CASE("action.ability.exclusiveReplacementBlockingAndRevokeLifecycle") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        light = ability("light", eve::action::AbilityInstancingPolicy::PerExecution,
                                                eve::action::AbilityActivationGroup::ExclusiveReplaceable);
    auto                        heavy = ability("heavy", eve::action::AbilityInstancingPolicy::PerExecution,
                                                eve::action::AbilityActivationGroup::ExclusiveBlocking);
    REQUIRE(runtime.registerDefinition(light).ok());
    REQUIRE(runtime.registerDefinition(heavy).ok());
    auto lightGrant = runtime.grant("fighter", light.id);
    auto heavyGrant = runtime.grant("fighter", heavy.id);
    REQUIRE(lightGrant.ok());
    REQUIRE(heavyGrant.ok());
    auto first = runtime.activate(lightGrant.value(), eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!runtime.revoke(lightGrant.value()).ok());
    auto blocker = runtime.activate(heavyGrant.value(), eve::SimulationTick(2));
    REQUIRE(blocker.ok());
    CHECK(actions.find(first.value().executionId)->phase() == eve::action::ActionPhase::Cancelled);
    CHECK(!runtime.activate(lightGrant.value(), eve::SimulationTick(3)).ok());
    REQUIRE(runtime.cancel(blocker.value().id, eve::SimulationTick(4)).ok());
    REQUIRE(runtime.revoke(heavyGrant.value()).ok());
}

TEST_CASE("action.ability.eventTriggersAndStepReportUseCanonicalState") {
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime runtime(actions);
    auto                        definition = ability("react");
    definition.action.timing.active        = eve::Duration::fromNanoseconds(200);
    definition.triggers                    = {{"Event.Combat.Hit", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(runtime.registerDefinition(definition).ok());
    auto grant = runtime.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    auto matched = runtime.matchingGrants("fighter", "Event.Combat.Hit.Heavy");
    REQUIRE_EQ(matched.size(), std::size_t{1});
    CHECK(matched[0] == grant.value());
    CHECK(runtime.matchingGrants("other", "Event.Combat.Hit.Heavy").empty());
    auto activation = runtime.activate(grant.value(), eve::SimulationTick(1));
    REQUIRE(activation.ok());
    auto step = runtime.step(eve::SimulationTick(2), eve::Duration::fromNanoseconds(100));
    REQUIRE(step.ok());
    REQUIRE_EQ(step.value().advances.size(), std::size_t{1});
    CHECK(step.value().advances[0].activationId == activation.value().id);
    CHECK_EQ(step.value().activeCount, std::size_t{1});
    auto end = runtime.step(eve::SimulationTick(3), eve::Duration::fromNanoseconds(100));
    REQUIRE(end.ok());
    CHECK_EQ(end.value().completedCount, std::size_t{1});
    CHECK(runtime.activeActivations().empty());
}
}  // namespace action_ability_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_notify_registry)
#include "PrivateTestScope.h"
#include "action/ActionNotifyRegistry.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>

// action_notify_registry.cpp
namespace action_notify_contract {
eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

class RecordingHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& event,
                             const eve::action::ActionNotifyContext&) override {
        ++calls;
        lastType = event.type.format();
        return eve::Result<void>::success();
    }
    int         calls = 0;
    std::string lastType;
};

TEST_CASE("action.notify.builtinCatalogHasStableShapeAndRequiredFields") {
    auto built = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(built.ok());
    const auto descriptors = built.value().descriptors();
    REQUIRE_EQ(descriptors.size(), std::size_t{18});
    CHECK_EQ(descriptors.front().type, std::string("collision:ignore-window"));
    CHECK_EQ(descriptors.back().type, std::string("presentation:vfx-state"));
    auto damage = built.value().descriptor("combat:damage");
    REQUIRE(damage.ok());
    CHECK(damage.value().shape == eve::action::ActionNotifyShape::Instant);
    CHECK_EQ(damage.value().requiredPayloadFields.size(), std::size_t{2});
    auto hitbox = built.value().descriptor("combat:hitbox-window");
    REQUIRE(hitbox.ok());
    CHECK(hitbox.value().shape == eve::action::ActionNotifyShape::State);
}

TEST_CASE("action.notify.registrationRejectsInvalidAndDuplicateDescriptors") {
    eve::action::ActionNotifyRegistry registry;
    CHECK(!registry.registerDescriptor({"", "empty", "test", eve::action::ActionNotifyShape::Instant, {}}).ok());
    REQUIRE(
        registry
            .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
            .ok());
    CHECK(
        !registry
             .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
             .ok());
    CHECK(!registry.descriptor("test:missing").ok());
}

TEST_CASE("action.notify.handlerOwnershipAndDispatchAreExplicit") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(
        registry
            .registerDescriptor({"test:flash", "Flash", "test", eve::action::ActionNotifyShape::Instant, {"strength"}})
            .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("test:flash", handler).ok());
    CHECK(registry.hasHandler("test:flash"));
    eve::action::ActionTimelineEvent event{
        eve::action::ActionTimelineEventKind::Notify, id("test:track"),   id("test:notify"), id("test:flash"),
        eve::Duration::fromNanoseconds(10),           {{"strength", 1.0}}};
    CHECK(registry.validate(event).ok());
    eve::action::ActionNotifyContext context;
    CHECK(registry.dispatch(event, context).ok());
    CHECK_EQ(handler->calls, 1);
    CHECK_EQ(handler->lastType, std::string("test:flash"));
    REQUIRE(registry.unregisterHandler("test:flash").ok());
    CHECK(!registry.hasHandler("test:flash"));
}
}  // namespace action_notify_contract
#endif
