#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "dialogue/Conversation.h"
#include "dialogue/ConversationCompiler.h"
#include "i18n/I18n.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "rpg/Battle.h"
#include "rpg/BattleTactics.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/QuestReward.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/ShopTransaction.h"
#include "rpg/Skill.h"
#include "rpg/StoryEvent.h"
#include "rpg/Tracker.h"
#include "rpg/WorldInteraction.h"
#include "rpg/WorldState.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readClassicContent(const std::filesystem::path& root, const char* name) {
    std::ifstream input(root / name, std::ios::binary);
    if (!input.is_open()) return {};
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

const eve::dialogue::ConversationAsset* findConversation(const std::vector<eve::dialogue::ConversationAsset>& assets,
                                                         const std::string&                                   id) {
    for (const auto& asset : assets)
        if (asset.id == id) return &asset;
    return nullptr;
}

void acceptQuestThroughDialogue(const std::vector<eve::dialogue::ConversationAsset>& assets,
                                const std::string& conversationId, eve::rpg::Tracker& tracker,
                                const std::string& questId) {
    const auto* conversation = findConversation(assets, conversationId);
    REQUIRE(conversation != nullptr);
    eve::dialogue::ConversationRunner runner;
    std::string                       error;
    REQUIRE(runner.start(conversation, eve::StateValue::object(), &error));
    REQUIRE(runner.advance(&error));
    REQUIRE_EQ(runner.currentNodeId(), std::string("decision"));
    REQUIRE(runner.select("accept", &error));
    REQUIRE(tracker.activate(questId));
}

void winEncounter(eve::rpg::Party& party, eve::rpg::RPGActor& hero, eve::rpg::RPGActor& companion,
                  eve::rpg::Tracker& tracker, eve::rpg::GameState& gameState, const std::string& mapId,
                  const std::string& objectId, const std::string& encounterId) {
    eve::rpg::Battle battle;
    auto             composed = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    REQUIRE(composed.ok());
    REQUIRE_EQ(composed.value(), 2);
    std::vector<eve::rpg::RPGActor*> enemies;
    const int                        memberCount = eve::rpg::EncounterCatalogue::memberCount(encounterId);
    REQUIRE(memberCount > 0);
    for (int memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
        auto* enemy = eve::rpg::EncounterCatalogue::createMemberActor(encounterId, memberIndex);
        REQUIRE(enemy != nullptr);
        enemies.push_back(enemy);
        battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    }
    while (!battle.isFinished()) {
        if (!hero.isDead("hp")) REQUIRE(battle.setActionChecked(&hero, "", nullptr).ok());
        if (!companion.isDead("hp"))
            REQUIRE(eve::rpg::BattleTacticsCatalogue::queueAction(&battle, &companion, "companion.ranger").ok());
        battle.autoEnemyActions();
        battle.startRound();
        while (battle.executeNextAction() && !battle.isFinished()) {
        }
    }
    REQUIRE(battle.isVictory());
    eve::rpg::RPG rpg;
    auto          settled = rpg.settleEncounterVictory(&hero, &tracker, &gameState, encounterId, mapId, objectId);
    REQUIRE(settled.ok());
    for (auto* enemy : enemies) enemy->release();
}

}  // namespace

TEST_CASE("rpg.classic.playthroughCompletesBothQuestsAndRestoresCheckpoint") {
    const std::filesystem::path contentRoot =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "rpg-classic" / "data";

    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    eve::inventory::ItemRegistry::clear();
    REQUIRE_EQ(eve::rpg::SkillRegistry::loadFromJson(readClassicContent(contentRoot, "skills.json")), 6);
    auto quests = eve::rpg::QuestRegistry::replaceFromJsonStrict(readClassicContent(contentRoot, "quests.json"));
    REQUIRE(quests.ok());
    auto encounters =
        eve::rpg::EncounterCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "encounters.json"));
    REQUIRE(encounters.ok());
    REQUIRE_EQ(encounters.value(), 3);
    auto tactics =
        eve::rpg::BattleTacticsCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "battle-tactics.json"));
    REQUIRE(tactics.ok());
    auto storyEvents =
        eve::rpg::StoryEventCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "story-events.json"));
    REQUIRE(storyEvents.ok());
    REQUIRE_EQ(storyEvents.value(), 1);
    REQUIRE_EQ(eve::rpg::EncounterCatalogue::memberCount("slime.forest"), 2);
    REQUIRE_EQ(eve::inventory::ItemRegistry::loadFromJson(readClassicContent(contentRoot, "items.json")), 3);
    auto shopCatalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(readClassicContent(contentRoot, "shop.json"));
    REQUIRE(shopCatalogue.ok());

    std::vector<eve::dialogue::ConversationAsset>      conversations;
    std::vector<eve::dialogue::ConversationDiagnostic> diagnostics;
    REQUIRE(eve::dialogue::compileDnutConversations(readClassicContent(contentRoot, "village-dialogue.dnut"),
                                                    "village-dialogue.dnut", conversations, diagnostics));
    REQUIRE(diagnostics.empty());
    auto* localization = eve::i18n::I18n::create();
    REQUIRE(localization != nullptr);
    localization->clear();
    auto localized = localization->replaceBundleFromJson(readClassicContent(contentRoot, "localization.json"));
    REQUIRE(localized.ok());
    for (const auto& conversation : conversations)
        for (const auto& node : conversation.nodes)
            if (!node.i18nKey.empty()) REQUIRE(localization->hasInLanguage("zh-CN", node.i18nKey));

    eve::rpg::GameState gameState;
    gameState.setVariable("gold", 0.0);
    gameState.setVariable("kills", 0.0);
    gameState.setVariable("statPoints", 0.0);
    gameState.setVariable("save.mapCode", 0.0);
    eve::rpg::WorldState world(gameState);
    eve::rpg::Tracker    tracker;
    eve::rpg::RPGActor*  hero      = eve::rpg::RPGActor::createActor();
    eve::rpg::RPGActor*  companion = eve::rpg::RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("attack", 30.0);
    hero->setBaseAttribute("defense", 5.0);
    hero->setBaseAttribute("hp", 100.0);
    hero->setBaseAttribute("speed", 10.0);
    hero->setCurrent("hp", 100.0);
    companion->setBaseAttribute("attack", 16.0);
    companion->setBaseAttribute("defense", 8.0);
    companion->setBaseAttribute("hp", 80.0);
    companion->setBaseAttribute("speed", 12.0);
    companion->setCurrent("hp", 80.0);
    companion->learnSkill("skill.strike");
    companion->learnSkill("skill.ranger_aid");
    eve::rpg::Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    eve::inventory::Bag          bag(12);
    eve::inventory::EquipmentSet equipment;
    equipment.setId("hero");
    equipment.defineSlot("weapon");
    equipment.defineSlot("armor");

    eve::rpg::StoryEventSession arrival;
    REQUIRE(arrival.begin("forest.arrival", &gameState).ok());
    REQUIRE_EQ(arrival.getStepKind(), std::string("dialogue"));
    REQUIRE(findConversation(conversations, arrival.getReference()) != nullptr);
    REQUIRE(arrival.advance(&gameState).ok());
    REQUIRE_EQ(arrival.getStepKind(), std::string("wait"));
    eve::rpg::RPGSaveSession eventSave;
    eventSave.bindParty(gameState, tracker, party, bag, equipment);
    REQUIRE(eventSave.setContentVersion("rpg-classic.content.v9").ok());
    auto eventCheckpoint = eventSave.snapshotJson();
    REQUIRE(eventCheckpoint.ok());
    REQUIRE(arrival.advance(&gameState).ok());
    REQUIRE(eventSave.restoreSnapshotJson(eventCheckpoint.value()).ok());
    eve::rpg::StoryEventSession resumedArrival;
    REQUIRE(resumedArrival.begin("forest.arrival", &gameState).ok());
    CHECK_EQ(resumedArrival.getStepIndex(), 1);
    REQUIRE(resumedArrival.advance(&gameState).ok());
    CHECK_EQ(resumedArrival.getStepKind(), std::string("message"));
    REQUIRE(resumedArrival.advance(&gameState).ok());
    CHECK(resumedArrival.isFinished());

    acceptQuestThroughDialogue(conversations, "village.elder.offer", tracker, "quest.slayer");
    winEncounter(party, *hero, *companion, tracker, gameState, "village", "slime_west", "slime.west");
    winEncounter(party, *hero, *companion, tracker, gameState, "village", "slime_north", "slime.north");
    gameState.setVariable("save.mapCode", 1.0);
    winEncounter(party, *hero, *companion, tracker, gameState, "forest", "forest_slime", "slime.forest");
    REQUIRE_EQ(tracker.getState("quest.slayer"), std::string("ready"));
    auto mainRewards = eve::rpg::QuestReward::claim(tracker, gameState, bag, "quest.slayer");
    REQUIRE(mainRewards.ok());

    acceptQuestThroughDialogue(conversations, "forest.ranger.offer", tracker, "quest.ranger_cache");
    eve::rpg::WorldLootRequest cache;
    cache.mapId           = "forest";
    cache.objectId        = "forest_cache";
    cache.requiredQuestId = "quest.ranger_cache";
    cache.itemId          = "potion";
    cache.itemQuantity    = 1;
    cache.attributeId     = "gold";
    cache.attributeAmount = 10.0;
    cache.notifyTopic     = "interact";
    cache.notifyTarget    = "forest_cache";
    cache.notifyAmount    = 1;
    auto cacheLoot        = eve::rpg::WorldInteraction::collectLoot(gameState, tracker, bag, cache);
    REQUIRE(cacheLoot.ok());
    REQUIRE_EQ(tracker.getState("quest.ranger_cache"), std::string("ready"));
    auto sideRewards = eve::rpg::QuestReward::claim(tracker, gameState, bag, "quest.ranger_cache");
    REQUIRE(sideRewards.ok());
    auto boughtPotion = eve::rpg::ShopTransaction::buyOffer(gameState, bag, "gold", "potion", 1);
    REQUIRE(boughtPotion.ok());
    auto soldPotion = eve::rpg::ShopTransaction::sellOffer(gameState, bag, "gold", "potion", 1);
    REQUIRE(soldPotion.ok());

    REQUIRE_EQ(tracker.getState("quest.slayer"), std::string("completed"));
    REQUIRE_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    REQUIRE_EQ(gameState.getVariable("gold"), 115.0);
    REQUIRE_EQ(gameState.getVariable("kills"), 3.0);
    REQUIRE_EQ(gameState.getVariable("statPoints"), 3.0);
    REQUIRE_EQ(bag.countItem("potion"), 2);

    eve::rpg::RPGSaveSession save;
    save.bindParty(gameState, tracker, party, bag, equipment);
    REQUIRE(save.setContentVersion("rpg-classic.content.v9").ok());
    auto checkpoint = save.snapshotJson();
    REQUIRE(checkpoint.ok());

    gameState.setVariable("gold", 0.0);
    gameState.setVariable("kills", 0.0);
    gameState.setVariable("statPoints", 0.0);
    gameState.setVariable("save.mapCode", 0.0);
    bag.clear();
    hero->setCurrent("hp", 1.0);
    companion->setCurrent("hp", 1.0);
    REQUIRE(world.resetObject("forest", "forest_cache").ok());
    REQUIRE(save.restoreSnapshotJson(checkpoint.value()).ok());

    CHECK_EQ(gameState.getVariable("gold"), 115.0);
    CHECK_EQ(gameState.getVariable("kills"), 3.0);
    CHECK_EQ(gameState.getVariable("statPoints"), 3.0);
    CHECK_EQ(gameState.getVariable("save.mapCode"), 1.0);
    CHECK_EQ(tracker.getState("quest.slayer"), std::string("completed"));
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    CHECK_EQ(bag.countItem("potion"), 2);
    CHECK_EQ(hero->getCurrent("hp"), 100.0);
    CHECK(companion->getCurrent("hp") > 1.0);
    CHECK(world.isObjectConsumed("village", "slime_west"));
    CHECK(world.isObjectConsumed("village", "slime_north"));
    CHECK(world.isObjectConsumed("forest", "forest_slime"));
    CHECK(world.isObjectConsumed("forest", "forest_cache"));

    hero->release();
    companion->release();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::StoryEventCatalogue::clear();
}
