#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "rpg/Battle.h"
#include "rpg/BattleControl.h"
#include "rpg/RPGActor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::rpg::RPGActor* fighter(double attack, double hp, double speed) {
    auto* actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0.0);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", speed);
    actor->setCurrent("hp", hp);
    return actor;
}

}  // namespace

TEST_CASE("gameplay.control.rpgBattleUsesTheSameCheckedActionForPlayerAndAutomation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto* hero = fighter(50.0, 100.0, 10.0);
    auto* enemy = fighter(1.0, 20.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto battleSubject = subject("00000000-0000-7000-8000-000000000601");
    const auto heroSubject = subject("00000000-0000-7000-8000-000000000602");
    const auto enemySubject = subject("00000000-0000-7000-8000-000000000603");
    eve::rpg::BattleControl control(battle, battleSubject);
    REQUIRE(control.bindParticipant(heroSubject, hero).ok());
    REQUIRE(control.bindParticipant(enemySubject, enemy).ok());

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "rpg.battle") discovered = true;
    });
    CHECK(discovered);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroSubject}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {heroSubject}};
    auto playerActions = control.availableGameplayActions(player, battleSubject, heroSubject);
    auto automationActions = control.availableGameplayActions(automation, battleSubject, heroSubject);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerDescriptors = std::move(playerActions).takeValue();
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(playerDescriptors.size(), automationDescriptors.size());
    for (std::size_t index = 0; index < playerDescriptors.size(); ++index)
        CHECK_EQ(playerDescriptors[index].id, automationDescriptors[index].id);

    auto observed = control.observeGameplay(player, battleSubject);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();
    eve::GameplayCommand attack;
    attack.id = "rpg-player-command-1";
    attack.action = action("rpg:attack");
    attack.subject = heroSubject;
    attack.observedTick = before.tick;
    attack.expectedRevision = before.revision;
    attack.parameters = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto submitted = control.submitGameplay(player, battleSubject, attack);
    REQUIRE(submitted.ok());
    auto advanced = control.advanceGameplay(
        player, battleSubject,
        {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    REQUIRE(advanced.ok());
    CHECK(battle.isVictory());

    auto events = control.gameplayEvents(player, battleSubject, 0);
    REQUIRE(events.ok());
    bool hasCausedDamage = false;
    for (const auto& event : std::move(events).takeValue())
        if (event.type == "rpg.battle.damage" &&
            event.causationCommandId == attack.id)
            hasCausedDamage = true;
    CHECK(hasCausedDamage);

    hero->release();
    enemy->release();
}

TEST_CASE("gameplay.control.rpgBattleRejectsStaleCommandWithoutQueueMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto* hero = fighter(10.0, 100.0, 2.0);
    auto* enemy = fighter(10.0, 100.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto battleSubject = subject("00000000-0000-7000-8000-000000000611");
    const auto heroSubject = subject("00000000-0000-7000-8000-000000000612");
    const auto enemySubject = subject("00000000-0000-7000-8000-000000000613");
    eve::rpg::BattleControl control(battle, battleSubject);
    REQUIRE(control.bindParticipant(heroSubject, hero).ok());
    REQUIRE(control.bindParticipant(enemySubject, enemy).ok());
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroSubject}};

    eve::GameplayCommand stale;
    stale.id = "rpg-stale";
    stale.action = action("rpg:attack");
    stale.subject = heroSubject;
    stale.expectedRevision = 0;
    stale.parameters = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto rejected = control.submitGameplay(player, battleSubject, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);

    hero->release();
    enemy->release();
}
