#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "game_event/GameEvent.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

using namespace eve::game_event;

namespace {

EventId id(const char* text) {
    const auto parsed = EventId::parse(text);
    if (!parsed) return {};
    return *parsed;
}

GameEvent metadata(const char* eventId, const char* schema, uint64_t tick, const char* causation = "",
                   const char* correlation = "") {
    GameEvent envelope;
    envelope.eventId = id(eventId);
    envelope.type    = "damage.applied";
    envelope.source  = "combat:system";
    envelope.subject = "rpg:target-7";
    if (causation[0] != '\0') envelope.causation = CausationRef::fromEventId(id(causation));
    if (correlation[0] != '\0') envelope.correlation = CorrelationId::fromEventId(id(correlation));
    envelope.schemaId      = *SchemaId::parse(schema);
    envelope.schemaVersion = SchemaVersion(2);
    envelope.tick          = SimulationTick(tick);
    envelope.flags         = 4;
    return envelope;
}

struct DamagePayload {
    int         amount = 0;
    std::string kind;
};

}  // namespace

TEST_CASE("event_envelope.typedPayloadAndCausalMetadata") {
    GameEventLog stream;
    const auto   causation   = id("018f0f4e-6b3c-7abc-8def-0123456789ab");
    const auto   correlation = id("018f0f4e-6b3c-7abd-8def-0123456789ab");
    const auto   event       = id("018f0f4e-6b3c-7abe-8def-0123456789ab");

    auto envelope =
        metadata(event.format().c_str(), "combat:damage", 77, causation.format().c_str(), correlation.format().c_str());
    DamagePayload payload{12, "fire"};
    auto appended = stream.appendTyped<DamagePayload>(std::move(envelope), payload, [](const DamagePayload& value) {
        return eve::Result<std::string>::success("{\"amount\":" + std::to_string(value.amount) + ",\"kind\":\"" +
                                                 value.kind + "\"}");
    });
    REQUIRE(appended.ok());
    CHECK_EQ(appended.value().value(), uint64_t{1});

    const auto* stored = stream.find(EventSequence(1));
    REQUIRE(stored != nullptr);
    CHECK_EQ(stored->eventId, event);
    CHECK_EQ(stored->sequence.value(), uint64_t{1});
    CHECK_EQ(stored->tick.value(), uint64_t{77});
    CHECK_EQ(stored->schemaId.format(), std::string("combat:damage"));
    CHECK_EQ(stored->schemaVersion.value(), uint64_t{2});
    CHECK_EQ(stored->causation.format(), causation.format());
    CHECK_EQ(stored->causation.kind(), CausationRef::Kind::Event);
    CHECK_EQ(stored->correlation.format(), correlation.format());
    CHECK_EQ(stored->correlation.kind(), CorrelationId::Kind::Id);
    CHECK_EQ(stored->payload, std::string("{\"amount\":12,\"kind\":\"fire\"}"));
}

TEST_CASE("event_envelope.uuidV7GeneratorFillsMissingEventId") {
    const auto   timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1710000000123));
    GameEventLog stream(
        [](std::span<uint8_t> bytes) {
            for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i + 1);
            return true;
        },
        [timestamp] { return timestamp; });

    auto envelope    = metadata("00000000-0000-0000-0000-000000000000", "combat:damage", 3);
    envelope.eventId = EventId::nil();
    auto appended    = stream.append(std::move(envelope));
    REQUIRE(appended.ok());
    CHECK(!stream.find(EventSequence(1))->eventId.isNil());
    CHECK_EQ(stream.find(EventSequence(1))->eventId.format().substr(14, 1), std::string("7"));
}

TEST_CASE("event_envelope.restorePreservesStreamOrderAndNextSequence") {
    GameEventLog original;
    auto         first  = metadata("018f0f4e-6b3c-7ab1-8def-0123456789ab", "combat:damage", 10);
    auto         second = metadata("018f0f4e-6b3c-7ab2-8def-0123456789ab", "combat:damage", 11,
                                   "018f0f4e-6b3c-7ab1-8def-0123456789ab", "018f0f4e-6b3c-7ab0-8def-0123456789ab");
    first.payload       = "{\"amount\":1}";
    second.payload      = "{\"amount\":2}";
    auto firstResult    = original.append(std::move(first));
    auto secondResult   = original.append(std::move(second));
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    CHECK_EQ(firstResult.value().value(), uint64_t{1});
    CHECK_EQ(secondResult.value().value(), uint64_t{2});
    const std::string snapshot = original.snapshotJson();

    GameEventLog restored;
    auto         restoredResult = restored.restore(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.find(EventSequence(1))->eventId, id("018f0f4e-6b3c-7ab1-8def-0123456789ab"));
    CHECK_EQ(restored.find(EventSequence(2))->causation.format(), std::string("018f0f4e-6b3c-7ab1-8def-0123456789ab"));
    CHECK_EQ(restored.find(EventSequence(2))->correlation.format(),
             std::string("018f0f4e-6b3c-7ab0-8def-0123456789ab"));

    auto third       = metadata("018f0f4e-6b3c-7ab3-8def-0123456789ab", "combat:damage", 12);
    auto thirdResult = restored.append(std::move(third));
    REQUIRE(thirdResult.ok());
    CHECK_EQ(thirdResult.value().value(), uint64_t{3});
    CHECK_EQ(restored.find(EventSequence(3))->sequence.value(), uint64_t{3});
    CHECK_EQ(restored.size(), 3);
}

TEST_CASE("event_envelope.restoreFailureIsTransactional") {
    GameEventLog stream;
    auto         event    = metadata("018f0f4e-6b3c-7ab4-8def-0123456789ab", "combat:damage", 20);
    auto         appended = stream.append(std::move(event));
    REQUIRE(appended.ok());
    const std::string before = stream.snapshotJson();

    auto failed = stream.restore("{\"version\":2,\"nextSequence\":\"2\",\"events\":[");
    CHECK(!failed.ok());
    CHECK_EQ(stream.snapshotJson(), before);
    CHECK_EQ(stream.find(EventSequence(1))->sequence.value(), uint64_t{1});
}

TEST_CASE("event_envelope.restoreRejectsDuplicateNonNilEventId") {
    const std::string duplicate =
        "{\"version\":2,\"nextSequence\":\"3\",\"events\":["
        "{\"eventId\":\"018f0f4e-6b3c-7ab1-8def-0123456789ab\",\"sequence\":\"1\","
        "\"type\":\"a\",\"source\":\"s\",\"subject\":\"x\",\"causationKind\":\"none\","
        "\"causation\":\"\",\"correlationKind\":\"none\",\"correlation\":\"\","
        "\"schemaId\":\"test:a\",\"schemaVersion\":\"1\",\"tick\":\"1\",\"flags\":0,\"payload\":null},"
        "{\"eventId\":\"018f0f4e-6b3c-7ab1-8def-0123456789ab\",\"sequence\":\"2\","
        "\"type\":\"b\",\"source\":\"s\",\"subject\":\"x\",\"causationKind\":\"none\","
        "\"causation\":\"\",\"correlationKind\":\"none\",\"correlation\":\"\","
        "\"schemaId\":\"test:b\",\"schemaVersion\":\"1\",\"tick\":\"2\",\"flags\":0,\"payload\":null}]}";

    GameEventLog stream;
    auto         result = stream.restore(duplicate);
    CHECK(!result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Conflict);
    CHECK_EQ(stream.size(), 0);
}
