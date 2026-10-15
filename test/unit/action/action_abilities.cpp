#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "action/AbilitySystem.h"
#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    result.action.condition = eve::decision::Condition::all(
        {eve::decision::Condition::hasTag("State.Combat.Ready"),
         eve::decision::Condition::compare("mana", eve::decision::CompareOperator::GreaterEqual, 12),
         eve::decision::Condition::policyCall(
             "can_cast", eve::Value::Object{{"school", "fire"}},
             eve::decision::ScriptConditionDeclaration{
                 "can_cast", {"attributes.mana"}, eve::decision::DeterminismLevel::TickDeterministic})});
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
    source.action.condition = eve::decision::Condition{};
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
