#include "common/ECS.h"
#include "rpg/Settlement.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

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
    REQUIRE(tactics.newUnit(battle, blueSide.value(), attacker, {}, {0, 0, 0}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), defender, {}, {1, 0, 0}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id = *eve::LogicalId::parse("test:eliminate-after-settlement");
    objective.kind = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blue;
    objective.targetSide = red;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());

    constexpr const char* pipeline = "test.tactics.damage";
    eve::rpg::SettlementPipeline::clearPipeline(pipeline);
    eve::rpg::SettlementPipeline::registerStage(pipeline, "armor", 10, [](eve::rpg::SettlementContext& context) {
        const double damage = std::max(0.0, context.get("attack") - context.get("armor"));
        context.set("healthAfter", context.get("health") - damage);
    });
    eve::rpg::SettlementContext settlement;
    settlement.kind = "damage";
    settlement.set("attack", 12.0);
    settlement.set("armor", 2.0);
    settlement.set("health", 8.0);
    eve::rpg::SettlementPipeline::run(pipeline, settlement);
    REQUIRE(settlement.get("healthAfter") <= 0.0);

    // The game-level adapter translates only the tactical outcome. Tactics
    // never reads RPG health or retains an RPGActor pointer.
    REQUIRE(tactics.defeatUnit(battle, defender).ok());
    eve::rpg::SettlementPipeline::clearPipeline(pipeline);
    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK_EQ(static_cast<int>(status.value()), static_cast<int>(eve::tactics::BattleStatus::Ended));
}
