#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Value.h"
#include "rpg/Battle.h"
#include "rpg/BattleControl.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// gameplay_control_rpg.cpp
namespace {
namespace gameplay_control_rpg {
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
    auto*            hero  = fighter(50.0, 100.0, 10.0);
    auto*            enemy = fighter(1.0, 20.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto              battleSubject = subject("00000000-0000-7000-8000-000000000601");
    const auto              heroSubject   = subject("00000000-0000-7000-8000-000000000602");
    const auto              enemySubject  = subject("00000000-0000-7000-8000-000000000603");
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
    auto                 playerActions     = control.availableGameplayActions(player, battleSubject, heroSubject);
    auto                 automationActions = control.availableGameplayActions(automation, battleSubject, heroSubject);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerDescriptors     = std::move(playerActions).takeValue();
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(playerDescriptors.size(), automationDescriptors.size());
    for (std::size_t index = 0; index < playerDescriptors.size(); ++index)
        CHECK_EQ(playerDescriptors[index].id, automationDescriptors[index].id);

    auto observed = control.observeGameplay(player, battleSubject);
    REQUIRE(observed.ok());
    const auto           before = std::move(observed).takeValue();
    eve::GameplayCommand attack;
    attack.id               = "rpg-player-command-1";
    attack.action           = action("rpg:attack");
    attack.subject          = heroSubject;
    attack.observedTick     = before.tick;
    attack.expectedRevision = before.revision;
    attack.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto submitted          = control.submitGameplay(player, battleSubject, attack);
    REQUIRE(submitted.ok());
    auto advanced =
        control.advanceGameplay(player, battleSubject, {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    REQUIRE(advanced.ok());
    CHECK(battle.isVictory());

    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), battleSubject.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "rpg.battle") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto events = control.gameplayEvents(player, battleSubject, 0);
    REQUIRE(events.ok());
    bool hasCausedDamage = false;
    for (const auto& event : std::move(events).takeValue())
        if (event.type == "rpg.battle.damage" && event.causationCommandId == attack.id) hasCausedDamage = true;
    CHECK(hasCausedDamage);

    hero->release();
    enemy->release();
}

TEST_CASE("gameplay.control.rpgBattleRejectsStaleCommandWithoutQueueMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            hero  = fighter(10.0, 100.0, 2.0);
    auto*            enemy = fighter(10.0, 100.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto              battleSubject = subject("00000000-0000-7000-8000-000000000611");
    const auto              heroSubject   = subject("00000000-0000-7000-8000-000000000612");
    const auto              enemySubject  = subject("00000000-0000-7000-8000-000000000613");
    eve::rpg::BattleControl control(battle, battleSubject);
    REQUIRE(control.bindParticipant(heroSubject, hero).ok());
    REQUIRE(control.bindParticipant(enemySubject, enemy).ok());
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroSubject}};

    eve::GameplayCommand stale;
    stale.id               = "rpg-stale";
    stale.action           = action("rpg:attack");
    stale.subject          = heroSubject;
    stale.expectedRevision = 0;
    stale.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto rejected          = control.submitGameplay(player, battleSubject, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);

    hero->release();
    enemy->release();
}
}  // namespace gameplay_control_rpg
}  // namespace

// rpg_encounter_catalogue.cpp
namespace {
namespace rpg_encounter_catalogue {
namespace {

void registerEncounterDependencies() {
    eve::rpg::SkillRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillDefinition skill;
    skill.id = "skill.enemy_claw";
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::QuestDefinition quest;
    quest.id = "quest.slayer";
    eve::rpg::QuestRegistry::registerQuest(quest);
}

constexpr const char* kValidEncounter = R"([
  {
    "id":"slime.west",
    "displayName":"West Slime",
    "members":[{"id":"west","displayName":"West Slime","skillId":"skill.enemy_claw",
      "attack":12,"defense":2.8,"maxHp":42,"speed":5}],
    "xpReward":40,
    "xpGrowth":1.2,
    "goldReward":10,
    "requiredQuestId":"quest.slayer",
    "notifyTopic":"kill",
    "notifyTarget":"slime",
    "notifyAmount":1,
    "defeatCounterId":"kills",
    "defeatCounterAmount":1,
    "levelPointAttributeId":"statPoints",
    "pointsPerLevel":3
  }
])";

}  // namespace

TEST_CASE("rpg.encounter.strictCatalogueCreatesExactActor") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();

    auto replaced = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value(), 1);
    const auto* definition = eve::rpg::EncounterCatalogue::find("slime.west");
    REQUIRE(definition != nullptr);
    CHECK_EQ(definition->displayName, std::string("West Slime"));
    CHECK_EQ(definition->xpReward, 40.0);
    CHECK_EQ(definition->members.size(), std::size_t(1));
    CHECK_EQ(eve::rpg::EncounterCatalogue::memberCount("slime.west"), 1);

    eve::rpg::RPGActor* actor = eve::rpg::EncounterCatalogue::createActor("slime.west");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getFinalAttribute("attack"), 12.0);
    CHECK_EQ(actor->getFinalAttribute("defense"), 2.8);
    CHECK_EQ(actor->getFinalAttribute("hp"), 42.0);
    CHECK_EQ(actor->getFinalAttribute("speed"), 5.0);
    CHECK_EQ(actor->getCurrent("hp"), 42.0);
    CHECK(actor->knowsSkill("skill.enemy_claw"));
    actor->release();

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}

TEST_CASE("rpg.encounter.invalidReplacementPreservesPublishedCatalogue") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();
    REQUIRE(eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter).ok());

    auto unknownField = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.enemy_claw","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0,"typo":true}])");
    CHECK(!unknownField.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);
    CHECK_EQ(eve::rpg::EncounterCatalogue::count(), 1);

    auto missingSkill = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.missing","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0}])");
    CHECK(!missingSkill.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}
}  // namespace rpg_encounter_catalogue
}  // namespace

// rpg_party.cpp
namespace {
namespace rpg_party {
namespace {

eve::rpg::RPGActor* makePartyActor() {
    auto* actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 100.0);
    return actor;
}

}  // namespace

TEST_CASE("rpg.party.orderedRosterRejectsDuplicateIdsAndActors") {
    eve::rpg::Party party;
    auto*           hero      = makePartyActor();
    auto*           companion = makePartyActor();

    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    CHECK_EQ(party.count(), 2);
    CHECK_EQ(party.getMemberId(0), std::string("hero"));
    CHECK(party.getMemberActor(1) == companion);
    CHECK(party.findMemberActor("hero") == hero);
    CHECK(!party.addMember("hero", companion).ok());
    CHECK(!party.addMember("duplicate.actor", hero).ok());
    CHECK_EQ(party.count(), 2);

    REQUIRE(party.removeMember("companion.ranger").ok());
    CHECK_EQ(party.count(), 1);
    CHECK(companion->getCurrent("hp") == 100.0);
    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.staleRosterFailsBattleCompositionBeforeMutation") {
    eve::rpg::Party party;
    auto*           hero      = makePartyActor();
    auto*           companion = makePartyActor();
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    companion->release();

    CHECK(party.hasStaleMembers());
    eve::rpg::Battle battle;
    auto             composed = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    CHECK(!composed.ok());
    CHECK_EQ(battle.getActorCount(), 0);

    party.clear();
    REQUIRE(party.addMember("hero", hero).ok());
    auto valid = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value(), 1);
    CHECK_EQ(battle.getActorCount(), 1);
    hero->release();
}

TEST_CASE("rpg.party.checkpointRestoresAllMembersAtomically") {
    eve::rpg::Party party;
    auto*           hero      = makePartyActor();
    auto*           companion = makePartyActor();
    hero->setBaseAttribute("attack", 20.0);
    companion->setBaseAttribute("attack", 12.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    auto checkpoint = party.checkpointJson();
    REQUIRE(checkpoint.ok());
    hero->setBaseAttribute("attack", 1.0);
    companion->setBaseAttribute("attack", 2.0);
    REQUIRE(party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);

    REQUIRE(party.removeMember("companion").ok());
    hero->setBaseAttribute("attack", 7.0);
    CHECK(!party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);

    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.checkpointRecoveryValidatesWholeRosterBeforeReviving") {
    eve::rpg::Party party;
    auto*           hero      = makePartyActor();
    auto*           companion = makePartyActor();
    hero->setBaseAttribute("mp", 40.0);
    companion->setBaseAttribute("mp", 20.0);
    hero->setCurrent("hp", 0.0);
    companion->setCurrent("hp", 0.0);
    hero->setCurrent("mp", 0.0);
    companion->setCurrent("mp", 0.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    CHECK(!party.recoverAtCheckpoint("hp", 0.0, "mp", 0.5).ok());
    CHECK_EQ(hero->getCurrent("hp"), 0.0);
    CHECK_EQ(companion->getCurrent("hp"), 0.0);
    auto recovered = party.recoverAtCheckpoint("hp", 0.5, "mp", 0.25);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 2);
    CHECK_EQ(hero->getCurrent("hp"), 50.0);
    CHECK_EQ(companion->getCurrent("hp"), 50.0);
    CHECK_EQ(hero->getCurrent("mp"), 10.0);
    CHECK_EQ(companion->getCurrent("mp"), 5.0);

    companion->release();
    hero->setCurrent("hp", 3.0);
    CHECK(!party.recoverAtCheckpoint("hp", 1.0, "mp", 1.0).ok());
    CHECK_EQ(hero->getCurrent("hp"), 3.0);
    hero->release();
}
}  // namespace rpg_party
}  // namespace

// rpg_state_canonical.cpp
namespace {
namespace rpg_state_canonical {
TEST_CASE("rpg.state.canonicalRestoreRejectsLaterMalformedActorWithoutMutation") {
    eve::rpg::SkillRegistry::clear();
    auto* first  = eve::rpg::RPGActor::createActor();
    auto* second = eve::rpg::RPGActor::createActor();

    eve::rpg::SkillDefinition definition;
    definition.id = "fireball";
    eve::rpg::SkillRegistry::registerSkill(definition);
    first->learnSkill("fireball");
    first->setSkillCooldown("fireball", 3.f);

    auto captured = eve::rpg::captureRpgState();
    REQUIRE(captured.ok());
    auto state = std::move(captured).takeValue();
    state.at(0).find("skills")->set("fireball", eve::Value::number(9.0));
    state.at(1) = eve::Value::integer(42);

    auto restored = eve::rpg::restoreRpgState(state);
    CHECK(!restored.ok());
    CHECK(eve::rpg::SkillSystem::getCooldownRemaining(first, "fireball") == 3.f);

    second->release();
    first->release();
    eve::rpg::SkillRegistry::clear();
}
}  // namespace rpg_state_canonical
}  // namespace
