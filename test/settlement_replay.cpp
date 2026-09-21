#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "settlement/Settlement.h"

#include <array>
#include <string_view>

namespace {

eve::SnapshotHashProvider testHashProvider() {
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

eve::settlement::SettlementResult sampleResult() {
    eve::settlement::SettlementResult result;
    result.requested   = 20.0;
    result.applied     = 15.0;
    result.resisted    = 5.0;
    result.disposition = eve::settlement::SettlementDisposition::PartiallyApplied;
    result.tick        = eve::SimulationTick(42);
    result.stages.push_back({eve::settlement::StageKind::TargetMitigation,
                             "armor",
                             eve::StatusCode::Applied,
                             20.0,
                             15.0,
                             eve::Value::Object{{"rule", "heavy_armor"}}});
    eve::game_event::GameEvent event;
    event.type          = "settlement.damage";
    event.source        = "attacker";
    event.subject       = "target";
    event.tick          = result.tick;
    event.payload       = "{\"applied\":15}";
    event.eventId       = eve::EventId::parse("018f0f4e-6b3c-7ab1-8def-0123456789ab").value();
    event.sequence      = eve::EventSequence(7);
    result.event        = event;
    return result;
}

eve::ContentId contentId(std::uint8_t suffix) {
    eve::ContentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::ContentId(bytes);
}

eve::SubjectRef subject(std::uint8_t suffix) {
    eve::PersistentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::settlement::SettlementRequest sampleRequest() {
    eve::settlement::SettlementRequest request;
    request.source    = subject(1);
    request.target    = subject(2);
    request.kind      = "damage";
    request.resource  = "hp";
    request.magnitude = 20.0;
    request.tags      = {"physical", "melee"};
    request.context   = eve::Value::Object{{"distance", 1.5}};
    request.tick      = eve::SimulationTick(42);
    request.decisions.push_back({*eve::LogicalId::parse("rng:combat"), 4, 0.2, 0.5, true});
    return request;
}

}  // namespace

TEST_CASE("settlement.replay.digestIsStableAndIgnoresStreamIdentity") {
    auto expected = sampleResult();
    auto actual   = expected;
    actual.event->eventId = eve::EventId::parse("018f0f4e-6b3c-7ab1-8def-0123456789ac").value();
    actual.event->sequence = eve::EventSequence(99);

    auto expectedDigest = eve::settlement::settlementResultDigest(expected, testHashProvider());
    auto actualDigest   = eve::settlement::settlementResultDigest(actual, testHashProvider());
    REQUIRE(expectedDigest.ok());
    REQUIRE(actualDigest.ok());
    CHECK_EQ(expectedDigest.value(), actualDigest.value());

    auto verified = eve::settlement::verifySettlementResult(expected, actual);
    REQUIRE(verified.ok());
    CHECK(verified.value().empty());
}

TEST_CASE("settlement.replay.verifyReportsStageFieldPaths") {
    const auto expected = sampleResult();
    auto       actual   = expected;
    actual.applied                      = 14.0;
    actual.stages[0].details            = eve::Value::Object{{"rule", "light_armor"}};

    auto verified = eve::settlement::verifySettlementResult(expected, actual);
    REQUIRE(verified.ok());
    REQUIRE_EQ(verified.value().size(), 2u);
    CHECK_EQ(verified.value()[0], std::string("applied"));
    CHECK_EQ(verified.value()[1], std::string("stages[0].details"));

    auto expectedDigest = eve::settlement::settlementResultDigest(expected, testHashProvider());
    auto actualDigest   = eve::settlement::settlementResultDigest(actual, testHashProvider());
    REQUIRE(expectedDigest.ok());
    REQUIRE(actualDigest.ok());
    CHECK(expectedDigest.value() != actualDigest.value());
}

TEST_CASE("settlement.replay.digestRequiresExplicitProvider") {
    const eve::SnapshotHashProvider missing;
    auto result = eve::settlement::settlementResultDigest(sampleResult(), missing);
    CHECK(!result.ok());
}

TEST_CASE("settlement.replay.recordRoundTripsRequestAndLocatesDifferences") {
    const auto request = sampleRequest();
    const auto result  = sampleResult();
    auto record = eve::settlement::createSettlementReplayRecord(request, contentId(7), result, testHashProvider());
    REQUIRE(record.ok());

    auto decoded = eve::settlement::settlementReplayRequest(record.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().source, request.source);
    CHECK_EQ(decoded.value().target, request.target);
    CHECK_EQ(decoded.value().kind, request.kind);
    CHECK_EQ(decoded.value().resource, request.resource);
    CHECK_EQ(decoded.value().magnitude, request.magnitude);
    CHECK_EQ(decoded.value().decisions.size(), 1u);
    CHECK_EQ(decoded.value().decisions[0].stream, request.decisions[0].stream);
    CHECK_EQ(decoded.value().decisions[0].sequence, request.decisions[0].sequence);

    auto actual = result;
    actual.stages[0].details = eve::Value::Object{{"rule", "different"}};
    auto verified = eve::settlement::verifySettlementReplayRecord(record.value(), contentId(8), actual,
                                                                   testHashProvider());
    REQUIRE(verified.ok());
    REQUIRE_EQ(verified.value().size(), 2u);
    CHECK_EQ(verified.value()[0], std::string("ruleDigest"));
    CHECK_EQ(verified.value()[1], std::string("result.payload.stages[0].details.rule"));
}

TEST_CASE("settlement.replay.recordRejectsUnknownFieldsVersionsAndTampering") {
    auto record = eve::settlement::createSettlementReplayRecord(sampleRequest(), contentId(7), sampleResult(),
                                                                 testHashProvider());
    REQUIRE(record.ok());
    auto parsed = eve::Value::fromJson(record.value());
    REQUIRE(parsed.ok());

    auto unknown = parsed.value();
    unknown.set("unexpected", true);
    auto unknownJson = unknown.toJson();
    REQUIRE(unknownJson.ok());
    CHECK(!eve::settlement::settlementReplayRequest(unknownJson.value()).ok());

    auto future = parsed.value();
    future.set("version", 2);
    auto futureJson = future.toJson();
    REQUIRE(futureJson.ok());
    CHECK(!eve::settlement::settlementReplayRequest(futureJson.value()).ok());

    auto nestedUnknown = parsed.value();
    auto* nestedResult = nestedUnknown.find("result");
    REQUIRE(nestedResult != nullptr);
    auto* nestedPayload = nestedResult->find("payload");
    REQUIRE(nestedPayload != nullptr);
    nestedPayload->set("unexpected", true);
    auto nestedUnknownJson = nestedUnknown.toJson();
    REQUIRE(nestedUnknownJson.ok());
    CHECK(!eve::settlement::settlementReplayRequest(nestedUnknownJson.value()).ok());

    auto tampered = parsed.value();
    auto* result = tampered.find("result");
    REQUIRE(result != nullptr);
    auto* payload = result->find("payload");
    REQUIRE(payload != nullptr);
    payload->set("applied", 14.0);
    auto tamperedJson = tampered.toJson();
    REQUIRE(tamperedJson.ok());
    auto verified = eve::settlement::verifySettlementReplayRecord(tamperedJson.value(), contentId(7), sampleResult(),
                                                                   testHashProvider());
    CHECK(!verified.ok());
}
