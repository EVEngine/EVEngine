// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"rpg"}
*/
// clang-format on
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/ActionDamageSink.h"
#include "rpg/ActionEncounter.h"
#include "rpg/ActionKillReward.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "rpg/WorldInteraction.h"
#include "rpg/WorldState.h"
#include "zeroerr/unittest.h"

namespace {
using namespace eve;
using namespace eve::rpg;
SubjectRef subject(const char* text) {
    auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}
RPGActor* actor() {
    auto* value = RPGActor::createActor();
    value->setBaseAttribute("hp", 100);
    value->setCurrent("hp", 100);
    return value;
}
struct Fixture {
    ecs::Table       world;
    ecs::ScopedTable scope{world};
    RPGActor*        hero    = actor();
    RPGActor*        enemy   = actor();
    SubjectRef       heroId  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    SubjectRef       enemyId = subject("01020304-0506-0708-890a-0b0c0d0e0f11");
    GameState        state;
    Tracker          tracker;
    explicit Fixture(bool questGated = true) {
        SkillDefinition skill;
        skill.id = "kill-reward.claw";
        SkillRegistry::registerSkill(skill);
        QuestDefinition quest;
        quest.id          = "kill-reward.hunt";
        quest.startPolicy = "auto";
        quest.objectives  = {{"kill", "kill", "wolf", 1}};
        if (questGated) QuestRegistry::registerQuest(quest);
        tracker.syncAuto();
        std::string definition = R"([
          {"id":"wolf","displayName":"Wolf","members":[
            {"id":"wolf","displayName":"Wolf","skillId":"kill-reward.claw","attack":1,"defense":0,"maxHp":100,"speed":1}],
           "xpReward":100,"xpGrowth":1.2,"goldReward":10,"requiredQuestId":"kill-reward.hunt",
           "notifyTopic":"kill","notifyTarget":"wolf","notifyAmount":1,
           "defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"points","pointsPerLevel":2}
        ])";
        if (!questGated) {
            const std::string gate = "\"requiredQuestId\":\"kill-reward.hunt\"";
            definition.replace(definition.find(gate), gate.size(), "\"requiredQuestId\":\"\"");
        }
        auto loaded = EncounterCatalogue::replaceFromJsonStrict(definition);
        REQUIRE(loaded.ok());
    }
    ~Fixture() {
        EncounterCatalogue::clear();
        QuestRegistry::clear();
        SkillRegistry::clear();
        std::vector<LevelUpEvent> events;
        LevelSystem::pollLevelUps(events);
    }
    RPGDamageTarget        recipient() { return {ecs::handle_of(hero), heroId}; }
    RPGActionDamageReceipt kill() {
        const auto                  heroHandle  = ecs::handle_of(hero);
        const auto                  enemyHandle = ecs::handle_of(enemy);
        RPGActionDamageSink         sink([&](ecs::EntityHandle h) -> std::optional<RPGDamageTarget> {
            if (ecs::try_get(h) == hero) return RPGDamageTarget{heroHandle, heroId};
            if (ecs::try_get(h) == enemy) return RPGDamageTarget{enemyHandle, enemyId};
            return std::nullopt;
        });
        action::ActionDamageBinding hit;
        hit.damageType = "Damage.Physical";
        hit.amount     = 100;
        action::ActionNotifyContext context;
        context.source  = heroHandle;
        context.targets = {enemyHandle};
        context.tick    = SimulationTick(11);
        auto result     = sink.apply(hit, context);
        REQUIRE(result.ok());
        auto receipts = sink.takeReceipts();
        REQUIRE_EQ(receipts.size(), 1u);
        REQUIRE(receipts.front().killed);
        return receipts.front();
    }
};
}  // namespace

TEST_CASE("Action kill reward commits progression quest and gold only once after corpse destruction") {
    Fixture    f;
    const auto death = f.kill();
    f.enemy->release();
    auto reward = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(reward.ok());
    CHECK_EQ(reward.value().levelsGained, 1);
    CHECK_EQ(f.hero->getLevel(), 2);
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
    CHECK_EQ(f.state.getVariable("kills"), 1.0);
    CHECK_EQ(f.state.getVariable("points"), 2.0);
    CHECK_EQ(f.tracker.getState("kill-reward.hunt"), "completed");
    EncounterCatalogue::clear();
    auto duplicate = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "removed");
    REQUIRE(duplicate.ok());
    CHECK(duplicate.code() == StatusCode::NoOp);
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
    CHECK_EQ(f.hero->getLevel(), 2);
    std::vector<LevelUpEvent> levels;
    LevelSystem::pollLevelUps(levels);
    CHECK_EQ(levels.size(), 1u);
}

TEST_CASE("Action kill reward failures preserve progress and permit retry of the same death") {
    Fixture    f;
    const auto death  = f.kill();
    auto       before = f.state.snapshotJson();
    REQUIRE(before.ok());
    auto missing = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "missing");
    REQUIRE(!missing.ok());
    auto wrong    = f.recipient();
    wrong.subject = f.enemyId;
    auto rejected = ActionKillReward::settle(wrong, f.state, f.tracker, death, "wolf");
    REQUIRE(!rejected.ok());
    auto after = f.state.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(before.value(), after.value());
    CHECK_EQ(f.hero->getLevel(), 1);
    CHECK_EQ(f.tracker.getState("kill-reward.hunt"), "active");
    auto retry = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(retry.ok());
    CHECK_EQ(f.hero->getLevel(), 2);
}

TEST_CASE("Action kill reward consumed identity survives complete save-session restore") {
    Fixture    f;
    const auto death  = f.kill();
    auto       reward = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(reward.ok());
    inventory::Bag          bag(3);
    inventory::EquipmentSet equipment;
    bag.setId("bag");
    equipment.setId("equipment");
    RPGSaveSession session;
    session.bind(f.state, f.tracker, *f.hero, bag, equipment);
    auto configured = session.setContentVersion("action-kill-test-v1");
    REQUIRE(configured.ok());
    auto saved = session.snapshotJson();
    REQUIRE(saved.ok());
    GameState restoredState;
    Tracker   restoredTracker;
    auto*     restoredActor = actor();
    session.bind(restoredState, restoredTracker, *restoredActor, bag, equipment);
    auto restored = session.restoreSnapshotJson(saved.value());
    REQUIRE(restored.ok());
    f.hero->release();
    auto duplicate = ActionKillReward::settle({ecs::handle_of(restoredActor), f.heroId}, restoredState, restoredTracker,
                                              death, "wolf");
    REQUIRE(duplicate.ok());
    CHECK(duplicate.code() == StatusCode::NoOp);
    CHECK_EQ(restoredActor->getLevel(), 2);
    CHECK_EQ(restoredState.getVariable("gold"), 10.0);
    CHECK_EQ(restoredTracker.getState("kill-reward.hunt"), "completed");
}

TEST_CASE("Action kill reward rejects stale recipients and nondeath receipts without consuming reward") {
    Fixture f;
    auto    death = f.kill();
    death.killed  = false;
    auto nondeath = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(!nondeath.ok());
    death.killed   = true;
    auto recipient = f.recipient();
    f.hero->release();
    auto stale = ActionKillReward::settle(recipient, f.state, f.tracker, death, "wolf");
    REQUIRE(!stale.ok());
    CHECK_EQ(f.state.getVariable("gold"), 0.0);
    CHECK_EQ(f.tracker.getState("kill-reward.hunt"), "active");
}

TEST_CASE("Action kill reward failed quest precondition permits retry without consuming death") {
    Fixture    f;
    const auto death = f.kill();
    REQUIRE(f.tracker.abandon("kill-reward.hunt"));
    auto before = f.state.snapshotJson();
    REQUIRE(before.ok());
    auto rejected = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(!rejected.ok());
    auto after = f.state.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(before.value(), after.value());
    CHECK_EQ(f.hero->getLevel(), 1);
    REQUIRE(f.tracker.reset("kill-reward.hunt"));
    f.tracker.syncAuto();
    auto retry = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(retry.ok());
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
}

TEST_CASE("Action kill reward rejects a multi-member encounter without granting aggregate rewards") {
    Fixture    f;
    const auto death  = f.kill();
    auto       loaded = EncounterCatalogue::replaceFromJsonStrict(R"([
      {"id":"pack","displayName":"Pack","members":[
        {"id":"wolf-a","displayName":"Wolf","skillId":"kill-reward.claw","attack":1,"defense":0,"maxHp":100,"speed":1},
        {"id":"wolf-b","displayName":"Wolf","skillId":"kill-reward.claw","attack":1,"defense":0,"maxHp":100,"speed":1}],
       "xpReward":100,"xpGrowth":1.2,"goldReward":10,"requiredQuestId":"kill-reward.hunt",
       "notifyTopic":"kill","notifyTarget":"wolf","notifyAmount":1,
       "defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"points","pointsPerLevel":2}
    ])");
    REQUIRE(loaded.ok());
    auto before = f.state.snapshotJson();
    REQUIRE(before.ok());
    auto rejected = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "pack");
    REQUIRE(!rejected.ok());
    auto after = f.state.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(before.value(), after.value());
    CHECK_EQ(f.hero->getLevel(), 1);
    CHECK_EQ(f.tracker.getState("kill-reward.hunt"), "active");
}

TEST_CASE("Action kill reward without quest prerequisite grants ordinary enemy rewards once") {
    Fixture    f(false);
    const auto death  = f.kill();
    auto       reward = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(reward.ok());
    CHECK_EQ(f.hero->getLevel(), 2);
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
    CHECK_EQ(f.state.getVariable("kills"), 1.0);
    auto repeat = ActionKillReward::settle(f.recipient(), f.state, f.tracker, death, "wolf");
    REQUIRE(repeat.ok());
    CHECK(repeat.code() == StatusCode::NoOp);
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
}

TEST_CASE("Action kill reward without quest prerequisite still notifies active quests") {
    Fixture         f(false);
    QuestDefinition quest;
    quest.id          = "observer";
    quest.startPolicy = "auto";
    quest.objectives  = {{"kill", "kill", "wolf", 1}};
    QuestRegistry::registerQuest(quest);
    f.tracker.syncAuto();
    auto reward = ActionKillReward::settle(f.recipient(), f.state, f.tracker, f.kill(), "wolf");
    REQUIRE(reward.ok());
    CHECK_EQ(f.tracker.getState("observer"), "completed");
    f.enemy->release();
    f.enemy   = actor();
    f.enemyId = subject("01020304-0506-0708-890a-0b0c0d0e0f12");
    auto next = ActionKillReward::settle(f.recipient(), f.state, f.tracker, f.kill(), "wolf");
    REQUIRE(next.ok());
    CHECK_EQ(f.state.getVariable("gold"), 20.0);
    CHECK_EQ(f.state.getVariable("kills"), 2.0);
    CHECK_EQ(f.tracker.getState("observer"), "completed");
}

TEST_CASE("Action encounter closes damage rewards full-bag pickup equipment and save restore") {
    Fixture         f(false);
    ActionEncounter encounter;
    REQUIRE(encounter.bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
    f.hero->setBaseAttribute("attack", 10);
    auto hit = encounter.hit(true, 100, 1);
    REQUIRE(hit.ok());
    REQUIRE(encounter.hasPendingReward());
    CHECK(!encounter.hit(true, 100, 2).ok());
    CHECK(!encounter.claim(f.state, f.tracker, "missing").ok());
    CHECK(encounter.hasPendingReward());
    REQUIRE(encounter.claim(f.state, f.tracker, "wolf").ok());
    CHECK(!encounter.hasPendingReward());
    REQUIRE(encounter.claim(f.state, f.tracker, "wolf").ok());
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
    inventory::ItemDefinition sword;
    sword.id        = "loop.sword";
    sword.equipSlot = "weapon";
    inventory::ItemRegistry::registerItem(sword);
    EquipmentSystem::registerItemStat(sword.id, {"attack", "add", 5, 0});
    inventory::Bag bag(1);
    bag.setId("loop.bag");
    REQUIRE_EQ(bag.addItem(sword.id, 1), 1);
    inventory::EquipmentSet equipment;
    equipment.setId("loop.equipment");
    equipment.defineSlot("weapon");
    WorldLootRequest loot;
    loot.mapId        = "loop.loot";
    loot.objectId     = f.enemyId.format();
    loot.itemId       = sword.id;
    loot.itemQuantity = 1;
    CHECK(!WorldInteraction::collectLoot(f.state, f.tracker, bag, loot).ok());
    WorldState world(f.state);
    CHECK(!world.isObjectConsumed(loot.mapId, loot.objectId));
    REQUIRE(equipment.equipFromBag("weapon", &bag, 0));
    REQUIRE_EQ(EquipmentSystem::syncEquipModifiers(f.hero, &equipment), 1);
    CHECK_EQ(f.hero->getFinalAttribute("attack"), 15.0);
    REQUIRE(WorldInteraction::collectLoot(f.state, f.tracker, bag, loot).ok());
    CHECK(!WorldInteraction::collectLoot(f.state, f.tracker, bag, loot).ok());
    CHECK_EQ(bag.countItem(sword.id), 1);
    RPGSaveSession save;
    save.bind(f.state, f.tracker, *f.hero, bag, equipment);
    REQUIRE(save.setContentVersion("loop-v1").ok());
    auto snapshot = save.snapshotJson();
    REQUIRE(snapshot.ok());
    REQUIRE(world.resetObject(loot.mapId, loot.objectId).ok());
    f.state.setVariable("gold", 0);
    REQUIRE(save.restoreSnapshotJson(snapshot.value()).ok());
    CHECK(world.isObjectConsumed(loot.mapId, loot.objectId));
    CHECK_EQ(f.state.getVariable("gold"), 10.0);
    CHECK_EQ(f.hero->getFinalAttribute("attack"), 15.0);
    CHECK_EQ(bag.countItem(sword.id), 1);
    CHECK(!WorldInteraction::collectLoot(f.state, f.tracker, bag, loot).ok());
    EquipmentSystem::clearAll();
    inventory::ItemRegistry::clear();
}

TEST_CASE("Action encounter rejects stale participants and backwards ticks") {
    Fixture         f(false);
    ActionEncounter encounter;
    REQUIRE(encounter.bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
    REQUIRE(encounter.hit(true, 10, 4).ok());
    CHECK(!encounter.hit(true, 10, 3).ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 90.0);
    encounter.close();
    CHECK(!encounter.hit(false, 10, 5).ok());
    REQUIRE(encounter.bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
    f.hero->release();
    CHECK(!encounter.hit(false, 10, 5).ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 90.0);
}


TEST_CASE("Action encounter rejects overlapping providers before mutation in either registration order") {
    for (bool rivalFirst : {false, true}) {
        Fixture                          f(false);
        std::unique_ptr<ActionEncounter> primary, rival;
        if (rivalFirst) rival = std::make_unique<ActionEncounter>();
        primary = std::make_unique<ActionEncounter>();
        if (!rivalFirst) rival = std::make_unique<ActionEncounter>();
        REQUIRE(primary->bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
        REQUIRE(rival->bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
        auto rejected = primary->hit(true, 100, 10);
        REQUIRE(!rejected.ok());
        CHECK_EQ(f.enemy->getCurrent("hp"), 100.0);
        CHECK_EQ(f.hero->getCurrent("hp"), 100.0);
        CHECK(!primary->hasPendingReward());
        CHECK(!rival->hasPendingReward());
        rival->close();
        // A rejected command must not advance the encounter clock or consume a kill.
        REQUIRE(primary->hit(true, 100, 9).ok());
        REQUIRE(primary->hasPendingReward());
        REQUIRE(primary->claim(f.state, f.tracker, "wolf").ok());
        CHECK_EQ(f.state.getVariable("gold"), 10.0);
    }
}

TEST_CASE("Action encounter permits disjoint providers without global exclusivity") {
    Fixture         f(false);
    ActionEncounter primary, other;
    auto*           otherHero  = actor();
    auto*           otherEnemy = actor();
    REQUIRE(primary.bind(*f.hero, *f.enemy, f.heroId.format(), f.enemyId.format()).ok());
    REQUIRE(other
                .bind(*otherHero, *otherEnemy, "01020304-0506-0708-890a-0b0c0d0e0f20",
                      "01020304-0506-0708-890a-0b0c0d0e0f21")
                .ok());
    REQUIRE(primary.hit(true, 10, 1).ok());
    REQUIRE(other.hit(true, 20, 1).ok());
    CHECK_EQ(f.enemy->getCurrent("hp"), 90.0);
    CHECK_EQ(otherEnemy->getCurrent("hp"), 80.0);
}
