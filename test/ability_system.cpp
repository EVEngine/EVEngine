#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "action/AbilitySystem.h"

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
