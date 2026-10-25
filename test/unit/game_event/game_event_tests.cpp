// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "event_envelope.cpp",
    "eventstream.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_event_envelope)
#include "PrivateTestScope.h"
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include "game_event/GameEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// event_envelope.cpp
namespace test_event_envelope {
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
}  // namespace test_event_envelope
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_eventstream)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "GameEventTestSupport.h"
#include "common/Module.h"
#include "game_event/GameEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// eventstream.cpp
namespace test_eventstream_unit {
using namespace eve::game_event;

namespace {

[[maybe_unused]] std::uint64_t appendSuccess(GameEventLog& stream, std::uint64_t serial, std::string type,
                                             std::string source, std::string subject, std::string causation,
                                             std::string correlation, std::uint64_t tick, std::uint32_t flags,
                                             std::string payload = "null") {
    auto result =
        stream.append(test::envelope(serial, std::move(type), std::move(source), std::move(subject),
                                     std::move(causation), std::move(correlation), tick, flags, std::move(payload)));
    REQUIRE(result.ok());
    return result.value().value();
}

}  // namespace

TEST_CASE("game_event.emit.monotonicEnvelope") {
    GameEventLog stream;
    CHECK_EQ(appendSuccess(stream, 1, "created", "system", "entity-7", "", "flow-2", 41, 3, "{\"hp\":9}"), uint64_t{1});
    CHECK_EQ(appendSuccess(stream, 2, "changed", "rule", "entity-7", "1", "flow-2", 42, 0), uint64_t{2});
    REQUIRE(stream.find(2) != nullptr);
    CHECK(!stream.find(1)->eventId.isNil());
    CHECK(!stream.find(2)->eventId.isNil());
    CHECK_EQ(stream.find(2)->causation.format(), std::string("1"));
    CHECK_EQ(stream.find(2)->tick.value(), uint64_t{42});
    CHECK_EQ(stream.find(2)->flags, uint32_t{0});
    CHECK_EQ(stream.find(1)->payload, std::string("{\"hp\":9}"));
    auto emptyType = stream.append(test::envelope(3, "", "", "", "", "", 0, 0));
    REQUIRE(!emptyType.ok());
    auto invalidPayload = stream.append(test::envelope(4, "bad", "", "", "", "", 0, 0, "{"));
    REQUIRE(!invalidPayload.ok());
}

TEST_CASE("game_event.query.filtersInSequenceOrder") {
    GameEventLog stream;
    CHECK_EQ(appendSuccess(stream, 1, "a", "one", "x", "", "", 0, 0), uint64_t{1});
    CHECK_EQ(appendSuccess(stream, 2, "b", "two", "x", "", "", 0, 0), uint64_t{2});
    CHECK_EQ(appendSuccess(stream, 3, "a", "two", "y", "", "", 0, 0), uint64_t{3});
    CHECK_EQ(stream.queryType("a"), 2);
    CHECK_EQ(stream.queryAt(0)->sequence.value(), uint64_t{1});
    CHECK_EQ(stream.queryAt(1)->sequence.value(), uint64_t{3});
    CHECK_EQ(stream.querySource("two"), 2);
    CHECK_EQ(stream.querySubject("x"), 2);
    CHECK_EQ(stream.querySequence(2), 2);
}

TEST_CASE("game_event.consumer.cursorReadsBatches") {
    GameEventLog stream;
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(appendSuccess(stream, static_cast<std::uint64_t>(i + 1), "tick", "clock", "", "", "",
                               static_cast<std::uint64_t>(i), 0),
                 static_cast<uint64_t>(i + 1));
    auto* consumer = stream.newConsumer(2);
    REQUIRE(consumer != nullptr);
    CHECK_EQ(consumer->read(2), 2);
    CHECK_EQ(consumer->batchAt(0)->sequence.value(), uint64_t{2});
    CHECK_EQ(consumer->batchAt(1)->sequence.value(), uint64_t{3});
    CHECK_EQ(consumer->position().value(), uint64_t{4});
    CHECK_EQ(consumer->read(10), 2);
    CHECK_EQ(consumer->position().value(), uint64_t{6});
}

TEST_CASE("game_event.retention.clearAndResetSemantics") {
    GameEventLog stream;
    const auto   a = appendSuccess(stream, 1, "a", "", "", "", "", 0, 0);
    const auto   b = appendSuccess(stream, 2, "b", "", "", "", "", 0, 0);
    const auto   c = appendSuccess(stream, 3, "c", "", "", "", "", 0, 0);
    CHECK_EQ(a, uint64_t{1});
    CHECK_EQ(b, uint64_t{2});
    CHECK_EQ(c, uint64_t{3});
    stream.clearBefore(3);
    REQUIRE(stream.size() == 1);
    CHECK_EQ(stream.at(0)->sequence.value(), uint64_t{3});
    stream.clear();
    const auto d = appendSuccess(stream, 4, "d", "", "", "", "", 0, 0);
    CHECK_EQ(d, uint64_t{4});
    stream.reset();
    const auto e = appendSuccess(stream, 5, "e", "", "", "", "", 0, 0);
    CHECK_EQ(e, uint64_t{1});
}

TEST_CASE("game_event.snapshot.roundTripsDeterministically") {
    GameEventLog original;
    CHECK_EQ(appendSuccess(original, 1, "created", "admin", "base-1", "", "campaign", 17, 5,
                           "{ \"rank\": 3, \"general\": \"g-1\" }"),
             uint64_t{1});
    CHECK_EQ(
        appendSuccess(original, 2, "assigned", "admin", "general-1", "1", "campaign", 18, 0, "[\"unit-2\",\"unit-3\"]"),
        uint64_t{2});
    const std::string snapshot = original.snapshotJson();

    GameEventLog restored;
    REQUIRE(restored.restore(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.find(1)->tick.value(), uint64_t{17});
    CHECK_EQ(restored.find(1)->payload, std::string("{\"general\":\"g-1\",\"rank\":3}"));
    CHECK_EQ(appendSuccess(restored, 3, "next", "", "", "", "", 19, 0), uint64_t{3});

    const std::string before = restored.snapshotJson();
    CHECK(!restored.restore("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);

    const std::string unknownField   = snapshot.substr(0, snapshot.size() - 1) + ",\"unexpected\":true}";
    auto              schemaRejected = restored.restore(unknownField);
    CHECK(!schemaRejected.ok());
    CHECK_EQ(restored.snapshotJson(), before);
}
}  // namespace test_eventstream_unit
#endif
