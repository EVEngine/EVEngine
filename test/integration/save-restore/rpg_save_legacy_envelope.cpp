#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Snapshot.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySaveSession.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"

#include <cstdint>

namespace {

eve::SnapshotHashProvider legacyRpgHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        eve::ContentId::Bytes digest{};
        for (int index = 0; index < 8; ++index) {
            digest[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            digest[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(digest));
    };
}

}  // namespace

TEST_CASE("rpg.saveSessionProtobuf.readsLegacyJsonEnvelopeWithoutRewritingIt") {
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker tracker;
    auto             *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 70.0);
    eve::inventory::Bag                  bag(2);
    eve::inventory::EquipmentSet         equipment;
    eve::inventory::InventorySaveSession inventory;
    inventory.bind(bag, equipment);

    auto stateJson     = state.snapshotJson();
    auto trackerJson   = tracker.snapshotJson();
    auto actorJson     = actor->checkpointJson();
    auto inventoryJson = inventory.snapshotJson();
    REQUIRE(stateJson.ok());
    REQUIRE(trackerJson.ok());
    REQUIRE(actorJson.ok());
    REQUIRE(inventoryJson.ok());
    auto stateValue     = eve::Value::fromJson(stateJson.value());
    auto trackerValue   = eve::Value::fromJson(trackerJson.value());
    auto actorValue     = eve::Value::fromJson(actorJson.value());
    auto inventoryValue = eve::Value::fromJson(inventoryJson.value());
    REQUIRE(stateValue.ok());
    REQUIRE(trackerValue.ok());
    REQUIRE(actorValue.ok());
    REQUIRE(inventoryValue.ok());
    eve::Value::Object payload;
    payload.emplace("contentVersion", eve::Value("content.v1"));
    payload.emplace("gameState", std::move(stateValue).takeValue());
    payload.emplace("questTracker", std::move(trackerValue).takeValue());
    payload.emplace("actor", std::move(actorValue).takeValue());
    payload.emplace("inventory", std::move(inventoryValue).takeValue());
    const auto schema = eve::LogicalId::parse("rpg:save-session");
    REQUIRE(schema.has_value());
    auto legacy = eve::makeSnapshotEnvelope("rpg.save-session", *schema, eve::SchemaVersion(1),
                                            eve::PersistentId::nil(), eve::Revision(0), eve::SimulationTick(0),
                                            eve::Value(std::move(payload)), legacyRpgHash());
    REQUIRE(legacy.ok());
    auto json = eve::serializeSnapshotEnvelope(legacy.value());
    REQUIRE(json.ok());

    eve::rpg::RPGSaveSession session;
    session.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(session.setContentVersion("content.v1").ok());
    state.setVariable("gold", 99.0);
    actor->setCurrent("hp", 1.0);
    REQUIRE(session.restoreSnapshotJson(json.value()).ok());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK_EQ(actor->getCurrent("hp"), 70.0);
    actor->release();
}
