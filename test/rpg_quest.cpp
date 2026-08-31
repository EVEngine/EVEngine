#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Quest.h"
#include "rpg/Tracker.h"
#include "rpg/RPG.h"

using namespace eve::rpg;

TEST_CASE("rpg.quest.registryCRUD") {
    QuestRegistry::clear();
    QuestDefinition def;
    def.id = "quest.intro";
    def.tags = {"main"};
    def.extra["title"] = "Intro";
    QuestRegistry::registerQuest(def);
    CHECK_EQ(QuestRegistry::count(), 1);
    const QuestDefinition *found = QuestRegistry::find("quest.intro");
    REQUIRE(found != nullptr);
    CHECK(found->hasTag("main"));
    CHECK_EQ(found->getExtra("title", ""), std::string("Intro"));
    CHECK_EQ(found->startPolicy, std::string("manual"));
    CHECK_EQ(found->completePolicy, std::string("auto"));
    CHECK(QuestRegistry::remove("quest.intro"));
    CHECK_EQ(QuestRegistry::count(), 0);
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonDefaultsAndRewards") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {
        "id": "quest.hunt",
        "startPolicy": "manual",
        "completePolicy": "claim",
        "requires": ["quest.intro"],
        "tags": ["main"],
        "objectives": [
          {"id": "kill_slime", "topic": "kill", "target": "slime", "count": 10},
          {"id": "any_kill", "topic": "kill", "count": 0}
        ],
        "rewards": [
          {"type": "item", "id": "potion", "amount": 3},
          {"type": "attribute", "id": "xp", "amount": 50}
        ],
        "extra": {"title": "Hunt"}
      }
    ])");
    CHECK_EQ(n, 1);
    const QuestDefinition *d = QuestRegistry::find("quest.hunt");
    REQUIRE(d != nullptr);
    CHECK_EQ(d->startPolicy, std::string("manual"));
    CHECK_EQ(d->completePolicy, std::string("claim"));
    REQUIRE(d->requiresIds.size() == 1);
    CHECK_EQ(d->requiresIds[0], std::string("quest.intro"));
    REQUIRE(d->objectives.size() == 2);
    CHECK_EQ(d->objectives[1].count, 1);  // count<=0 → 1
    CHECK(d->objectives[1].target.empty());
    REQUIRE(d->rewards.size() == 2);
    CHECK_EQ(d->rewards[0].type, std::string("item"));
    CHECK_EQ(d->rewards[1].amount, 50.0);
    CHECK_EQ(d->getExtra("title", ""), std::string("Hunt"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonRejectsBadAndCycles") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {"startPolicy": "auto"},
      {
        "id": "dup.obj",
        "objectives": [
          {"id": "a", "topic": "kill", "count": 1},
          {"id": "a", "topic": "talk", "count": 1}
        ]
      },
      {"id": "cyc.a", "requires": ["cyc.b"]},
      {"id": "cyc.b", "requires": ["cyc.a"]},
      {"id": "ok.solo", "startPolicy": "weird", "completePolicy": "nope"}
    ])");
    CHECK_EQ(n, 1);
    CHECK(QuestRegistry::find("dup.obj") == nullptr);
    CHECK(QuestRegistry::find("cyc.a") == nullptr);
    CHECK(QuestRegistry::find("cyc.b") == nullptr);
    const QuestDefinition *ok = QuestRegistry::find("ok.solo");
    REQUIRE(ok != nullptr);
    CHECK_EQ(ok->startPolicy, std::string("manual"));
    CHECK_EQ(ok->completePolicy, std::string("auto"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.trackerInitialStatesAndSyncAuto") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.intro","startPolicy":"manual"},
      {"id":"q.auto","startPolicy":"auto","objectives":[{"id":"o","topic":"kill","target":"x","count":1}]},
      {"id":"q.locked","startPolicy":"auto","requires":["q.missing"]},
      {"id":"q.chain","startPolicy":"manual","requires":["q.intro"]}
    ])");

    Tracker log;
    CHECK_EQ(log.getCount(), 4);
    CHECK_EQ(log.getState("q.intro"), std::string("inactive"));
    CHECK_EQ(log.getState("q.auto"), std::string("active"));
    CHECK_EQ(log.getState("q.locked"), std::string("locked"));
    CHECK_EQ(log.getState("q.chain"), std::string("locked"));
    CHECK_EQ(log.getState("nope"), std::string(""));

    QuestRegistry::loadFromJson(R"([{"id":"q.late","startPolicy":"auto","objectives":[{"id":"o","topic":"kill","target":"x","count":1}]}])");
    CHECK_EQ(log.getCount(), 4);
    log.syncAuto();
    CHECK_EQ(log.getCount(), 5);
    CHECK_EQ(log.getState("q.late"), std::string("active"));
    CHECK(log.hasTag("q.intro", "x") == false);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.activateAndNotify") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.lock","startPolicy":"manual","requires":["nope"]},
      {"id":"q.hunt","startPolicy":"manual","completePolicy":"auto",
       "objectives":[
         {"id":"k","topic":"kill","target":"slime","count":2},
         {"id":"any","topic":"kill","count":3}
       ]},
      {"id":"q.flag","startPolicy":"manual","completePolicy":"auto","objectives":[]}
    ])");
    Tracker log;
    CHECK_EQ(log.canActivateReason("q.lock"), std::string("locked"));
    CHECK(!log.activate("q.lock"));
    CHECK_EQ(log.getState("q.lock"), std::string("locked"));

    CHECK(log.canActivate("q.hunt"));
    CHECK(log.activate("q.hunt"));
    CHECK_EQ(log.getState("q.hunt"), std::string("active"));
    CHECK_EQ(log.canActivateReason("q.hunt"), std::string("alreadyActive"));

    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 0);

    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 1);  // target 空，吃所有 kill
    log.notify("kill", "goblin", 5);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);  // slime 不涨
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 3);  // 封顶 3
    CHECK(log.isObjectiveDone("q.hunt", 1));

    log.notify("kill", "slime", 0);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    log.notify("kill", "slime", 1);
    CHECK(log.isObjectiveDone("q.hunt", 0));
    CHECK_EQ(log.getState("q.hunt"), std::string("completed"));

    CHECK(log.activate("q.flag"));
    CHECK_EQ(log.getState("q.flag"), std::string("completed"));

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.claimEventsAndChain") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"a","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"b","startPolicy":"auto","completePolicy":"auto","requires":["a"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    CHECK_EQ(log.getState("b"), std::string("locked"));
    CHECK(log.activate("a"));
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("a"), std::string("ready"));
    CHECK(!log.claim("nope"));
    CHECK(!log.claim("b"));
    CHECK(log.claim("a"));
    CHECK_EQ(log.getState("a"), std::string("completed"));
    CHECK_EQ(log.getState("b"), std::string("active"));

    log.pollEvents();
    bool sawProgress = false, sawReady = false, sawComplete = false, sawActivateB = false;
    for (int i = 0; i < log.getEventCount(); ++i) {
        auto act = log.getEventAction(i);
        auto id = log.getEventEntryId(i);
        if (act == "progress" && id == "a") sawProgress = true;
        if (act == "ready" && id == "a") sawReady = true;
        if (act == "complete" && id == "a") sawComplete = true;
        if (act == "activate" && id == "b") sawActivateB = true;
    }
    CHECK(sawProgress);
    CHECK(sawReady);
    CHECK(sawComplete);
    CHECK(sawActivateB);

    Tracker ach;
    QuestRegistry::loadFromJson(R"([
      {"id":"ach.kill","startPolicy":"auto","completePolicy":"auto","tags":["achievement"],
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]}
    ])");
    ach.syncAuto();
    log.notify("kill", "slime", 1);
    ach.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("ach.kill"), std::string(""));  // log 未 sync 这条也可；隔离重点是事件
    CHECK_EQ(ach.getState("ach.kill"), std::string("completed"));
    log.pollEvents();
    ach.pollEvents();
    bool logCompletedAch = false;
    for (int i = 0; i < log.getEventCount(); ++i)
        if (log.getEventEntryId(i) == "ach.kill") logCompletedAch = true;
    CHECK(!logCompletedAch);
    CHECK(ach.getEventCount() >= 1);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.resetAbandonFailNoCascade") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"daily","startPolicy":"auto","completePolicy":"auto",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"main","startPolicy":"manual","completePolicy":"auto","requires":["daily"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("daily"), std::string("completed"));
    CHECK(log.activate("main"));
    CHECK_EQ(log.getState("main"), std::string("active"));

    CHECK(log.reset("daily"));
    CHECK_EQ(log.getState("daily"), std::string("active"));
    CHECK_EQ(log.getObjectiveCurrent("daily", 0), 0);
    CHECK_EQ(log.getState("main"), std::string("active"));  // 不级联

    CHECK(log.abandon("main"));
    CHECK_EQ(log.getState("main"), std::string("failed"));
    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("main", 0), 0);
    CHECK(!log.activate("main"));
    CHECK_EQ(log.canActivateReason("main"), std::string("failed"));
    CHECK(log.reset("main"));
    // daily 已 reset 且未完成，main 的 requires 不满足 → locked
    CHECK_EQ(log.getState("main"), std::string("locked"));

    Tracker t2;
    QuestRegistry::loadFromJson(R"([{"id":"only","startPolicy":"manual"}])");
    t2.syncAuto();
    CHECK(t2.fail("only", "timeout"));
    CHECK_EQ(t2.getState("only"), std::string("failed"));
    t2.pollEvents();
    bool sawTimeout = false;
    for (int i = 0; i < t2.getEventCount(); ++i)
        if (t2.getEventAction(i) == "fail" && t2.getEventReason(i) == "timeout") sawTimeout = true;
    CHECK(sawTimeout);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.facadeManualClaim") {
    auto *rpg = RPG::create();
    rpg->clearQuestDefinitions();
    int n = rpg->registerQuestsFromJson(R"([
      {"id":"quest.hunt","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}],
       "rewards":[{"type":"item","id":"potion","amount":3}]}
    ])");
    CHECK_EQ(n, 1);
    CHECK_EQ(rpg->getQuestDefinitionCount(), 1);

    Tracker *log = rpg->newTracker();
    REQUIRE(log != nullptr);
    CHECK(log->activate("quest.hunt"));
    log->notify("kill", "slime", 1);
    CHECK_EQ(log->getState("quest.hunt"), std::string("ready"));
    CHECK(log->claim("quest.hunt"));
    CHECK_EQ(log->getState("quest.hunt"), std::string("completed"));
    CHECK_EQ(log->getRewardCount("quest.hunt"), 1);
    CHECK_EQ(log->getRewardId("quest.hunt", 0), std::string("potion"));

    log->pollEvents();
    CHECK(log->getEventCount() >= 1);

    rpg->clearQuestDefinitions();
}