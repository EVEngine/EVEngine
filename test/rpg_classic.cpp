#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Runtime.h"
#include "dialogue/ConversationCompiler.h"
#include "i18n/I18n.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/MapObjectContract.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/Quest.h"
#include "rpg/Skill.h"
#include "rpg/StoryEvent.h"
#include "rpg/Tracker.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

TEST_CASE("rpg.classic.mainScriptCompilesThroughEveScriptFrontend") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "rpg-classic" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    const std::string script = source.str();

    CHECK(script.find("const SAVE_SLOT_COUNT = 3") != std::string::npos);
    CHECK(script.find("function inspectSaveSlot(slot)") != std::string::npos);
    CHECK(script.find("function migrateLegacySingleSlot()") != std::string::npos);
    CHECK(script.find("loadCheckpoint(true)") != std::string::npos);
    CHECK(script.find("newRunBeforeFirstSave") != std::string::npos);
    CHECK(script.find("const SETTINGS_SCHEMA = \"rpg-classic.settings.v2\"") != std::string::npos);
    CHECK(script.find("const LEGACY_SETTINGS_SCHEMA = \"rpg-classic.settings.v1\"") != std::string::npos);
    CHECK(script.find("function selectProductLanguage(language)") != std::string::npos);
    CHECK(script.find("dialogueFlow.validateLocalization(text, language)") != std::string::npos);
    CHECK(script.find("function validateProductLocalization()") != std::string::npos);
    CHECK(script.find("function openPauseMenu()") != std::string::npos);
    CHECK(script.find("function confirmDeleteSaveSlot()") != std::string::npos);
    CHECK(script.find("function confirmReturnToTitle()") != std::string::npos);
    CHECK(script.find("saveFs.writeTextAtomic(SETTINGS_PATH") != std::string::npos);
    CHECK(script.find("const SAVE_CONTENT_VERSION = \"rpg-classic.content.v9\"") != std::string::npos);
    CHECK(script.find("function loadWorldMap(mapId, x, y, fx, fy)") != std::string::npos);
    CHECK(script.find("function transitionThroughPortal(index)") != std::string::npos);
    CHECK(script.find("function beginQuestNpcDialogue(index)") != std::string::npos);
    CHECK(script.find("function openLootObject(index)") != std::string::npos);
    CHECK(script.find("function claimQuestRewards(questId)") != std::string::npos);
    CHECK(script.find("rpg.replaceQuestsFromJson(questsJson)") != std::string::npos);
    CHECK(script.find("rpg.replaceEncountersFromJson(encountersJson)") != std::string::npos);
    CHECK(script.find("rpg.newEncounterMemberActor(activeEncounterId, memberIndex)") != std::string::npos);
    CHECK(script.find("function selectNextEnemy()") != std::string::npos);
    CHECK(script.find("rpg.settleEncounterVictory(") != std::string::npos);
    CHECK(script.find("previous catalogue retained") != std::string::npos);
    CHECK(script.find("function validateWorldContentLinks()") != std::string::npos);
    CHECK(script.find("dialogueFlow.hasConversation(conversationId)") != std::string::npos);
    CHECK(script.find("rpg.hasQuestDefinition(questId)") != std::string::npos);
    CHECK(script.find("persist acceptedQuestContent = null") != std::string::npos);
    CHECK(script.find("function publishNarrativeContentPackage(reload = false)") != std::string::npos);
    CHECK(script.find(
              "function restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,") !=
          std::string::npos);
    CHECK(script.find("rpg.replaceStoryEventsFromJson(storyEventJson)") != std::string::npos);
    CHECK(script.find("text.replaceBundleFromJson(source)") != std::string::npos);
    CHECK(script.find("dialogueFlow.validateLocalization(text, \"zh-CN\")") != std::string::npos);
    CHECK(script.find("function resumePendingStoryEvent()") != std::string::npos);
    CHECK(script.find("accepted content restored") != std::string::npos);
    CHECK(script.find("publishNarrativeContentPackage(true)") != std::string::npos);
    CHECK(script.find("loadFromFileWithObjectContract(path, worldObjectContract)") != std::string::npos);
    CHECK(script.find("allowCompatibleContentVersion(PREVIOUS_SAVE_CONTENT_VERSION)") != std::string::npos);
    CHECK(script.find("addQuestAdditionMigration(LEGACY_QUEST_CONTENT_VERSION") != std::string::npos);
    CHECK(script.find("worldState.consumeObject(worldMapId, map.getObjectName(index))") != std::string::npos);
    CHECK(script.find("save.mapCode") != std::string::npos);

    eve::Runtime runtime(2048, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(script, "examples/rpg-classic/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}

TEST_CASE("rpg.classic.villageSupportsExplorationInteractionAndThreeEncounters") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path mapPath    = sourceRoot / "examples" / "rpg-classic" / "data" / "village.json";
    std::ifstream               input(mapPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    std::vector<eve::map::MapObject> objects;
    std::string                      error;
    auto                             layers = eve::map::loadMapText(source.str(), &objects, &error);
    CHECK(error.empty());
    REQUIRE_EQ(layers.size(), std::size_t(1));
    auto* layer = layers.front();
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getMapWidth(), 20);
    CHECK_EQ(layer->getMapHeight(), 12);

    eve::map::Pathfinder pathfinder(layer);
    pathfinder.blockGid(2);
    pathfinder.syncFromLayer();
    CHECK(pathfinder.isWalkable(2, 5));
    CHECK(!pathfinder.isWalkable(0, 5));
    CHECK(!pathfinder.isWalkable(9, 2));

    std::set<std::string> names;
    int                   spawnCount     = 0;
    int                   questNpcCount  = 0;
    int                   merchantCount  = 0;
    int                   lootCount      = 0;
    int                   encounterCount = 0;
    int                   portalCount    = 0;
    for (const auto& object : objects) {
        CHECK(names.emplace(object.name).second);
        if (object.type == "spawn")
            ++spawnCount;
        else if (object.type == "quest_npc")
            ++questNpcCount;
        else if (object.type == "merchant")
            ++merchantCount;
        else if (object.type == "loot")
            ++lootCount;
        else if (object.type == "portal")
            ++portalCount;
        else if (object.type == "encounter") {
            ++encounterCount;
            CHECK(pathfinder.isWalkable(static_cast<int>(object.x), static_cast<int>(object.y)));
        }
    }
    CHECK_EQ(spawnCount, 1);
    CHECK_EQ(questNpcCount, 1);
    CHECK_EQ(merchantCount, 1);
    CHECK_EQ(lootCount, 1);
    CHECK_EQ(encounterCount, 2);
    CHECK_EQ(portalCount, 1);

    eve::map::Map objectQueries;
    objectQueries.setObjects(objects);
    const auto elder = objectQueries.findObjectByName("elder");
    REQUIRE(elder.has_value());
    const int elderIndex = static_cast<int>(*elder);
    CHECK_EQ(objectQueries.getObjectType(elderIndex), std::string("quest_npc"));
    CHECK_EQ(objectQueries.getObjectProperty(elderIndex, "questId", ""), std::string("quest.slayer"));
    CHECK_EQ(objectQueries.getObjectProperty(elderIndex, "dialogueBase", ""), std::string("village.elder"));
    const auto encounter = objectQueries.findObjectAt(7.f, 6.f, "encounter");
    REQUIRE(encounter.has_value());
    CHECK_EQ(objectQueries.getObjectName(static_cast<int>(*encounter)), std::string("slime_west"));
    CHECK(!objectQueries.findObjectAt(7.f, 6.f, "merchant").has_value());

    const auto villageGate = objectQueries.findObjectByName("forest_gate");
    REQUIRE(villageGate.has_value());
    const int villageGateIndex = static_cast<int>(*villageGate);
    CHECK_EQ(objectQueries.getObjectPropertyCount(villageGateIndex), 5);
    CHECK(objectQueries.hasObjectProperty(villageGateIndex, "targetMap"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "targetMap", ""), std::string("forest"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "targetX", ""), std::string("2"));
    CHECK_EQ(objectQueries.getObjectProperty(villageGateIndex, "missing", "fallback"), std::string("fallback"));
    CHECK_EQ(objectQueries.getObjectPropertyName(villageGateIndex, 0), std::string("facingX"));

    const std::filesystem::path forestPath = sourceRoot / "examples" / "rpg-classic" / "data" / "forest.json";
    std::ifstream               forestInput(forestPath, std::ios::binary);
    REQUIRE(forestInput.is_open());
    std::ostringstream forestSource;
    forestSource << forestInput.rdbuf();
    std::vector<eve::map::MapObject> forestObjects;
    auto                             forestLayers = eve::map::loadMapText(forestSource.str(), &forestObjects, &error);
    CHECK(error.empty());
    REQUIRE_EQ(forestLayers.size(), std::size_t(1));

    const std::filesystem::path contractPath =
        sourceRoot / "examples" / "rpg-classic" / "data" / "world-object-contract.json";
    std::ifstream contractInput(contractPath, std::ios::binary);
    REQUIRE(contractInput.is_open());
    std::ostringstream contractSource;
    contractSource << contractInput.rdbuf();
    auto villageAdmission = eve::map::validateMapObjects(objects, contractSource.str());
    CHECK(villageAdmission.ok());
    auto forestAdmission = eve::map::validateMapObjects(forestObjects, contractSource.str());
    CHECK(forestAdmission.ok());

    eve::map::Map transactionalMap;
    auto          villageLoad = transactionalMap.loadFromTextWithObjectContract(source.str(), contractSource.str());
    REQUIRE(villageLoad.ok());
    CHECK(transactionalMap.findObjectByName("forest_gate").has_value());
    const std::string restrictiveContract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    auto rejectedForest = transactionalMap.loadFromTextWithObjectContract(forestSource.str(), restrictiveContract);
    CHECK(!rejectedForest.ok());
    CHECK(transactionalMap.findObjectByName("forest_gate").has_value());
    CHECK(!transactionalMap.findObjectByName("ranger").has_value());
    eve::map::Pathfinder forestPathfinder(forestLayers.front());
    forestPathfinder.blockGid(2);
    forestPathfinder.syncFromLayer();
    int forestEncounterCount = 0;
    int forestQuestNpcCount  = 0;
    int forestLootCount      = 0;
    for (const auto& object : forestObjects)
        if (object.type == "encounter")
            ++forestEncounterCount;
        else if (object.type == "quest_npc")
            ++forestQuestNpcCount;
        else if (object.type == "loot")
            ++forestLootCount;
    CHECK_EQ(encounterCount + forestEncounterCount, 3);
    CHECK_EQ(forestQuestNpcCount, 1);
    CHECK_EQ(forestLootCount, 1);
    CHECK(forestPathfinder.isWalkable(std::stoi(objectQueries.getObjectProperty(villageGateIndex, "targetX", "-1")),
                                      std::stoi(objectQueries.getObjectProperty(villageGateIndex, "targetY", "-1"))));

    eve::map::Map forestQueries;
    forestQueries.setObjects(forestObjects);
    const auto returnGate = forestQueries.findObjectByName("village_gate");
    REQUIRE(returnGate.has_value());
    const int returnGateIndex = static_cast<int>(*returnGate);
    CHECK_EQ(forestQueries.getObjectProperty(returnGateIndex, "targetMap", ""), std::string("village"));
    CHECK(pathfinder.isWalkable(std::stoi(forestQueries.getObjectProperty(returnGateIndex, "targetX", "-1")),
                                std::stoi(forestQueries.getObjectProperty(returnGateIndex, "targetY", "-1"))));
    const auto ranger = forestQueries.findObjectByName("ranger");
    REQUIRE(ranger.has_value());
    CHECK_EQ(forestQueries.getObjectProperty(static_cast<int>(*ranger), "questId", ""),
             std::string("quest.ranger_cache"));
    const auto cache = forestQueries.findObjectByName("forest_cache");
    REQUIRE(cache.has_value());
    const int cacheIndex = static_cast<int>(*cache);
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "requiredQuest", ""), std::string("quest.ranger_cache"));
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "notifyTopic", ""), std::string("interact"));
    CHECK_EQ(forestQueries.getObjectProperty(cacheIndex, "notifyTarget", ""), std::string("forest_cache"));
}

TEST_CASE("rpg.classic.elderDialogueHasOfferReminderTurnInAndCompletionRoutes") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path dialoguePath =
        sourceRoot / "examples" / "rpg-classic" / "data" / "village-dialogue.dnut";
    std::ifstream input(dialoguePath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    std::vector<eve::dialogue::ConversationAsset>      assets;
    std::vector<eve::dialogue::ConversationDiagnostic> diagnostics;
    CHECK(eve::dialogue::compileDnutConversations(source.str(), dialoguePath.string(), assets, diagnostics));
    CHECK_EQ(assets.size(), std::size_t(9));
    CHECK(diagnostics.empty());

    const auto findAsset = [&](const std::string& id) -> const eve::dialogue::ConversationAsset* {
        for (const auto& asset : assets)
            if (asset.id == id) return &asset;
        return nullptr;
    };
    const auto* offer        = findAsset("village.elder.offer");
    const auto* turnIn       = findAsset("village.elder.turnin");
    const auto* rangerOffer  = findAsset("forest.ranger.offer");
    const auto* rangerTurnIn = findAsset("forest.ranger.turnin");
    REQUIRE(offer != nullptr);
    REQUIRE(turnIn != nullptr);
    REQUIRE(rangerOffer != nullptr);
    REQUIRE(rangerTurnIn != nullptr);
    const auto* offerChoice  = offer->findNode("decision");
    const auto* turnInChoice = turnIn->findNode("decision");
    REQUIRE(offerChoice != nullptr);
    REQUIRE(turnInChoice != nullptr);
    REQUIRE_EQ(offerChoice->routes.size(), std::size_t(2));
    REQUIRE_EQ(turnInChoice->routes.size(), std::size_t(2));
    CHECK_EQ(offerChoice->routes[0].first, std::string("accept"));
    CHECK_EQ(offerChoice->routes[1].first, std::string("later"));
    CHECK_EQ(turnInChoice->routes[0].first, std::string("claim"));
    CHECK_EQ(turnInChoice->routes[1].first, std::string("later"));
    REQUIRE(rangerOffer->findNode("decision") != nullptr);
    REQUIRE(rangerTurnIn->findNode("decision") != nullptr);

    eve::dialogue::ConversationRunner runner;
    std::string                       error;
    CHECK(runner.start(offer, eve::StateValue::object(), &error));
    CHECK_EQ(runner.currentNodeId(), std::string("intro"));
    CHECK(runner.advance(&error));
    CHECK_EQ(runner.currentNodeId(), std::string("decision"));
    CHECK(runner.select("accept", &error));
    CHECK_EQ(runner.currentNodeId(), std::string("accepted"));
    CHECK(runner.advance(&error));
    CHECK(!runner.isActive());
}

TEST_CASE("rpg.classic.forestSideQuestHasDataDrivenObjectiveAndRewards") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path questPath  = sourceRoot / "examples" / "rpg-classic" / "data" / "quests.json";
    std::ifstream               input(questPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    eve::rpg::QuestRegistry::clear();
    REQUIRE_EQ(eve::rpg::QuestRegistry::loadFromJson(source.str()), 2);
    const auto* definition = eve::rpg::QuestRegistry::find("quest.ranger_cache");
    REQUIRE(definition != nullptr);
    REQUIRE_EQ(definition->objectives.size(), std::size_t(1));
    CHECK_EQ(definition->objectives[0].topic, std::string("interact"));
    CHECK_EQ(definition->objectives[0].target, std::string("forest_cache"));
    REQUIRE_EQ(definition->rewards.size(), std::size_t(2));
    CHECK_EQ(definition->rewards[0].type, std::string("item"));
    CHECK_EQ(definition->rewards[0].id, std::string("potion"));
    CHECK_EQ(definition->rewards[1].type, std::string("attribute"));
    CHECK_EQ(definition->rewards[1].id, std::string("gold"));

    eve::rpg::Tracker tracker;
    CHECK(tracker.activate("quest.ranger_cache"));
    tracker.notify("interact", "forest_cache", 1);
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("ready"));
    CHECK(tracker.claim("quest.ranger_cache"));
    CHECK_EQ(tracker.getState("quest.ranger_cache"), std::string("completed"));
    eve::rpg::QuestRegistry::clear();
}

TEST_CASE("rpg.classic.mapsQuestsAndDialogueComposeWithoutDanglingReferences") {
    const std::filesystem::path root =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "rpg-classic" / "data";
    const auto readFile = [&](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        std::ostringstream source;
        source << input.rdbuf();
        return source.str();
    };

    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    REQUIRE_EQ(eve::rpg::SkillRegistry::loadFromJson(readFile(root / "skills.json")), 6);
    auto quests = eve::rpg::QuestRegistry::replaceFromJsonStrict(readFile(root / "quests.json"));
    REQUIRE(quests.ok());
    auto encounters = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(readFile(root / "encounters.json"));
    REQUIRE(encounters.ok());
    auto storyEvents = eve::rpg::StoryEventCatalogue::replaceFromJsonStrict(readFile(root / "story-events.json"));
    REQUIRE(storyEvents.ok());

    std::vector<eve::dialogue::ConversationAsset>      assets;
    std::vector<eve::dialogue::ConversationDiagnostic> diagnostics;
    REQUIRE(eve::dialogue::compileDnutConversations(readFile(root / "village-dialogue.dnut"), "village-dialogue.dnut",
                                                    assets, diagnostics));
    REQUIRE(diagnostics.empty());
    auto* localization = eve::i18n::I18n::create();
    REQUIRE(localization != nullptr);
    localization->clear();
    auto localizationBundle = localization->replaceBundleFromJson(readFile(root / "localization.json"));
    REQUIRE(localizationBundle.ok());
    CHECK_EQ(localizationBundle.value(), 2);
    const std::vector<std::string> productKeys{
        "content.map.village.name",
        "content.map.forest.name",
        "content.item.potion.name",
        "content.item.iron_sword.name",
        "content.item.leather_armor.name",
        "content.quest.quest.slayer.title",
        "content.quest.quest.ranger_cache.title",
        "content.shop.potion.description",
        "content.skill.skill.fireball.name",
        "content.encounter.slime.forest.name",
        "content.encounterMember.slime.forest.forest.alpha.name",
        "ui.title.continue",
        "ui.adjust.offer",
        "ui.gameOver.summary",
    };
    for (const auto& language : {"zh-CN", "en"})
        for (const auto& key : productKeys) CHECK(localization->hasInLanguage(language, key));
    const std::string scriptSource = readFile(root.parent_path() / "main.nut");
    const std::regex  translationCall(R"rx(\btrp?\("([^"]+)")rx");
    for (std::sregex_iterator it(scriptSource.begin(), scriptSource.end(), translationCall), end; it != end; ++it) {
        const std::string key = (*it)[1].str();
        auto              coverage = localization->validateKeyCoverage(key);
        CHECK(coverage.ok());
        if (coverage.ok()) CHECK_EQ(coverage.value(), 2);
    }
    const std::regex logCall(R"rx(\blogMessage\("([^"]+)")rx");
    for (std::sregex_iterator it(scriptSource.begin(), scriptSource.end(), logCall), end; it != end; ++it) {
        auto coverage = localization->validateKeyCoverage("gameplayLog." + (*it)[1].str());
        CHECK(coverage.ok());
        if (coverage.ok()) CHECK_EQ(coverage.value(), 2);
    }
    REQUIRE(localization->selectLanguage("zh-CN").ok());
    CHECK_EQ(localization->get("content.item.iron_sword.name"), std::string("铁剑"));
    REQUIRE(localization->selectLanguage("en").ok());
    CHECK_EQ(localization->get("content.item.iron_sword.name"), std::string("Iron Sword"));
    std::set<std::string> conversationIds;
    for (const auto& asset : assets) {
        conversationIds.insert(asset.id);
        for (const auto& node : asset.nodes) {
            if (node.i18nKey.empty()) continue;
            CHECK(localization->hasInLanguage("zh-CN", node.i18nKey));
            CHECK(localization->hasInLanguage("en", node.i18nKey));
        }
    }
    const auto* arrival = eve::rpg::StoryEventCatalogue::find("forest.arrival");
    REQUIRE(arrival != nullptr);
    for (const auto& step : arrival->steps) {
        if (step.kind == eve::rpg::StoryEventStepKind::Dialogue) CHECK(conversationIds.count(step.reference) == 1);
        if (step.kind == eve::rpg::StoryEventStepKind::Message)
            CHECK(localization->validateKeyCoverage(step.reference).ok());
    }

    const std::vector<std::string> states{"offer", "active", "turnin", "completed"};
    for (const auto& mapName : {"village.json", "forest.json"}) {
        std::vector<eve::map::MapObject> objects;
        std::string                      error;
        auto                             layers = eve::map::loadMapText(readFile(root / mapName), &objects, &error);
        REQUIRE(error.empty());
        REQUIRE(!layers.empty());
        for (const auto& object : objects) {
            const auto property = [&](const char* name) {
                const auto found = object.properties.find(name);
                return found == object.properties.end() ? std::string{} : found->second;
            };
            if (object.type == "quest_npc") {
                const auto questId      = property("questId");
                const auto dialogueBase = property("dialogueBase");
                CHECK(eve::rpg::QuestRegistry::contains(questId));
                for (const auto& state : states) CHECK(conversationIds.count(dialogueBase + "." + state) == 1);
            } else if (object.type == "loot") {
                const auto requiredQuest = property("requiredQuest");
                const bool validRequiredQuest =
                    requiredQuest.empty() || eve::rpg::QuestRegistry::contains(requiredQuest);
                CHECK(validRequiredQuest);
            } else if (object.type == "encounter") {
                CHECK(eve::rpg::EncounterCatalogue::find(property("encounterId")) != nullptr);
            }
        }
    }
    eve::rpg::QuestRegistry::clear();
    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::StoryEventCatalogue::clear();
    localization->clear();
}
