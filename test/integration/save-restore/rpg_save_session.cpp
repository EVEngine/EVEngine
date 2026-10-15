#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/RPGActor.h"
#include "rpg/Tracker.h"

using namespace eve::rpg;

TEST_CASE("rpg.saveSession.partyRoundTripIsAtomic") {
    QuestRegistry::clear();
    GameState state;
    state.setVariable("gold", 25.0);
    Tracker tracker;
    auto *hero = RPGActor::createActor();
    auto *companion = RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("hp", 100.0);
    companion->setBaseAttribute("hp", 80.0);
    hero->setCurrent("hp", 70.0);
    companion->setCurrent("hp", 45.0);
    Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    eve::inventory::Bag bag(4);
    eve::inventory::EquipmentSet equipment;
    RPGSaveSession session;
    session.bindParty(state, tracker, party, bag, equipment);
    REQUIRE(session.setContentVersion("test.party.v1").ok());
    auto encoded = session.snapshotJson();
    REQUIRE(encoded.ok());

    state.setVariable("gold", 99.0);
    hero->setCurrent("hp", 1.0);
    companion->setCurrent("hp", 2.0);
    REQUIRE(session.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(state.getVariable("gold"), 99.0);
    CHECK_EQ(hero->getCurrent("hp"), 1.0);
    CHECK_EQ(companion->getCurrent("hp"), 2.0);
    REQUIRE(session.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK_EQ(hero->getCurrent("hp"), 70.0);
    CHECK_EQ(companion->getCurrent("hp"), 45.0);

    REQUIRE(party.removeMember("companion").ok());
    state.setVariable("gold", 123.0);
    hero->setCurrent("hp", 6.0);
    CHECK(!session.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(state.getVariable("gold"), 123.0);
    CHECK_EQ(hero->getCurrent("hp"), 6.0);
    hero->release();
    companion->release();
}

TEST_CASE("rpg.saveSession.explicitSingleActorPartyMigrationPreservesCompanionBaseline") {
    QuestRegistry::clear();
    GameState state;
    Tracker tracker;
    eve::inventory::Bag bag(2);
    eve::inventory::EquipmentSet equipment;
    auto *oldHero = RPGActor::createActor();
    REQUIRE(oldHero != nullptr);
    oldHero->setBaseAttribute("hp", 100.0);
    oldHero->setCurrent("hp", 31.0);
    RPGSaveSession oldSession;
    oldSession.bind(state, tracker, *oldHero, bag, equipment);
    REQUIRE(oldSession.setContentVersion("test.single.v1").ok());
    auto oldSave = oldSession.snapshotJson();
    REQUIRE(oldSave.ok());

    auto *hero = RPGActor::createActor();
    auto *companion = RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("hp", 100.0);
    companion->setBaseAttribute("hp", 80.0);
    hero->setCurrent("hp", 100.0);
    companion->setCurrent("hp", 64.0);
    Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    RPGSaveSession migrated;
    migrated.bindParty(state, tracker, party, bag, equipment);
    REQUIRE(migrated.setContentVersion("test.party.v2").ok());
    CHECK(!migrated.restoreSnapshotJson(oldSave.value()).ok());
    REQUIRE(migrated.allowSingleActorPartyMigration("test.single.v1").ok());
    REQUIRE(migrated.restoreSnapshotJson(oldSave.value()).ok());
    CHECK_EQ(hero->getCurrent("hp"), 31.0);
    CHECK_EQ(companion->getCurrent("hp"), 64.0);

    oldHero->release();
    hero->release();
    companion->release();
}

TEST_CASE("rpg.saveSession.roundTripAndCrossParticipantRollback") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.hunt","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}]}
    ])"), 1);
    GameState sourceState;
    sourceState.setVariable("gold", 40.0);
    Tracker sourceTracker;
    sourceTracker.notify("kill", "slime", 2);
    RPGActor *sourceActor = RPGActor::createActor();
    REQUIRE(sourceActor != nullptr);
    sourceActor->setBaseAttribute("hp", 100.0);
    sourceActor->setCurrent("hp", 65.0);
    REQUIRE(sourceActor->restoreProgression(3, 12.0, 90.0).ok());
    eve::inventory::ItemDefinition potion;
    potion.id = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag sourceBag(4);
    sourceBag.addItem("potion", 3);
    eve::inventory::EquipmentSet sourceEquipment;
    sourceEquipment.defineSlot("weapon");
    RPGSaveSession source;
    source.bind(sourceState, sourceTracker, *sourceActor, sourceBag, sourceEquipment);
    REQUIRE(source.setContentVersion("test.content.v1").ok());
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    GameState restoredState;
    restoredState.setVariable("gold", 999.0);
    Tracker restoredTracker;
    restoredTracker.notify("kill", "slime", 1);
    RPGActor *restoredActor = RPGActor::createActor();
    REQUIRE(restoredActor != nullptr);
    restoredActor->setBaseAttribute("hp", 100.0);
    restoredActor->setCurrent("hp", 100.0);
    eve::inventory::Bag restoredBag(1);
    restoredBag.addItem("potion", 1);
    eve::inventory::EquipmentSet restoredEquipment;
    RPGSaveSession restored;
    restored.bind(restoredState, restoredTracker, *restoredActor, restoredBag, restoredEquipment);
    REQUIRE(restored.setContentVersion("test.content.v1").ok());
    REQUIRE(restored.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restoredState.getVariable("gold"), 999.0);
    CHECK_EQ(restoredTracker.getObjectiveCurrent("quest.hunt", 0), 1);
    CHECK_EQ(restoredActor->getCurrent("hp"), 100.0);
    CHECK_EQ(restoredBag.getSlotCount(), 1);
    CHECK_EQ(restoredBag.countItem("potion"), 1);
    REQUIRE(restored.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restoredState.getVariable("gold"), 40.0);
    CHECK_EQ(restoredTracker.getObjectiveCurrent("quest.hunt", 0), 2);
    CHECK_EQ(restoredActor->getLevel(), 3);
    CHECK_EQ(restoredActor->getXp(), 12.0);
    CHECK_EQ(restoredActor->getCurrent("hp"), 65.0);
    CHECK_EQ(restoredBag.getSlotCount(), 4);
    CHECK_EQ(restoredBag.countItem("potion"), 3);

    RPGSaveSession compatible;
    compatible.bind(restoredState, restoredTracker, *restoredActor, restoredBag, restoredEquipment);
    REQUIRE(compatible.setContentVersion("test.content.v2").ok());
    REQUIRE(compatible.allowCompatibleContentVersion("test.content.v1").ok());
    CHECK(!compatible.allowCompatibleContentVersion("test.content.v1").ok());
    REQUIRE(compatible.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restoredState.getVariable("gold"), 40.0);
    CHECK_EQ(restoredBag.countItem("potion"), 3);

    RPGSaveSession incompatible;
    incompatible.bind(restoredState, restoredTracker, *restoredActor, restoredBag, restoredEquipment);
    REQUIRE(incompatible.setContentVersion("test.content.v2").ok());
    CHECK(!incompatible.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restoredState.getVariable("gold"), 40.0);
    CHECK_EQ(restoredActor->getCurrent("hp"), 65.0);
    CHECK_EQ(restoredBag.countItem("potion"), 3);

    auto parsed = eve::Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    eve::Value corrupted = std::move(parsed).takeValue();
    eve::Value *payload = corrupted.find("payload");
    REQUIRE(payload != nullptr);
    eve::Value *gameState = payload->find("gameState");
    eve::Value *questTracker = payload->find("questTracker");
    eve::Value *inventory = payload->find("inventory");
    REQUIRE(gameState != nullptr);
    REQUIRE(questTracker != nullptr);
    REQUIRE(inventory != nullptr);
    eve::Value *variables = gameState->find("variables");
    eve::Value *entries = questTracker->find("entries");
    REQUIRE(variables != nullptr);
    REQUIRE(entries != nullptr);
    variables->set("gold", eve::Value(5.0));
    entries->at(0).set("state", eve::Value("completed"));
    inventory->find("bag")->find("slots")->at(0).set("itemId", eve::Value("missing.item"));
    auto corruptedJson = corrupted.toJson();
    REQUIRE(corruptedJson.ok());
    auto invalidPreview = restored.validateSnapshotJson(corruptedJson.value());
    CHECK(!invalidPreview.ok());
    REQUIRE(invalidPreview.error() != nullptr);
    CHECK_EQ(static_cast<int>(invalidPreview.error()->code()),
             static_cast<int>(eve::DiagnosticCode::HashMismatch));
    CHECK_EQ(restoredState.getVariable("gold"), 40.0);
    CHECK_EQ(restoredBag.countItem("potion"), 3);
    auto rejected = restored.restoreSnapshotJson(corruptedJson.value());
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::HashMismatch));
    CHECK_EQ(restoredState.getVariable("gold"), 40.0);
    CHECK_EQ(restoredTracker.getObjectiveCurrent("quest.hunt", 0), 2);
    CHECK_EQ(restoredActor->getLevel(), 3);
    CHECK_EQ(restoredActor->getCurrent("hp"), 65.0);
    CHECK_EQ(restoredBag.countItem("potion"), 3);
    sourceActor->release();
    restoredActor->release();
    eve::inventory::ItemRegistry::clear();
    QuestRegistry::clear();
}

TEST_CASE("rpg.saveSession.unboundOperationsFailStructurally") {
    RPGSaveSession session;
    REQUIRE(session.setContentVersion("test.content.v1").ok());
    CHECK(!session.snapshotJson().ok());
    CHECK(!session.validateSnapshotJson("{}").ok());
    CHECK(!session.restoreSnapshotJson("{}").ok());
}

TEST_CASE("rpg.saveSession.explicitQuestAdditionMigrationPreservesOldProgress") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.slayer","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}]}
    ])"), 1);
    GameState sourceState;
    Tracker sourceTracker;
    sourceTracker.notify("kill", "slime", 2);
    RPGActor *sourceActor = RPGActor::createActor();
    REQUIRE(sourceActor != nullptr);
    sourceActor->setBaseAttribute("hp", 100.0);
    sourceActor->setCurrent("hp", 75.0);
    eve::inventory::Bag sourceBag(2);
    eve::inventory::EquipmentSet sourceEquipment;
    RPGSaveSession source;
    source.bind(sourceState, sourceTracker, *sourceActor, sourceBag, sourceEquipment);
    REQUIRE(source.setContentVersion("content.v4").ok());
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.slayer","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}]},
      {"id":"quest.ranger_cache","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"recover","topic":"interact","target":"cache","count":1}]}
    ])"), 2);
    GameState targetState;
    Tracker targetTracker;
    targetTracker.notify("kill", "slime", 1);
    RPGActor *targetActor = RPGActor::createActor();
    REQUIRE(targetActor != nullptr);
    targetActor->setBaseAttribute("hp", 100.0);
    targetActor->setCurrent("hp", 100.0);
    eve::inventory::Bag targetBag(2);
    eve::inventory::EquipmentSet targetEquipment;

    RPGSaveSession missingMigration;
    missingMigration.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(missingMigration.setContentVersion("content.v5").ok());
    CHECK(!missingMigration.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetTracker.getObjectiveCurrent("quest.slayer", 0), 1);

    RPGSaveSession migrated;
    migrated.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(migrated.setContentVersion("content.v5").ok());
    CHECK(!migrated.addQuestAdditionMigration("content.v4", "quest.missing").ok());
    REQUIRE(migrated.addQuestAdditionMigration("content.v4", "quest.ranger_cache").ok());
    CHECK(!migrated.addQuestAdditionMigration("content.v4", "quest.ranger_cache").ok());
    CHECK(!migrated.allowCompatibleContentVersion("content.v4").ok());
    REQUIRE(migrated.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetTracker.getObjectiveCurrent("quest.slayer", 0), 1);
    CHECK_EQ(targetTracker.getState("quest.ranger_cache"), std::string("inactive"));
    REQUIRE(migrated.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetTracker.getObjectiveCurrent("quest.slayer", 0), 2);
    CHECK_EQ(targetTracker.getState("quest.ranger_cache"), std::string("inactive"));
    CHECK_EQ(targetActor->getCurrent("hp"), 75.0);

    RPGSaveSession oldVersionAlreadyContainingQuest;
    oldVersionAlreadyContainingQuest.bind(targetState, targetTracker, *targetActor, targetBag,
                                           targetEquipment);
    REQUIRE(oldVersionAlreadyContainingQuest.setContentVersion("content.v4").ok());
    auto invalidAdditionSource = oldVersionAlreadyContainingQuest.snapshotJson();
    REQUIRE(invalidAdditionSource.ok());
    CHECK(!migrated.validateSnapshotJson(invalidAdditionSource.value()).ok());

    sourceActor->release();
    targetActor->release();
    QuestRegistry::clear();
}

TEST_CASE("rpg.saveSession.explicitIdMigrationRewritesOwningPayloadBeforeValidation") {
    QuestRegistry::clear();
    eve::inventory::ItemRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.old","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"objective.old","topic":"kill","target":"slime","count":3}]}
    ])"), 1);
    eve::inventory::ItemDefinition oldPotion;
    oldPotion.id = "potion.old";
    oldPotion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(oldPotion);

    GameState sourceState;
    Tracker sourceTracker;
    sourceTracker.notify("kill", "slime", 1);
    RPGActor *sourceActor = RPGActor::createActor();
    REQUIRE(sourceActor != nullptr);
    sourceActor->setBaseAttribute("hp", 100.0);
    sourceActor->setCurrent("hp", 80.0);
    eve::inventory::Bag sourceBag(2);
    sourceBag.addItem("potion.old", 2);
    eve::inventory::EquipmentSet sourceEquipment;
    RPGSaveSession source;
    source.bind(sourceState, sourceTracker, *sourceActor, sourceBag, sourceEquipment);
    REQUIRE(source.setContentVersion("content.v1").ok());
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    QuestRegistry::clear();
    eve::inventory::ItemRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.new","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"objective.new","topic":"kill","target":"slime","count":3}]}
    ])"), 1);
    eve::inventory::ItemDefinition newPotion;
    newPotion.id = "potion.new";
    newPotion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(newPotion);

    GameState targetState;
    targetState.setVariable("sentinel", 7.0);
    Tracker targetTracker;
    RPGActor *targetActor = RPGActor::createActor();
    REQUIRE(targetActor != nullptr);
    targetActor->setBaseAttribute("hp", 100.0);
    targetActor->setCurrent("hp", 100.0);
    eve::inventory::Bag targetBag(0);
    eve::inventory::EquipmentSet targetEquipment;

    RPGSaveSession missingRoute;
    missingRoute.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(missingRoute.setContentVersion("content.v2").ok());
    REQUIRE(missingRoute.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item,
                                              "potion.old", "potion.new").ok());
    CHECK(!missingRoute.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetState.getVariable("sentinel"), 7.0);
    CHECK_EQ(targetBag.countItem("potion.new"), 0);

    RPGSaveSession migrated;
    migrated.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(migrated.setContentVersion("content.v2").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item,
                                          "potion.old", "potion.new").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Quest,
                                          "quest.old", "quest.new").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::QuestObjective,
                                          "objective.old", "objective.new").ok());
    CHECK(!migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item,
                                         "potion.old", "another.potion").ok());
    CHECK(!migrated.allowCompatibleContentVersion("content.v1").ok());
    REQUIRE(migrated.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetState.getVariable("sentinel"), 7.0);
    CHECK_EQ(targetTracker.getObjectiveCurrent("quest.new", 0), 0);
    CHECK_EQ(targetBag.countItem("potion.new"), 0);
    REQUIRE(migrated.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetTracker.getObjectiveCurrent("quest.new", 0), 1);
    CHECK_EQ(targetBag.countItem("potion.new"), 2);
    CHECK_EQ(targetActor->getCurrent("hp"), 80.0);

    auto rewritten = migrated.snapshotJson();
    REQUIRE(rewritten.ok());
    auto rewrittenEnvelope = eve::Value::fromJson(rewritten.value());
    REQUIRE(rewrittenEnvelope.ok());
    const eve::Value *payload = rewrittenEnvelope.value().find("payload");
    REQUIRE(payload != nullptr);
    const eve::Value *contentVersion = payload->find("contentVersion");
    REQUIRE(contentVersion != nullptr);
    CHECK_EQ(contentVersion->asString(), "content.v2");

    sourceActor->release();
    targetActor->release();
    eve::inventory::ItemRegistry::clear();
    QuestRegistry::clear();
}
