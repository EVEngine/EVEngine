#include "combat/StandardAbilities.h"

#include "action/AbilityAsset.h"
#include "action/AbilityController.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <set>
#include <string>

TEST_CASE("standardCombatAbilities.providesTwentyUniqueValidatedArchetypes") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    REQUIRE_EQ(catalog.value().size(), 20u);
    std::set<std::string> ids;
    for (const auto& definition : catalog.value()) {
        REQUIRE(definition.validate().ok());
        ids.insert(definition.id.format());
        CHECK(definition.action.metadata.contains("combat.adapter"));
    }
    CHECK_EQ(ids.size(), 20u);
    CHECK(ids.contains("combat-ability:light-attack"));
    CHECK(ids.contains("combat-ability:death"));
}

TEST_CASE("standardCombatAbilities.registerGrantAndActivateThroughSharedController") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    eve::action::ActionRuntime  actions;
    eve::action::AbilityRuntime abilities(actions);
    for (const auto& definition : catalog.value()) REQUIRE(abilities.registerDefinition(definition).ok());

    auto grant = abilities.grant("fighter", catalog.value()[0].id);
    REQUIRE(grant.ok());
    eve::action::PlayerAbilityIntentQueue player;
    eve::action::AbilityIntent             intent;
    intent.grantId         = grant.value();
    intent.request.actionId = catalog.value()[0].action.id;
    REQUIRE(player.enqueue(std::move(intent)).ok());
    eve::action::AbilityControllerRuntime controller(abilities);
    auto activation = controller.processNext(player, eve::SimulationTick(1));
    REQUIRE(activation.ok());
    REQUIRE(activation.value().has_value());
    CHECK(actions.find(activation.value()->executionId) != nullptr);
}

TEST_CASE("standardCombatAbilities.roundTripEveryArchetypeThroughVersionedJsonAssets") {
    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    eve::action::ActionRuntime actions;
    eve::action::AbilityRuntime abilities(actions);
    for (const auto& definition : catalog.value()) {
        auto encoded = eve::action::encodeAbilityAsset(definition);
        REQUIRE(encoded.ok());
        auto json = encoded.value().toJson();
        REQUIRE(json.ok());
        auto parsed = eve::Value::fromJson(json.value());
        REQUIRE(parsed.ok());
        auto decoded = eve::action::decodeAbilityAsset(parsed.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().id, definition.id);
        CHECK_EQ(decoded.value().action.metadata.at("combat.adapter").asString(),
                 definition.action.metadata.at("combat.adapter").asString());
        REQUIRE(abilities.registerDefinition(std::move(decoded).takeValue()).ok());
    }
}
