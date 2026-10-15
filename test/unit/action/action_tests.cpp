#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ability_asset) || \
    defined(EVE_TEST_ORIGIN_ability_controller) || defined(EVE_TEST_ORIGIN_ability_system)
#include "PrivateTestScope.h"
#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "action/AbilitySystem.h"
#include "common/Diagnostic.h"
#include "condition/ConditionKeys.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: action_abilities
namespace test_action_abilities {
// ability_asset.cpp
namespace {
namespace ability_asset {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::AbilityDefinition complexAbility() {
    eve::action::AbilityDefinition result;
    result.id               = id("ability:fire-cone");
    result.action.id        = id("action:fire-cone");
    result.action.timing    = {eve::Duration::fromNanoseconds(100'000'000), eve::Duration::fromNanoseconds(200'000'000),
                               eve::Duration::fromNanoseconds(300'000'000)};
    result.action.condition = eve::condition::Condition::all(
        {eve::condition::tagPresent("State.Combat.Ready"),
         eve::condition::Condition::compare("mana", eve::condition::CompareOperator::GreaterEqual, 12),
         eve::condition::Condition::policyCall(
             "can_cast", eve::Value::Object{{"school", "fire"}},
             eve::condition::ScriptConditionDeclaration{
                 "can_cast", {"attributes.mana"}, eve::condition::DeterminismLevel::TickDeterministic})});
    result.action.targetingMode = eve::action::TargetingMode::Query;
    eve::sensing::TargetingSpec targeting;
    targeting.space        = eve::sensing::CoordinateSpace::World2D;
    targeting.domain       = eve::sensing::TargetDomain::Enemy;
    targeting.minCount     = 1;
    targeting.maxCount     = 3;
    targeting.maxRange     = 8.f;
    targeting.requiredTags = {"Unit.Alive"};
    targeting.lineOfSight  = eve::sensing::LineOfSightMode::Required;
    auto center            = eve::sensing::WorldPoint::world2D(2.f, 3.f);
    REQUIRE(center.ok());
    auto area = eve::sensing::WorldArea::circle2D(center.value(), 5.f);
    REQUIRE(area.ok());
    targeting.worldArea         = area.value();
    result.action.targetingSpec = targeting;
    auto cost                   = eve::resource::CostSpec::from({{"mana", 12}, {"stamina", 3}});
    REQUIRE(cost.ok());
    result.action.cost                    = cost.value();
    result.action.effectIds               = {"effect:burn", "effect:impact"};
    result.action.activeExecutionRequired = true;
    result.action.metadata                = {{"category", "spell"}, {"rank", 2}};
    eve::action::ActionTimeline timeline;
    timeline.actionId      = result.action.id;
    timeline.duration      = eve::Duration::fromNanoseconds(600'000'000);
    timeline.metadata      = {{"author", "test"}};
    result.action.timeline = std::move(timeline);
    result.cooldown        = eve::Duration::fromNanoseconds(1'500'000'000);
    result.instancing      = eve::action::AbilityInstancingPolicy::PerExecution;
    result.activationGroup = eve::action::AbilityActivationGroup::ExclusiveReplaceable;
    result.triggers        = {{"Event.Combat.Cast", eve::tags::GameplayTagMatch::IncludeDescendants}};
    REQUIRE(result.validate().ok());
    return result;
}

}  // namespace

TEST_CASE("abilityAsset.roundTripsCompleteDefinitionAndRegistersDecodedCandidate") {
    const auto source  = complexAbility();
    auto       encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto decoded = eve::action::decodeAbilityAsset(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().id, source.id);
    CHECK_EQ(decoded.value().action.id, source.action.id);
    CHECK_EQ(decoded.value().action.timing.active, source.action.timing.active);
    CHECK_EQ(decoded.value().action.condition.children().size(), 3u);
    REQUIRE(decoded.value().action.condition.children()[2].scriptDeclaration().has_value());
    CHECK_EQ(decoded.value().action.condition.children()[2].scriptDeclaration()->dependencies[0],
             std::string("attributes.mana"));
    REQUIRE(decoded.value().action.targetingSpec.has_value());
    CHECK(decoded.value().action.targetingSpec->worldArea.has_value());
    CHECK_EQ(decoded.value().action.targetingSpec->maxCount, 3u);
    REQUIRE(decoded.value().action.cost.has_value());
    CHECK_EQ(decoded.value().action.cost->size(), 2u);
    CHECK_EQ(decoded.value().action.effectIds, source.action.effectIds);
    REQUIRE(decoded.value().action.timeline.has_value());
    CHECK_EQ(decoded.value().action.timeline->duration, source.action.timeline->duration);
    CHECK_EQ(decoded.value().triggers.size(), 1u);

    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(std::move(decoded).takeValue()).ok());
}

TEST_CASE("abilityAsset.migratesV1MillisecondsAndRejectsFutureUnknownOrMalformedData") {
    auto source                 = complexAbility();
    source.action.targetingMode = eve::action::TargetingMode::None;
    source.action.targetingSpec.reset();
    source.action.condition = eve::condition::Condition{};
    source.action.cost.reset();
    source.action.timeline.reset();
    auto encoded = eve::action::encodeAbilityAsset(source);
    REQUIRE(encoded.ok());
    auto  legacy             = encoded.value();
    auto* root               = legacy.getIf<eve::Value::Object>();
    (*root)["schemaVersion"] = 1;
    (*root)["cooldownMs"]    = 1500;
    root->erase("cooldownNs");
    auto* action           = root->at("action").getIf<eve::Value::Object>();
    auto* timing           = action->at("timing").getIf<eve::Value::Object>();
    (*timing)["windupMs"]  = 100;
    (*timing)["activeMs"]  = 200;
    (*timing)["recoverMs"] = 300;
    timing->erase("windupNs");
    timing->erase("activeNs");
    timing->erase("recoverNs");
    auto migrated = eve::action::decodeAbilityAsset(legacy);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().cooldown.nanoseconds(), 1'500'000'000);
    CHECK_EQ(migrated.value().action.timing.active.nanoseconds(), 200'000'000);
    auto canonical = eve::action::encodeAbilityAsset(migrated.value());
    REQUIRE(canonical.ok());
    CHECK_EQ(canonical.value().getIf<eve::Value::Object>()->at("schemaVersion").asInt(), 2);

    auto future                                            = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = 3;
    CHECK(!eve::action::decodeAbilityAsset(future).ok());
    auto unknown                                       = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["surprise"] = true;
    CHECK(!eve::action::decodeAbilityAsset(unknown).ok());
    auto malformed                                         = encoded.value();
    (*malformed.getIf<eve::Value::Object>())["cooldownNs"] = -1;
    CHECK(!eve::action::decodeAbilityAsset(malformed).ok());
    auto ambiguous                                         = encoded.value();
    (*ambiguous.getIf<eve::Value::Object>())["cooldownMs"] = 1500;
    CHECK(!eve::action::decodeAbilityAsset(ambiguous).ok());

    auto  fractionalGrid   = encoded.value();
    auto* fractionalAction = fractionalGrid.getIf<eve::Value::Object>()->at("action").getIf<eve::Value::Object>();
    (*fractionalAction)["targetingMode"] = "query";
    (*fractionalAction)["targetingSpec"] =
        eve::Value::Object{{"space", "grid2d"},
                           {"domain", 1},
                           {"minCount", 1},
                           {"maxCount", 1},
                           {"minRange", 0.0},
                           {"maxRange", 3.0},
                           {"requiredTags", eve::Value::Array{}},
                           {"excludedTags", eve::Value::Array{}},
                           {"lineOfSight", false},
                           {"gridArea", eve::Value::Object{{"shape", 0},
                                                           {"minimum", eve::Value::Array{0.5, 0, 0}},
                                                           {"maximum", eve::Value::Array{2, 2, 0}}}}};
    CHECK(!eve::action::decodeAbilityAsset(fractionalGrid).ok());
}
}  // namespace ability_asset
}  // namespace

// ability_controller.cpp
namespace {
namespace ability_controller {
namespace {

eve::LogicalId logicalId(const char* ns, const char* name) {
    const auto result = eve::LogicalId::fromParts(ns, name);
    REQUIRE(result.has_value());
    return *result;
}

struct Fixture {
    Fixture() : abilities(actions), controller(abilities) {
        definition.id        = logicalId("ability", "strike");
        definition.action.id = logicalId("action", "strike");
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

    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities;
    eve::action::AbilityControllerRuntime controller;
    eve::action::AbilityDefinition        definition;
    eve::action::AbilityGrantId           grant;
};

class AiIntentSource final : public eve::action::IAbilityIntentSource {
public:
    [[nodiscard]] eve::Result<std::optional<eve::action::AbilityIntent>> nextIntent(eve::SimulationTick tick) override {
        observedTick = tick;
        if (reject)
            return eve::Result<std::optional<eve::action::AbilityIntent>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "AI has no valid plan"));
        auto result = pending;
        pending.reset();
        return eve::Result<std::optional<eve::action::AbilityIntent>>::success(std::move(result));
    }

    bool                                      reject = false;
    eve::SimulationTick                       observedTick;
    std::optional<eve::action::AbilityIntent> pending;
};

}  // namespace

TEST_CASE("abilityController.playerQueueRoutesFifoIntentWithInjectedTick") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    REQUIRE(player.enqueue(fixture.intent()).ok());
    REQUIRE_EQ(player.pendingCount(), 1u);

    auto processed = fixture.controller.processNext(player, eve::SimulationTick(17));
    REQUIRE(processed.ok());
    REQUIRE(processed.value().has_value());
    CHECK_EQ(player.pendingCount(), 0u);
    const auto* execution = fixture.actions.find(processed.value()->executionId);
    REQUIRE(execution != nullptr);
    CHECK(execution->request().requestedTick == eve::SimulationTick(17));
}

TEST_CASE("abilityController.aiAndPlayerUseTheSameActivationPath") {
    Fixture        fixture;
    AiIntentSource ai;
    ai.pending = fixture.intent();

    auto processed = fixture.controller.processNext(ai, eve::SimulationTick(23));
    REQUIRE(processed.ok());
    REQUIRE(processed.value().has_value());
    CHECK(ai.observedTick == eve::SimulationTick(23));
    CHECK_EQ(fixture.abilities.activeActivations().size(), 1u);
}

TEST_CASE("abilityController.sourceFailureDoesNotMutateAbilityRuntime") {
    Fixture        fixture;
    AiIntentSource ai;
    ai.pending = fixture.intent();
    ai.reject  = true;

    auto processed = fixture.controller.processNext(ai, eve::SimulationTick(3));
    CHECK(!processed.ok());
    CHECK(fixture.abilities.activeActivations().empty());
    CHECK(ai.pending.has_value());
}

TEST_CASE("abilityController.emptySourceIsAnExplicitNoOp") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    auto                                  processed = fixture.controller.processNext(player, eve::SimulationTick(5));
    REQUIRE(processed.ok());
    CHECK(!processed.value().has_value());
    CHECK(fixture.abilities.activeActivations().empty());
}

TEST_CASE("abilityController.rejectedIntentIsConsumedExactlyOnce") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    auto                                  invalid = fixture.intent();
    invalid.request.actionId                      = logicalId("action", "other");
    REQUIRE(player.enqueue(std::move(invalid)).ok());

    auto processed = fixture.controller.processNext(player, eve::SimulationTick(9));
    CHECK(!processed.ok());
    CHECK_EQ(player.pendingCount(), 0u);
    CHECK(fixture.abilities.activeActivations().empty());
}
}  // namespace ability_controller
}  // namespace

// ability_system.cpp
namespace {
namespace ability_system {
namespace {

eve::LogicalId logicalId(const char* ns, const char* name) {
    const auto id = eve::LogicalId::fromParts(ns, name);
    REQUIRE(id.has_value());
    return *id;
}

eve::action::AbilityDefinition ability(const char* name, eve::action::AbilityInstancingPolicy instancing,
                                       eve::action::AbilityActivationGroup group,
                                       eve::Duration                       cooldown = eve::Duration::zero()) {
    eve::action::AbilityDefinition result;
    result.id              = logicalId("ability", name);
    result.action.id       = logicalId("action", name);
    result.instancing      = instancing;
    result.activationGroup = group;
    result.cooldown        = cooldown;
    return result;
}

eve::action::ActionRequest requestFor(const eve::action::AbilityDefinition& ability) {
    eve::action::ActionRequest request;
    request.actionId = ability.action.id;
    return request;
}

}  // namespace

TEST_CASE("abilitySystem.grantsDefinitionsAndKeepsPerOwnerInstanceIdentity") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        definition = ability("dash", eve::action::AbilityInstancingPolicy::PerOwner,
                                                     eve::action::AbilityActivationGroup::Independent);
    REQUIRE(abilities.registerDefinition(definition).ok());
    CHECK(!abilities.registerDefinition(definition).ok());
    auto grant = abilities.grant("fighter:a", definition.id);
    REQUIRE(grant.ok());
    auto first = abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(1));
    REQUIRE(first.ok());
    CHECK(!first.value().instanceId.isZero());
    REQUIRE(runtime.cancel(first.value().executionId, eve::SimulationTick(2)).ok());
    REQUIRE(abilities.synchronize().ok());
    auto second = abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(3));
    REQUIRE(second.ok());
    CHECK(second.value().instanceId == first.value().instanceId);
}

TEST_CASE("abilitySystem.cooldownsUseOnlyInjectedDeterministicDuration") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        oneSecond = eve::Duration::fromSeconds(1.0);
    REQUIRE(oneSecond.ok());
    auto definition = ability("fireball", eve::action::AbilityInstancingPolicy::PerExecution,
                              eve::action::AbilityActivationGroup::Independent, oneSecond.value());
    REQUIRE(abilities.registerDefinition(definition).ok());
    auto grant = abilities.grant("mage", definition.id);
    REQUIRE(grant.ok());
    auto activation = abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(1));
    REQUIRE(activation.ok());
    CHECK(!abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(2)).ok());
    auto partial = eve::Duration::fromSeconds(0.4);
    REQUIRE(partial.ok());
    REQUIRE(abilities.advanceCooldowns(partial.value()).ok());
    CHECK(!abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(3)).ok());
    auto remainder = eve::Duration::fromSeconds(0.6);
    REQUIRE(remainder.ok());
    REQUIRE(abilities.advanceCooldowns(remainder.value()).ok());
    auto second = abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(4));
    REQUIRE(second.ok());
    CHECK(second.value().instanceId != activation.value().instanceId);
    CHECK(!abilities.advanceCooldowns(eve::Duration::fromNanoseconds(-1)).ok());
}

TEST_CASE("abilitySystem.hotReloadPreservesGrantsAndActiveDefinitionSnapshots") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        original = ability("strike", eve::action::AbilityInstancingPolicy::PerExecution,
                                                   eve::action::AbilityActivationGroup::Independent);
    original.action.timing.active        = eve::Duration::fromNanoseconds(100);
    REQUIRE(abilities.registerDefinition(original).ok());
    auto grant = abilities.grant("fighter", original.id);
    REQUIRE(grant.ok());
    auto first = abilities.activate(grant.value(), requestFor(original), eve::SimulationTick(1));
    REQUIRE(first.ok());

    auto replacement                 = original;
    replacement.action.id            = logicalId("action", "strike-v2");
    replacement.action.timing.active = eve::Duration::fromNanoseconds(250);
    REQUIRE(abilities.replaceDefinition(replacement).ok());
    REQUIRE(runtime.find(first.value().executionId) != nullptr);
    CHECK(runtime.find(first.value().executionId)->definition().id == original.action.id);
    CHECK(runtime.find(first.value().executionId)->definition().timing.active == eve::Duration::fromNanoseconds(100));

    REQUIRE(runtime.cancel(first.value().executionId, eve::SimulationTick(2)).ok());
    REQUIRE(abilities.synchronize().ok());
    auto second = abilities.activate(grant.value(), requestFor(replacement), eve::SimulationTick(3));
    REQUIRE(second.ok());
    REQUIRE(runtime.find(second.value().executionId) != nullptr);
    CHECK(runtime.find(second.value().executionId)->definition().id == replacement.action.id);
    CHECK(runtime.find(second.value().executionId)->definition().timing.active == eve::Duration::fromNanoseconds(250));

    auto missing = replacement;
    missing.id   = logicalId("ability", "missing");
    CHECK(!abilities.replaceDefinition(missing).ok());
    auto invalid                 = replacement;
    invalid.action.timing.active = eve::Duration::fromNanoseconds(-1);
    CHECK(!abilities.replaceDefinition(invalid).ok());
    auto incompatible       = replacement;
    incompatible.instancing = eve::action::AbilityInstancingPolicy::PerOwner;
    CHECK(!abilities.replaceDefinition(incompatible).ok());
}

TEST_CASE("abilitySystem.exclusiveGroupsReplaceOrBlockDeterministically") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        light    = ability("light", eve::action::AbilityInstancingPolicy::PerExecution,
                                                   eve::action::AbilityActivationGroup::ExclusiveReplaceable);
    auto                        heavy    = ability("heavy", eve::action::AbilityInstancingPolicy::PerExecution,
                                                   eve::action::AbilityActivationGroup::ExclusiveReplaceable);
    auto                        ultimate = ability("ultimate", eve::action::AbilityInstancingPolicy::PerExecution,
                                                   eve::action::AbilityActivationGroup::ExclusiveBlocking);
    REQUIRE(abilities.registerDefinition(light).ok());
    REQUIRE(abilities.registerDefinition(heavy).ok());
    REQUIRE(abilities.registerDefinition(ultimate).ok());
    auto lightGrant    = abilities.grant("fighter", light.id);
    auto heavyGrant    = abilities.grant("fighter", heavy.id);
    auto ultimateGrant = abilities.grant("fighter", ultimate.id);
    REQUIRE(lightGrant.ok());
    REQUIRE(heavyGrant.ok());
    REQUIRE(ultimateGrant.ok());

    auto first = abilities.activate(lightGrant.value(), requestFor(light), eve::SimulationTick(1));
    REQUIRE(first.ok());
    auto replacement = abilities.activate(heavyGrant.value(), requestFor(heavy), eve::SimulationTick(2));
    REQUIRE(replacement.ok());
    REQUIRE(runtime.find(first.value().executionId) != nullptr);
    CHECK(static_cast<int>(runtime.find(first.value().executionId)->phase()) ==
          static_cast<int>(eve::action::ActionPhase::Cancelled));
    REQUIRE(runtime.cancel(replacement.value().executionId, eve::SimulationTick(3)).ok());
    REQUIRE(abilities.synchronize().ok());

    auto blocker = abilities.activate(ultimateGrant.value(), requestFor(ultimate), eve::SimulationTick(4));
    REQUIRE(blocker.ok());
    CHECK(!abilities.activate(lightGrant.value(), requestFor(light), eve::SimulationTick(5)).ok());
    CHECK(abilities.activeActivations().size() == 1);
}

TEST_CASE("abilitySystem.gameplayEventTriggersSupportHierarchicalTags") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        exact = ability("parry", eve::action::AbilityInstancingPolicy::NonInstanced,
                                                eve::action::AbilityActivationGroup::Independent);
    exact.triggers.push_back({"Event.Combat.Parry", eve::tags::GameplayTagMatch::Exact});
    auto family = ability("hit-react", eve::action::AbilityInstancingPolicy::PerOwner,
                          eve::action::AbilityActivationGroup::Independent);
    family.triggers.push_back({"Event.Combat.Hit", eve::tags::GameplayTagMatch::IncludeDescendants});
    REQUIRE(abilities.registerDefinition(exact).ok());
    REQUIRE(abilities.registerDefinition(family).ok());
    auto exactGrant  = abilities.grant("fighter", exact.id);
    auto familyGrant = abilities.grant("fighter", family.id);
    REQUIRE(exactGrant.ok());
    REQUIRE(familyGrant.ok());

    const auto hit = abilities.matchingGrants("fighter", "Event.Combat.Hit.Heavy");
    REQUIRE(hit.size() == 1);
    CHECK(hit[0] == familyGrant.value());
    const auto parry = abilities.matchingGrants("fighter", "Event.Combat.Parry");
    REQUIRE(parry.size() == 1);
    CHECK(parry[0] == exactGrant.value());
    CHECK(abilities.matchingGrants("other", "Event.Combat.Parry").empty());
}

TEST_CASE("abilitySystem.revokeRejectsActiveGrantAndSucceedsAfterSynchronization") {
    eve::action::ActionRuntime  runtime;
    eve::action::AbilityRuntime abilities(runtime);
    auto                        definition = ability("guard", eve::action::AbilityInstancingPolicy::PerOwner,
                                                     eve::action::AbilityActivationGroup::Independent);
    REQUIRE(abilities.registerDefinition(definition).ok());
    auto grant = abilities.grant("fighter", definition.id);
    REQUIRE(grant.ok());
    auto activation = abilities.activate(grant.value(), requestFor(definition), eve::SimulationTick(1));
    REQUIRE(activation.ok());
    CHECK(!abilities.revoke(grant.value()).ok());
    REQUIRE(runtime.cancel(activation.value().executionId, eve::SimulationTick(2)).ok());
    auto synchronized = abilities.synchronize();
    REQUIRE(synchronized.ok());
    CHECK(synchronized.value() == 1);
    REQUIRE(abilities.revoke(grant.value()).ok());
    CHECK(!abilities.findGrant(grant.value()).ok());
}
}  // namespace ability_system
}  // namespace
}  // namespace test_action_abilities
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_notify_registry)
#include "PrivateTestScope.h"
#include <memory>
#include <string_view>
#include "action/ActionAudioBlock.h"
#include "action/ActionBlockRuntime.h"
#include "action/ActionCameraBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionParameterCurve.h"
#include "action/ActionPrefabBlock.h"
#include "action/ActionPrefabInstances.h"
#include "action/ActionSpatialBlock.h"
#include "action/ActionVfxBlock.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_notify_registry.cpp
namespace test_action_notify_registry_unit {
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
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "sample context is not preview", "context"));
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

    int                            calls       = 0;
    int                            updates     = 0;
    mutable int                    samples     = 0;
    int                            advances    = 0;
    bool                           failAdvance = false;
    std::string                    lastType;
    eve::action::ActionExecutionId lastExecution;
    eve::Duration                  lastLocalTime = eve::Duration::zero();
};

class RecordingParameterSink final : public eve::action::IActionParameterSink {
public:
    bool supports(const eve::LogicalId& target) const noexcept override { return target.format() == "test:parameter"; }

    eve::Result<void> apply(const eve::action::ActionParameterSample& sample) override {
        samples.push_back(sample);
        return eve::Result<void>::success();
    }

    std::vector<eve::action::ActionParameterSample> samples;
};

class RecordingCameraSink final : public eve::action::IActionCameraCueSink {
public:
    bool supports(const eve::LogicalId& cue) const noexcept override { return cue.format() == "combat:impact"; }

    eve::Result<void> trigger(const eve::action::ActionCameraCueBinding& binding,
                              const eve::action::ActionNotifyContext&    context) override {
        ++calls;
        last      = binding;
        execution = context.executionId;
        return eve::Result<void>::success();
    }

    int                                 calls = 0;
    eve::action::ActionCameraCueBinding last;
    eve::action::ActionExecutionId      execution;
};

eve::Value::Object curvePayload() {
    return {
        {"target", "test:parameter"},
        {"operation", "multiply"},
        {"keys", eve::Value::Array{eve::Value::Object{{"time", 0.0}, {"value", 0.0}, {"interpolation", "linear"}},
                                   eve::Value::Object{{"time", 0.5}, {"value", 1.0}, {"interpolation", "step"}},
                                   eve::Value::Object{{"time", 1.0}, {"value", 0.25}, {"interpolation", "linear"}}}}};
}

}  // namespace

TEST_CASE("actionNotifyRegistry.builtinsExposeStableEditorContracts") {
    CHECK(eve::cap::query<eve::action::IActionPrefabInstances>() == nullptr);
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const auto descriptors = registry.value().descriptors();
    REQUIRE_EQ(descriptors.size(), 18u);
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

TEST_CASE("actionCameraCue.validatesAndRoutesToOptionalSink") {
    eve::Value::Object payload{{"cue", "combat:impact"}, {"positionAmplitude", 0.2}, {"rotationAmplitude", 2.5},
                               {"fovAmplitude", -1.0},   {"durationSeconds", 0.4},   {"seed", 17}};
    auto               binding = eve::action::ActionCameraCueBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().cue, id("combat:impact"));
    CHECK_EQ(binding.value().positionAmplitude, 0.2);
    CHECK_EQ(binding.value().rotationAmplitude, 2.5);
    CHECK_EQ(binding.value().fovAmplitude, -1.0);
    CHECK_EQ(binding.value().duration, eve::Duration::fromNanoseconds(400000000));
    CHECK_EQ(binding.value().seed, 17u);

    auto invalid               = payload;
    invalid["durationSeconds"] = 0.0;
    auto rejected              = eve::action::ActionCameraCueBinding::fromPayload(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "durationSeconds");

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    auto cameraEvent    = event(eve::action::ActionTimelineEventKind::Notify, "presentation:camera");
    cameraEvent.payload = payload;
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{202};
    auto missing        = registry.value().dispatch(cameraEvent, context);
    CHECK(!missing.ok());
    CHECK_EQ(missing.status().code(), eve::StatusCode::NotFound);

    RecordingCameraSink sink;
    eve::cap::addListener<eve::action::IActionCameraCueSink>(&sink);
    REQUIRE(registry.value().dispatch(cameraEvent, context).ok());
    REQUIRE_EQ(sink.calls, 1);
    CHECK_EQ(sink.last.seed, 17u);
    CHECK_EQ(sink.execution, context.executionId);
    eve::cap::removeListener<eve::action::IActionCameraCueSink>(&sink);
}

TEST_CASE("actionParameterCurve.validatesSamplesAndRoutesPairedRuntimeLifecycle") {
    auto payload = curvePayload();
    auto binding = eve::action::ActionParameterCurveBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().target, id("test:parameter"));
    CHECK_EQ(binding.value().operation, eve::action::ActionParameterOperation::Multiply);
    CHECK_EQ(binding.value().sample(0.25), 0.5);
    CHECK_EQ(binding.value().sample(0.75), 1.0);
    CHECK_EQ(binding.value().toPayload({{"extension", true}}).at("extension").asBool(), true);
    auto invalid    = payload;
    invalid["keys"] = eve::Value::Array{eve::Value::Object{{"time", 0.1}, {"value", 0.0}},
                                        eve::Value::Object{{"time", 1.0}, {"value", 1.0}}};
    auto rejected   = eve::action::ActionParameterCurveBinding::fromPayload(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "keys");

    {
        auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
        REQUIRE(registry.ok());
        eve::action::ActionBlockRuntime runtime(registry.value());
        eve::action::ActionAdvance      advance;
        advance.id = eve::action::ActionExecutionId{100};
        advance.activeBlocks.push_back({id("parameter-track:main"), id("parameter-curve:missing-sink"),
                                        id("presentation:parameter-curve"), eve::Duration::zero(),
                                        eve::Duration::fromNanoseconds(100), payload});
        eve::action::ActionNotifyContext context;
        context.executionId = advance.id;
        auto missing        = runtime.apply(advance, context);
        CHECK(!missing.ok());
        CHECK_EQ(missing.status().code(), eve::StatusCode::NotFound);
    }

    RecordingParameterSink sink;
    eve::cap::addListener<eve::action::IActionParameterSink>(&sink);
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::action::ActionAdvance      advance;
    advance.id = eve::action::ActionExecutionId{101};
    advance.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter, id("parameter-track:main"),
                                      id("parameter-curve:fade"), id("presentation:parameter-curve"),
                                      eve::Duration::zero(), payload});
    advance.activeBlocks.push_back({id("parameter-track:main"), id("parameter-curve:fade"),
                                    id("presentation:parameter-curve"), eve::Duration::zero(),
                                    eve::Duration::fromNanoseconds(100), payload});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    REQUIRE(runtime.apply(advance, context).ok());
    REQUIRE_EQ(sink.samples.size(), 1u);
    CHECK_EQ(sink.samples.back().phase, eve::action::ActionParameterPhase::Begin);
    CHECK_EQ(sink.samples.back().value, 0.0);

    advance.timelineEvents.clear();
    advance.activeBlocks.front().localTime = eve::Duration::fromNanoseconds(25);
    REQUIRE(runtime.apply(advance, context).ok());
    REQUIRE_EQ(sink.samples.size(), 2u);
    CHECK_EQ(sink.samples.back().phase, eve::action::ActionParameterPhase::Update);
    CHECK_EQ(sink.samples.back().value, 0.5);
    REQUIRE(runtime.interrupt(context).ok());
    REQUIRE_EQ(sink.samples.size(), 3u);
    CHECK_EQ(sink.samples.back().phase, eve::action::ActionParameterPhase::End);
    eve::cap::removeListener<eve::action::IActionParameterSink>(&sink);
}

TEST_CASE("entitySpatialResolver.absentProviderFailsObservably") {
    auto resolved = eve::resolveEntitySpatialPose({});
    CHECK(!resolved.ok());
    CHECK(static_cast<int>(resolved.status().code()) == static_cast<int>(eve::StatusCode::NotFound));
}

TEST_CASE("actionBlockRuntime.routesEnterUpdateExitAndSideEffectFreeSample") {
    eve::action::ActionNotifyRegistry registry;
    REQUIRE(registry
                .registerDescriptor(
                    {"project:combat.window", "Window", "Project", eve::action::ActionNotifyShape::State, {}})
                .ok());
    CHECK(!registry.hasHandler("project:combat.window"));
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:combat.window", handler).ok());
    CHECK(registry.hasHandler("project:combat.window"));
    eve::action::ActionBlockRuntime runtime(registry);

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId{7};
    advance.totalElapsed = eve::Duration::fromNanoseconds(25);
    advance.timelineEvents.push_back(event(eve::action::ActionTimelineEventKind::StateEnter, "project:combat.window"));
    advance.activeBlocks.push_back({id("combat-track:gameplay"),
                                    id("combat-notify:test"),
                                    id("project:combat.window"),
                                    eve::Duration::fromNanoseconds(15),
                                    eve::Duration::fromNanoseconds(40),
                                    {}});
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
    auto rejected    = eve::action::ActionSpatialBinding::fromPayload(payload);
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
    auto rejected                    = eve::action::ActionPrefabSpawnBinding::fromPayload(payload);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "customDurationSeconds");

    payload["customDurationSeconds"] = 0.5;
    payload["lifecycle"]             = "unknown";
    rejected                         = eve::action::ActionPrefabSpawnBinding::fromPayload(payload);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "lifecycle");
}

TEST_CASE("actionAudioBlock.validatesSharedRuntimeAndPreviewContract") {
    eve::Value::Object payload{
        {"uri", "asset://audio/sword.wav"},
        {"randomUris", eve::Value::Array{"asset://audio/sword-a.wav", "asset://audio/sword-b.wav"}},
        {"volume", 0.5},
        {"pitch", 1.25},
        {"randomPitchOffset", 0.1},
        {"spatialBlend", 0.75},
        {"minDistance", 2.0},
        {"maxDistance", 28.0},
        {"looping", true},
        {"fadeOutOnExit", true},
        {"fadeOutDuration", 0.25}};
    auto state = eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::State);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().uri, "asset://audio/sword.wav");
    CHECK_EQ(state.value().volume, 0.5);
    CHECK_EQ(state.value().pitch, 1.25);
    REQUIRE_EQ(state.value().randomUris.size(), 2U);
    CHECK_EQ(state.value().randomUris[1], "asset://audio/sword-b.wav");
    CHECK_EQ(state.value().randomPitchOffset, 0.1);
    CHECK_EQ(state.value().spatialBlend, 0.75);
    CHECK_EQ(state.value().minDistance, 2.0);
    CHECK_EQ(state.value().maxDistance, 28.0);
    CHECK(state.value().fadeOutOnExit);
    CHECK_EQ(state.value().fadeOutDuration, 0.25);
    CHECK(state.value().looping);

    auto instant = eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!instant.ok());
    CHECK_EQ(instant.status().diagnostics().front().path(), "looping");
    payload["looping"] = false;
    payload["pitch"]   = 0.0;
    auto invalidPitch  = eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!invalidPitch.ok());
    CHECK_EQ(invalidPitch.status().diagnostics().front().path(), "pitch");

    payload["pitch"]             = 1.0;
    payload["randomPitchOffset"] = 0.6;
    auto invalidVariation =
        eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!invalidVariation.ok());
    CHECK_EQ(invalidVariation.status().diagnostics().front().path(), "randomPitchOffset");
    payload["randomPitchOffset"] = 0.1;
    payload["maxDistance"]       = 1.0;
    auto invalidAttenuation =
        eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!invalidAttenuation.ok());
    CHECK_EQ(invalidAttenuation.status().diagnostics().front().path(), "maxDistance");
    payload["maxDistance"] = 28.0;
    payload["randomUris"]  = eve::Value::Array{"asset://audio/repeated.wav", "asset://audio/repeated.wav"};
    auto duplicateUri = eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!duplicateUri.ok());
    CHECK_EQ(duplicateUri.status().diagnostics().front().path(), "randomUris[1]");
    payload["randomUris"]      = eve::Value::Array{};
    payload["fadeOutDuration"] = 0.0;
    auto invalidFade = eve::action::ActionAudioBinding::fromPayload(payload, eve::action::ActionAudioShape::Instant);
    CHECK(!invalidFade.ok());
    CHECK_EQ(invalidFade.status().diagnostics().front().path(), "fadeOutDuration");
}

TEST_CASE("actionVfxBlock.validatesTrimStopAndLifetimeContract") {
    eve::Value::Object payload{{"uri", "asset://vfx/sword-trail.effect.json"},
                               {"stopBehavior", "clear_immediately"},
                               {"playbackRateSynced", false},
                               {"clipStartTime", 0.25},
                               {"clipEndTime", 1.75},
                               {"lifetimeSeconds", 2.0}};
    auto instant = eve::action::ActionVfxBinding::fromPayload(payload, eve::action::ActionVfxShape::Instant);
    REQUIRE(instant.ok());
    CHECK_EQ(instant.value().uri, "asset://vfx/sword-trail.effect.json");
    CHECK_EQ(instant.value().stopBehavior, eve::action::ActionVfxStopBehavior::ClearImmediately);
    CHECK(!instant.value().playbackRateSynced);
    CHECK_EQ(instant.value().clipStartTime, 0.25);
    CHECK_EQ(instant.value().clipEndTime, 1.75);
    CHECK_EQ(instant.value().lifetimeSeconds, 2.0);

    payload["clipEndTime"] = 0.25;
    auto invalidClip       = eve::action::ActionVfxBinding::fromPayload(payload, eve::action::ActionVfxShape::State);
    CHECK(!invalidClip.ok());
    CHECK_EQ(invalidClip.status().diagnostics().front().path(), "clipEndTime");
    payload["clipEndTime"]     = 1.75;
    payload["lifetimeSeconds"] = 0.0;
    auto invalidLifetime = eve::action::ActionVfxBinding::fromPayload(payload, eve::action::ActionVfxShape::Instant);
    CHECK(!invalidLifetime.ok());
    CHECK_EQ(invalidLifetime.status().diagnostics().front().path(), "lifetimeSeconds");
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
    auto dispatched     = registry.value().dispatch(prefab, context);
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
    REQUIRE(
        registry.registerDescriptor({"project:first", "First", "Project", eve::action::ActionNotifyShape::Instant, {}})
            .ok());
    REQUIRE(
        registry
            .registerDescriptor({"project:second", "Second", "Project", eve::action::ActionNotifyShape::Instant, {}})
            .ok());
    auto handler = std::make_shared<RecordingHandler>();
    REQUIRE(registry.registerHandler("project:first", handler).ok());
    REQUIRE(registry.registerHandler("project:second", handler).ok());

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{43};
    REQUIRE(registry.advanceHandlers(context).ok());
    CHECK_EQ(handler->advances, 1);

    handler->failAdvance = true;
    auto failed          = registry.advanceHandlers(context);
    CHECK(!failed.ok());
    CHECK_EQ(failed.status().diagnostics().front().path(), "handler");
    CHECK_EQ(handler->advances, 2);
}
}  // namespace test_action_notify_registry_unit
#endif
