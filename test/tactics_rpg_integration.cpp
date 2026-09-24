#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsSettlement.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

class HealthPolicy final : public eve::settlement::ISettlementPolicy {
public:
    explicit HealthPolicy(double& health) : health_(health) {}

    eve::Result<void> validate(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> sourceModifiers(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> targetMitigation(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> armorShield(eve::settlement::SettlementContext&) override {
        return eve::Result<void>::success();
    }
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
                return eve::Result<void>::success();
            },
            [this, before]() { health_ = before; }));
    }

private:
    double& health_;
};

}  // namespace

TEST_CASE("tactics.rpgSettlementOutcomeDefeatsUnitAndCompletesObjective") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000f0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto blue = subject("00000000-0000-0000-0000-0000000000f1");
    const auto red = subject("00000000-0000-0000-0000-0000000000f2");
    const auto attacker = subject("00000000-0000-0000-0000-0000000000f3");
    const auto defender = subject("00000000-0000-0000-0000-0000000000f4");
    auto blueSide = tactics.newSide(battle, blue);
    auto redSide = tactics.newSide(battle, red);
    REQUIRE(blueSide.ok());
    REQUIRE(redSide.ok());
    REQUIRE(tactics.newUnit(battle, blueSide.value(), attacker, {}, {0, 0, 0}, {1, 300, 0, 10}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), defender, {}, {1, 0, 0}, {1, 300, 0, 5}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id = *eve::LogicalId::parse("test:eliminate-after-settlement");
    objective.kind = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blue;
    objective.targetSide = red;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
        REQUIRE(tactics.advance(battle, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());

    const auto strike = eve::LogicalId::parse("test:settlement-strike");
    REQUIRE(strike.has_value());
    auto declared = tactics.useAbility(battle, attacker, *strike, {1, 0, 0}, defender, "{\"power\":12}");
    REQUIRE(declared.ok());

    eve::settlement::SettlementRule armor;
    armor.id           = "tactics.integration.armor";
    armor.source       = "test:defender";
    armor.stage        = eve::settlement::StageKind::TargetMitigation;
    armor.operation    = eve::settlement::RuleOperation::ResistFlat;
    armor.value        = 2.0;
    armor.filter.kinds = {"damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({armor}).ok());
    eve::tactics::TacticsSettlementRuntime runtime;
    REQUIRE(runtime.configureSettlementRules(rules).ok());

    eve::tactics::AbilitySettlementRequest request;
    request.ability   = declared.value();
    request.kind      = "damage";
    request.magnitude = 12.0;
    request.tick      = eve::SimulationTick(3);
    double       health = 8.0;
    HealthPolicy policy(health);
    auto         settled = runtime.settle(request, policy);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().applied, 8.0);
    REQUIRE(health <= 0.0);

    // Tactics consumes only the canonical settlement outcome. It never reads
    // the health owner's state or retains its policy.
    REQUIRE(tactics.defeatUnit(battle, defender).ok());
    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK_EQ(static_cast<int>(status.value()), static_cast<int>(eve::tactics::BattleStatus::Ended));
}
