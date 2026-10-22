#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_game_save_session)
#include "PrivateTestScope.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include "common/GameSaveSession.h"
#include "zeroerr/unittest.h"
// common_game_save_session.cpp
namespace test_common_game_save_session {
namespace {

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

class FakeParticipant final : public eve::IGameSaveParticipant {
public:
    FakeParticipant(std::string key, int value) : value(value), key_(std::move(key)) {}
    std::string_view        saveKey() const noexcept override { return key_; }
    eve::Result<eve::Value> capture() const override { return eve::Result<eve::Value>::success(eve::Value(value)); }
    eve::Result<std::unique_ptr<eve::IPreparedGameRestore>> prepare(const eve::Value &state) override {
        if (reject || !state.isInt64())
            return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "injected participant failure"));
        if (returnNull) return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::success(nullptr);
        class Prepared final : public eve::IPreparedGameRestore {
        public:
            Prepared(FakeParticipant &owner, int next) : owner_(owner), next_(next) {}
            void commit() noexcept override { owner_.value = next_; }

        private:
            FakeParticipant &owner_;
            int              next_;
        };
        std::unique_ptr<eve::IPreparedGameRestore> prepared =
            std::make_unique<Prepared>(*this, static_cast<int>(state.asInt()));
        return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::success(std::move(prepared));
    }
    int  value      = 0;
    bool reject     = false;
    bool returnNull = false;

private:
    std::string key_;
};

}  // namespace

TEST_CASE("gameSave.failedLaterPrepareLeavesEarlierParticipantUnchanged") {
    FakeParticipant      first("rpg", 25);
    FakeParticipant      second("tactics", 3);
    eve::GameSaveSession session("game-content-v1");
    REQUIRE(session.add(first).ok());
    REQUIRE(session.add(second).ok());
    auto saved = session.snapshotJson(testHash());
    REQUIRE(saved.ok());

    first.value   = 99;
    second.value  = 9;
    second.reject = true;
    CHECK(!session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);
    CHECK_EQ(second.value, 9);

    second.reject     = false;
    second.returnNull = true;
    CHECK(!session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);
    second.returnNull = false;
    REQUIRE(session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 25);
    CHECK_EQ(second.value, 3);
}

TEST_CASE("gameSave.absentProviderAndContentMigrationAreExplicit") {
    FakeParticipant      first("rpg", 25);
    FakeParticipant      second("tactics", 3);
    eve::GameSaveSession source("game-content-v1");
    REQUIRE(source.add(first).ok());
    REQUIRE(source.add(second).ok());
    auto saved = source.snapshotJson(testHash());
    REQUIRE(saved.ok());

    eve::GameSaveSession absent("game-content-v1");
    REQUIRE(absent.add(first).ok());
    first.value = 99;
    CHECK(!absent.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);

    eve::GameSaveSession migrated("game-content-v2");
    REQUIRE(migrated.add(first).ok());
    REQUIRE(migrated.add(second).ok());
    CHECK(!migrated.restoreJson(saved.value(), testHash()).ok());
    REQUIRE(migrated
                .addContentMigration("game-content-v1",
                                     [](const eve::Value &states) { return eve::Result<eve::Value>::success(states); })
                .ok());
    REQUIRE(migrated.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 25);
}
}  // namespace test_common_game_save_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_dialogue_persistence)
#include "PrivateTestScope.h"
#include "dialogue/ConversationPersistence.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DialogueState.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// dialogue_persistence.cpp
namespace test_dialogue_persistence {
using namespace eve;
using namespace eve::dialogue;

namespace {

eve::dnut::SequenceAsset makeAsset(std::string id, int version, std::string line) {
    eve::dnut::SequenceAsset asset;
    asset.id      = std::move(id);
    asset.version = version;
    asset.entry   = line;
    asset.nodes   = {{line, "line", "end"}, {"end", "end"}};
    return asset;
}

}  // namespace

TEST_CASE("dialoguePersistence.jsonRoundtrip") {
    StateValue state = StateValue::object();
    state.set("text", StateValue::string("你好 \"traveler\"\n"));
    state.set("count", StateValue::integer(9007199254740991LL));
    StateValue array = StateValue::array();
    array.pushBack(StateValue::boolean(true));
    array.pushBack(StateValue::number(2.5));
    state.set("values", std::move(array));
    auto encoded = conversationStateToJson(state);
    REQUIRE(encoded.ok());
    const auto json    = std::move(encoded).takeValue();
    auto       decoded = conversationStateFromJson(json);
    REQUIRE(decoded.ok());
    StateValue restored = std::move(decoded).takeValue();
    CHECK(restored == state);
}

TEST_CASE("dialoguePersistence.migratesCurrentAndCallFrames") {
    eve::dnut::SequenceAsset currentParent = makeAsset("scene.parent", 4, "after-renamed");
    eve::dnut::SequenceAsset currentChild  = makeAsset("common.child.v2", 3, "line-renamed");
    eve::dnut::SequenceAsset oldParent     = makeAsset("scene.parent", 1, "call");
    eve::dnut::SequenceAsset oldChild      = makeAsset("common.child", 1, "line");
    oldParent.nodes[0].type                = "call";
    oldParent.nodes[0].payload.set("target", Value(oldChild.id));
    oldParent.nodes[0].payload.set("return", Value("after"));
    oldParent.nodes.push_back({"after", "line", "end"});
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    eve::dnut::SequenceRuntime runner;
    runner.setStepRegistry(&registry);
    runner.setAssetResolver([&](const std::string &id) -> const eve::dnut::SequenceAsset * {
        if (id == oldParent.id) return &oldParent;
        if (id == oldChild.id) return &oldChild;
        return nullptr;
    });
    REQUIRE(runner.start(&oldParent).ok());
    Value captured;
    REQUIRE(runner.captureState(captured).ok());
    StateValue saved = toDialogueStateValue(captured);

    ConversationSaveMigrations migrations;
    CHECK(migrations.registerMigration("common.child", 1, "common.child.v2", "line:line-renamed").ok());
    CHECK(migrations.registerMigration("scene.parent", 1, "scene.parent", "after:after-renamed").ok());
    const auto resolveCurrent = [&](const std::string &id) -> const eve::dnut::SequenceAsset * {
        if (id == currentParent.id) return &currentParent;
        if (id == currentChild.id) return &currentChild;
        return nullptr;
    };
    auto migrated = migrations.migrate(saved, resolveCurrent);
    REQUIRE(migrated.ok());
    saved = std::move(migrated).takeValue();
    eve::dnut::SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver(resolveCurrent);
    CHECK(restored.restoreState(toCanonicalValue(saved)).ok());
    CHECK(restored.currentNodeId() == "line-renamed");
    CHECK(restored.advance().ok());
    CHECK(restored.currentNodeId() == "after-renamed");
}

TEST_CASE("dialoguePersistence.rejectsLegacyUnversionedState") {
    eve::dnut::SequenceRuntime runner;
    Value                      legacy   = Value::Object{{"active", Value(false)}};
    auto                       rejected = runner.restoreState(legacy);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}
}  // namespace test_dialogue_persistence
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_canvas_persistence)
#include "PrivateTestScope.h"
#include <cmath>
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "zeroerr/unittest.h"
// graphics_canvas_persistence.cpp
namespace test_graphics_canvas_persistence {
using namespace eve::graphics;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.canvas.separateSubmissionsPreserveAndClearRGBA8") {
    auto *gfx    = headlessGraphics();
    auto *canvas = gfx->newCanvas(16, 16);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 8, 16, Color(1.f, 0.f, 0.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();
    gfx->setCanvas(canvas);
    gfx->drawSolidRect(0, 0, 4, 16, Color(0.f, 0.f, 1.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> snapshot(canvas->newImageData());
    auto                                   overlap = snapshot->getPixel(2, 2);
    CHECK(std::abs(overlap.r - 0.25f) < 0.01f);
    CHECK(std::abs(overlap.b - 0.5f) < 0.01f);
    CHECK(std::abs(overlap.a - 0.75f) < 0.01f);
    CHECK(std::abs(snapshot->getPixel(6, 2).r - 0.5f) < 0.01f);
    CHECK(snapshot->getPixel(12, 2).a == 0.f);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(12, 0, 4, 16, Color(0.f, 1.f, 0.f, 1.f));
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> cleared(canvas->newImageData());
    CHECK(cleared->getPixel(2, 2).a == 0.f);
    CHECK(cleared->getPixel(14, 2).g == 1.f);
    CHECK(std::abs(snapshot->getPixel(2, 2).a - 0.75f) < 0.01f);
}
}  // namespace test_graphics_canvas_persistence
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_save_session)
#include "PrivateTestScope.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_save_session.cpp
namespace test_rpg_save_session {
using namespace eve::rpg;

TEST_CASE("rpg.saveSession.partyRoundTripIsAtomic") {
    QuestRegistry::clear();
    GameState state;
    state.setVariable("gold", 25.0);
    Tracker tracker;
    auto   *hero      = RPGActor::createActor();
    auto   *companion = RPGActor::createActor();
    REQUIRE(hero != nullptr);
    REQUIRE(companion != nullptr);
    hero->setBaseAttribute("hp", 100.0);
    companion->setBaseAttribute("hp", 80.0);
    hero->setCurrent("hp", 70.0);
    companion->setCurrent("hp", 45.0);
    Party party;
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    eve::inventory::Bag          bag(4);
    eve::inventory::EquipmentSet equipment;
    RPGSaveSession               session;
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
    GameState                    state;
    Tracker                      tracker;
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    auto                        *oldHero = RPGActor::createActor();
    REQUIRE(oldHero != nullptr);
    oldHero->setBaseAttribute("hp", 100.0);
    oldHero->setCurrent("hp", 31.0);
    RPGSaveSession oldSession;
    oldSession.bind(state, tracker, *oldHero, bag, equipment);
    REQUIRE(oldSession.setContentVersion("test.single.v1").ok());
    auto oldSave = oldSession.snapshotJson();
    REQUIRE(oldSave.ok());

    auto *hero      = RPGActor::createActor();
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
    ])"),
               1);
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
    potion.id       = "potion";
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
    RPGSaveSession               restored;
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
    eve::Value  corrupted = std::move(parsed).takeValue();
    eve::Value *payload   = corrupted.find("payload");
    REQUIRE(payload != nullptr);
    eve::Value *gameState    = payload->find("gameState");
    eve::Value *questTracker = payload->find("questTracker");
    eve::Value *inventory    = payload->find("inventory");
    REQUIRE(gameState != nullptr);
    REQUIRE(questTracker != nullptr);
    REQUIRE(inventory != nullptr);
    eve::Value *variables = gameState->find("variables");
    eve::Value *entries   = questTracker->find("entries");
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
    CHECK_EQ(static_cast<int>(invalidPreview.error()->code()), static_cast<int>(eve::DiagnosticCode::HashMismatch));
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
    ])"),
               1);
    GameState sourceState;
    Tracker   sourceTracker;
    sourceTracker.notify("kill", "slime", 2);
    RPGActor *sourceActor = RPGActor::createActor();
    REQUIRE(sourceActor != nullptr);
    sourceActor->setBaseAttribute("hp", 100.0);
    sourceActor->setCurrent("hp", 75.0);
    eve::inventory::Bag          sourceBag(2);
    eve::inventory::EquipmentSet sourceEquipment;
    RPGSaveSession               source;
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
    ])"),
               2);
    GameState targetState;
    Tracker   targetTracker;
    targetTracker.notify("kill", "slime", 1);
    RPGActor *targetActor = RPGActor::createActor();
    REQUIRE(targetActor != nullptr);
    targetActor->setBaseAttribute("hp", 100.0);
    targetActor->setCurrent("hp", 100.0);
    eve::inventory::Bag          targetBag(2);
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
    oldVersionAlreadyContainingQuest.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
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
    ])"),
               1);
    eve::inventory::ItemDefinition oldPotion;
    oldPotion.id       = "potion.old";
    oldPotion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(oldPotion);

    GameState sourceState;
    Tracker   sourceTracker;
    sourceTracker.notify("kill", "slime", 1);
    RPGActor *sourceActor = RPGActor::createActor();
    REQUIRE(sourceActor != nullptr);
    sourceActor->setBaseAttribute("hp", 100.0);
    sourceActor->setCurrent("hp", 80.0);
    eve::inventory::Bag sourceBag(2);
    sourceBag.addItem("potion.old", 2);
    eve::inventory::EquipmentSet sourceEquipment;
    RPGSaveSession               source;
    source.bind(sourceState, sourceTracker, *sourceActor, sourceBag, sourceEquipment);
    REQUIRE(source.setContentVersion("content.v1").ok());
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    QuestRegistry::clear();
    eve::inventory::ItemRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.new","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"objective.new","topic":"kill","target":"slime","count":3}]}
    ])"),
               1);
    eve::inventory::ItemDefinition newPotion;
    newPotion.id       = "potion.new";
    newPotion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(newPotion);

    GameState targetState;
    targetState.setVariable("sentinel", 7.0);
    Tracker   targetTracker;
    RPGActor *targetActor = RPGActor::createActor();
    REQUIRE(targetActor != nullptr);
    targetActor->setBaseAttribute("hp", 100.0);
    targetActor->setCurrent("hp", 100.0);
    eve::inventory::Bag          targetBag(0);
    eve::inventory::EquipmentSet targetEquipment;

    RPGSaveSession missingRoute;
    missingRoute.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(missingRoute.setContentVersion("content.v2").ok());
    REQUIRE(missingRoute.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item, "potion.old", "potion.new").ok());
    CHECK(!missingRoute.validateSnapshotJson(encoded.value()).ok());
    CHECK_EQ(targetState.getVariable("sentinel"), 7.0);
    CHECK_EQ(targetBag.countItem("potion.new"), 0);

    RPGSaveSession migrated;
    migrated.bind(targetState, targetTracker, *targetActor, targetBag, targetEquipment);
    REQUIRE(migrated.setContentVersion("content.v2").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item, "potion.old", "potion.new").ok());
    REQUIRE(migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Quest, "quest.old", "quest.new").ok());
    REQUIRE(
        migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::QuestObjective, "objective.old", "objective.new")
            .ok());
    CHECK(!migrated.addIdRenameMigration("content.v1", RPGSaveIdDomain::Item, "potion.old", "another.potion").ok());
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
}  // namespace test_rpg_save_session
#endif
