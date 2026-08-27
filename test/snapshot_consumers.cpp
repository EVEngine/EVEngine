#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "authority/Authority.h"
#include "common/Snapshot.h"
#include "GameEventTestSupport.h"
#include "definitions/Definitions.h"
#include "game_event/GameEvent.h"
#include "production/Production.h"
#include "statepatch/StatePatch.h"
#include "StatePatchTestSupport.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {

using eve::ContentId;
using eve::DiagnosticCode;
using eve::LogicalId;
using eve::PersistentId;
using eve::Result;
using eve::SchemaVersion;
using eve::SimulationTick;
using eve::SnapshotEnvelope;
using eve::SnapshotHashProvider;
using eve::Value;

PersistentId persistentId(std::string_view text) {
    const auto parsed = PersistentId::parse(text);
    return parsed ? *parsed : PersistentId::nil();
}

LogicalId logicalId(std::string_view text) {
    const auto parsed = LogicalId::parse(text);
    return parsed ? *parsed : LogicalId{};
}

SnapshotHashProvider testHashProvider() {
    return [](std::string_view input) -> Result<ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left ^= byte;
            left *= 1099511628211ull;
            right ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull;
            right *= 14029467366897019727ull;
        }
        ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] =
                static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return Result<ContentId>::success(ContentId(bytes));
    };
}

SnapshotEnvelope reseal(const SnapshotEnvelope& source, SchemaVersion version,
                        Value payload, const SnapshotHashProvider& hash) {
    auto result = eve::makeSnapshotEnvelope(source.type, source.schema, version,
                                            source.instanceId, source.revision,
                                            source.tick, std::move(payload), hash);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

Value withPayloadMetadata(const Value& payload, std::string field, std::uint64_t value) {
    Value::Object object;
    if (const auto* existing = payload.getIf<Value::Object>()) object = *existing;
    object[std::move(field)] = Value(std::to_string(value));
    return Value(std::move(object));
}

}  // namespace

TEST_CASE("snapshot.consumers.eventStreamEnvelopeRoundTripAndGeneratorSurvives") {
    const auto hash = testHashProvider();
    const auto id   = persistentId("11111111-2222-4333-8444-555555555555");
    eve::game_event::GameEventLog stream(id);
    auto appended = stream.append(eve::game_event::test::envelope(
        1, "test.event", "test", "subject", "", "", 5, 1, "{\"value\":1}"));
    REQUIRE(appended.ok());
    CHECK_EQ(appended.value().value(), uint64_t{1});

    auto captured = stream.snapshot(hash);
    REQUIRE(captured.ok());
    CHECK_EQ(captured.value().type, std::string("game_event.stream"));
    CHECK_EQ(captured.value().instanceId, id);
    CHECK_EQ(captured.value().revision, eve::Revision(1));

    eve::game_event::GameEventLog restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), stream.snapshotJson());

    auto old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::game_event::GameEventLog migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), stream.snapshotJson());

    auto newer = reseal(captured.value(), SchemaVersion(2), captured.value().payload, hash);
    const std::string before = restored.snapshotJson();
    auto unknown = restored.restoreSnapshot(newer, hash);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()),
             static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(restored.snapshotJson(), before);

    auto mismatched = captured.value();
    mismatched.contentHash = ContentId::nil();
    auto mismatch = restored.restoreSnapshot(mismatched, hash);
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(mismatch.error()->code()),
             static_cast<int>(DiagnosticCode::HashMismatch));
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateRevision = reseal(captured.value(), SchemaVersion(1),
                                    withPayloadMetadata(captured.value().payload, "revision",
                                                        captured.value().revision.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateRevision, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.transactionLedgerEnvelopeMigratesAtomically") {
    const auto hash = testHashProvider();
    const auto id   = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::transaction::Ledger ledger(id);
    auto created = ledger.create("chain", "command",
                                *eve::TransactionId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee01"));
    REQUIRE(created.ok());
    auto* plan = created.value();
    auto staged = plan->stage("set", "target", "{\"value\":1}",
                              *eve::OperationId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeee02"));
    REQUIRE(staged.ok());
    const auto operation = staged.value();
    REQUIRE(plan->markValid(operation).ok());
    REQUIRE(plan->validate().ok());
    REQUIRE(plan->commit().ok());

    auto captured = ledger.snapshot(hash);
    REQUIRE(captured.ok());
    eve::transaction::Ledger restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), ledger.snapshotJson());

    auto old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::transaction::Ledger migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), ledger.snapshotJson());

    const std::string before = restored.snapshotJson();
    Value::Object malformedObject;
    malformedObject.emplace("version", Value(std::int64_t(1)));
    auto malformed = reseal(captured.value(), SchemaVersion(1),
                            Value(std::move(malformedObject)), hash);
    auto failure = restored.restoreSnapshot(malformed, hash);
    CHECK(!failure.ok());
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateTick = reseal(captured.value(), SchemaVersion(1),
                                withPayloadMetadata(captured.value().payload, "tick",
                                                    captured.value().tick.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateTick, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.statePatchEnvelopeRejectsHashAndRevisionMismatch") {
    const auto hash = testHashProvider();
    const auto id   = persistentId("12345678-1234-4123-8123-123456789012");
    eve::statepatch::Store store(id);
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    REQUIRE(batch.view->set("actor", "value", "1"));
    REQUIRE(store.commit(batch.view.get()));

    auto captured = store.snapshot(hash);
    REQUIRE(captured.ok());
    eve::statepatch::Store restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(restored.snapshotJson(), store.snapshotJson());

    auto old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::statepatch::Store migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(migrated.snapshotJson(), store.snapshotJson());

    const std::string before = restored.snapshotJson();
    auto mismatch = captured.value();
    mismatch.contentHash = ContentId::nil();
    auto hashFailure = restored.restoreSnapshot(mismatch, hash);
    CHECK(!hashFailure.ok());
    CHECK_EQ(static_cast<int>(hashFailure.error()->code()),
             static_cast<int>(DiagnosticCode::HashMismatch));
    CHECK_EQ(restored.snapshotJson(), before);

    auto badRevision = captured.value();
    badRevision.revision = eve::Revision(captured.value().revision.value() + 1);
    auto badRevisionHash = eve::makeSnapshotEnvelope(
        badRevision.type, badRevision.schema, badRevision.schemaVersion,
        badRevision.instanceId, badRevision.revision, badRevision.tick,
        badRevision.payload, hash);
    REQUIRE(badRevisionHash.ok());
    auto revisionFailure = restored.restoreSnapshot(badRevisionHash.value(), hash);
    CHECK(!revisionFailure.ok());
    CHECK_EQ(restored.snapshotJson(), before);

    auto duplicateTick = reseal(captured.value(), SchemaVersion(1),
                                withPayloadMetadata(captured.value().payload, "tick",
                                                    captured.value().tick.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateTick, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("snapshot.consumers.productionEnvelopeRoundTripAndUnknownVersionIsAtomic") {
    const auto hash = testHashProvider();
    const auto id   = persistentId("99999999-8888-4777-8666-555555555555");
    auto snapshotJson = [](const eve::production::WorkQueue& queue) {
        auto result = queue.snapshot();
        return std::move(result).expect("production snapshot must serialize");
    };
    eve::production::WorkQueue queue(id);
    auto taskIdResult = queue.enqueue("factory", "smelt", "iron",
                                      eve::Value(eve::Value::Object{{"ore", eve::Value(1)}}), 2.0);
    REQUIRE(taskIdResult.ok());

    auto captured = queue.snapshot(hash);
    REQUIRE(captured.ok());
    eve::production::WorkQueue restored(id);
    REQUIRE(restored.restoreSnapshot(captured.value(), hash).ok());
    CHECK_EQ(snapshotJson(restored), snapshotJson(queue));

    auto old = reseal(captured.value(), SchemaVersion(0), captured.value().payload, hash);
    eve::production::WorkQueue migrated(id);
    REQUIRE(migrated.restoreSnapshot(old, hash).ok());
    CHECK_EQ(snapshotJson(migrated), snapshotJson(queue));

    auto newer = reseal(captured.value(), SchemaVersion(2), captured.value().payload, hash);
    const std::string before = snapshotJson(restored);
    auto unknown = restored.restoreSnapshot(newer, hash);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()),
             static_cast<int>(DiagnosticCode::UnknownVersion));
    CHECK_EQ(snapshotJson(restored), before);

    auto duplicateRevision = reseal(captured.value(), SchemaVersion(1),
                                    withPayloadMetadata(captured.value().payload, "revision",
                                                        captured.value().revision.value() + 1), hash);
    auto duplicateFailure = restored.restoreSnapshot(duplicateRevision, hash);
    CHECK(!duplicateFailure.ok());
    CHECK_EQ(static_cast<int>(duplicateFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(snapshotJson(restored), before);
}

TEST_CASE("snapshot.consumers.authorityAndDefinitionsRejectDuplicateMetadataAtomically") {
    const auto hash = testHashProvider();

    const auto authorityId = persistentId("11111111-2222-4333-8444-555555555555");
    eve::authority::Store authority(authorityId);
    REQUIRE(!authority.grant("actor", "scope", "read", "test").empty());
    auto authoritySnapshot = authority.snapshot(hash);
    REQUIRE(authoritySnapshot.ok());
    eve::authority::Store authorityRestored(authorityId);
    REQUIRE(authorityRestored.restoreSnapshot(authoritySnapshot.value(), hash).ok());
    const std::string authorityBefore = authorityRestored.snapshotJson();
    auto authorityConflict = reseal(
        authoritySnapshot.value(), SchemaVersion(1),
        withPayloadMetadata(authoritySnapshot.value().payload, "revision",
                            authoritySnapshot.value().revision.value() + 1), hash);
    auto authorityFailure = authorityRestored.restoreSnapshot(authorityConflict, hash);
    CHECK(!authorityFailure.ok());
    CHECK_EQ(static_cast<int>(authorityFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(authorityRestored.snapshotJson(), authorityBefore);

    const auto definitionsId = persistentId("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    eve::definitions::DefinitionRegistry definitions(definitionsId);
    REQUIRE(definitions.insert("unit", "tank", 1, "{\"speed\":4}").ok());
    auto definitionsSnapshot = definitions.snapshot(hash);
    REQUIRE(definitionsSnapshot.ok());
    eve::definitions::DefinitionRegistry definitionsRestored(definitionsId);
    REQUIRE(definitionsRestored.restoreSnapshot(definitionsSnapshot.value(), hash).ok());
    const std::string definitionsBefore = definitionsRestored.snapshotJson();
    auto definitionsConflict = reseal(
        definitionsSnapshot.value(), SchemaVersion(1),
        withPayloadMetadata(definitionsSnapshot.value().payload, "tick",
                            definitionsSnapshot.value().tick.value() + 1), hash);
    auto definitionsFailure = definitionsRestored.restoreSnapshot(definitionsConflict, hash);
    CHECK(!definitionsFailure.ok());
    CHECK_EQ(static_cast<int>(definitionsFailure.error()->code()),
             static_cast<int>(DiagnosticCode::Conflict));
    CHECK_EQ(definitionsRestored.snapshotJson(), definitionsBefore);
}
