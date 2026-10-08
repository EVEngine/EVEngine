#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "combat/Damage.h"

namespace {

eve::SubjectRef subject(const char* uuid) {
    auto id = eve::PersistentId::parse(uuid);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::combat::CombatState state() { return {subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 50.0, 50.0}; }

eve::combat::DamageRequest requestFor(const eve::combat::CombatState& target) {
    eve::combat::DamageRequest request;
    request.source       = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 10.0;
    request.poiseDamage  = 5.0;
    request.knockback    = {2.0, 0.0, -1.0};
    return request;
}

class ArmorRule final : public eve::combat::IDamageRule {
public:
    eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest& request,
                                                     const eve::combat::CombatState&) const override {
        ++calls;
        return eve::Result<eve::combat::DamageAmounts>::success(
            {request.healthDamage * 0.5, request.poiseDamage * 2.0, 0.25});
    }

    mutable int calls = 0;
};

class InvalidRule final : public eve::combat::IDamageRule {
public:
    eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest&,
                                                     const eve::combat::CombatState&) const override {
        return eve::Result<eve::combat::DamageAmounts>::success({-1.0, 0.0, 1.0});
    }
};

}  // namespace

TEST_CASE("combatDamage.defaultRuleCommitsHealthPoiseReactionAndKnockback") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    eve::combat::DamageRuntime damage;
    auto                       outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(target.health, 90.0);
    CHECK_EQ(target.poise, 45.0);
    CHECK_EQ(outcome.value().appliedHealthDamage, 10.0);
    CHECK_EQ(outcome.value().knockback.x, 2.0);
    CHECK(static_cast<int>(outcome.value().reaction) == static_cast<int>(eve::combat::HitReaction::Flinch));
    CHECK(static_cast<int>(outcome.value().ruleSource) == static_cast<int>(eve::combat::DamageRuleSource::Default));
}

TEST_CASE("combatDamage.providerPathIsObservableAndPreparedBeforeCommit") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    ArmorRule                  armor;
    eve::combat::DamageRuntime damage(&armor);
    auto                       outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK(armor.calls == 1);
    CHECK_EQ(target.health, 95.0);
    CHECK_EQ(target.poise, 40.0);
    CHECK_EQ(outcome.value().knockback.x, 0.5);
    CHECK(static_cast<int>(outcome.value().ruleSource) == static_cast<int>(eve::combat::DamageRuleSource::Provider));

    InvalidRule                invalid;
    eve::combat::DamageRuntime rejected(&invalid);
    const auto                 before = target;
    CHECK(!rejected.apply(target, request).ok());
    CHECK_EQ(target.health, before.health);
    CHECK_EQ(target.poise, before.poise);
}

TEST_CASE("combatDamage.settlementRulesDriveTheRuntimeUsedByRts") {
    eve::settlement::SettlementRule vulnerability;
    vulnerability.id                  = "rts.burning-oil";
    vulnerability.source              = "effect:burning-oil";
    vulnerability.stage               = eve::settlement::StageKind::SourceModifiers;
    vulnerability.operation           = eve::settlement::RuleOperation::Multiply;
    vulnerability.value               = 1.5;
    vulnerability.filter.kinds        = {"damage"};
    vulnerability.filter.requiredTags = {"Damage.Physical.Slash"};

    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({vulnerability}).ok());
    eve::combat::DamageRuntime damage;
    REQUIRE(damage.configureSettlementRules(rules).ok());

    auto target  = state();
    auto request = requestFor(target);
    auto preview = damage.previewSettlement(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(preview.value().appliedHealthDamage, 15.0);
    CHECK_EQ(target.health, 100.0);
    auto outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().appliedHealthDamage, 15.0);
    CHECK_EQ(target.health, 85.0);
    CHECK_EQ(target.poise, 45.0);
}

TEST_CASE("combatDamage.shieldIsResolvedInsideSharedSettlement") {
    eve::combat::CombatState target;
    target.subject = subject("00000000-0000-0000-0000-000000000002");
    target.maxHealth = 100.0;
    target.health = 100.0;
    target.maxPoise = 10.0;
    target.poise = 10.0;
    eve::combat::DamageRequest request;
    request.source = subject("00000000-0000-0000-0000-000000000001");
    request.target = target.subject;
    request.damageType = "damage.energy";
    request.healthDamage = 30.0;
    request.availableShield = 12.0;
    eve::combat::DamageRuntime damage;
    auto outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(outcome.value().absorbedShieldDamage, 12.0);
    CHECK_EQ(outcome.value().appliedHealthDamage, 18.0);
    CHECK_EQ(target.health, 82.0);
    REQUIRE_EQ(outcome.value().settlementResult.derived.size(), 1u);
    CHECK_EQ(outcome.value().settlementResult.derived[0].trigger, std::string("shield_break"));
    CHECK_EQ(outcome.value().settlementResult.derived[0].magnitude, 0.0);
}

TEST_CASE("combatDamage.standardTransitionsAreStableAndEmittedOnce") {
    eve::combat::DamageRuntime damage;
    auto                       target  = state();
    auto                       request = requestFor(target);
    request.healthDamage                = 200.0;
    request.availableShield             = 25.0;

    auto preview = damage.previewSettlement(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(target.health, 100.0);
    REQUIRE_EQ(preview.value().settlementResult.derived.size(), 3u);
    CHECK_EQ(preview.value().settlementResult.derived[0].trigger, std::string("shield_break"));
    CHECK_EQ(preview.value().settlementResult.derived[1].trigger, std::string("death"));
    CHECK_EQ(preview.value().settlementResult.derived[2].trigger, std::string("kill"));

    auto committed = damage.apply(target, request);
    REQUIRE(committed.ok());
    CHECK_EQ(target.health, 0.0);
    REQUIRE_EQ(committed.value().settlementResult.derived.size(), 3u);
    for (std::size_t index = 0; index < 3; ++index)
        CHECK_EQ(committed.value().settlementResult.derived[index].trigger,
                 preview.value().settlementResult.derived[index].trigger);

    request.availableShield = 0.0;
    auto alreadyDead = damage.apply(target, request);
    REQUIRE(alreadyDead.ok());
    CHECK(alreadyDead.value().settlementResult.derived.empty());
}

TEST_CASE("combatDamage.previewUsesCanonicalRuleWithoutMutatingTarget") {
    auto                       target  = state();
    auto                       request = requestFor(target);
    ArmorRule                  armor;
    eve::combat::DamageRuntime damage(&armor);

    auto preview = damage.preview(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(armor.calls, 1);
    CHECK_EQ(preview.value().healthDamage, 5.0);
    CHECK_EQ(preview.value().poiseDamage, 10.0);
    CHECK_EQ(preview.value().knockbackScale, 0.25);
    CHECK_EQ(target.health, 100.0);
    CHECK_EQ(target.poise, 50.0);

    auto outcome = damage.apply(target, request);
    REQUIRE(outcome.ok());
    CHECK_EQ(armor.calls, 2);
    CHECK_EQ(outcome.value().appliedHealthDamage, preview.value().healthDamage);
}

TEST_CASE("combatDamage.reactionPriorityIsDeathKnockdownStaggerFlinch") {
    eve::combat::HitReactionPolicy policy;
    policy.flinchDamageThreshold = 1.0;
    policy.staggerPoiseFraction  = 0.5;
    eve::combat::DamageRuntime damage(nullptr, policy);

    auto staggered              = state();
    auto staggerRequest         = requestFor(staggered);
    staggerRequest.healthDamage = 1.0;
    staggerRequest.poiseDamage  = 25.0;
    auto stagger                = damage.apply(staggered, staggerRequest);
    REQUIRE(stagger.ok());
    CHECK(static_cast<int>(stagger.value().reaction) == static_cast<int>(eve::combat::HitReaction::Stagger));

    auto knocked              = state();
    auto knockRequest         = requestFor(knocked);
    knockRequest.healthDamage = 1.0;
    knockRequest.poiseDamage  = 100.0;
    auto knockdown            = damage.apply(knocked, knockRequest);
    REQUIRE(knockdown.ok());
    CHECK(static_cast<int>(knockdown.value().reaction) == static_cast<int>(eve::combat::HitReaction::Knockdown));

    auto killed         = state();
    auto lethal         = requestFor(killed);
    lethal.healthDamage = 200.0;
    lethal.poiseDamage  = 100.0;
    auto death          = damage.apply(killed, lethal);
    REQUIRE(death.ok());
    CHECK(static_cast<int>(death.value().reaction) == static_cast<int>(eve::combat::HitReaction::Death));
}

TEST_CASE("combatDamage.poiseRecoveryIsExplicitCheckedAndClamped") {
    auto target  = state();
    target.poise = 10.0;
    eve::combat::DamageRuntime damage;
    auto                       recovered = damage.recoverPoise(target, 15.0);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 25.0);
    recovered = damage.recoverPoise(target, 100.0);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 50.0);
    CHECK(!damage.recoverPoise(target, -1.0).ok());
    CHECK_EQ(target.poise, 50.0);
}

TEST_CASE("combatDamage.rejectsMismatchedTargetAndInvalidPayloadAtomically") {
    auto target    = state();
    auto request   = requestFor(target);
    request.target = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    eve::combat::DamageRuntime damage;
    CHECK(!damage.apply(target, request).ok());
    CHECK_EQ(target.health, 100.0);
    request.target     = target.subject;
    request.damageType = "Damage..Invalid";
    CHECK(!damage.apply(target, request).ok());
    CHECK_EQ(target.health, 100.0);
}
