#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "card/CardEffects.h"
#include "combat/Damage.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "settlement/SettlementRules.h"
#include "tactics/TacticsSettlement.h"

#include <array>
#include <string_view>

namespace {

eve::SubjectRef subject(std::uint8_t suffix) {
    eve::PersistentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::SnapshotHashProvider replayHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::rpg::RPGActor* fighter(double health) {
    auto* actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", health);
    return actor;
}

eve::SimulationStep step(std::uint64_t tick, double seconds) {
    auto delta = eve::Duration::fromSeconds(seconds);
    REQUIRE(delta.ok());
    return {eve::SimulationTick(tick), std::move(delta).takeValue()};
}

eve::effects::EffectPolicy oncePolicy() {
    eve::effects::EffectPolicy policy;
    policy.stackMode  = eve::effects::StackMode::NewInstance;
    policy.stackCount = eve::effects::StackCountPolicy::Keep;
    policy.maxStacks  = 1;
    return policy;
}

class TacticalHealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit TacticalHealthPolicy(double& health, bool failCommit = false)
        : health_(health), failCommit_(failCommit) {}
    eve::Result<void> validate(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override { return eve::Result<void>::success(); }
    eve::Result<void> clamp(eve::settlement::SettlementContext& context) override {
        return context.setClampMax(health_);
    }
    eve::Result<eve::settlement::PreparedApply> prepareApply(
        const eve::settlement::SettlementContext& context) override {
        const double before = health_;
        const double after  = before - context.magnitude();
        return eve::Result<eve::settlement::PreparedApply>::success(eve::settlement::PreparedApply(
            [this, after]() {
                health_ = after;
                if (failCommit_)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected tactics commit failure"));
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
    bool    failCommit_ = false;
};

eve::settlement::SettlementRuleSet lifestealRules() {
    eve::settlement::SettlementRule rule;
    rule.id           = "rpg.replay.lifesteal";
    rule.source       = "test:vampiric";
    rule.stage        = eve::settlement::StageKind::Trigger;
    rule.operation    = eve::settlement::RuleOperation::Lifesteal;
    rule.value        = 0.5;
    rule.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    rules.configure({std::move(rule)}).ignore("test fixture rule configuration");
    return rules;
}

}  // namespace

TEST_CASE("settlement.replay.rpgLifestealChainReplaysThroughDomainAdapter") {
    const auto attackerRef = subject(41);
    const auto targetRef   = subject(42);
    auto       rules       = lifestealRules();
    auto       ruleDigest  = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::settlement::SettlementPipeline pipeline;
    REQUIRE(rules.install(pipeline).ok());
    auto* attacker = fighter(50.0);
    auto* target   = fighter(100.0);
    const auto execute = [&](const eve::settlement::SettlementRequest& request) {
        auto* targetActor = request.target == attackerRef ? attacker : target;
        auto* sourceActor = request.source == attackerRef ? attacker : target;
        eve::rpg::RPGSettlementAdapter policy(*targetActor, request.target, sourceActor, request.source);
        return pipeline.settle(request, policy);
    };

    eve::settlement::SettlementRequest root;
    root.source    = attackerRef;
    root.target    = targetRef;
    root.kind      = "damage";
    root.resource  = "hp";
    root.magnitude = 20.0;
    root.tick      = eve::SimulationTick(9);
    auto original = pipeline.settleChain(root, execute, 4, 8);
    REQUIRE(original.ok());
    REQUIRE_EQ(original.value().size(), 2u);
    REQUIRE(original.value()[0].result.has_value());
    REQUIRE(original.value()[1].result.has_value());
    CHECK_EQ(target->getCurrent("hp"), 80.0);
    CHECK_EQ(attacker->getCurrent("hp"), 60.0);

    std::array<std::string, 2> records;
    for (std::size_t index = 0; index < records.size(); ++index) {
        auto record = eve::settlement::createSettlementReplayRecord(
            original.value()[index].request, ruleDigest.value(), *original.value()[index].result, replayHash());
        REQUIRE(record.ok());
        records[index] = std::move(record).takeValue();
    }
    attacker->release();
    target->release();

    attacker = fighter(50.0);
    target   = fighter(100.0);
    auto replayRoot = eve::settlement::settlementReplayRequest(records[0]);
    REQUIRE(replayRoot.ok());
    auto replayed = pipeline.settleChain(replayRoot.value(), execute, 4, 8);
    REQUIRE(replayed.ok());
    REQUIRE_EQ(replayed.value().size(), 2u);
    for (std::size_t index = 0; index < records.size(); ++index) {
        REQUIRE(replayed.value()[index].result.has_value());
        auto verified = eve::settlement::verifySettlementReplayRecord(
            records[index], ruleDigest.value(), *replayed.value()[index].result, replayHash());
        REQUIRE(verified.ok());
        CHECK(verified.value().empty());
    }
    CHECK_EQ(target->getCurrent("hp"), 80.0);
    CHECK_EQ(attacker->getCurrent("hp"), 60.0);
    attacker->release();
    target->release();
}

TEST_CASE("settlement.replay.rtsDamageBatchRetainsAndVerifiesCanonicalAudits") {
    eve::settlement::SettlementRule fortified;
    fortified.id           = "rts.replay.fortified";
    fortified.source       = "test:fortification";
    fortified.stage        = eve::settlement::StageKind::TargetMitigation;
    fortified.operation    = eve::settlement::RuleOperation::ResistPercent;
    fortified.value        = 0.25;
    fortified.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({fortified}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::combat::DamageRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    std::array<eve::combat::CombatState, 2> targets;
    std::array<eve::combat::DamageRequest, 2> requests;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        targets[index].subject = subject(static_cast<std::uint8_t>(50 + index));
        targets[index].health = targets[index].maxHealth = 100.0;
        targets[index].poise = targets[index].maxPoise = 20.0;
        requests[index].source       = subject(49);
        requests[index].target       = targets[index].subject;
        requests[index].damageType   = "damage.physical";
        requests[index].healthDamage = 20.0 + static_cast<double>(index) * 4.0;
    }

    std::array<std::string, 2> records;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto outcome = runtime.apply(targets[index], requests[index]);
        REQUIRE(outcome.ok());
        CHECK_EQ(outcome.value().settlementRequest.target, targets[index].subject);
        CHECK_EQ(outcome.value().settlementResult.applied, requests[index].healthDamage * 0.75);
        auto record = eve::settlement::createSettlementReplayRecord(
            outcome.value().settlementRequest, ruleDigest.value(), outcome.value().settlementResult, replayHash());
        REQUIRE(record.ok());
        records[index] = std::move(record).takeValue();
    }

    std::array<eve::combat::CombatState, 2> replayTargets;
    for (std::size_t index = 0; index < replayTargets.size(); ++index) {
        replayTargets[index].subject = targets[index].subject;
        replayTargets[index].health = replayTargets[index].maxHealth = 100.0;
        replayTargets[index].poise = replayTargets[index].maxPoise = 20.0;
        auto restoredRequest = eve::settlement::settlementReplayRequest(records[index]);
        REQUIRE(restoredRequest.ok());
        CHECK_EQ(restoredRequest.value().magnitude, requests[index].healthDamage);
        auto replayed = runtime.apply(replayTargets[index], requests[index]);
        REQUIRE(replayed.ok());
        auto verified = eve::settlement::verifySettlementReplayRecord(
            records[index], ruleDigest.value(), replayed.value().settlementResult, replayHash());
        REQUIRE(verified.ok());
        CHECK(verified.value().empty());
    }
}

TEST_CASE("settlement.replay.cardPeriodicRestoreRetainsAndVerifiesCanonicalAudit") {
    eve::settlement::SettlementRule resistance;
    resistance.id           = "card.replay.resistance";
    resistance.source       = "test:ward";
    resistance.stage        = eve::settlement::StageKind::TargetMitigation;
    resistance.operation    = eve::settlement::RuleOperation::ResistPercent;
    resistance.value        = 0.2;
    resistance.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({resistance}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::card::CardEffectAdapter card;
    REQUIRE(card.configureSettlementRules(rules).ok());
    eve::card::CardEffectDefinition damage;
    damage.id        = "card.replay.periodic";
    damage.source    = "test:spell";
    damage.duration  = 2.0;
    damage.period    = 1.0;
    damage.magnitude = 10.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject(60)).ok());
    const auto before = card.snapshot();

    auto original = card.advance(step(1, 1.0));
    REQUIRE(original.ok());
    REQUIRE_EQ(original.value().settlements.size(), 1u);
    REQUIRE(original.value().settlements[0].result.has_value());
    CHECK_EQ(card.target().health, 92);
    auto record = eve::settlement::createSettlementReplayRecord(
        original.value().settlements[0].request, ruleDigest.value(),
        *original.value().settlements[0].result, replayHash());
    REQUIRE(record.ok());

    REQUIRE(card.restore(before).ok());
    auto replayed = card.advance(step(1, 1.0));
    REQUIRE(replayed.ok());
    REQUIRE_EQ(replayed.value().settlements.size(), 1u);
    REQUIRE(replayed.value().settlements[0].result.has_value());
    auto verified = eve::settlement::verifySettlementReplayRecord(
        record.value(), ruleDigest.value(), *replayed.value().settlements[0].result, replayHash());
    REQUIRE(verified.ok());
    CHECK(verified.value().empty());
    CHECK_EQ(card.target().health, 92);
}

TEST_CASE("settlement.replay.cardProjectionFailureDoesNotCommitLifecycleCandidate") {
    eve::card::CardEffectAdapter card;
    eve::card::CardEffectDefinition damage;
    damage.id        = "card.failure.periodic";
    damage.duration  = 3.0;
    damage.period    = 1.0;
    damage.magnitude = 10.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject(63)).ok());
    const auto clean = card.snapshot();

    auto malformed = clean;
    eve::effects::EffectDefinition invalidRule;
    invalidRule.id        = "card.failure.invalid-rule";
    invalidRule.stackKey  = invalidRule.id;
    invalidRule.duration  = 3.0;
    invalidRule.policy    = oncePolicy();
    REQUIRE(invalidRule.payload.setJson("settlement.rule", "42").ok());
    REQUIRE(malformed.effects.apply(invalidRule, subject(63).format()).ok());
    REQUIRE(card.restore(malformed).ok());

    const auto before = card.snapshot();
    auto       failed = card.advance(step(1, 1.0));
    CHECK(!failed.ok());
    const auto after = card.snapshot();
    CHECK_EQ(after.target.health, before.target.health);
    CHECK_EQ(after.effects.effectCount(), before.effects.effectCount());
    for (int index = 0; index < before.effects.effectCount(); ++index) {
        REQUIRE(before.effects.effectAt(index) != nullptr);
        REQUIRE(after.effects.effectAt(index) != nullptr);
        CHECK_EQ(after.effects.effectAt(index)->id, before.effects.effectAt(index)->id);
        CHECK_EQ(after.effects.effectAt(index)->remaining, before.effects.effectAt(index)->remaining);
        CHECK_EQ(after.effects.effectAt(index)->periodElapsed, before.effects.effectAt(index)->periodElapsed);
    }

    REQUIRE(card.restore(clean).ok());
    auto retried = card.advance(step(1, 1.0));
    REQUIRE(retried.ok());
    CHECK_EQ(card.target().health, 90);
}

TEST_CASE("settlement.replay.tacticsCanonicalProjectionReplaysThroughDomainAdapter") {
    eve::settlement::SettlementRule highGround;
    highGround.id                  = "tactics.replay.high-ground";
    highGround.source              = "test:board";
    highGround.stage               = eve::settlement::StageKind::SourceModifiers;
    highGround.operation           = eve::settlement::RuleOperation::Multiply;
    highGround.value               = 1.5;
    highGround.filter.kinds        = {"damage"};
    highGround.filter.requiredTags = {"tactics:ability"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({highGround}).ok());
    auto ruleDigest = rules.digest(replayHash());
    REQUIRE(ruleDigest.ok());

    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(61);
    request.ability.targetUnit = subject(62);
    request.kind               = "damage";
    request.magnitude          = 20.0;
    request.tick               = eve::SimulationTick(12);
    request.context            = eve::Value(eve::Value::Object{{"range", 2.0}});
    auto canonical = eve::tactics::makeSettlementRequest(request);
    REQUIRE(canonical.ok());

    double               health = 100.0;
    TacticalHealthPolicy policy(health);
    auto                 original = runtime.settle(request, policy);
    REQUIRE(original.ok());
    CHECK_EQ(health, 70.0);
    auto record = eve::settlement::createSettlementReplayRecord(
        canonical.value(), ruleDigest.value(), original.value(), replayHash());
    REQUIRE(record.ok());
    auto restoredRequest = eve::settlement::settlementReplayRequest(record.value());
    REQUIRE(restoredRequest.ok());
    CHECK_EQ(restoredRequest.value().source, canonical.value().source);
    CHECK_EQ(restoredRequest.value().target, canonical.value().target);
    CHECK_EQ(restoredRequest.value().tags, canonical.value().tags);

    health = 100.0;
    auto replayed = runtime.settle(request, policy);
    REQUIRE(replayed.ok());
    auto verified = eve::settlement::verifySettlementReplayRecord(
        record.value(), ruleDigest.value(), replayed.value(), replayHash());
    REQUIRE(verified.ok());
    CHECK(verified.value().empty());
    CHECK_EQ(health, 70.0);
}

TEST_CASE("settlement.replay.tacticsCommitFailureRollsBackDomainState") {
    eve::tactics::TacticsSettlementRuntime runtime;
    eve::tactics::AbilitySettlementRequest request;
    request.ability.actor      = subject(64);
    request.ability.targetUnit = subject(65);
    request.kind               = "damage";
    request.magnitude          = 25.0;
    request.tick               = eve::SimulationTick(13);

    double                 health = 100.0;
    TacticalHealthPolicy   policy(health, true);
    auto                   failed = runtime.settle(request, policy);
    CHECK(!failed.ok());
    CHECK_EQ(health, 100.0);
}
