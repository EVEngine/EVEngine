#include "action/AbilityAsset.h"
#include "action/AbilitySystem.h"
#include "action/ActionModule.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace {

eve::action::AbilityDefinition lightAttack() {
    eve::action::AbilityDefinition definition;
    definition.id                    = *eve::LogicalId::fromParts("ability", "light-attack");
    definition.action.id             = *eve::LogicalId::fromParts("action", "light-attack");
    definition.action.timing.windup  = eve::Duration::fromNanoseconds(100'000'000);
    definition.action.timing.active  = eve::Duration::fromNanoseconds(200'000'000);
    definition.action.timing.recover = eve::Duration::fromNanoseconds(200'000'000);
    definition.cooldown              = eve::Duration::fromNanoseconds(500'000'000);
    definition.instancing            = eve::action::AbilityInstancingPolicy::PerOwner;
    definition.activationGroup       = eve::action::AbilityActivationGroup::Independent;
    definition.triggers.push_back({"Gameplay.Input.Attack", eve::tags::GameplayTagMatch::Exact});
    return definition;
}

}  // namespace

TEST_CASE("actionScript.runtimeOwnsAbilityLifecycle") {
    auto encoded = eve::action::encodeAbilityAsset(lightAttack());
    REQUIRE(encoded.ok());
    auto abilityJson = encoded.value().toJson();
    REQUIRE(abilityJson.ok());

    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::action::Action::expose(eve);
    const std::string abilityLiteral = abilityJson.value();
    eve.addFunc("abilityJson", [abilityLiteral]() { return abilityLiteral; });
    vm.run(vm.compileSource(R"(
        action <- eve.Action();
        created <- action.newRuntime();
        runtime <- created.value;
        registered <- runtime.registerAbilityJson(eve.abilityJson());
        granted <- runtime.grantAbility("fighter:player", "ability:light-attack");
        grantId <- granted.value.grantId;
        activated <- runtime.activateAbility(grantId, 1);
        blocked <- runtime.activateAbility(grantId, 1);
        stepped <- runtime.advanceAbilities(2, 0.10);
        cooling <- runtime.abilityGrant(grantId);
        finished <- runtime.advanceAbilities(3, 0.50);
        ready <- runtime.abilityGrant(grantId);
        matched <- runtime.matchingAbilities("fighter:player", "Gameplay.Input.Attack");
        submitted <- runtime.submitAction("action:direct", 0, 100000000, 0, 10);
        advanced <- runtime.advanceAction(submitted.value.executionId, 11, 0.05);
        found <- runtime.findAction(submitted.value.executionId);
        invalid <- runtime.grantAbility("fighter:player", "ability:missing");
        coolingSeconds <- cooling.value.cooldownSeconds;
        finishedActive <- finished.value.activeCount;
        readySeconds <- ready.value.cooldownSeconds;
        matchCount <- matched.value.len();
        foundPhase <- found.value.phase;
        ownership <- runtime.ownership();
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK_EQ(vm.find("ownership").toString(), std::string("owned"));
    CHECK(vm.find("registered").toTable().get<bool>("ok"));
    CHECK(vm.find("granted").toTable().get<bool>("ok"));
    CHECK(vm.find("activated").toTable().get<bool>("ok"));
    CHECK(!vm.find("blocked").toTable().get<bool>("ok"));
    CHECK(vm.find("stepped").toTable().get<bool>("ok"));
    CHECK(vm.find("coolingSeconds").toFloat() > 0.0f);
    CHECK_EQ(vm.find("finishedActive").toInt(), 0);
    CHECK_EQ(vm.find("readySeconds").toFloat(), 0.0f);
    CHECK(vm.find("matched").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("matchCount").toInt(), 1);
    CHECK(vm.find("submitted").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("found").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
}
