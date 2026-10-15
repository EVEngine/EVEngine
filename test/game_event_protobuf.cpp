#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/SnapshotHash.h"
#include "game_event/GameEvent.h"

#include <cstdint>
#include <string>
#include <utility>

using eve::game_event::GameEvent;
using eve::game_event::GameEventLog;

TEST_CASE("game_event.protobuf.binaryRoundTripAndAtomicFailure") {
    GameEventLog original;
    GameEvent    event;
    event.eventId       = *eve::game_event::EventId::parse("018f0f4e-6b3c-7abc-8def-0123456789ab");
    event.type          = "damage.applied";
    event.source        = "combat:system";
    event.subject       = "rpg:target-7";
    event.schemaId      = *eve::LogicalId::parse("combat:damage");
    event.schemaVersion = eve::SchemaVersion(2);
    event.tick          = eve::SimulationTick(77);
    event.flags         = 4;
    event.payload       = "{\"amount\":12}";
    REQUIRE(original.append(std::move(event)).ok());

    GameEventLog importedLegacy;
    REQUIRE(importedLegacy.restore(original.snapshotJson()).ok());
    auto upgraded = importedLegacy.snapshotBinary();
    REQUIRE(upgraded.ok());

    auto binary = original.snapshotBinary();
    REQUIRE(binary.ok());
    CHECK_EQ(upgraded.value(), binary.value());
    GameEventLog restored;
    REQUIRE(restored.restoreBinary(binary.value()).ok());
    REQUIRE(restored.find(1) != nullptr);
    CHECK_EQ(restored.find(1)->payload, std::string("{\"amount\":12}"));
    CHECK_EQ(restored.find(1)->tick.value(), std::uint64_t{77});
    auto recaptured = restored.snapshotBinary();
    REQUIRE(recaptured.ok());
    CHECK_EQ(recaptured.value(), binary.value());

    const auto  before  = restored.snapshotJson();
    std::string unknown = binary.value();
    unknown.append("\xA0\x06\x01", 3);
    CHECK(!restored.restoreBinary(unknown).ok());
    CHECK(!restored.restoreBinary("invalid").ok());
    CHECK_EQ(restored.snapshotJson(), before);

    const auto hash     = eve::snapshotContentHashProvider();
    auto       envelope = original.snapshotEnvelopeBinary(hash);
    REQUIRE(envelope.ok());
    GameEventLog sealed;
    REQUIRE(sealed.restoreSnapshotBinary(envelope.value(), hash).ok());
    CHECK_EQ(sealed.snapshotJson(), original.snapshotJson());
    std::string tampered = envelope.value();
    tampered.back() ^= 1;
    CHECK(!sealed.restoreSnapshotBinary(tampered, hash).ok());
    CHECK_EQ(sealed.snapshotJson(), original.snapshotJson());
}
