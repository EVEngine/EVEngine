#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rts/RTS.h"

#include <array>

namespace {

eve::SubjectRef subject(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

}  // namespace

TEST_CASE("rts.settlement.configurationUsesAttachedCombatProvider") {
    eve::rts::RTS                      rts;
    eve::settlement::SettlementRuleSet emptyRules;
    REQUIRE(emptyRules.configure({}).ok());
    CHECK(!rts.configureSettlementRules(emptyRules).ok());

    eve::combat::DamageRuntime damage;
    rts.setCombatProviders(nullptr, &damage);
    CHECK(rts.configureSettlementRules(emptyRules).ok());
}

TEST_CASE("rts.settlement.rulesReadCanonicalCombatContext") {
    eve::settlement::SettlementRule exposed;
    exposed.id        = "rts.exposed";
    exposed.stage     = eve::settlement::StageKind::TargetMitigation;
    exposed.operation = eve::settlement::RuleOperation::Multiply;
    exposed.value     = 2.0;
    exposed.when      = "context.incoming_damage_multiplier > 1.2 && context.target_health_ratio == 1";
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({exposed}).ok());

    eve::combat::DamageRuntime damage;
    eve::rts::RTS              rts;
    rts.setCombatProviders(nullptr, &damage);
    REQUIRE(rts.configureSettlementRules(rules).ok());

    eve::combat::CombatState target;
    target.subject = subject(2);
    target.health = target.maxHealth = 100.0;
    target.poise = target.maxPoise = 10.0;
    eve::combat::DamageRequest request;
    request.source                   = subject(1);
    request.target                   = target.subject;
    request.damageType               = "damage.physical";
    request.healthDamage             = 10.0;
    request.incomingDamageMultiplier = 1.5;
    auto outcome                     = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().appliedHealthDamage, 30.0);
    CHECK_EQ(target.health, 70.0);
}

TEST_CASE("rts.settlement.healingUsesTheConfiguredCombatPipeline") {
    eve::settlement::SettlementRule amplified;
    amplified.id           = "rts.healing.amplified";
    amplified.stage        = eve::settlement::StageKind::SourceModifiers;
    amplified.operation    = eve::settlement::RuleOperation::Multiply;
    amplified.value        = 2.0;
    amplified.filter.kinds = {"heal"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({amplified}).ok());

    eve::combat::DamageRuntime settlement;
    REQUIRE(settlement.configureSettlementRules(rules).ok());
    eve::combat::CombatState target;
    target.subject = subject(2);
    target.health = 50.0;
    target.maxHealth = 100.0;

    auto restored = settlement.heal(target, subject(1), 10.0);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().requested, 10.0);
    CHECK_EQ(restored.value().applied, 20.0);
    CHECK_EQ(target.health, 70.0);
}
