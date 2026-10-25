#include "game_event/GameEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_event_envelope)
#include <chrono>
#include <span>
#include "PrivateTestScope.h"
namespace test_event_envelope {
using namespace eve::game_event;

namespace {
EventId id(const char* text) {
    auto parsed = EventId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

GameEvent envelope(EventId eventId, std::uint64_t tick) {
    GameEvent result;
    result.eventId       = eventId;
    result.type          = "damage.applied";
    result.source        = "combat:system";
    result.subject       = "rpg:target-7";
    result.schemaId      = *SchemaId::parse("combat:damage");
    result.schemaVersion = SchemaVersion(2);
    result.tick          = SimulationTick(tick);
    result.flags         = 4;
    return result;
}
}  // namespace

TEST_CASE("Game event typed envelope publication and persistence contract") {
    SUB_CASE("game event envelope contract: typed payload retains causal metadata and stream order") {
        const auto cause       = id("018f0f4e-6b3c-7abc-8def-0123456789ab");
        const auto correlation = id("018f0f4e-6b3c-7abd-8def-0123456789ab");
        const auto eventId     = id("018f0f4e-6b3c-7abe-8def-0123456789ab");
        auto       event       = envelope(eventId, 77);
        event.causation        = CausationRef::fromEventId(cause);
        event.correlation      = CorrelationId::fromEventId(correlation);
        struct Damage {
            int         amount;
            std::string kind;
        };
        GameEventLog log;
        auto         appended = log.appendTyped<Damage>(event, {12, "fire"}, [](const Damage& value) {
            return eve::Result<std::string>::success("{\"amount\":" + std::to_string(value.amount) + ",\"kind\":\"" +
                                                             value.kind + "\"}");
        });
        REQUIRE(appended.ok());
        REQUIRE_EQ(appended.value().value(), 1u);
        const auto* stored = log.find(EventSequence(1));
        REQUIRE(stored != nullptr);
        REQUIRE_EQ(stored->eventId, eventId);
        REQUIRE_EQ(stored->tick.value(), 77u);
        REQUIRE_EQ(stored->schemaId.format(), "combat:damage");
        REQUIRE_EQ(stored->schemaVersion.value(), 2u);
        REQUIRE_EQ(stored->causation.kind(), CausationRef::Kind::Event);
        REQUIRE_EQ(stored->causation.format(), cause.format());
        REQUIRE_EQ(stored->correlation.kind(), CorrelationId::Kind::Id);
        REQUIRE_EQ(stored->correlation.format(), correlation.format());
        REQUIRE_EQ(stored->payload, "{\"amount\":12,\"kind\":\"fire\"}");
    };

    SUB_CASE("game event envelope contract: encoder and envelope validation fail before append") {
        GameEventLog log;
        auto         valid = envelope(id("018f0f4e-6b3c-7ab1-8def-0123456789ab"), 1);
        REQUIRE(!log.appendTyped<int>(valid, 3, {}).ok());
        REQUIRE(!log.appendTyped<int>(valid, 3,
                                      [](const int&) {
                                          return eve::Result<std::string>::failure(
                                              eve::Diagnostic::error(eve::DiagnosticCode::Cancelled, "encode failed"));
                                      })
                     .ok());
        valid.type.clear();
        REQUIRE(!log.append(valid).ok());
        REQUIRE_EQ(log.size(), 0);
    };

    SUB_CASE("game event envelope contract: injected clock and entropy create UUIDv7 for nil ID") {
        const auto   timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1710000000123));
        GameEventLog log(
            [](std::span<std::uint8_t> bytes) {
                for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i + 1);
                return true;
            },
            [timestamp] { return timestamp; });
        auto event    = envelope(EventId::nil(), 3);
        auto appended = log.append(event);
        REQUIRE(appended.ok());
        REQUIRE(!log.find(EventSequence(1))->eventId.isNil());
        REQUIRE_EQ(log.find(EventSequence(1))->eventId.format().substr(14, 1), "7");
    };

    SUB_CASE("game event envelope contract: snapshot restore retains IDs and next sequence") {
        GameEventLog source;
        const auto   firstId = id("018f0f4e-6b3c-7ab1-8def-0123456789ab");
        auto         first   = envelope(firstId, 10);
        first.payload        = "{\"amount\":1}";
        REQUIRE(source.append(first).ok());
        auto second      = envelope(id("018f0f4e-6b3c-7ab2-8def-0123456789ab"), 11);
        second.causation = CausationRef::fromEventId(firstId);
        second.payload   = "{\"amount\":2}";
        REQUIRE(source.append(second).ok());
        GameEventLog restored;
        REQUIRE(restored.restore(source.snapshotJson()).ok());
        REQUIRE_EQ(restored.snapshotJson(), source.snapshotJson());
        REQUIRE_EQ(restored.find(EventSequence(2))->causation.format(), firstId.format());
        auto third = restored.append(envelope(id("018f0f4e-6b3c-7ab3-8def-0123456789ab"), 12));
        REQUIRE(third.ok());
        REQUIRE_EQ(third.value().value(), 3u);
    };

    SUB_CASE("game event envelope contract: failed restore preserves published events") {
        GameEventLog log;
        REQUIRE(log.append(envelope(id("018f0f4e-6b3c-7ab4-8def-0123456789ab"), 20)).ok());
        const auto before = log.snapshotJson();
        REQUIRE(!log.restore("{\"version\":2,\"events\":[").ok());
        REQUIRE_EQ(log.snapshotJson(), before);
    };
}
}  // namespace test_event_envelope
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_eventstream)
#include "GameEventTestSupport.h"
#include "PrivateTestScope.h"
namespace test_eventstream_unit {
using namespace eve::game_event;

namespace {
EventSequence append(GameEventLog& log, std::uint64_t serial, const char* type, const char* source,
                     const char* subject) {
    auto result = log.append(test::envelope(serial, type, source, subject, "", "", serial, 0));
    REQUIRE(result.ok());
    return result.value();
}
}

TEST_CASE("Game event stream query cursor and retention contract") {
    SUB_CASE("game event stream contract: query filters retained envelopes in sequence order") {
        GameEventLog log;
        append(log, 1, "a", "one", "x");
        append(log, 2, "b", "two", "x");
        append(log, 3, "a", "two", "y");
        const auto typeCount = log.queryType("a");
        REQUIRE_EQ(typeCount, 2);
        REQUIRE_EQ(log.queryAt(0)->sequence.value(), 1u);
        REQUIRE_EQ(log.queryAt(1)->sequence.value(), 3u);
        const auto sourceCount = log.querySource("two");
        REQUIRE_EQ(sourceCount, 2);
        const auto subjectCount = log.querySubject("x");
        REQUIRE_EQ(subjectCount, 2);
        const auto sequenceCount = log.querySequence(EventSequence(2));
        REQUIRE_EQ(sequenceCount, 2);
        REQUIRE(log.queryAt(2) == nullptr);
    };

    SUB_CASE("game event stream contract: consumer reads bounded batches from its own cursor") {
        GameEventLog log;
        for (int serial = 1; serial <= 5; ++serial) append(log, serial, "tick", "clock", "");
        auto* consumer = log.newConsumer(EventSequence(2));
        REQUIRE(consumer != nullptr);
        const auto firstRead = consumer->read(2);
        REQUIRE_EQ(firstRead, 2);
        REQUIRE_EQ(consumer->batchAt(0)->sequence.value(), 2u);
        REQUIRE_EQ(consumer->batchAt(1)->sequence.value(), 3u);
        REQUIRE_EQ(consumer->position().value(), 4u);
        const auto secondRead = consumer->read(10);
        REQUIRE_EQ(secondRead, 2);
        REQUIRE_EQ(consumer->position().value(), 6u);
        REQUIRE(consumer->batchAt(2) == nullptr);
    };

    SUB_CASE("game event stream contract: clearBefore and clear retain sequence while reset restarts it") {
        GameEventLog log;
        for (int serial = 1; serial <= 3; ++serial) append(log, serial, "tick", "clock", "");
        log.clearBefore(EventSequence(3));
        REQUIRE_EQ(log.size(), 1);
        REQUIRE_EQ(log.at(0)->sequence.value(), 3u);
        REQUIRE(log.find(EventSequence(1)) == nullptr);
        log.clear();
        const auto later = append(log, 4, "later", "", "");
        REQUIRE_EQ(later.value(), 4u);
        log.reset();
        const auto fresh = append(log, 5, "fresh", "", "");
        REQUIRE_EQ(fresh.value(), 1u);
    };

    SUB_CASE("game event stream contract: canonical snapshot and failed restore are transactional") {
        GameEventLog source;
        auto         event = test::envelope(1, "created", "admin", "base-1", "", "campaign", 17, 5,
                                            "{ \"rank\": 3, \"general\": \"g-1\" }");
        REQUIRE(source.append(event).ok());
        const auto   snapshot = source.snapshotJson();
        GameEventLog restored;
        REQUIRE(restored.restore(snapshot).ok());
        REQUIRE_EQ(restored.snapshotJson(), snapshot);
        REQUIRE_EQ(restored.find(EventSequence(1))->payload, "{\"general\":\"g-1\",\"rank\":3}");
        REQUIRE(!restored.restore("{\"version\":1}").ok());
        REQUIRE_EQ(restored.snapshotJson(), snapshot);
        const auto unknown = snapshot.substr(0, snapshot.size() - 1) + ",\"unexpected\":true}";
        REQUIRE(!restored.restore(unknown).ok());
        REQUIRE_EQ(restored.snapshotJson(), snapshot);
    };
}
}  // namespace test_eventstream_unit
#endif
