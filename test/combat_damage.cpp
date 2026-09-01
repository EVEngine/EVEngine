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
