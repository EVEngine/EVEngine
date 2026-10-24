#include "combat/Damage.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace {
eve::combat::CombatState targetState() {
    auto id = eve::PersistentId::parse("01020304-0506-0708-890a-0b0c0d0e0f10");
    REQUIRE(id.has_value());
    return {eve::SubjectRef::fromPersistentId(*id), 30.0, 30.0, 20.0, 20.0};
}
eve::combat::DamageRequest hit(const eve::combat::CombatState& target) {
    eve::combat::DamageRequest request;
    request.source       = target.subject;
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 50.0;
    request.poiseDamage  = 15.0;
    request.tick         = eve::SimulationTick(42);
    request.critical     = true;
    return request;
}
}  // namespace

TEST_CASE("combatSettlement.canonicalStagesAndActualDamage") {
    auto                       target  = targetState();
    auto                       request = hit(target);
    eve::combat::DamageRuntime runtime;
    auto                       predicted = runtime.previewSettlement(target, request);
    REQUIRE(predicted.ok());
    CHECK_EQ(target.health, 30.0);
    CHECK_EQ(predicted.value().settlementResult.applied, 30.0);
    auto outcome = runtime.apply(target, request);
    REQUIRE(outcome.ok());
    const auto& result = outcome.value().settlementResult;
    CHECK_EQ(result.requested, 50.0);
    CHECK_EQ(result.applied, 30.0);
    CHECK_EQ(result.clamped, 20.0);
    CHECK(result.critical);
    CHECK_EQ(result.tick.value(), 42u);
    CHECK_EQ(result.stages.size(), 8u);
    REQUIRE(result.event.has_value());
    CHECK_EQ(target.health, 0.0);
    CHECK_EQ(target.poise, 5.0);
    CHECK_EQ(outcome.value().appliedHealthDamage, result.applied);
}

TEST_CASE("combatSettlement.customStageAndTriggerFailureAreAtomic") {
    auto                                target  = targetState();
    auto                                request = hit(target);
    eve::settlement::SettlementPipeline pipeline;
    auto                                added = pipeline.addStage(
        eve::settlement::StageKind::SourceModifiers, "test.amplify", 10,
        [](eve::settlement::SettlementContext& context) { return context.setMagnitude(context.magnitude() * 2.0); });
    REQUIRE(added.ok());
    added = pipeline.addStage(eve::settlement::StageKind::Trigger, "test.reject", 10,
                              [](eve::settlement::SettlementContext&) {
                                  return eve::Result<void>::failure(
                                      eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "injected failure"));
                              });
    REQUIRE(added.ok());
    eve::combat::DamageRuntime runtime;
    auto                       rejected = runtime.apply(target, request, pipeline);
    CHECK(!rejected.ok());
    CHECK_EQ(target.health, 30.0);
    CHECK_EQ(target.poise, 20.0);
}

TEST_CASE("combatSettlement.eventAppendFailureRollsBackBothAttributes") {
    auto                                target = targetState();
    eve::settlement::SettlementPipeline pipeline;
    // A log without entropy cannot assign the canonical event's nil ID.
    eve::game_event::GameEventLog log;
    eve::combat::DamageRuntime    runtime;
    auto                          rejected = runtime.apply(target, hit(target), pipeline, std::ref(log));
    CHECK(!rejected.ok());
    CHECK_EQ(target.health, 30.0);
    CHECK_EQ(target.poise, 20.0);
    CHECK_EQ(log.size(), 0);
}

TEST_CASE("combatSettlement.rejectsOverflowBeforeCommit") {
    class OverflowRule final : public eve::combat::IDamageRule {
        eve::Result<eve::combat::DamageAmounts> evaluate(const eve::combat::DamageRequest&,
                                                         const eve::combat::CombatState&) const override {
            return eve::Result<eve::combat::DamageAmounts>::success({1.0, 1.0, std::numeric_limits<double>::max()});
        }
    } rule;
    auto target         = targetState();
    auto request        = hit(target);
    request.knockback.x = 2.0;
    eve::combat::DamageRuntime runtime(&rule);
    CHECK(!runtime.apply(target, request).ok());
    CHECK_EQ(target.health, 30.0);
    CHECK_EQ(target.poise, 20.0);
}

TEST_CASE("combatSettlement.presentationPreservesRulesShieldAndHealing") {
    eve::settlement::SettlementRule amplify;
    amplify.id           = "presentation.amplify";
    amplify.source       = "effect:amplify";
    amplify.stage        = eve::settlement::StageKind::SourceModifiers;
    amplify.operation    = eve::settlement::RuleOperation::Multiply;
    amplify.value        = 2.0;
    amplify.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({amplify}).ok());
    eve::combat::DamageRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    auto target                      = targetState();
    auto request                     = hit(target);
    request.healthDamage             = 10.0;
    request.incomingDamageMultiplier = 0.5;
    request.availableShield          = 4.0;
    auto preview                     = runtime.previewSettlement(target, request);
    REQUIRE(preview.ok());
    CHECK_EQ(target.health, 30.0);
    auto result = runtime.apply(target, request);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().settlementResult.applied, 6.0);
    CHECK_EQ(result.value().absorbedShieldDamage, 4.0);
    CHECK(result.value().settlementResult.critical);
    CHECK_EQ(result.value().settlementResult.tick.value(), 42u);
    REQUIRE_EQ(result.value().settlementResult.derived.size(), 1u);
    CHECK_EQ(result.value().settlementResult.derived[0].trigger, std::string("shield_break"));
    CHECK_EQ(preview.value().settlementResult.applied, result.value().settlementResult.applied);
    auto healed = runtime.heal(target, request.source, 100.0);
    REQUIRE(healed.ok());
    CHECK_EQ(healed.value().applied, 6.0);
    CHECK_EQ(target.health, 30.0);
}
