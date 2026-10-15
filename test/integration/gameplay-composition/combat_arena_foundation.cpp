#include "action/AbilityController.h"
#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/BodyPartDamage.h"
#include "combat/CombatCharacter.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatTarget.h"
#include "combat/ComboGraph.h"
#include "combat/Damage.h"
#include "combat/HitFeel.h"
#include "combat/MeleeHit.h"
#include "combat/StandardAbilities.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class ArenaPose final : public eve::combat::IMeleePoseSource {
public:
    eve::combat::MeleePose poseValue{{0.5, 1.0, 0.0}, 0.0};
    [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
        return eve::Result<eve::combat::MeleePose>::success(poseValue);
    }
};

}  // namespace

TEST_CASE("combatArenaFoundation.composesSlicesABCDEF") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {1.2, 0.0, 0.0}}).ok());
    REQUIRE(characters.dodge(player).ok());
    CHECK(characters.state(player).value().invulnerable);

    ArenaPose poses;
    eve::combat::MeleeHitRuntime melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.5, 0.0},
                                 {},
                                 10.0,
                                 5.0,
                                 "Damage.Physical.Slash"})
                .ok());
    REQUIRE(melee.registerHurtbox({enemy, "torso", "torso", {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0}}).ok());
    REQUIRE(melee.setHurtboxPose(enemy, "torso", {{1.0, 1.0, 0.0}, 0.0}).ok());
    REQUIRE(melee.armHitbox(player, "weapon.main", eve::action::ActionExecutionId(1)).ok());
    auto hits = melee.advance(eve::SimulationTick(1));
    REQUIRE(hits.ok());
    REQUIRE_EQ(hits.value().hits.size(), 1u);

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(enemy.format(), eve::combat::CombatState{enemy, 100.0, 100.0, 30.0, 30.0});
    eve::combat::BodyPartDamageRule parts;
    REQUIRE(parts.setMultiplier("torso", 1.0).ok());
    eve::combat::DamageRuntime damage(&parts);
    auto outcomes = melee.applyHits(damage, states, hits.value().hits, {{"torso", 1.0}});
    REQUIRE(outcomes.ok());
    eve::combat::HitFeelRuntime feel;
    REQUIRE(feel.applyFromOutcome(outcomes.value().front()).ok());

    eve::action::input::ActionInputBuffer buffer(4, 5);
    REQUIRE(buffer.push({player, "heavy-attack", eve::SimulationTick(1), 1}).ok());
    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, false}).ok());
    REQUIRE(graph.match(*light, "heavy-attack", true, false).ok());

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.registerOwner(enemy).ok());
    REQUIRE(targets.hardLock(enemy, player).ok());
    REQUIRE(targets.softLock(player, {{enemy, 1.0, 1.2, 0.0, 0.0}}).ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto lightDef =
        std::find_if(catalog.value().begin(), catalog.value().end(),
                     [](const auto& definition) { return definition.id.format() == "combat-ability:light-attack"; });
    REQUIRE(lightDef != catalog.value().end());
    eve::action::ActionRuntime actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(*lightDef).ok());
    auto grant = abilities.grant("fighter:enemy", lightDef->id);
    REQUIRE(grant.ok());
    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy({enemy, "fighter:enemy", grant.value()}).ok());
    REQUIRE(ai.setLightAction(enemy, lightDef->action.id).ok());
    REQUIRE(ai.setPosition(enemy, 1.2, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 0.0, 0.0, 0.0).ok());
    auto intent = ai.nextIntent(eve::SimulationTick(2));
    REQUIRE(intent.ok());
    REQUIRE(intent.value().has_value());

    CHECK(states.at(enemy.format()).health < 100.0);
    CHECK(feel.activeCount() >= 1u);
}
