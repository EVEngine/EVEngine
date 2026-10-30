// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"rpg"}
*/
// clang-format on
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/ActionDamageSink.h"
#include "rpg/ActionKillReward.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
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
